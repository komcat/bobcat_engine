// SwirAutoSatController.cpp
#include "SwirAutoSatController.h"
#include "SwirBeamWindow.h"   // PSUDeviceState definition

#include "PSU/PowerSupplyManager.h"
#include "motions/unified_motion_manager.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
#include <utility>

namespace {

constexpr int    AS_PK_TARGET_LOW    = 180;   // tune until pk >= this
constexpr int    AS_PK_TARGET_HIGH   = 250;   // and pk < this (header room before 255)
constexpr int    AS_PK_SAT           = 254;   // any pixel >= this counts as saturated
constexpr double AS_EXP_STEP_COARSE  = 20.0;
constexpr double AS_EXP_STEP_FINE    = 10.0;  // used after the first oscillation
constexpr int    AS_CUR_STEP_MA      = 5;
constexpr double AS_EXP_MIN_US       = 30.0;
constexpr double AS_EXP_MAX_US       = 1000.0;
constexpr int    AS_CUR_MIN_MA       = 100;
constexpr int    AS_CUR_MAX_MA       = 200;
constexpr int    AS_BEAM_POLL_MS     = 250;   // settle after exp/cur change
constexpr int    AS_TUNE_MAX_ITERS   = 80;
constexpr double AS_Z_STEP_MM        = 0.010; // 10 um
constexpr int    AS_Z_HALF_STEPS     = 5;     // ±50 um
constexpr int    AS_FOCUS_MAX_ITERS  = 3;
constexpr int    AS_MOTION_SETTLE_MS = 350;
constexpr double AS_MOTION_VEL_MMS   = 5.0;

// Suppress the "unused" warning since AS_PK_TARGET_HIGH is informational
// (the algorithm uses AS_PK_SAT as the upper bound).
[[maybe_unused]] inline void TouchUnused() { (void)AS_PK_TARGET_HIGH; }

}  // anonymous namespace

void SwirAutoSatController::SetDeps(Deps deps)
{
    m_deps = std::move(deps);
}

void SwirAutoSatController::Toggle()
{
    if (m_running.load()) {
        Stop();
        return;
    }
    Start();
}

void SwirAutoSatController::Start()
{
    if (m_running.load()) return;

    m_abort.store(false);
    m_running.store(true);
    if (m_deps.invalidatePanel) m_deps.invalidatePanel();
    std::cout << "[AutoSat] starting algorithm" << std::endl;

    // std::async with std::launch::async returns a future that joins in its
    // destructor — assigning to a member future cancels (= joins) any prior
    // run, so a quick double-start is safe.
    m_future = std::async(std::launch::async, [this]() {
        try {
            RunAlgorithm();
        } catch (const std::exception& e) {
            std::cerr << "[AutoSat] exception: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[AutoSat] unknown exception" << std::endl;
        }
        m_running.store(false);
        m_abort.store(false);
        if (m_deps.invalidatePanel) m_deps.invalidatePanel();
        std::cout << "[AutoSat] done" << std::endl;
    });
}

void SwirAutoSatController::Stop()
{
    if (!m_running.load()) return;
    m_abort.store(true);
    std::cout << "[AutoSat] abort requested" << std::endl;
    if (m_future.valid()) m_future.wait();
}

bool SwirAutoSatController::IncreaseCurrent(int stepMa)
{
    if (!m_deps.psu || !m_deps.psuMutex || !m_deps.psuDevices) return false;
    struct Target { std::string name; int channel; };
    std::vector<Target> targets;
    int curMa = 0, newMa = 0;
    {
        std::lock_guard<std::mutex> lock(*m_deps.psuMutex);
        if (m_deps.psuDevices->empty()) return false;
        curMa = static_cast<int>(std::round(
            (*m_deps.psuDevices)[0].setCurrent * 1000.0));
        newMa = curMa + stepMa;
        if (newMa > AS_CUR_MAX_MA) return false;
        const double newAmps = newMa / 1000.0;
        for (auto& d : *m_deps.psuDevices) {
            targets.push_back({d.name, d.selectedChannel});
            d.setCurrent = newAmps;
        }
    }
    const double newAmps = newMa / 1000.0;
    for (const auto& t : targets) {
        auto psu = m_deps.psu->getPowerSupply(t.name);
        if (psu && psu->isConnected()) psu->setCurrent(newAmps, t.channel);
    }
    std::cout << "[AutoSat] current " << curMa << " -> " << newMa << " mA"
              << std::endl;
    if (m_deps.invalidatePanel) m_deps.invalidatePanel();
    return true;
}

bool SwirAutoSatController::DecreaseCurrent(int stepMa)
{
    if (!m_deps.psu || !m_deps.psuMutex || !m_deps.psuDevices) return false;
    struct Target { std::string name; int channel; };
    std::vector<Target> targets;
    int curMa = 0, newMa = 0;
    {
        std::lock_guard<std::mutex> lock(*m_deps.psuMutex);
        if (m_deps.psuDevices->empty()) return false;
        curMa = static_cast<int>(std::round(
            (*m_deps.psuDevices)[0].setCurrent * 1000.0));
        newMa = curMa - stepMa;
        if (newMa < AS_CUR_MIN_MA) return false;
        const double newAmps = newMa / 1000.0;
        for (auto& d : *m_deps.psuDevices) {
            targets.push_back({d.name, d.selectedChannel});
            d.setCurrent = newAmps;
        }
    }
    const double newAmps = newMa / 1000.0;
    for (const auto& t : targets) {
        auto psu = m_deps.psu->getPowerSupply(t.name);
        if (psu && psu->isConnected()) psu->setCurrent(newAmps, t.channel);
    }
    std::cout << "[AutoSat] current " << curMa << " -> " << newMa << " mA"
              << std::endl;
    if (m_deps.invalidatePanel) m_deps.invalidatePanel();
    return true;
}

void SwirAutoSatController::RunAlgorithm()
{
    // Make sure detection is running — algorithm is no-op without it.
    if (m_deps.ensureBeamDetect) {
        m_deps.ensureBeamDetect(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
    }

    if (!TuneExposureCurrent()) {
        std::cout << "[AutoSat] phase 1 (tune) failed/aborted — stopping"
                  << std::endl;
        return;
    }
    if (m_abort.load()) return;
    FocusSearch();
}

bool SwirAutoSatController::TuneExposureCurrent()
{
    int prevDir = 0;
    int flipCount = 0;
    double expStep = AS_EXP_STEP_COARSE;

    auto getExp = [&]() {
        return m_deps.getCurrentExposure ? m_deps.getCurrentExposure() : 0.0;
    };
    auto setExp = [&](double v) {
        if (m_deps.setExposure) m_deps.setExposure(v);
    };
    auto snap = [&]() {
        return m_deps.snapshotBeams ? m_deps.snapshotBeams()
                                     : std::vector<SwirBeamSnapshot>{};
    };

    for (int iter = 0; iter < AS_TUNE_MAX_ITERS; ++iter) {
        if (m_abort.load()) return false;

        auto snaps = snap();
        if (snaps.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(AS_BEAM_POLL_MS));
            continue;
        }
        int pkMax = 0;
        for (const auto& s : snaps) {
            if (s.peakIntensity > pkMax) pkMax = s.peakIntensity;
        }

        int wantedDir;
        if (pkMax >= AS_PK_SAT)             wantedDir = -1;    // dim
        else if (pkMax < AS_PK_TARGET_LOW)  wantedDir = +1;    // brighten
        else {
            std::cout << "[AutoSat] tune: PK=" << pkMax
                      << " in band, done" << std::endl;
            return true;
        }

        const bool flipped = (prevDir != 0 && wantedDir != prevDir);
        if (flipped) {
            ++flipCount;
            if (expStep > AS_EXP_STEP_FINE + 0.5) {
                expStep = AS_EXP_STEP_FINE;
                std::cout << "[AutoSat] tune: oscillation, narrowing step to "
                          << expStep << " us" << std::endl;
            } else {
                bool ok = (wantedDir == -1) ? DecreaseCurrent(AS_CUR_STEP_MA)
                                             : IncreaseCurrent(AS_CUR_STEP_MA);
                if (!ok) {
                    std::cout << "[AutoSat] tune: oscillation at fine step "
                              << "and current rail — giving up" << std::endl;
                    return false;
                }
                std::cout << "[AutoSat] tune: oscillation at fine step, "
                          << "current "
                          << (wantedDir == -1 ? "-" : "+") << AS_CUR_STEP_MA
                          << " mA, resetting step" << std::endl;
                expStep = AS_EXP_STEP_COARSE;
                prevDir = 0;
                flipCount = 0;
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(AS_BEAM_POLL_MS));
                continue;
            }
        }

        const double curExp = getExp();
        const double signedStep = wantedDir * expStep;
        const double targetExp = curExp + signedStep;
        const bool railLow  = (wantedDir == -1) && (targetExp < AS_EXP_MIN_US);
        const bool railHigh = (wantedDir == +1) && (targetExp > AS_EXP_MAX_US);

        if (railLow) {
            if (!DecreaseCurrent(AS_CUR_STEP_MA)) {
                std::cout << "[AutoSat] tune: cannot dim further (rails)"
                          << std::endl;
                return false;
            }
            std::cout << "[AutoSat] tune: PK=" << pkMax
                      << " exposure floored, current -" << AS_CUR_STEP_MA
                      << " mA" << std::endl;
        } else if (railHigh) {
            if (!IncreaseCurrent(AS_CUR_STEP_MA)) {
                std::cout << "[AutoSat] tune: cannot brighten further (rails)"
                          << std::endl;
                return false;
            }
            std::cout << "[AutoSat] tune: PK=" << pkMax
                      << " exposure capped, current +" << AS_CUR_STEP_MA
                      << " mA" << std::endl;
        } else {
            std::cout << "[AutoSat] tune: PK=" << pkMax
                      << (wantedDir < 0 ? " (sat)" : "")
                      << " -> exposure " << (wantedDir < 0 ? "-" : "+")
                      << expStep << " us" << std::endl;
            setExp(targetExp);
        }

        prevDir = wantedDir;
        std::this_thread::sleep_for(std::chrono::milliseconds(AS_BEAM_POLL_MS));
    }
    std::cout << "[AutoSat] tune: max iters reached without converging"
              << std::endl;
    return false;
}

void SwirAutoSatController::FocusSearch()
{
    if (!m_deps.motion) {
        std::cout << "[AutoSat] focus: no motion manager — skipping focus search"
                  << std::endl;
        return;
    }

    PositionStruct startPos = m_deps.motion->GetCurrentPosition("gantry-main");
    double centerZ = startPos.z;
    std::cout << "[AutoSat] focus: starting at Z=" << centerZ << " mm" << std::endl;

    double bestZ = centerZ;
    int bestPk = -1;

    auto snap = [&]() {
        return m_deps.snapshotBeams ? m_deps.snapshotBeams()
                                     : std::vector<SwirBeamSnapshot>{};
    };

    for (int iter = 0; iter < AS_FOCUS_MAX_ITERS; ++iter) {
        if (m_abort.load()) return;

        std::vector<std::pair<double, int>> trace;  // (z, pk)
        for (int k = -AS_Z_HALF_STEPS; k <= AS_Z_HALF_STEPS; ++k) {
            if (m_abort.load()) return;
            const double z = centerZ + k * AS_Z_STEP_MM;
            m_deps.motion->MoveToAbs("gantry-main",
                                     startPos.x, startPos.y, z,
                                     AS_MOTION_VEL_MMS, true);
            std::this_thread::sleep_for(std::chrono::milliseconds(AS_MOTION_SETTLE_MS));

            auto snaps = snap();
            if (snaps.empty()) continue;
            int pk = 0;
            for (const auto& s : snaps) {
                if (s.peakIntensity > pk) pk = s.peakIntensity;
            }

            // Saturated mid-sweep → re-tune, then re-read at this z.
            if (pk >= AS_PK_SAT) {
                std::cout << "[AutoSat] focus: PK=" << pk
                          << " saturated at Z=" << z << " — re-tuning"
                          << std::endl;
                if (!TuneExposureCurrent()) return;
                if (m_abort.load()) return;
                snaps = snap();
                if (snaps.empty()) continue;
                pk = 0;
                for (const auto& s : snaps) {
                    if (s.peakIntensity > pk) pk = s.peakIntensity;
                }
            }
            std::cout << "[AutoSat] focus iter " << iter
                      << "  Z=" << z << " mm  PK=" << pk << std::endl;
            trace.emplace_back(z, pk);
        }
        if (trace.empty()) {
            std::cout << "[AutoSat] focus: no readings — stopping" << std::endl;
            return;
        }

        auto best = std::max_element(trace.begin(), trace.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });
        bestZ = best->first;
        bestPk = best->second;

        const bool atEdge = (best == trace.begin() || best == trace.end() - 1);
        if (!atEdge) {
            std::cout << "[AutoSat] focus converged at Z=" << bestZ
                      << " mm (PK=" << bestPk << ")" << std::endl;
            break;
        }
        std::cout << "[AutoSat] focus iter " << iter
                  << ": best Z is at sweep edge (Z=" << bestZ
                  << "), shifting and resweeping" << std::endl;
        centerZ = bestZ;
    }

    if (m_abort.load()) return;
    m_deps.motion->MoveToAbs("gantry-main",
                             startPos.x, startPos.y, bestZ,
                             AS_MOTION_VEL_MMS, true);
    std::cout << "[AutoSat] moved to best focus Z=" << bestZ
              << " mm (PK=" << bestPk << ")" << std::endl;
}
