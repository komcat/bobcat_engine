// YawTrainerData.h - Data structures and yaw math for Yaw Trainer
#pragma once

#include <string>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// Data Structures
// ============================================================================

struct LinePoints {
    std::string name;       // "Reference" or "Target"
    double p1x = 0.0;      // gantry-main X of first point (mm)
    double p1y = 0.0;      // gantry-main Y of first point (mm)
    double p1z = 0.0;      // gantry-main Z of first point (mm)
    double p2x = 0.0;      // gantry-main X of second point (mm)
    double p2y = 0.0;      // gantry-main Y of second point (mm)
    double p2z = 0.0;      // gantry-main Z of second point (mm)
    double angleDeg = 0.0;  // computed line angle in XY plane (degrees)
    bool valid = false;     // true when both points have been captured
};

struct YawCorrectionConfig {
    std::string targetDevice;   // "hex-left" or "hex-right"
    std::string targetAxis;     // "U", "V", or "W"
    bool reverseSign = false;   // flip correction sign
    bool applied = false;       // whether correction was applied
    double appliedValue = 0.0;  // actual degrees sent to hex
};

// ============================================================================
// Yaw Math (pure functions, no external dependencies)
// ============================================================================

namespace YawTrainerMath {

/**
 * @brief Compute the angle of a line formed by two XY points
 * @return angle in degrees, range (-180, 180]
 */
inline double ComputeLineAngle(double p1x, double p1y, double p2x, double p2y)
{
    double dx = p2x - p1x;
    double dy = p2y - p1y;
    return std::atan2(dy, dx) * 180.0 / M_PI;
}

/**
 * @brief Compute the delta angle between reference and target lines
 * @return delta in degrees, normalized to [-180, 180]
 */
inline double ComputeDeltaAngle(double refAngleDeg, double targetAngleDeg)
{
    double delta = targetAngleDeg - refAngleDeg;
    for (int i = 0; i < 4 && delta > 180.0; ++i) {
        delta -= 360.0;
    }
    for (int i = 0; i < 4 && delta < -180.0; ++i) {
        delta += 360.0;
    }
    return delta;
}

/**
 * @brief Get correction value with optional sign reversal
 * Default: apply delta directly (positive delta = positive correction)
 */
inline double GetCorrectionValue(double deltaDeg, bool reverseSign)
{
    return reverseSign ? -deltaDeg : deltaDeg;
}

/**
 * @brief Compute 2D distance between two points (XY plane)
 */
inline double LineLength(double p1x, double p1y, double p2x, double p2y)
{
    double dx = p2x - p1x;
    double dy = p2y - p1y;
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace YawTrainerMath

// ============================================================================
// YawTrainerSet (defined after math namespace so it can use it)
// ============================================================================

struct YawTrainerSet {
    std::string uid;
    std::string name;
    std::string deviceName = "gantry-main";  // measurement device

    LinePoints referenceLine;
    LinePoints targetLine;

    // Computed (not stored, recalculated on load)
    double deltaAngleDeg = 0.0;   // target angle - reference angle, normalized
    bool deltaValid = false;

    YawCorrectionConfig correction;

    std::string createdAt;
    bool valid = false;
    std::string sourceFilename;

    void RecalculateDelta() {
        if (!referenceLine.valid || !targetLine.valid) {
            deltaValid = false;
            return;
        }
        referenceLine.angleDeg = YawTrainerMath::ComputeLineAngle(
            referenceLine.p1x, referenceLine.p1y,
            referenceLine.p2x, referenceLine.p2y);
        targetLine.angleDeg = YawTrainerMath::ComputeLineAngle(
            targetLine.p1x, targetLine.p1y,
            targetLine.p2x, targetLine.p2y);
        deltaAngleDeg = YawTrainerMath::ComputeDeltaAngle(
            referenceLine.angleDeg, targetLine.angleDeg);
        deltaValid = true;
    }
};
