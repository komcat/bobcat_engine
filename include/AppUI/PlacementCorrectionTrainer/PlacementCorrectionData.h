// PlacementCorrectionData.h - Data structures and math for Placement Correction Trainer
#pragma once

#include <string>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// Data Structures
// ============================================================================

struct PlacementLinePoints {
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

struct PlacementCorrectionConfig {
    std::string targetDevice;           // "hex-left" or "hex-right"

    // Master position — golden reference, never modified
    std::string masterPositionName;     // e.g., "PLACE"
    double masterX = 0.0;
    double masterY = 0.0;
    double masterZ = 0.0;
    double masterU = 0.0;
    double masterV = 0.0;
    double masterW = 0.0;

    // Target position — receives corrected values after retrace
    std::string targetPositionName;     // e.g., "PLACE_ADJ"
    double targetX = 0.0;
    double targetY = 0.0;
    double targetZ = 0.0;
    double targetU = 0.0;
    double targetV = 0.0;
    double targetW = 0.0;

    bool reverseSign = false;           // flip correction sign
};

// ============================================================================
// Placement Correction Math (pure functions, no external dependencies)
// ============================================================================

namespace PlacementCorrectionMath {

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
 * @brief Compute corrected U value from baseline and delta angle
 * @param baselineU The U value from the named position
 * @param deltaDeg The angular difference (target - reference)
 * @param reverseSign If true, subtract instead of add
 * @return corrected U value
 */
inline double ComputeCorrectedU(double baselineU, double deltaDeg, bool reverseSign)
{
    return reverseSign ? (baselineU - deltaDeg) : (baselineU + deltaDeg);
}

/**
 * @brief Get correction value with optional sign reversal
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

/**
 * @brief Compute midpoint (center) of two points
 */
inline void Midpoint(double p1x, double p1y, double p2x, double p2y,
                     double& cx, double& cy)
{
    cx = (p1x + p2x) / 2.0;
    cy = (p1y + p2y) / 2.0;
}

/**
 * @brief Compute XY distance between two points
 */
inline double Distance2D(double x1, double y1, double x2, double y2)
{
    double dx = x2 - x1;
    double dy = y2 - y1;
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace PlacementCorrectionMath

// ============================================================================
// PlacementCorrectionSet (defined after math namespace so it can use it)
// ============================================================================

struct PlacementCorrectionSet {
    std::string uid;
    std::string name;
    std::string measureDevice = "gantry-main";

    PlacementLinePoints referenceLine;
    PlacementLinePoints targetLine;
    PlacementCorrectionConfig config;

    // Computed (not stored, recalculated on load)
    double refAngleDeg = 0.0;
    double targetAngleDeg = 0.0;
    double deltaAngleDeg = 0.0;     // theta-Y difference
    double correctedU = 0.0;        // baseline U + delta
    bool deltaValid = false;

    std::string createdAt;
    bool valid = false;
    std::string sourceFilename;

    void Recalculate() {
        if (!referenceLine.valid || !targetLine.valid) {
            deltaValid = false;
            return;
        }
        referenceLine.angleDeg = PlacementCorrectionMath::ComputeLineAngle(
            referenceLine.p1x, referenceLine.p1y,
            referenceLine.p2x, referenceLine.p2y);
        targetLine.angleDeg = PlacementCorrectionMath::ComputeLineAngle(
            targetLine.p1x, targetLine.p1y,
            targetLine.p2x, targetLine.p2y);
        refAngleDeg = referenceLine.angleDeg;
        targetAngleDeg = targetLine.angleDeg;
        deltaAngleDeg = PlacementCorrectionMath::ComputeDeltaAngle(
            referenceLine.angleDeg, targetLine.angleDeg);
        correctedU = PlacementCorrectionMath::ComputeCorrectedU(
            config.masterU, deltaAngleDeg, config.reverseSign);
        deltaValid = true;
    }
};
