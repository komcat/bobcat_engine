// CalibYawMath.h
// Shared yaw-angle math used by Family E (CalibYawCorrectionCommands) and
// Family F (CalibYawToTemplateCommands). Inline so both helpers pick up the
// same body without an extra TU. The angle is measured from +Y axis using
// atan2(deltaX, deltaY) — that convention is baked into how downstream
// branches apply the correction (sign + flip-by-orientation logic in
// ApplyYawCorrection / ApplyYawToTemplateCorrection), so the function must
// stay shared rather than fork per family.
#pragma once

#include <cmath>
#include <string>

class LogManager;

namespace calib_yaw {

inline double CalculateYawAngle(
    double corner1X, double corner1Y,
    double corner2X, double corner2Y,
    LogManager* logger = nullptr);

}  // namespace calib_yaw

// LogManager forward decl is enough at the inline call site, but we need the
// full type to call ->Info(), so the implementation lives below the inline
// declaration and pulls LogManager.h in. Helpers that include this header
// already include LogManager.h transitively via ApplicationContext.h.

#include "LogManager.h"

namespace calib_yaw {

inline double CalculateYawAngle(
    double corner1X, double corner1Y,
    double corner2X, double corner2Y,
    LogManager* logger)
{
  double deltaX = corner2X - corner1X;
  double deltaY = corner2Y - corner1Y;

  // Yaw measured from +Y axis (atan2 args swapped intentionally).
  double yawRadians = std::atan2(deltaX, deltaY);
  double yawDegrees = yawRadians * 180.0 / 3.14159265358979323846;

  if (logger) {
    logger->Info("Calculated yaw angle: " + std::to_string(yawDegrees) + " deg"
                 " (deltaX=" + std::to_string(deltaX) +
                 ", deltaY=" + std::to_string(deltaY) + ")",
                 "CalibrationHandler");
  }

  return yawDegrees;
}

}  // namespace calib_yaw
