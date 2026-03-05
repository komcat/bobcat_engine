// PlanarAdjustmentData.h - Data structures and tilt math for Planar Adjustment Trainer
#pragma once

#include <string>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// Data Structures
// ============================================================================

struct TaughtPoint {
    std::string name;       // "Point1", "Point2"
    double x = 0.0;        // gantry-main X (mm)
    double y = 0.0;        // gantry-main Y (mm)
    double z = 0.0;        // gantry-main Z (mm)
    bool valid = false;
};

struct CorrectionConfig {
    std::string sourceAngle;    // "angle_x" or "angle_y"
    std::string targetDevice;   // "hex-left" or "hex-right"
    std::string targetAxis;     // "U", "V", or "W"
    bool reverseSign = false;   // flip correction sign
    bool applied = false;       // whether correction was applied
    double appliedValue = 0.0;  // actual degrees sent to hex
};

// ============================================================================
// Tilt Math (pure functions, no external dependencies)
// ============================================================================

namespace PlanarAdjustmentMath {

/**
 * @brief Compute both tilt angles from two points
 *
 * Angle X: tilt in YZ plane (rotation around X-axis) = atan2(dZ, dY)
 * Angle Y: tilt in XZ plane (rotation around Y-axis) = atan2(dZ, dX)
 */
inline void ComputeTiltAngles(
    const TaughtPoint& p1, const TaughtPoint& p2,
    double& angleX_deg, double& angleY_deg,
    double& dX, double& dY, double& dZ,
    bool& xValid, bool& yValid)
{
    dX = p2.x - p1.x;
    dY = p2.y - p1.y;
    dZ = p2.z - p1.z;

    // Angle X: tilt in YZ plane (rotation around X-axis)
    // = elevation angle from horizontal, using abs(dY) so travel direction doesn't matter
    // Valid only if there's meaningful Y separation
    xValid = std::abs(dY) > 1e-6;
    angleX_deg = xValid ? std::atan2(dZ, std::abs(dY)) * 180.0 / M_PI : 0.0;

    // Angle Y: tilt in XZ plane (rotation around Y-axis)
    // = elevation angle from horizontal, using abs(dX)
    // Valid only if there's meaningful X separation
    yValid = std::abs(dX) > 1e-6;
    angleY_deg = yValid ? std::atan2(dZ, std::abs(dX)) * 180.0 / M_PI : 0.0;
}

/**
 * @brief Get correction value with optional sign reversal
 * Default: negate the angle to flatten the surface
 */
inline double GetCorrectionValue(double angle_deg, bool reverseSign) {
    double val = -angle_deg;  // default: negate to flatten
    return reverseSign ? -val : val;
}

/**
 * @brief Compute separation distance between two points (3D)
 */
inline double Separation(const TaughtPoint& p1, const TaughtPoint& p2) {
    double dx = p2.x - p1.x;
    double dy = p2.y - p1.y;
    double dz = p2.z - p1.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace PlanarAdjustmentMath

// ============================================================================
// PlanarAdjustmentSet (defined after math namespace so it can use it)
// ============================================================================

struct PlanarAdjustmentSet {
    std::string uid;
    std::string name;
    std::string deviceName = "gantry-main";  // measurement device

    TaughtPoint point1;
    TaughtPoint point2;

    // Computed (not stored, recalculated on load)
    double deltaX = 0.0, deltaY = 0.0, deltaZ = 0.0;
    double angleX_deg = 0.0;   // atan2(dZ, dY) - tilt in YZ plane
    double angleY_deg = 0.0;   // atan2(dZ, dX) - tilt in XZ plane
    bool angleXValid = false;
    bool angleYValid = false;
    bool anglesValid = false;

    CorrectionConfig correction;

    std::string createdAt;
    bool valid = false;
    std::string sourceFilename;

    void RecalculateAngles() {
        if (!point1.valid || !point2.valid) {
            anglesValid = false;
            return;
        }
        PlanarAdjustmentMath::ComputeTiltAngles(
            point1, point2,
            angleX_deg, angleY_deg,
            deltaX, deltaY, deltaZ,
            angleXValid, angleYValid);
        anglesValid = angleXValid || angleYValid;
    }
};
