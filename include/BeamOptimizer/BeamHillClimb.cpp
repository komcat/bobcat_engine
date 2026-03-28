// BeamHillClimb.cpp
// Reusable 1D coarse-to-fine hill-climb optimizer.
#include "BeamHillClimb.h"

#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <thread>

void BeamHillClimb::WaitForSettle(const HillClimbConfig& config)
{
    // Sleep in 50ms chunks so cancellation is responsive
    int remaining = config.settleMs;
    while (remaining > 0 && !IsCancelled()) {
        int chunk = std::min(remaining, 50);
        std::this_thread::sleep_for(std::chrono::milliseconds(chunk));
        remaining -= chunk;
    }
}

bool BeamHillClimb::IsCancelled() const
{
    return m_cancelFlag && !m_cancelFlag->load();
}

HillClimbResult BeamHillClimb::Run(const HillClimbConfig& config)
{
    HillClimbResult result;
    const std::string& tag = config.label;

    if (!m_scoreFunc || !m_moveFunc) {
        std::cerr << "[" << tag << "] ScoreFunc or MoveFunc not set" << std::endl;
        return result;
    }

    std::cout << "\n========================================" << std::endl;
    std::cout << "[" << tag << "] Starting hill-climb optimization" << std::endl;
    std::cout << "[" << tag << "] Step sizes:";
    for (double s : config.stepSizes) std::cout << " " << (s * 1000.0) << "um";
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;

    // Get initial score
    WaitForSettle(config);
    result.initialScore = m_scoreFunc();
    if (result.initialScore < 0) {
        std::cout << "[" << tag << "] No valid score, aborting" << std::endl;
        return result;
    }

    double bestScore = result.initialScore;
    std::cout << "[" << tag << "] Initial score: " << std::fixed << std::setprecision(1) << bestScore << std::endl;

    // For each step size: coarse-to-fine
    for (size_t si = 0; si < config.stepSizes.size(); ++si) {
        if (IsCancelled()) { result.cancelled = true; break; }

        double stepMm = config.stepSizes[si];
        std::cout << "[" << tag << "] Step size: " << (stepMm * 1000.0) << " um" << std::endl;

        // --- Phase 1: Determine direction ---
        // Try +step
        if (!m_moveFunc(stepMm)) {
            std::cout << "[" << tag << "] Move failed, aborting" << std::endl;
            break;
        }
        ++result.totalMoves;
        WaitForSettle(config);
        if (IsCancelled()) { result.cancelled = true; break; }

        double newScore = m_scoreFunc();
        if (newScore < 0) {
            // Lost signal, reverse
            m_moveFunc(-stepMm);
            ++result.totalMoves;
            WaitForSettle(config);
            std::cout << "[" << tag << "]   Lost signal after +step, reverting" << std::endl;
            continue;
        }

        double direction = stepMm;
        if (newScore > bestScore) {
            bestScore = newScore;
            std::cout << "[" << tag << "]   +" << (stepMm * 1000.0) << "um -> score="
                      << std::fixed << std::setprecision(1) << bestScore << " (better)" << std::endl;
        } else {
            // Positive was worse or equal, try negative
            m_moveFunc(-stepMm);  // back to origin
            m_moveFunc(-stepMm);  // now at -step
            result.totalMoves += 2;
            WaitForSettle(config);
            if (IsCancelled()) { result.cancelled = true; break; }

            newScore = m_scoreFunc();
            if (newScore < 0) {
                m_moveFunc(stepMm);
                ++result.totalMoves;
                WaitForSettle(config);
                std::cout << "[" << tag << "]   Lost signal after -step, reverting" << std::endl;
                continue;
            }

            if (newScore > bestScore) {
                direction = -stepMm;
                bestScore = newScore;
                std::cout << "[" << tag << "]   -" << (stepMm * 1000.0) << "um -> score="
                          << std::fixed << std::setprecision(1) << bestScore << " (better)" << std::endl;
            } else {
                // Neither direction improved, return to origin
                m_moveFunc(stepMm);
                ++result.totalMoves;
                WaitForSettle(config);
                std::cout << "[" << tag << "]   No improvement at " << (stepMm * 1000.0) << "um, skipping" << std::endl;
                continue;
            }
        }

        // --- Phase 2: Continue in winning direction ---
        for (int step = 0; step < config.maxStepsPerLevel; ++step) {
            if (IsCancelled()) { result.cancelled = true; break; }

            if (!m_moveFunc(direction)) {
                std::cout << "[" << tag << "]   Move failed, stopping" << std::endl;
                break;
            }
            ++result.totalMoves;
            WaitForSettle(config);
            if (IsCancelled()) { result.cancelled = true; break; }

            newScore = m_scoreFunc();
            if (newScore < 0) {
                m_moveFunc(-direction);
                ++result.totalMoves;
                WaitForSettle(config);
                std::cout << "[" << tag << "]   Lost signal, reverting last step" << std::endl;
                break;
            }

            if (newScore > bestScore) {
                bestScore = newScore;
                std::cout << "[" << tag << "]   step " << (step + 2) << " -> score="
                          << std::fixed << std::setprecision(1) << bestScore << std::endl;
            } else {
                // Score decreased, reverse to best position
                m_moveFunc(-direction);
                ++result.totalMoves;
                WaitForSettle(config);
                std::cout << "[" << tag << "]   Score decreased, reverting to best" << std::endl;
                break;
            }
        }
    }

    result.bestScore = bestScore;
    result.success = !result.cancelled;

    std::cout << "[" << tag << "] Complete. Score: " << std::fixed << std::setprecision(1)
              << result.initialScore << " -> " << result.bestScore
              << " (" << result.totalMoves << " moves)"
              << (result.cancelled ? " [CANCELLED]" : "") << std::endl;
    std::cout << "========================================\n" << std::endl;

    return result;
}
