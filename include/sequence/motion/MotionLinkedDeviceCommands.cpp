// MotionLinkedDeviceCommands.cpp
// Family H implementation. See MotionLinkedDeviceCommands.h.
#include "MotionLinkedDeviceCommands.h"

#include "MotionCommandHandler.h"          // for parent->WaitForMotionWithStopCheck
#include "unified_motion_manager.h"
#include "DeviceLinkManager.h"             // brings CoordinateTransformer transitively
#include "LogManager.h"
#include "SequenceContext.h"

#include <cmath>
#include <string>

std::optional<bool> MotionLinkedDeviceCommands::TryExecute(
  const std::string& /*deviceName*/,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context,
  const MotionCommandDeps& deps)
{
  UnifiedMotionManager* motionManager = deps.motion;

  // ============================================================================
  // REGISTER DEVICE LINK - Link a device to gantry-main for coordinated movement
  // ============================================================================
  if (methodName == "RegisterDeviceLink") {
    if (!deps.linkMgr) {
      errorMessage = "RegisterDeviceLink: DeviceLinkManager not available";
      return false;
    }

    // Get parameters
    auto primaryIt = params.find("primaryDevice");
    auto linkedIt = params.find("linkedDevice");

    if (primaryIt == params.end()) {
      errorMessage = "RegisterDeviceLink: Missing required parameter 'primaryDevice'";
      return false;
    }
    if (linkedIt == params.end()) {
      errorMessage = "RegisterDeviceLink: Missing required parameter 'linkedDevice'";
      return false;
    }

    std::string primaryDevice = primaryIt->second;
    std::string linkedDevice = linkedIt->second;

    if (deps.logger) {
      deps.logger->Info("RegisterDeviceLink: Registering link " + primaryDevice + " -> " + linkedDevice, "MotionHandler");
    }

    // Register the link (captures current positions and calculates offset)
    if (!deps.linkMgr->RegisterLink(primaryDevice, linkedDevice)) {
      errorMessage = "RegisterDeviceLink: Failed to register link between " + primaryDevice + " and " + linkedDevice;
      return false;
    }

    // Save to file
    auto saveIt = params.find("save_to_file");
    bool saveToFile = true;
    if (saveIt != params.end() && (saveIt->second == "false" || saveIt->second == "0")) {
      saveToFile = false;
    }

    if (saveToFile) {
      if (!deps.linkMgr->SaveToFile("config/device_links.json")) {
        if (deps.logger) {
          deps.logger->Warning("RegisterDeviceLink: Link registered but failed to save to file", "MotionHandler");
        }
      }
    }

    if (deps.logger) {
      deps.logger->Info("RegisterDeviceLink: Successfully registered link", "MotionHandler");
    }

    return true;
  }

  // ============================================================================
  // COMPENSATE DEVICE LINK - Move linked device to restore original relationship
  // ============================================================================
  if (methodName == "CompensateDeviceLink") {
    if (!deps.linkMgr) {
      errorMessage = "CompensateDeviceLink: DeviceLinkManager not available";
      return false;
    }

    // Get parameters
    auto primaryIt = params.find("primaryDevice");
    auto linkedIt = params.find("linkedDevice");

    if (primaryIt == params.end()) {
      errorMessage = "CompensateDeviceLink: Missing required parameter 'primaryDevice'";
      return false;
    }
    if (linkedIt == params.end()) {
      errorMessage = "CompensateDeviceLink: Missing required parameter 'linkedDevice'";
      return false;
    }

    std::string primaryDevice = primaryIt->second;
    std::string linkedDevice = linkedIt->second;

    // Get axes to compensate (default: XY)
    std::string axes = "XY";
    auto axesIt = params.find("axes");
    if (axesIt != params.end()) {
      axes = axesIt->second;
    }

    if (deps.logger) {
      deps.logger->Info("CompensateDeviceLink: Compensating " + linkedDevice + " relative to " + primaryDevice + " (axes: " + axes + ")", "MotionHandler");
    }

    // Check if link exists
    if (!deps.linkMgr->HasLink(primaryDevice, linkedDevice)) {
      errorMessage = "CompensateDeviceLink: No link registered for " + primaryDevice + " -> " + linkedDevice;
      return false;
    }

    // Get the compensation move (in linked device's local coordinates)
    CoordinateTransformer::Vector6D compensation;
    if (!deps.linkMgr->GetCompensationMove(primaryDevice, linkedDevice, compensation, axes)) {
      errorMessage = "CompensateDeviceLink: Failed to calculate compensation move";
      return false;
    }

    // Log the compensation values
    if (deps.logger) {
      std::string compStr = "CompensateDeviceLink: Moving " + linkedDevice + " by X=" +
                            std::to_string(compensation.x) + ", Y=" +
                            std::to_string(compensation.y) + ", Z=" +
                            std::to_string(compensation.z);
      deps.logger->Info(compStr, "MotionHandler");
    }

    // Execute relative moves for each LOCAL axis that has non-zero compensation
    // NOTE: The "axes" parameter filters GLOBAL axes in GetCompensationMove(),
    // so we move ALL non-zero local compensation values regardless of axis names.
    // (e.g., "XY" global may result in local Y and Z movement due to coordinate transform)
    bool hasError = false;
    std::string errorAxis;

    // Move local X if compensation is significant
    if (std::abs(compensation.x) > 0.0001) {  // Only move if > 0.1 micron
      if (!motionManager->MoveRelative(linkedDevice, "X", compensation.x, false)) {
        hasError = true;
        errorAxis = "X";
      }
    }

    // Move local Y if compensation is significant
    if (!hasError && std::abs(compensation.y) > 0.0001) {
      if (!motionManager->MoveRelative(linkedDevice, "Y", compensation.y, false)) {
        hasError = true;
        errorAxis = "Y";
      }
    }

    // Move local Z if compensation is significant
    if (!hasError && std::abs(compensation.z) > 0.0001) {
      if (!motionManager->MoveRelative(linkedDevice, "Z", compensation.z, false)) {
        hasError = true;
        errorAxis = "Z";
      }
    }

    if (hasError) {
      errorMessage = "CompensateDeviceLink: Failed to move " + linkedDevice + " axis " + errorAxis;
      return false;
    }

    // Wait for motion to complete (callback through orchestrator — see plan §3)
    if (!deps.parent->WaitForMotionWithStopCheck(motionManager, linkedDevice, context, 30000)) {
      errorMessage = "CompensateDeviceLink: Motion stopped or timed out";
      return false;
    }

    if (deps.logger) {
      deps.logger->Info("CompensateDeviceLink: Compensation complete", "MotionHandler");
    }

    return true;
  }

  return std::nullopt;
}
