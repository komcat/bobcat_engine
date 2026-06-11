// SwirBeamFitController.cpp
#include "SwirBeamFitController.h"

#include "motions/unified_motion_manager.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>
#include <utility>

namespace {

constexpr double BF_Z_STEP_MM      = 0.005;  // 5 um
constexpr int    BF_Z_HALF_STEPS   = 3;      // ±15 um -> 7 layers (3 each side + center)
constexpr int    BF_MOTION_SETTLE_MS = 300;  // settle after each Z move
constexpr double BF_MOTION_VEL_MMS   = 5.0;
constexpr int    BF_BEAM_SETTLE_MS   = 600;  // wait after enabling detection

// Per-beam best-Z accumulator. Tracks the Z at which this beam's
// saturated-pixel count peaks. `bestSat` < 0 means "no reading yet".
// `bestPk` is a fallback used only when the beam never saturates.
struct BeamWaist {
    std::string label;
    double      bestZ   = 0.0;
    int         bestSat = -1;
    double      bestPkZ = 0.0;
    int         bestPk  = -1;
    bool        seen    = false;
};

}  // anonymous namespace

void SwirBeamFitController::SetDeps(Deps deps)
{
    m_deps = std::move(deps);
}

void SwirBeamFitController::Toggle()
{
    if (m_running.load()) {
        Stop();
        return;
    }
    Start();
}

void SwirBeamFitController::Start()
{
    if (m_running.load()) return;

    m_abort.store(false);
    m_running.store(true);
    if (m_deps.invalidatePanel) m_deps.invalidatePanel();
    std::cout << "[BeamFit] starting Z sweep" << std::endl;

    // std::async with std::launch::async returns a future that joins in its
    // destructor — assigning to a member future cancels (= joins) any prior
    // run, so a quick double-start is safe.
    m_future = std::async(std::launch::async, [this]() {
        try {
            RunAlgorithm();
        } catch (const std::exception& e) {
            std::cerr << "[BeamFit] exception: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[BeamFit] unknown exception" << std::endl;
        }
        m_running.store(false);
        m_abort.store(false);
        if (m_deps.invalidatePanel) m_deps.invalidatePanel();
        std::cout << "[BeamFit] done" << std::endl;
    });
}

void SwirBeamFitController::Stop()
{
    if (!m_running.load()) return;
    m_abort.store(true);
    std::cout << "[BeamFit] abort requested" << std::endl;
    if (m_future.valid()) m_future.wait();
}

void SwirBeamFitController::RunAlgorithm()
{
    if (!m_deps.motion) {
        std::cout << "[BeamFit] no motion manager — aborting" << std::endl;
        return;
    }

    // Detection must be running — the sweep is a no-op without beam readings.
    if (m_deps.ensureBeamDetect) {
        m_deps.ensureBeamDetect(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(BF_BEAM_SETTLE_MS));
    }

    auto snap = [&]() {
        return m_deps.snapshotBeams ? m_deps.snapshotBeams()
                                     : std::vector<SwirBeamSnapshot>{};
    };

    const PositionStruct startPos = m_deps.motion->GetCurrentPosition("gantry-main");
    const double centerZ = startPos.z;
    std::cout << "[BeamFit] center Z=" << centerZ << " mm, sweeping +/-"
              << (BF_Z_HALF_STEPS * BF_Z_STEP_MM * 1000.0) << " um in "
              << (BF_Z_STEP_MM * 1000.0) << " um steps" << std::endl;

    std::vector<BeamWaist> waists;  // one entry per beam label seen

    auto recordBeam = [&](const SwirBeamSnapshot& s, double z) {
        auto it = std::find_if(waists.begin(), waists.end(),
                               [&](const BeamWaist& w) { return w.label == s.label; });
        if (it == waists.end()) {
            waists.push_back(BeamWaist{});
            it = waists.end() - 1;
            it->label = s.label;
        }
        it->seen = true;
        if (s.saturatedPixelCount > it->bestSat) {
            it->bestSat = s.saturatedPixelCount;
            it->bestZ   = z;
        }
        if (s.peakIntensity > it->bestPk) {
            it->bestPk  = s.peakIntensity;
            it->bestPkZ = z;
        }
    };

    // Move to the bottom of the sweep first, then step upward through the
    // range (monotonic direction avoids backlash mid-measurement).
    for (int k = -BF_Z_HALF_STEPS; k <= BF_Z_HALF_STEPS; ++k) {
        if (m_abort.load()) {
            std::cout << "[BeamFit] aborted mid-sweep" << std::endl;
            // Return to where we started rather than leaving the stage parked.
            m_deps.motion->MoveToAbs("gantry-main", startPos.x, startPos.y,
                                     centerZ, BF_MOTION_VEL_MMS, true);
            return;
        }

        const double z = centerZ + k * BF_Z_STEP_MM;
        m_deps.motion->MoveToAbs("gantry-main", startPos.x, startPos.y, z,
                                 BF_MOTION_VEL_MMS, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(BF_MOTION_SETTLE_MS));

        auto snaps = snap();
        for (const auto& s : snaps) recordBeam(s, z);

        int satB0 = -1, satB1 = -1;
        for (const auto& s : snaps) {
            if (s.label == "B0") satB0 = s.saturatedPixelCount;
            else if (s.label == "B1") satB1 = s.saturatedPixelCount;
        }
        std::cout << "[BeamFit] Z=" << z << " mm  B0.sat="
                  << satB0 << "  B1.sat=" << satB1 << std::endl;

        if (m_deps.invalidatePanel) m_deps.invalidatePanel();
    }

    // Resolve each beam's waist Z. Prefer the saturated-pixel peak; fall back
    // to the peak-intensity Z if the beam never saturated during the sweep.
    std::vector<double> targetZs;
    for (const auto& w : waists) {
        if (!w.seen) continue;
        if (w.bestSat > 0) {
            std::cout << "[BeamFit] " << w.label << " waist Z=" << w.bestZ
                      << " mm (sat=" << w.bestSat << ")" << std::endl;
            targetZs.push_back(w.bestZ);
        } else {
            std::cout << "[BeamFit] " << w.label
                      << " never saturated; using peak-intensity Z="
                      << w.bestPkZ << " mm (pk=" << w.bestPk << ")" << std::endl;
            targetZs.push_back(w.bestPkZ);
        }
    }

    if (targetZs.empty()) {
        std::cout << "[BeamFit] no beams detected during sweep — returning to "
                     "start Z=" << centerZ << " mm" << std::endl;
        m_deps.motion->MoveToAbs("gantry-main", startPos.x, startPos.y,
                                 centerZ, BF_MOTION_VEL_MMS, true);
        return;
    }

    double sum = 0.0;
    for (double z : targetZs) sum += z;
    const double finalZ = sum / static_cast<double>(targetZs.size());

    if (m_abort.load()) return;
    m_deps.motion->MoveToAbs("gantry-main", startPos.x, startPos.y, finalZ,
                             BF_MOTION_VEL_MMS, true);
    std::cout << "[BeamFit] moved to fitted waist Z=" << finalZ << " mm (avg of "
              << targetZs.size() << " beam"
              << (targetZs.size() == 1 ? "" : "s") << ")" << std::endl;
}
