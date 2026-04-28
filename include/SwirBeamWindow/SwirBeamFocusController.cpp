// SwirBeamFocusController.cpp
#include "SwirBeamFocusController.h"

#include "BeamOptimizer/BeamScoreProvider.h"
#include "motions/unified_motion_manager.h"

#include <iomanip>
#include <iostream>

bool SwirBeamFocusController::Start(UnifiedMotionManager* motion,
                                    BeamScoreProvider* score,
                                    const HillClimbConfig& config,
                                    std::function<void()> onComplete)
{
    if (m_running.load()) return false;
    if (!motion || !score) return false;

    m_running.store(true);

    m_future = std::async(std::launch::async,
        [this, motion, score, config, onComplete]() {
            BeamHillClimb climber;
            climber.SetScoreFunc([score]() -> double {
                return score->GetScore();
            });
            climber.SetMoveFunc([motion](double distance) -> bool {
                return motion->MoveRelative("gantry-main", "Z", distance, true);
            });
            climber.SetCancelFlag(&m_running);

            HillClimbResult result = climber.Run(config);

            // Log final position for the operator.
            double finalZ = 0.0;
            motion->GetPosition("gantry-main", "Z", finalZ);
            std::cout << "[BeamFocus] Final Z=" << std::fixed
                      << std::setprecision(4) << finalZ << " mm" << std::endl;
            (void)result;

            m_running.store(false);
            if (onComplete) onComplete();
        });

    return true;
}

void SwirBeamFocusController::RequestStop()
{
    m_running.store(false);
}

void SwirBeamFocusController::StopAndWait()
{
    m_running.store(false);
    if (m_future.valid()) m_future.wait();
}
