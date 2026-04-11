// PlacementCorrectionData.h - Data structures and math for Placement Correction Trainer
//
// ============================================================================
// PLACEMENT CORRECTION MODULE — OVERVIEW
// ============================================================================
//
// PURPOSE:
//   Corrects hexapod placement angle by comparing a reference line and a
//   target line measured on the gantry. The angular difference (delta) is
//   applied to the hex device's U axis (rotation) and/or XY position.
//
// CONCEPT:
//   1. TEACH phase (PlacementCorrectionPage / TeachPlacementLineWidget):
//      - User captures 2 gantry XY points for a "reference line" (golden edge)
//      - User captures 2 gantry XY points for a "target line" (placed component edge)
//      - Each point stores gantry position (X,Y,Z) and camera exposure
//      - A "master position" on the hex device is the golden reference pose
//      - A "target position" on the hex device receives the corrected values
//
//   2. RETRACE phase (PlacementRetraceWidget):
//      - Re-measures reference and target lines at current gantry positions
//      - Computes angular delta between the two lines
//      - Computes XY correction vector: expected center vs actual center
//      - Optionally transforms correction from gantry-global to device-local
//        coordinates using a transformation matrix
//      - Saves corrected XYZUVW to the hex device's "target position"
//
//   3. RECIPE INTEGRATION:
//      - PlacementCorrectionProcesses.cpp defines the recipe plugin
//        ("Placement Correction Retrace") that creates a system command
//      - SystemCommandHandler.cpp handles "PlacementCorrectionRetrace" by
//        invoking a UI callback to show the retrace wizard
//      - RunProgramPage.cpp provides the callback, manages the QEventLoop
//        blocking, and handles the widget's completion/skip/end signals
//
// DATA FLOW (retrace):
//   PlacementCorrectionSet (loaded from JSON)
//     → PlacementRetraceWidget.SetData()
//     → 6-state wizard: Idle → WaitRefP1 → WaitRefP2 → WaitTgtP1 → WaitTgtP2 → Review
//     → computeRetraceResults(): angle delta + XY correction + optional matrix transform
//     → onSaveToTarget(): writes corrected XYZUVW to motor_devices.json
//     → User clicks "Continue" → recipe proceeds to next step
//
// IMPORTANT — KEEPING TRAINER AND PLUGIN IN SYNC:
//   If you modify the trainer (data structures, math, retrace logic, or UI),
//   verify that the following files remain consistent:
//     - PlacementCorrectionData.h        (this file — structs and math)
//     - PlacementCorrectionStore.h/cpp   (JSON serialization of these structs)
//     - PlacementRetraceWidget.h/cpp     (retrace wizard — uses these structs)
//     - PlacementCorrectionProcesses.cpp (recipe plugin — parameter names)
//     - SystemCommandHandler.cpp         (command handler — callback interface)
//     - RunProgramPage.cpp               (callback wiring and signal handlers)
//   In particular:
//     - Adding a new parameter to PlacementCorrectionConfig requires updating
//       the Store (JSON read/write), the plugin (UI + BuildSequence), and
//       the retrace widget (if it uses the new parameter)
//     - Changing the retrace flow (signals, states) requires updating the
//       RunProgramPage signal handlers that control recipe advancement
//
// ============================================================================
#pragma once

#include <string>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// Data Structures
// ============================================================================

// Two gantry XY(Z) points that define a line on the measurement device.
// Used for both teach and retrace — the line angle is computed from P1→P2.
struct PlacementLinePoints {
    std::string name;       // "Reference" or "Target"
    double p1x = 0.0;      // gantry-main X of first point (mm)
    double p1y = 0.0;      // gantry-main Y of first point (mm)
    double p1z = 0.0;      // gantry-main Z of first point (mm)
    double p1Exposure = 800.0; // camera exposure for P1 (us)
    std::string p1ImageFile;   // captured image filename for P1
    double p2x = 0.0;      // gantry-main X of second point (mm)
    double p2y = 0.0;      // gantry-main Y of second point (mm)
    double p2z = 0.0;      // gantry-main Z of second point (mm)
    double p2Exposure = 800.0; // camera exposure for P2 (us)
    std::string p2ImageFile;   // captured image filename for P2
    double angleDeg = 0.0;  // computed line angle in XY plane (degrees)
    bool valid = false;     // true when both points have been captured
};

// Configuration that maps the correction to a hex device's named positions.
// masterPosition is the golden reference (read-only); targetPosition receives
// the corrected XYZUVW after retrace. These position names correspond to
// entries in motor_devices.json / motor_devices.local.json.
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
    bool applyUCorrection = true;       // apply U (rotation) correction; default yes

    // Camera for retrace exposure control
    std::string cameraId;               // camera to configure during retrace

    // ------------------------------------------------------------------
    // Manual offset tuner (persisted). Applied at Save time in
    // PlacementRetraceWidget::onSaveToTarget() after computeRetraceResults.
    // XYZ are in millimeters, UVW in degrees — both are added directly
    // (positive = +, negative = -) to the corrected result:
    //   savedX = correctedX + tuneOffsetX   (mm)
    //   savedU = correctedU + tuneOffsetU   (deg)
    // Persisted in the training set JSON so the offset sticks across
    // retraces until the user clears it.
    // ------------------------------------------------------------------
    double tuneOffsetX = 0.0;
    double tuneOffsetY = 0.0;
    double tuneOffsetZ = 0.0;
    double tuneOffsetU = 0.0;
    double tuneOffsetV = 0.0;
    double tuneOffsetW = 0.0;
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
// Summary diagram thumbnail offset (scene-space, persisted)
// ============================================================================

struct ThumbOffset {
    double dx = 80.0;
    double dy = -60.0;
};

// ============================================================================
// PlacementCorrectionSet (defined after math namespace so it can use it)
// ============================================================================

// Complete training set — contains everything needed for a retrace:
// the taught reference/target lines, the correction config (device + positions),
// and computed results. Persisted as JSON via PlacementCorrectionStore.
// The measureDevice is the gantry that physically moves to measure lines;
// the targetDevice (in config) is the hex that receives the correction.
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

    // Summary diagram thumbnail offsets: [0]=RefP1 [1]=RefP2 [2]=TgtP1 [3]=TgtP2
    ThumbOffset summaryThumbOffsets[4];

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
