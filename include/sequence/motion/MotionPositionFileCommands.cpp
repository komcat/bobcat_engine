// MotionPositionFileCommands.cpp
// Family G implementation. See MotionPositionFileCommands.h.
#include "MotionPositionFileCommands.h"

#include "unified_motion_manager.h"
#include "product_position_manager.h"
#include "SequenceContext.h"
#include "motions/MoeChannelLayout.h"

#include <iostream>

std::optional<bool> MotionPositionFileCommands::TryExecute(
  const std::string& deviceName,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context,
  const MotionCommandDeps& deps)
{
  UnifiedMotionManager* motionManager = deps.motion;

  // ============================================================================
  // UPDATE POSITION AXIS FROM CONTEXT
  // ============================================================================
  // Updates a single axis of a named position with a value from context
  // Then saves the product file
  // params: position_name, axis (X/Y/Z/U/V/W), context_key, save_to_file (true/false)
  if (methodName == "UpdatePositionAxis") {
    auto posNameIt = params.find("position_name");
    auto axisIt = params.find("axis");
    auto contextKeyIt = params.find("context_key");

    if (posNameIt == params.end() || axisIt == params.end() || contextKeyIt == params.end()) {
      errorMessage = "UpdatePositionAxis: Missing required parameters (position_name, axis, context_key)";
      return false;
    }

    std::string positionName = posNameIt->second;
    std::string axis = axisIt->second;
    std::string contextKey = contextKeyIt->second;

    // Validate axis
    if (axis != "X" && axis != "Y" && axis != "Z" &&
        axis != "U" && axis != "V" && axis != "W") {
      errorMessage = "UpdatePositionAxis: Invalid axis '" + axis + "'. Must be X, Y, Z, U, V, or W";
      return false;
    }

    // Get value from context
    if (!context) {
      errorMessage = "UpdatePositionAxis: No context available";
      return false;
    }

    // Strip $ prefix if present
    std::string actualKey = contextKey;
    while (!actualKey.empty() && actualKey[0] == '$') {
      actualKey = actualKey.substr(1);
    }

    if (!context->HasValue(actualKey)) {
      errorMessage = "UpdatePositionAxis: Context key '" + contextKey + "' not found";
      return false;
    }

    double newValue = static_cast<double>(context->GetNumeric(actualKey, 0.0f));

    // Check if ProductPositionManager is available
    if (!deps.posMgr) {
      errorMessage = "UpdatePositionAxis: ProductPositionManager not available";
      return false;
    }

    // Get current position
    PositionStruct currentPos;
    if (!deps.posMgr->GetPosition(deviceName, positionName, currentPos)) {
      errorMessage = "UpdatePositionAxis: Position '" + positionName + "' not found for device '" + deviceName + "'";
      return false;
    }

    // Update the specific axis
    if (axis == "X") currentPos.x = newValue;
    else if (axis == "Y") currentPos.y = newValue;
    else if (axis == "Z") currentPos.z = newValue;
    else if (axis == "U") currentPos.u = newValue;
    else if (axis == "V") currentPos.v = newValue;
    else if (axis == "W") currentPos.w = newValue;

    // Update position in ProductPositionManager
    deps.posMgr->AddPosition(deviceName, positionName, currentPos);

    // Also update in UnifiedMotionManager (runtime)
    motionManager->AddNamedPosition(deviceName, positionName, currentPos);

    std::cout << "[MotionCommandHandler] Updated position '" << positionName
              << "' " << axis << " = " << newValue
              << " (from context: " << contextKey << ")" << std::endl;

    // Save to file if requested (default: true)
    bool saveToFile = true;
    auto saveIt = params.find("save_to_file");
    if (saveIt != params.end() && (saveIt->second == "false" || saveIt->second == "0")) {
      saveToFile = false;
    }

    if (saveToFile) {
      if (deps.posMgr->SaveProduct()) {
        std::cout << "[MotionCommandHandler] Saved product file with updated position" << std::endl;
      } else {
        errorMessage = "UpdatePositionAxis: Position updated but failed to save product file";
        return false;
      }
    }

    // Store result in context
    context->SetValue("update_position.success", true);
    context->SetValue("update_position.device", deviceName);
    context->SetValue("update_position.position", positionName);
    context->SetValue("update_position.axis", axis);
    context->SetValue("update_position.value", static_cast<float>(newValue));

    return true;
  }

  // ============================================================================
  // UPDATE POSITION ALL FROM CURRENT DEVICE POSITION
  // ============================================================================
  // Updates all available axes of a named position with current device position
  // Then saves the product file
  // params: position_name, save_to_file (true/false)
  if (methodName == "UpdatePositionAll") {
    auto posNameIt = params.find("position_name");

    if (posNameIt == params.end()) {
      errorMessage = "UpdatePositionAll: Missing required parameter (position_name)";
      return false;
    }

    std::string positionName = posNameIt->second;

    // Check if ProductPositionManager is available
    if (!deps.posMgr) {
      errorMessage = "UpdatePositionAll: ProductPositionManager not available";
      return false;
    }

    // Get current device position
    PositionStruct currentPos = motionManager->GetCurrentPosition(deviceName);

    // Get installed axes for this device
    std::string installedAxes = motionManager->GetInstalledAxes(deviceName);

    // Get existing position (if any) to preserve waypoints
    PositionStruct existingPos;
    bool hasExisting = deps.posMgr->GetPosition(deviceName, positionName, existingPos);

    // Build updated position - only update axes that are installed
    PositionStruct updatedPos;
    if (hasExisting) {
      updatedPos = existingPos;  // Preserve waypoints and other data
    }

    // Update each axis that exists in installedAxes
    if (installedAxes.find('X') != std::string::npos) updatedPos.x = currentPos.x;
    if (installedAxes.find('Y') != std::string::npos) updatedPos.y = currentPos.y;
    if (installedAxes.find('Z') != std::string::npos) updatedPos.z = currentPos.z;
    if (installedAxes.find('U') != std::string::npos) updatedPos.u = currentPos.u;
    if (installedAxes.find('V') != std::string::npos) updatedPos.v = currentPos.v;
    if (installedAxes.find('W') != std::string::npos) updatedPos.w = currentPos.w;

    // Update position in ProductPositionManager
    deps.posMgr->AddPosition(deviceName, positionName, updatedPos);

    // Also update in UnifiedMotionManager (runtime)
    motionManager->AddNamedPosition(deviceName, positionName, updatedPos);

    std::cout << "[MotionCommandHandler] Updated position '" << positionName
              << "' with all axes from current position"
              << " (Axes: " << installedAxes << ")" << std::endl;
    std::cout << "  X=" << updatedPos.x << " Y=" << updatedPos.y << " Z=" << updatedPos.z
              << " U=" << updatedPos.u << " V=" << updatedPos.v << " W=" << updatedPos.w << std::endl;

    // Save to file if requested (default: true)
    bool saveToFile = true;
    auto saveIt = params.find("save_to_file");
    if (saveIt != params.end() && (saveIt->second == "false" || saveIt->second == "0")) {
      saveToFile = false;
    }

    if (saveToFile) {
      if (deps.posMgr->SaveProduct()) {
        std::cout << "[MotionCommandHandler] Saved product file with updated position" << std::endl;
      } else {
        errorMessage = "UpdatePositionAll: Position updated but failed to save product file";
        return false;
      }
    }

    // Store result in context
    if (context) {
      context->SetValue("update_position_all.success", true);
      context->SetValue("update_position_all.device", deviceName);
      context->SetValue("update_position_all.position", positionName);
      context->SetValue("update_position_all.axes", installedAxes);
    }

    return true;
  }

  // ============================================================================
  // UPDATE CH1-6 (+ MOE-A2) FROM MOE-A1 ANCHOR
  // ============================================================================
  // Derives the SWIR channel named positions ch1..ch6 plus MOE-A2 on
  // "gantry-main" from the taught MOE-A1 position (X/Z kept, Y shifted in Y- by
  // the fixed per-channel spacing) and saves them to the product file. This is
  // the headless equivalent of the SWIR window "update ch1-6" button, exposed
  // for Lua / recipes. No parameters.
  if (methodName == "UpdateChannelsFromMoeA1") {
    if (!motionManager) {
      errorMessage = "UpdateChannelsFromMoeA1: Motion manager not available";
      return false;
    }

    const bobcat::MoeChannelDeriveResult r =
        bobcat::DeriveAndSaveMoeChannelsFromMoeA1(motionManager);

    if (!r.anchorFound) {
      errorMessage = "UpdateChannelsFromMoeA1: No saved 'MOE-A1' position on "
                     "gantry-main - teach MOE-A1 first";
      return false;
    }

    if (context) {
      context->SetValue("update_channels.saved", static_cast<float>(r.saved));
      context->SetValue("update_channels.total", static_cast<float>(r.total));
      context->SetValue("update_channels.success", r.ok());
    }

    if (!r.ok()) {
      std::string failed;
      for (const std::string& f : r.failed) {
        if (!failed.empty()) failed += ", ";
        failed += f;
      }
      errorMessage = "UpdateChannelsFromMoeA1: saved " + std::to_string(r.saved) +
                     "/" + std::to_string(r.total) + " positions; failed: " + failed;
      return false;
    }

    std::cout << "[MotionCommandHandler] UpdateChannelsFromMoeA1: saved "
              << r.saved << "/" << r.total << " positions from MOE-A1" << std::endl;
    return true;
  }

  return std::nullopt;
}
