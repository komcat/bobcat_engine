// SwirBeamFocusController.h
// Wraps the BF (beam focus) hill-climb optimisation that the SWIR window
// runs on its BF button. The controller owns the worker future + the
// running/cancel flag (a single atomic — setting it false also stops the
// optimiser via BeamHillClimb's cancel hook). The window keeps its
// click handler as a thin wrapper that does the prerequisite checks and
// calls Start / RequestStop on this controller.
#pragma once

#include <atomic>
#include <functional>
#include <future>

#include "BeamOptimizer/BeamHillClimb.h"  // HillClimbConfig

class BeamScoreProvider;
class UnifiedMotionManager;

class SwirBeamFocusController {
public:
    bool IsRunning() const { return m_running.load(); }

    // Launches the hill-climb worker. Returns false if a run is already
    // in flight. The completion callback (if non-null) is called from the
    // worker thread once the climb returns — typical use is to post a
    // repaint or refresh some UI state.
    bool Start(UnifiedMotionManager* motion,
               BeamScoreProvider* score,
               const HillClimbConfig& config,
               std::function<void()> onComplete = nullptr);

    // Set the cancel flag. The optimiser checks this between iterations
    // and exits cleanly.
    void RequestStop();

    // Same as RequestStop but also blocks until the worker has finished.
    // Use from destructors / window shutdown.
    void StopAndWait();

private:
    // m_running doubles as the BeamHillClimb cancel flag — the optimiser
    // exits as soon as it observes the flag flipping to false.
    std::atomic<bool> m_running{false};
    std::future<void> m_future;
};
