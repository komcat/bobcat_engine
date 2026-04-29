// MotionVelocityCommands.cpp
// Family B implementation. See MotionVelocityCommands.h.
#include "MotionVelocityCommands.h"

#include "unified_motion_manager.h"
#include "LogManager.h"
#include "SequenceContext.h"

#include <string>

std::optional<bool> MotionVelocityCommands::TryExecute(
  const std::string& deviceName,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context,
  const MotionCommandDeps& deps)
{
  UnifiedMotionManager* motionManager = deps.motion;

  // ============================================================================
  // SET VELOCITY
  // ============================================================================
  if (methodName == "SetVelocity") {
    auto axisIt = params.find("axis");
    auto velIt = params.find("velocity");

    if (axisIt == params.end() || velIt == params.end()) {
      errorMessage = "Missing required parameters: axis, velocity";
      return false;
    }

    std::string axis = axisIt->second;
    double velocity = std::stod(velIt->second);

    if (deps.logger) {
      deps.logger->Info(
        "Setting velocity for " + deviceName + " axis " + axis +
        " to " + std::to_string(velocity),
        "MotionHandler"
      );
    }

    if (!motionManager->SetVelocity(deviceName, axis, velocity)) {
      errorMessage = "SetVelocity failed";
      return false;
    }

    return true;
  }

  // ============================================================================
  // SET ALL AXIS VELOCITY (ACS - only installed axes)
  // ============================================================================
  if (methodName == "SetAllAxisVelocity") {
    auto velIt = params.find("velocity");

    if (velIt == params.end()) {
      errorMessage = "Missing required parameter: velocity";
      return false;
    }

    double velocity = std::stod(velIt->second);
    std::string installedAxes = motionManager->GetInstalledAxes(deviceName);

    if (installedAxes.empty()) {
      errorMessage = "No installed axes found for device: " + deviceName;
      return false;
    }

    if (deps.logger) {
      deps.logger->Info(
        "Setting velocity for " + deviceName + " installed axes [" + installedAxes +
        "] to " + std::to_string(velocity) + " mm/s",
        "MotionHandler"
      );
    }

    // Set velocity only on axes that are actually installed
    static const std::string allAxes = "XYZUVW";
    bool anyFailed = false;
    for (char axisChar : allAxes) {
      if (installedAxes.find(axisChar) != std::string::npos) {
        std::string axis(1, axisChar);
        if (!motionManager->SetVelocity(deviceName, axis, velocity)) {
          if (deps.logger) {
            deps.logger->Warning(
              "Failed to set velocity for axis " + axis + " on " + deviceName,
              "MotionHandler"
            );
          }
          anyFailed = true;
        }
      }
    }

    if (anyFailed) {
      errorMessage = "SetAllAxisVelocity: one or more axes failed";
      return false;
    }

    return true;
  }

  // ============================================================================
  // SET SYSTEM VELOCITY (PI hexapods)
  // ============================================================================
  if (methodName == "SetSystemVelocity") {
    auto velIt = params.find("velocity");

    if (velIt == params.end()) {
      errorMessage = "Missing required parameter: velocity";
      return false;
    }

    double velocity = std::stod(velIt->second);

    if (deps.logger) {
      deps.logger->Info(
        "Setting system velocity for " + deviceName +
        " to " + std::to_string(velocity) + " mm/s",
        "MotionHandler"
      );
    }

    if (!motionManager->SetSystemVelocity(deviceName, velocity)) {
      errorMessage = "SetSystemVelocity failed";
      return false;
    }

    return true;
  }

  // ============================================================================
  // GET SYSTEM VELOCITY (PI hexapods) — captures mm/s to context
  // ============================================================================
  if (methodName == "GetSystemVelocity") {
    double velocity = 0.0;
    if (!motionManager->GetSystemVelocity(deviceName, velocity)) {
      errorMessage = "GetSystemVelocity failed (device may not be a PI hexapod)";
      return false;
    }

    auto captureIt = params.find("captureAs");
    if (captureIt != params.end() && !captureIt->second.empty() && context) {
      context->SetValue(captureIt->second, static_cast<float>(velocity),
                        deviceName, "GetSystemVelocity");
    }

    if (deps.logger) {
      deps.logger->Debug(
        "GetSystemVelocity " + deviceName + " = " +
        std::to_string(velocity) + " mm/s",
        "MotionHandler"
      );
    }

    return true;
  }

  return std::nullopt;
}
