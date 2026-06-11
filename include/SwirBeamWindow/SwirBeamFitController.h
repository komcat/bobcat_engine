// SwirBeamFitController.h
// One-shot "BEAM FIT" sweep for the SWIR window. Standalone — it does NOT
// share any state with AUTO SAT; it only moves gantry-main Z and reads beam
// snapshots.
//
//   Sweep gantry-main Z about the current position: -15um first, then step
//   +5um up to +15um (7 layers spanning ±15um). At each Z, record each beam's
//   saturated-pixel count. For every beam, the waist Z is where its
//   saturated-pixel count peaks. The final move target is the average of the
//   per-beam waist Z values (e.g. (B0.z + B1.z) / 2 for two beams).
//
// Runs on its own worker thread (std::async); the button label flips between
// "BEAM FIT" and "STOP" based on IsRunning(). All hardware/state access goes
// through the Deps callback bundle so this class never reaches back into
// SwirBeamWindow members.
#pragma once

#include <atomic>
#include <functional>
#include <future>
#include <vector>

#include "sequence/SystemCommandHandler.h"  // SwirBeamSnapshot

class UnifiedMotionManager;

class SwirBeamFitController {
public:
    struct Deps {
        UnifiedMotionManager* motion = nullptr;

        // Live beam state (size 0..2). Returned by SwirBeamWindow::
        // GetBeamSnapshots().
        std::function<std::vector<SwirBeamSnapshot>()> snapshotBeams;

        std::function<void(bool)> ensureBeamDetect;  // turn detection on
        std::function<void()>     invalidatePanel;
    };

    bool IsRunning() const { return m_running.load(); }

    // Click handler: starts a run if idle, otherwise aborts the active run.
    void Toggle();

    // Set abort flag and wait for the worker to exit. Safe to call from
    // destructors / window shutdown.
    void Stop();

    // Wire dependencies. Must be called before Toggle.
    void SetDeps(Deps deps);

private:
    void Start();
    void RunAlgorithm();

    Deps              m_deps;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_abort{false};
    std::future<void> m_future;
};
