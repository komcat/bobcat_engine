// SwirAutoSatController.h
// One-shot auto-tune & focus algorithm for the SWIR window's AUTO SAT
// button. Runs on its own worker thread (std::async); the button label
// flips between "AUTO SAT" and "STOP" based on IsRunning().
//
//   Phase 1: tune exposure (20us steps, narrows to 10us on oscillation,
//            shifts PSU current 5mA when fine step also straddles) until
//            max(B0_PK, B1_PK) ∈ [PK_TARGET_LOW, PK_TARGET_HIGH].
//   Phase 2: sweep gantry-main Z by ±50um in 10um steps; pick max-PK Z.
//            If saturated mid-sweep, re-run Phase 1 then re-read at this Z.
//            Converges when best-Z is interior to the sweep, or after 3
//            iterations.
//
// All hardware/state access goes through the Deps callback bundle so this
// class doesn't reach back into SwirBeamWindow members.
#pragma once

#include <atomic>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <vector>

#include "sequence/SystemCommandHandler.h"  // SwirBeamSnapshot

class PowerSupplyManager;
class UnifiedMotionManager;
struct PSUDeviceState;

class SwirAutoSatController {
public:
    struct Deps {
        UnifiedMotionManager* motion = nullptr;
        PowerSupplyManager*   psu    = nullptr;

        // Shared PSU device list (window-owned) and its mutex. The
        // controller mutates setCurrent on every device when shifting
        // current — same lock-step convention the manual buttons use.
        std::mutex*                       psuMutex   = nullptr;
        std::vector<PSUDeviceState>*      psuDevices = nullptr;

        // Live beam state (size 0..2). Returned by SwirBeamWindow::
        // GetBeamSnapshots().
        std::function<std::vector<SwirBeamSnapshot>()> snapshotBeams;

        std::function<double()>     getCurrentExposure;
        std::function<void(double)> setExposure;
        std::function<void(bool)>   ensureBeamDetect;   // turn detection on
        std::function<void()>       invalidatePanel;
    };

    bool IsRunning() const { return m_running.load(); }

    // Click handler: starts a run if idle, otherwise aborts the active run.
    void Toggle();

    // Explicit start (used by the SetAutoSat dispatch). No-op if running.
    void Start();

    // Set abort flag and wait for the worker to exit. Safe to call from
    // destructors / window shutdown.
    void Stop();

    // Wire dependencies. Must be called before Start/Toggle.
    void SetDeps(Deps deps);

private:
    void RunAlgorithm();
    bool TuneExposureCurrent();
    void FocusSearch();
    bool IncreaseCurrent(int stepMa);
    bool DecreaseCurrent(int stepMa);

    Deps              m_deps;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_abort{false};
    std::future<void> m_future;
};
