// EdgeTransferData.h - Data structures and math for Edge Transfer Trainer
//
// ============================================================================
// EDGE TRANSFER MODULE - OVERVIEW (V1: display-only)
// ============================================================================
//
// PURPOSE:
//   Transfer the orientation (roll / pitch / yaw) of a reference edge onto a
//   motion device (hex-left / hex-right). Two edges are captured in the
//   gantry-main coordinate frame, each by two XYZ points:
//
//     * Reference edge: P1, P2 (golden geometry)
//     * Target edge:    P3, P4 (edge currently held on the target hex device)
//
//   From each edge the trainer computes:
//     - xyDistance   = sqrt(dx^2 + dy^2)
//     - zDelta       = p2z - p1z
//     - angleToX     = atan2(dy, dx)   (deg, CCW from +X axis)
//     - angleToY     = atan2(dx, dy)   (deg, angle from +Y axis toward +X)
//     - verticalTilt = atan2(dz, xyDistance)   (deg, elevation above XY)
//
//   The user then assigns each target-device axis (U, V, W) a role:
//     None / Roll / Pitch / Yaw
//   For the chosen roles, the trainer shows the correction (delta) needed
//   to make the target edge's orientation match the reference edge's.
//
// SIGN CONVENTION:
//   delta  = (target metric) - (reference metric)
//   correction = -delta   (motion required to align target back to reference)
//   reverseSign flips all corrections for devices whose encoder convention
//   is inverted vs. the gantry XY frame.
//
// V1 SCOPE:
//   This trainer only displays calculations. It does NOT move the hex device
//   or persist sets. The UI is intentionally minimal; persistence and
//   recipe-plugin integration can be added later if the math proves correct.
//
#pragma once

#include <string>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// Edge: two gantry-main XYZ points + computed metrics
// ============================================================================

struct EdgePoints {
    std::string name;       // "Reference" or "Target"
    double p1x = 0.0;      // mm
    double p1y = 0.0;
    double p1z = 0.0;
    double p2x = 0.0;
    double p2y = 0.0;
    double p2z = 0.0;

    // Captured image filenames (relative to the trainer image directory).
    // Empty string = no image captured for that point.
    std::string p1ImageFile;
    std::string p2ImageFile;

    // Computed (filled by EdgeTransferMath::Recalc)
    double xyDistance = 0.0;       // mm
    double zDelta = 0.0;           // mm  (p2z - p1z)
    double angleToX = 0.0;         // deg, atan2(dy, dx)
    double angleToY = 0.0;         // deg, atan2(dx, dy)
    double verticalTilt = 0.0;     // deg, atan2(dz, xyDist)

    bool valid = false;            // true once both points captured
};

// ============================================================================
// Axis role assignment for target device UVW
// ============================================================================

enum class EdgeAxisRole {
    None = 0,
    Roll,    // rotation about X - mapped from verticalTilt delta
    Pitch,   // rotation about Y - mapped from verticalTilt delta
    Yaw      // rotation about Z - mapped from angleToX delta
};

inline const char* EdgeAxisRoleToString(EdgeAxisRole r) {
    switch (r) {
        case EdgeAxisRole::None:  return "None";
        case EdgeAxisRole::Roll:  return "Roll";
        case EdgeAxisRole::Pitch: return "Pitch";
        case EdgeAxisRole::Yaw:   return "Yaw";
    }
    return "None";
}

inline EdgeAxisRole EdgeAxisRoleFromString(const std::string& s) {
    if (s == "Roll")  return EdgeAxisRole::Roll;
    if (s == "Pitch") return EdgeAxisRole::Pitch;
    if (s == "Yaw")   return EdgeAxisRole::Yaw;
    return EdgeAxisRole::None;
}

struct EdgeAxisMapping {
    EdgeAxisRole u = EdgeAxisRole::None;
    EdgeAxisRole v = EdgeAxisRole::None;
    EdgeAxisRole w = EdgeAxisRole::None;
    bool reverseU = false;   // flip sign of U correction
    bool reverseV = false;   // flip sign of V correction
    bool reverseW = false;   // flip sign of W correction

    // Per-axis "Apply Selected Corrections" flags. Default ON — the axis is
    // included in the sequential apply unless the user explicitly disables it.
    bool applyU = true;
    bool applyV = true;
    bool applyW = true;
};

struct EdgeTransferConfig {
    std::string targetDevice = "hex-left";   // "hex-left" or "hex-right"
    std::string cameraId;                    // camera used to capture point images
    EdgeAxisMapping mapping;
};

// ============================================================================
// Math (pure functions, no external dependencies)
// ============================================================================

namespace EdgeTransferMath {

inline double NormalizeAngle(double deg) {
    for (int i = 0; i < 4 && deg > 180.0;  ++i) deg -= 360.0;
    for (int i = 0; i < 4 && deg < -180.0; ++i) deg += 360.0;
    return deg;
}

inline void Recalc(EdgePoints& e) {
    const double dx = e.p2x - e.p1x;
    const double dy = e.p2y - e.p1y;
    const double dz = e.p2z - e.p1z;
    e.xyDistance = std::sqrt(dx * dx + dy * dy);
    e.zDelta = dz;
    e.angleToX = std::atan2(dy, dx) * 180.0 / M_PI;
    e.angleToY = std::atan2(dx, dy) * 180.0 / M_PI;
    e.verticalTilt = (e.xyDistance > 1e-9)
        ? std::atan2(dz, e.xyDistance) * 180.0 / M_PI
        : 0.0;
}

inline double DeltaYaw(const EdgePoints& ref, const EdgePoints& tgt) {
    return NormalizeAngle(tgt.angleToX - ref.angleToX);
}

inline double DeltaTilt(const EdgePoints& ref, const EdgePoints& tgt) {
    return NormalizeAngle(tgt.verticalTilt - ref.verticalTilt);
}

// Correction = -delta so that applying it to the target brings it toward ref.
inline double CorrectionForRole(EdgeAxisRole role,
                                double deltaYaw,
                                double deltaTilt,
                                bool reverseSign)
{
    double base = 0.0;
    switch (role) {
        case EdgeAxisRole::Yaw:   base = -deltaYaw;  break;
        case EdgeAxisRole::Roll:  base = -deltaTilt; break;
        case EdgeAxisRole::Pitch: base = -deltaTilt; break;
        case EdgeAxisRole::None:  base = 0.0;        break;
    }
    return reverseSign ? -base : base;
}

} // namespace EdgeTransferMath

// ============================================================================
// EdgeTransferSet
// ============================================================================

struct EdgeTransferSet {
    std::string name;
    std::string measureDevice = "gantry-main";

    EdgePoints referenceEdge;
    EdgePoints targetEdge;
    EdgeTransferConfig config;

    // Computed
    double deltaYawDeg = 0.0;
    double deltaTiltDeg = 0.0;
    double correctionU = 0.0;
    double correctionV = 0.0;
    double correctionW = 0.0;
    bool   deltasValid = false;

    void Recalculate() {
        EdgeTransferMath::Recalc(referenceEdge);
        EdgeTransferMath::Recalc(targetEdge);
        if (!referenceEdge.valid || !targetEdge.valid) {
            deltasValid = false;
            correctionU = correctionV = correctionW = 0.0;
            return;
        }
        deltaYawDeg  = EdgeTransferMath::DeltaYaw (referenceEdge, targetEdge);
        deltaTiltDeg = EdgeTransferMath::DeltaTilt(referenceEdge, targetEdge);
        correctionU = EdgeTransferMath::CorrectionForRole(
            config.mapping.u, deltaYawDeg, deltaTiltDeg, config.mapping.reverseU);
        correctionV = EdgeTransferMath::CorrectionForRole(
            config.mapping.v, deltaYawDeg, deltaTiltDeg, config.mapping.reverseV);
        correctionW = EdgeTransferMath::CorrectionForRole(
            config.mapping.w, deltaYawDeg, deltaTiltDeg, config.mapping.reverseW);
        deltasValid = true;
    }
};
