// BeamHillClimb.h
// Reusable 1D coarse-to-fine hill-climb optimizer.
// Maximizes a score by stepping along one axis at a time.
// Used for beam focus (gantry Z) and active alignment (hexapod axes).
#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

/// Configuration for a single hill-climb optimization run.
struct HillClimbConfig {
    /// Step sizes in mm, applied coarse-to-fine order (e.g., {0.010, 0.005, 0.002}).
    std::vector<double> stepSizes = {0.010, 0.005, 0.002};

    /// Milliseconds to wait after each move for the score to stabilize.
    int settleMs = 500;

    /// Maximum steps in one direction before giving up at a given step size.
    int maxStepsPerLevel = 100;

    /// Label for logging (e.g., "BeamFocus", "HexAlign-Z").
    std::string label = "HillClimb";
};

/// Result from a hill-climb run.
struct HillClimbResult {
    double bestScore = 0.0;         // Best score achieved
    double initialScore = 0.0;      // Score before optimization
    int totalMoves = 0;             // Total moves executed
    bool cancelled = false;         // True if stopped via cancel flag
    bool success = false;           // True if optimization completed normally
};

/// Reusable 1D hill-climb optimizer.
///
/// Usage:
///   BeamHillClimb climber;
///   climber.SetScoreFunc([&]() { return computeScore(); });
///   climber.SetMoveFunc([&](double dist) { return motor.moveRelative(dist); });
///   climber.SetCancelFlag(&cancelFlag);
///   auto result = climber.Run(config);
///
class BeamHillClimb {
public:
    /// Callback to read the current score. Returns <0 if measurement failed (e.g., beam lost).
    using ScoreFunc = std::function<double()>;

    /// Callback to move the axis by `distance` mm. Returns true on success.
    using MoveFunc = std::function<bool(double distance)>;

    /// Set the score acquisition function.
    void SetScoreFunc(ScoreFunc func) { m_scoreFunc = std::move(func); }

    /// Set the motion function.
    void SetMoveFunc(MoveFunc func) { m_moveFunc = std::move(func); }

    /// Set a cancel flag (checked between steps). Caller owns the atomic.
    void SetCancelFlag(std::atomic<bool>* flag) { m_cancelFlag = flag; }

    /// Run the optimization. Blocks until complete or cancelled.
    [[nodiscard]] HillClimbResult Run(const HillClimbConfig& config);

private:
    /// Wait for the score to stabilize after a move.
    void WaitForSettle(const HillClimbConfig& config);

    /// Check if cancellation was requested.
    bool IsCancelled() const;

    ScoreFunc m_scoreFunc;
    MoveFunc m_moveFunc;
    std::atomic<bool>* m_cancelFlag = nullptr;
};
