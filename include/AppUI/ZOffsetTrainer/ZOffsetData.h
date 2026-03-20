// ZOffsetData.h - Data structures and Z offset math for Z Offset Trainer
#pragma once

#include <string>
#include <cmath>

// ============================================================================
// Data Structures
// ============================================================================

struct ZOffsetPoint {
    std::string name;       // "Reference" or "Target"
    double x = 0.0;        // gantry-main X (mm)
    double y = 0.0;        // gantry-main Y (mm)
    double z = 0.0;        // gantry-main Z (mm)
    bool valid = false;
};

struct ZOffsetCorrectionConfig {
    std::string targetDevice;       // "hex-left" or "hex-right"
    double targetZDiffUm = 150.0;   // desired Z difference in microns (default 150um)
    bool reverseSign = false;       // flip correction sign
    bool applied = false;           // whether correction was applied
    double appliedValueMm = 0.0;    // actual mm sent to hex Z
};

// ============================================================================
// Z Offset Math (pure functions, no external dependencies)
// ============================================================================

namespace ZOffsetMath {

/**
 * @brief Compute measured Z difference between reference and target surfaces
 * @return dZ in mm (target.z - reference.z)
 */
inline double ComputeZDifference(const ZOffsetPoint& reference, const ZOffsetPoint& target) {
    return target.z - reference.z;
}

/**
 * @brief Compute correction value to achieve the desired Z offset
 *
 * correction = (targetZDiff_mm) - measuredZDiff_mm
 * This moves the hex Z to compensate for the difference between
 * actual and desired surface separation.
 *
 * @param measuredZDiffMm Measured Z difference (target.z - reference.z) in mm
 * @param targetZDiffUm   Desired Z difference in microns
 * @param reverseSign     Flip the correction sign
 * @return correction value in mm to apply to hex Z
 */
inline double ComputeCorrection(double measuredZDiffMm, double targetZDiffUm, bool reverseSign) {
    double targetZDiffMm = targetZDiffUm / 1000.0;
    double correction = targetZDiffMm - measuredZDiffMm;
    return reverseSign ? -correction : correction;
}

} // namespace ZOffsetMath

// ============================================================================
// ZOffsetSet (complete training data set)
// ============================================================================

struct ZOffsetSet {
    std::string uid;
    std::string name;
    std::string deviceName = "gantry-main";  // measurement device

    ZOffsetPoint reference;  // Point 1: reference surface
    ZOffsetPoint target;     // Point 2: target surface

    // Computed (recalculated on load)
    double measuredZDiffMm = 0.0;   // target.z - reference.z
    bool zDiffValid = false;

    ZOffsetCorrectionConfig correction;

    std::string createdAt;
    bool valid = false;
    std::string sourceFilename;

    void RecalculateZDiff() {
        if (!reference.valid || !target.valid) {
            zDiffValid = false;
            measuredZDiffMm = 0.0;
            return;
        }
        measuredZDiffMm = ZOffsetMath::ComputeZDifference(reference, target);
        zDiffValid = true;
    }
};
