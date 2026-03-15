// MotionCommandHandler.cpp
#include "MotionCommandHandler.h"
#include "GraphMoveWorker.h"          // ✅ NEW: For parallel graphNode moves
#include "unified_motion_manager.h"
#include "LogManager.h"
#include "SequenceContext.h"
#include "AdaptiveScanController.h"  // ✅ NEW: For XCAN command
#include "DataStore.h"                // ✅ NEW: For XCAN command
#include "ScanTypes.h"                // ✅ NEW: For XCAN command
#include "../graph/MotionGraphManager.h"  // ✅ NEW: For MoveToGraphNode command
#include "../products/product_position_manager.h"  // ✅ NEW: For MoveToGraphNode command
#include "../vision/VisionManager.h"  // For pixel-to-mm calibration in MoveToContextPosition
#include "../motions/DeviceLinkManager.h"  // For linked device commands
#include "../motions/PI/pi_controller.h"   // For FastAlignXY/ZXY commands (direct PI access)
#include <chrono>
#include <thread>
#include <iostream>                   // ✅ For debug logging
#include <regex>                      // ✅ NEW: For device name validation
#include <atomic>                     // ✅ NEW: For scan completion flag
#include <condition_variable>         // ✅ NEW: For scan completion signaling
#include <cmath>                      // ✅ NEW: For position distance calculation
#include <vector>                     // ✅ NEW: For parallel workers
#include <sstream>                    // ✅ NEW: For XCANExtended step size parsing
#include <algorithm>                  // ✅ NEW: For std::sort, std::greater
#include <limits>                     // ✅ NEW: For std::numeric_limits

// Helper to resolve context: references or parse direct values
double MotionCommandHandler::ResolveNumericValue(const std::string& valueStr, SequenceContext* context)
{
  // Check if value is a context reference (e.g., "context:needle_target_x")
  if (valueStr.find("context:") == 0) {
    std::string refKey = valueStr.substr(8);  // Remove "context:" prefix
    if (context) {
      double value = static_cast<double>(context->GetNumeric(refKey, 0.0f));
      if (m_logger) {
        m_logger->Info("Resolved " + valueStr + " = " + std::to_string(value), "MotionHandler");
      }
      return value;
    }
    if (m_logger) {
      m_logger->Warning("No context available to resolve " + valueStr + ", using 0.0", "MotionHandler");
    }
    return 0.0;  // Fallback if no context
  } else {
    // Parse as direct numeric value
    double value = std::stod(valueStr);
    if (m_logger) {
      m_logger->Info("Parsed direct value " + valueStr + " = " + std::to_string(value), "MotionHandler");
    }
    return value;
  }
}

bool MotionCommandHandler::ExecuteCommand(
  void* manager,
  const std::string& deviceName,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context)
{
  UnifiedMotionManager* motionManager = static_cast<UnifiedMotionManager*>(manager);

  // Context can be used for capturing positions, velocities, etc.
  // Example: if (context && params.find("captureAs") != params.end()) {
  //   context->SetValue(params.at("captureAs"), value, deviceName, methodName);
  // }

  // ============================================================================
  // SINGLE AXIS ABSOLUTE MOVE
  // ============================================================================
  if (methodName == "MoveToPosition") {
    auto axisIt = params.find("axis");
    auto posIt = params.find("position");

    if (axisIt == params.end() || posIt == params.end()) {
      errorMessage = "Missing required parameters: axis, position";
      return false;
    }

    std::string axis = axisIt->second;
    double position = std::stod(posIt->second);

    // Check if non-blocking mode requested explicitly
    bool explicitNonBlocking = false;
    auto blockingIt = params.find("blocking");
    if (blockingIt != params.end() && blockingIt->second == "false") {
      explicitNonBlocking = true;
    }

    if (m_logger) {
      m_logger->Info(
        "Moving " + deviceName + " axis " + axis + " to " + std::to_string(position),
        "MotionHandler"
      );
    }

    // ✅ SAFETY: Always use non-blocking mode + stop checking (unless explicitly requested non-blocking)
    if (!motionManager->MoveToPosition(deviceName, axis, position, false)) {
      errorMessage = "MoveToPosition failed to start";
      return false;
    }

    // If explicit non-blocking requested, return immediately
    if (explicitNonBlocking) {
      return true;
    }

    // ✅ SAFETY: Wait for completion with stop flag monitoring and position verification
    TargetPosition target;
    if (axis == "X") {
      target.hasX = true;
      target.x = position;
    } else if (axis == "Y") {
      target.hasY = true;
      target.y = position;
    } else if (axis == "Z") {
      target.hasZ = true;
      target.z = position;
    }

    if (!WaitForMotionWithStopCheck(manager, deviceName, context, 30000, &target)) {
      errorMessage = "Motion stopped, timeout, or position verification failed";
      return false;
    }

    return true;
  }

  // ============================================================================
  // MULTI-AXIS ABSOLUTE MOVE (X, Y, Z)
  // ============================================================================
  else if (methodName == "MoveToAbs") {
    auto xIt = params.find("x");
    auto yIt = params.find("y");
    auto zIt = params.find("z");

    if (xIt == params.end() || yIt == params.end() || zIt == params.end()) {
      errorMessage = "Missing required parameters: x, y, z";
      return false;
    }

    double x = ResolveNumericValue(xIt->second, context);
    double y = ResolveNumericValue(yIt->second, context);
    double z = ResolveNumericValue(zIt->second, context);

    // ✅ NEW: Support optional U, V, W parameters for hexapods
    double u = 0.0, v = 0.0, w = 0.0;
    bool hasUVW = false;

    auto uIt = params.find("u");
    auto vIt = params.find("v");
    auto wIt = params.find("w");

    if (uIt != params.end() && vIt != params.end() && wIt != params.end()) {
      u = ResolveNumericValue(uIt->second, context);
      v = ResolveNumericValue(vIt->second, context);
      w = ResolveNumericValue(wIt->second, context);
      hasUVW = true;
    }

    double velocity = 100.0;  // Default velocity

    auto velIt = params.find("velocity");
    if (velIt != params.end()) {
      velocity = ResolveNumericValue(velIt->second, context);
    }

    // Check if non-blocking mode requested explicitly
    bool explicitNonBlocking = false;
    auto blockingIt = params.find("blocking");
    if (blockingIt != params.end() && blockingIt->second == "false") {
      explicitNonBlocking = true;
    }

    if (m_logger) {
      if (hasUVW) {
        m_logger->Info(
          "Moving " + deviceName + " to (X=" +
          std::to_string(x) + ", Y=" +
          std::to_string(y) + ", Z=" +
          std::to_string(z) + ", U=" +
          std::to_string(u) + ", V=" +
          std::to_string(v) + ", W=" +
          std::to_string(w) + ")",
          "MotionHandler"
        );
      }
      else {
        m_logger->Info(
          "Moving " + deviceName + " to (" +
          std::to_string(x) + ", " +
          std::to_string(y) + ", " +
          std::to_string(z) + ")",
          "MotionHandler"
        );
      }
    }

    // ✅ NEW: For 6-axis moves, move each axis individually
    bool moveSuccess = true;

    if (hasUVW) {
      // For hexapods with UVW, move all 6 axes
      moveSuccess &= motionManager->MoveToPosition(deviceName, "X", x, false);
      moveSuccess &= motionManager->MoveToPosition(deviceName, "Y", y, false);
      moveSuccess &= motionManager->MoveToPosition(deviceName, "Z", z, false);
      moveSuccess &= motionManager->MoveToPosition(deviceName, "U", u, false);
      moveSuccess &= motionManager->MoveToPosition(deviceName, "V", v, false);
      moveSuccess &= motionManager->MoveToPosition(deviceName, "W", w, false);
    }
    else {
      // Use the 3-axis move command
      moveSuccess = motionManager->MoveToAbs(deviceName, x, y, z, velocity, false);
    }

    if (!moveSuccess) {
      errorMessage = "MoveToAbs failed to start";
      return false;
    }

    // If explicit non-blocking requested, return immediately
    if (explicitNonBlocking) {
      return true;
    }

    // ✅ SAFETY: Wait for completion with stop flag monitoring and position verification
    TargetPosition target;
    target.hasX = true;
    target.hasY = true;
    target.hasZ = true;
    target.hasU = hasUVW;
    target.hasV = hasUVW;
    target.hasW = hasUVW;
    target.x = x;
    target.y = y;
    target.z = z;
    target.u = u;
    target.v = v;
    target.w = w;

    if (!WaitForMotionWithStopCheck(manager, deviceName, context, 30000, &target)) {
      errorMessage = "Motion stopped, timeout, or position verification failed";
      return false;
    }

    return true;
  }

  // ============================================================================
  // RELATIVE MOVE
  // ============================================================================
  else if (methodName == "MoveRelative") {
    auto axisIt = params.find("axis");
    auto distIt = params.find("distance");

    if (axisIt == params.end() || distIt == params.end()) {
      errorMessage = "Missing required parameters: axis, distance";
      return false;
    }

    std::string axis = axisIt->second;
    double distance = ResolveNumericValue(distIt->second, context);

    // Check if non-blocking mode requested explicitly
    bool explicitNonBlocking = false;
    auto blockingIt = params.find("blocking");
    if (blockingIt != params.end() && blockingIt->second == "false") {
      explicitNonBlocking = true;
    }

    // Get current position to calculate expected final position
    double currentPos = 0.0;
    if (!motionManager->GetPosition(deviceName, axis, currentPos)) {
      errorMessage = "Failed to get current position for relative move";
      return false;
    }

    double targetPos = currentPos + distance;

    if (m_logger) {
      m_logger->Info(
        "Moving " + deviceName + " axis " + axis + " by " + std::to_string(distance) +
        " (current: " + std::to_string(currentPos) + " -> target: " + std::to_string(targetPos) + ")",
        "MotionHandler"
      );
    }

    // DEBUG: Check if axis is currently moving before starting new move
    if (m_logger) {
      bool isMoving = motionManager->IsMoving(deviceName, axis);
      bool isEnabled = motionManager->IsServoEnabled(deviceName, axis);
      m_logger->Info(
        "[DEBUG] Pre-move state for " + deviceName + " axis " + axis +
        ": isMoving=" + std::to_string(isMoving) +
        ", servoEnabled=" + std::to_string(isEnabled),
        "MotionHandler"
      );
    }

    // ✅ SAFETY: Always use non-blocking mode + stop checking
    if (m_logger) {
      m_logger->Info("[DEBUG] Calling MoveRelative on " + deviceName + " axis " + axis, "MotionHandler");
    }
    if (!motionManager->MoveRelative(deviceName, axis, distance, false)) {
      // Get detailed error info from the controller
      int errorCode = motionManager->GetLastErrorCode(deviceName);
      std::string errorDetail = motionManager->GetLastErrorMessage(deviceName);
      if (m_logger) {
        m_logger->Error("[DEBUG] MoveRelative FAILED for " + deviceName + " axis " + axis +
          " - errorCode=" + std::to_string(errorCode) + " detail=" + errorDetail, "MotionHandler");
      }
      errorMessage = "MoveRelative failed to start (code=" + std::to_string(errorCode) + "): " + errorDetail;
      return false;
    }
    if (m_logger) {
      m_logger->Info("[DEBUG] MoveRelative returned true for " + deviceName + " axis " + axis, "MotionHandler");
    }

    // If explicit non-blocking requested, return immediately
    if (explicitNonBlocking) {
      return true;
    }

    // ✅ SAFETY: Wait for completion with stop flag monitoring and position verification
    TargetPosition target;
    if (axis == "X") {
      target.hasX = true;
      target.x = targetPos;
    } else if (axis == "Y") {
      target.hasY = true;
      target.y = targetPos;
    } else if (axis == "Z") {
      target.hasZ = true;
      target.z = targetPos;
    } else if (axis == "U") {
      target.hasU = true;
      target.u = targetPos;
    } else if (axis == "V") {
      target.hasV = true;
      target.v = targetPos;
    } else if (axis == "W") {
      target.hasW = true;
      target.w = targetPos;
    }

    if (!WaitForMotionWithStopCheck(manager, deviceName, context, 30000, &target)) {
      errorMessage = "Motion stopped, timeout, or position verification failed";
      return false;
    }

    return true;
  }

  // ============================================================================
  // MOVE TO NAMED POSITION
  // ============================================================================
  else if (methodName == "MoveToNamedPosition") {
    auto posNameIt = params.find("positionName");

    if (posNameIt == params.end()) {
      errorMessage = "Missing required parameter: positionName";
      return false;
    }

    std::string positionName = posNameIt->second;

    if (m_logger) {
      m_logger->Info(
        "Moving " + deviceName + " to named position: " + positionName,
        "MotionHandler"
      );
    }

    // ✅ Store retrace info in context for later retrieval by WaitForMotionComplete
    if (context && m_retraceEditMode.load()) {
      // Get the target position coordinates before moving
      PositionStruct targetPos;
      if (motionManager->GetPositionWithCalculation(deviceName, positionName, targetPos)) {
        context->SetValue("_retrace_pending_device", deviceName);
        context->SetValue("_retrace_pending_position", positionName);
        context->SetValue("_retrace_original_x", static_cast<float>(targetPos.x));
        context->SetValue("_retrace_original_y", static_cast<float>(targetPos.y));
        context->SetValue("_retrace_original_z", static_cast<float>(targetPos.z));
        context->SetValue("_retrace_original_u", static_cast<float>(targetPos.u));
        context->SetValue("_retrace_original_v", static_cast<float>(targetPos.v));
        context->SetValue("_retrace_original_w", static_cast<float>(targetPos.w));
        if (m_logger) {
          m_logger->Info("Retrace: Stored pending move info for " + deviceName + "/" + positionName, "MotionHandler");
        }
      }
    }

    // ✅ SAFETY: Start motion in non-blocking mode (blocking=false is default)
    // Recipe plugins typically follow this with WaitForMotionComplete for safety stop checking
    if (!motionManager->MoveToNamedPosition(deviceName, positionName, false)) {
      errorMessage = "MoveToNamedPosition failed to start";
      return false;
    }

    // Return immediately - let WaitForMotionComplete handle the waiting with stop checking
    return true;
  }

  // ============================================================================
  // STOP AXIS
  // ============================================================================
  else if (methodName == "StopAxis") {
    auto axisIt = params.find("axis");

    if (axisIt == params.end()) {
      errorMessage = "Missing required parameter: axis";
      return false;
    }

    std::string axis = axisIt->second;

    if (m_logger) {
      m_logger->Info("Stopping " + deviceName + " axis " + axis, "MotionHandler");
    }

    if (!motionManager->StopAxis(deviceName, axis)) {
      errorMessage = "StopAxis failed";
      return false;
    }

    return true;
  }

  // ============================================================================
  // EMERGENCY STOP
  // ============================================================================
  else if (methodName == "EmergencyStop") {
    if (m_logger) {
      m_logger->Warning("EMERGENCY STOP: " + deviceName, "MotionHandler");
    }

    if (!motionManager->EmergencyStop(deviceName)) {
      errorMessage = "EmergencyStop failed";
      return false;
    }

    return true;
  }

  // ============================================================================
  // HOME AXIS
  // ============================================================================
  else if (methodName == "HomeAxis") {
    auto axisIt = params.find("axis");

    if (axisIt == params.end()) {
      errorMessage = "Missing required parameter: axis";
      return false;
    }

    std::string axis = axisIt->second;

    if (m_logger) {
      m_logger->Info("Homing " + deviceName + " axis " + axis, "MotionHandler");
    }

    if (!motionManager->HomeAxis(deviceName, axis)) {
      errorMessage = "HomeAxis failed";
      return false;
    }

    return true;
  }

  // ============================================================================
  // SET VELOCITY
  // ============================================================================
  else if (methodName == "SetVelocity") {
    auto axisIt = params.find("axis");
    auto velIt = params.find("velocity");

    if (axisIt == params.end() || velIt == params.end()) {
      errorMessage = "Missing required parameters: axis, velocity";
      return false;
    }

    std::string axis = axisIt->second;
    double velocity = std::stod(velIt->second);

    if (m_logger) {
      m_logger->Info(
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
  else if (methodName == "SetAllAxisVelocity") {
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

    if (m_logger) {
      m_logger->Info(
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
          if (m_logger) {
            m_logger->Warning(
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
  else if (methodName == "SetSystemVelocity") {
    auto velIt = params.find("velocity");

    if (velIt == params.end()) {
      errorMessage = "Missing required parameter: velocity";
      return false;
    }

    double velocity = std::stod(velIt->second);

    if (m_logger) {
      m_logger->Info(
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
  // ENABLE/DISABLE SERVO
  // ============================================================================
  else if (methodName == "EnableServo") {
    auto axisIt = params.find("axis");
    auto enableIt = params.find("enable");

    if (axisIt == params.end() || enableIt == params.end()) {
      errorMessage = "Missing required parameters: axis, enable";
      return false;
    }

    std::string axis = axisIt->second;
    bool enable = (enableIt->second == "true");

    if (m_logger) {
      m_logger->Info(
        (enable ? "Enabling" : "Disabling") + std::string(" servo for ") +
        deviceName + " axis " + axis,
        "MotionHandler"
      );
    }

    if (!motionManager->EnableServo(deviceName, axis, enable)) {
      errorMessage = "EnableServo failed";
      return false;
    }

    return true;
  }

  else if (methodName == "WaitForMotionComplete") {
    int timeoutMs = 30000;
    auto timeoutIt = params.find("timeout_ms");
    if (timeoutIt != params.end()) {
      timeoutMs = std::stoi(timeoutIt->second);
    }

    if (m_logger) {
      m_logger->Info("Waiting for motion complete on: " + deviceName, "MotionHandler");
    }

    // ✅ SAFETY: Use unified wait method with stop checking
    if (!WaitForMotionWithStopCheck(manager, deviceName, context, timeoutMs)) {
      errorMessage = "Motion stopped or timeout waiting for motion complete on: " + deviceName;
      return false;
    }

    if (m_logger) {
      m_logger->Info("Motion complete for: " + deviceName, "MotionHandler");
    }

    // ✅ RETRACE EDIT: Check if we have pending retrace info for this device
    if (m_retraceEditMode.load() && m_retraceEditCallback && context) {
      std::string pendingDevice = context->GetString("_retrace_pending_device", "");
      std::string pendingPosition = context->GetString("_retrace_pending_position", "");

      if (pendingDevice == deviceName && !pendingPosition.empty()) {
        // Retrieve original target coordinates
        double originalX = static_cast<double>(context->GetNumeric("_retrace_original_x", 0.0f));
        double originalY = static_cast<double>(context->GetNumeric("_retrace_original_y", 0.0f));
        double originalZ = static_cast<double>(context->GetNumeric("_retrace_original_z", 0.0f));
        double originalU = static_cast<double>(context->GetNumeric("_retrace_original_u", 0.0f));
        double originalV = static_cast<double>(context->GetNumeric("_retrace_original_v", 0.0f));
        double originalW = static_cast<double>(context->GetNumeric("_retrace_original_w", 0.0f));

        // Clear pending retrace info
        context->SetValue("_retrace_pending_device", std::string(""));
        context->SetValue("_retrace_pending_position", std::string(""));

        // Invoke retrace edit callback
        if (!InvokeRetraceEditCallback(
          manager, deviceName, pendingPosition, "MoveToNamedPosition", params,
          originalX, originalY, originalZ, originalU, originalV, originalW)) {
          errorMessage = "Retrace edit stopped by user";
          return false;
        }
      }
    }

    return true;
  }

  // ============================================================================
  // XCAN - HEXAPOD ADAPTIVE SCANNING
  // ============================================================================
  else if (methodName == "XCAN") {
    // ✅ VALIDATE DEVICE NAME (must match hex-*)
    std::regex hexPattern("^hex-.*");
    if (!std::regex_match(deviceName, hexPattern)) {
      errorMessage = "XCAN: Device must be a PI hexapod (hex-*), got: " + deviceName;
      return false;
    }

    // ✅ REQUIRED PARAMETER: mode (coarse/fine)
    auto modeIt = params.find("mode");
    if (modeIt == params.end()) {
      errorMessage = "XCAN: Missing required parameter 'mode' (coarse/fine)";
      return false;
    }
    std::string mode = modeIt->second;
    bool isCoarse;
    if (mode == "coarse") {
      isCoarse = true;
    } else if (mode == "fine") {
      isCoarse = false;
    } else {
      errorMessage = "XCAN: Invalid mode '" + mode + "' (must be 'coarse' or 'fine')";
      return false;
    }

    // ✅ REQUIRED PARAMETER: channel (data channel to optimize)
    auto channelIt = params.find("channel");
    if (channelIt == params.end()) {
      errorMessage = "XCAN: Missing required parameter 'channel'";
      return false;
    }
    std::string channel = channelIt->second;
    if (channel.empty()) {
      errorMessage = "XCAN: Data channel cannot be empty - please select a data channel in the recipe configuration";
      return false;
    }

    // ✅ OPTIONAL PARAMETER: settling_time_ms (default 300)
    int settlingTimeMs = 300;
    auto settlingIt = params.find("settling_time_ms");
    if (settlingIt != params.end()) {
      settlingTimeMs = std::stoi(settlingIt->second);
    }

    // ✅ GET DATA STORE
    if (!m_dataStore) {
      errorMessage = "XCAN: DataStore not available";
      return false;
    }

    if (m_logger) {
      m_logger->Info(
        "XCAN: Starting " + mode + " scan on " + deviceName +
        " for channel '" + channel + "' (settling=" + std::to_string(settlingTimeMs) + "ms)",
        "MotionHandler"
      );
    }

    // ✅ CREATE SCAN PARAMETERS
    ScanParameters scanParams;
    scanParams.axesToScan = { "Z", "Y", "X" };  // Scan in ZYX order

    // Set step sizes based on mode
    if (isCoarse) {
      // Coarse: 10, 5, 2, 1 µm = 0.010, 0.005, 0.002, 0.001 mm
      scanParams.stepSizes = { 0.010, 0.005, 0.002, 0.001 };
    } else {
      // Fine: 2, 1, 0.5 µm = 0.002, 0.001, 0.0005 mm
      scanParams.stepSizes = { 0.002, 0.001, 0.0005 };
    }

    scanParams.motionSettleTimeMs = settlingTimeMs;
    scanParams.velocityPercent = 50.0;

    // Termination criteria
    scanParams.consecutiveDecreasesLimit = 3;
    scanParams.significantDecreaseThreshold = 0.05;  // 5%
    scanParams.maxTotalDistance = 0.2;  // 200µm = 0.2mm per direction

    // Position optimization
    scanParams.improvementThreshold = 0.01;  // 1%

    // Measurement parameters
    scanParams.averagingCount = 3;
    scanParams.measurementTimeout = std::chrono::seconds(5);
    scanParams.minValidValue = -1000.0;
    scanParams.maxValidValue = 1000.0;

    // Safety limits
    scanParams.maxStepSize = 0.5;  // 500µm max
    scanParams.scanTimeout = std::chrono::minutes(10);

    // ✅ CREATE SCAN CONTROLLER
    AdaptiveScanController scanController(
      *motionManager,
      *m_dataStore,
      deviceName,
      channel
    );

    if (m_logger) {
      scanController.SetLogger(m_logger);
    }

    // ✅ SETUP COMPLETION TRACKING
    std::atomic<bool> scanComplete(false);
    std::atomic<bool> scanSuccess(false);
    std::string scanErrorMsg;
    std::mutex completionMutex;
    std::condition_variable completionCV;

    // Completion callback
    scanController.SetCompletionCallback(
      [&](const ScanResults& results) {
        std::lock_guard<std::mutex> lock(completionMutex);
        scanComplete = true;
        scanSuccess = results.globalPeak.valid;
        if (m_logger) {
          if (results.globalPeak.valid) {
            m_logger->Info(
              "XCAN: Scan complete - Peak value: " + std::to_string(results.globalPeak.value),
              "MotionHandler"
            );
          } else {
            m_logger->Warning("XCAN: Scan complete - No peak found", "MotionHandler");
          }
        }
        completionCV.notify_one();
      }
    );

    // Error callback
    scanController.SetErrorCallback(
      [&](const ScanError& error) {
        std::lock_guard<std::mutex> lock(completionMutex);
        scanComplete = true;
        scanSuccess = false;
        scanErrorMsg = error.message;
        if (m_logger) {
          m_logger->Error("XCAN: Scan error - " + error.message, "MotionHandler");
        }
        completionCV.notify_one();
      }
    );

    // ✅ START SCAN
    if (!scanController.StartScan(scanParams)) {
      errorMessage = "XCAN: Failed to start scan";
      return false;
    }

    // ✅ WAIT FOR COMPLETION WITH STOP FLAG MONITORING
    while (!scanComplete) {
      // Check if stop was requested
      if (context && context->IsStopRequestedAny()) {
        if (m_logger) {
          m_logger->Info("XCAN: Stop requested, halting scan", "MotionHandler");
        }
        scanController.HaltScan();
        errorMessage = "XCAN: Scan stopped by user";
        return false;
      }

      // Wait for 100ms before checking again
      std::unique_lock<std::mutex> lock(completionMutex);
      completionCV.wait_for(lock, std::chrono::milliseconds(100));
    }

    // ✅ CHECK FINAL RESULT
    if (!scanSuccess) {
      if (!scanErrorMsg.empty()) {
        errorMessage = "XCAN: Scan failed - " + scanErrorMsg;
      } else {
        errorMessage = "XCAN: Scan completed but no peak found";
      }
      return false;
    }

    if (m_logger) {
      m_logger->Info("XCAN: Scan completed successfully", "MotionHandler");
    }

    return true;
  }

  // ============================================================================
  // XCAN EXTENDED - FLEXIBLE ADAPTIVE SCANNING WITH CUSTOM PARAMETERS
  // ============================================================================
  else if (methodName == "XCANExtended") {
    // ✅ REQUIRED PARAMETER: axes
    auto axesIt = params.find("axes");
    if (axesIt == params.end()) {
      errorMessage = "XCANExtended: Missing required parameter 'axes'";
      return false;
    }
    std::string axesStr = axesIt->second;

    // Validate axes - must contain only valid axis characters (X, Y, Z)
    std::vector<std::string> axesToScan;
    for (char c : axesStr) {
      char upper = std::toupper(c);
      if (upper == 'X' || upper == 'Y' || upper == 'Z') {
        axesToScan.push_back(std::string(1, upper));
      } else if (c != ' ' && c != ',') {
        errorMessage = "XCANExtended: Invalid axis character '" + std::string(1, c) + "' in axes '" + axesStr + "'. Valid axes are X, Y, Z.";
        return false;
      }
    }

    if (axesToScan.empty()) {
      errorMessage = "XCANExtended: No valid axes specified in '" + axesStr + "'";
      return false;
    }

    // ✅ REQUIRED PARAMETER: step_sizes_um (comma-separated microns)
    auto stepSizesIt = params.find("step_sizes_um");
    if (stepSizesIt == params.end()) {
      errorMessage = "XCANExtended: Missing required parameter 'step_sizes_um'";
      return false;
    }
    std::string stepSizesStr = stepSizesIt->second;

    // Parse step sizes
    std::vector<double> stepSizesMm;
    std::stringstream ss(stepSizesStr);
    std::string token;
    while (std::getline(ss, token, ',')) {
      // Trim whitespace
      token.erase(0, token.find_first_not_of(" \t"));
      token.erase(token.find_last_not_of(" \t") + 1);

      if (!token.empty()) {
        try {
          double stepUm = std::stod(token);
          if (stepUm <= 0) {
            errorMessage = "XCANExtended: Step size must be positive, got: " + token;
            return false;
          }
          // Convert microns to mm
          stepSizesMm.push_back(stepUm / 1000.0);
        } catch (...) {
          errorMessage = "XCANExtended: Invalid step size value: " + token;
          return false;
        }
      }
    }

    if (stepSizesMm.empty()) {
      errorMessage = "XCANExtended: No valid step sizes specified in '" + stepSizesStr + "'";
      return false;
    }

    // Sort step sizes descending (largest first)
    std::sort(stepSizesMm.begin(), stepSizesMm.end(), std::greater<double>());

    // ✅ REQUIRED PARAMETER: channel
    auto channelIt = params.find("channel");
    if (channelIt == params.end()) {
      errorMessage = "XCANExtended: Missing required parameter 'channel'";
      return false;
    }
    std::string channel = channelIt->second;
    if (channel.empty()) {
      errorMessage = "XCANExtended: Data channel cannot be empty";
      return false;
    }

    // ✅ OPTIONAL PARAMETER: settling_time_ms (default 300)
    int settlingTimeMs = 300;
    auto settlingIt = params.find("settling_time_ms");
    if (settlingIt != params.end() && !settlingIt->second.empty()) {
      settlingTimeMs = std::stoi(settlingIt->second);
    }

    // ✅ OPTIONAL PARAMETER: exit_threshold (early exit if value exceeds threshold)
    double exitThreshold = std::numeric_limits<double>::max();
    bool hasExitThreshold = false;
    auto exitThresholdIt = params.find("exit_threshold");
    if (exitThresholdIt != params.end() && !exitThresholdIt->second.empty()) {
      try {
        exitThreshold = std::stod(exitThresholdIt->second);
        hasExitThreshold = true;
      } catch (...) {
        errorMessage = "XCANExtended: Invalid exit_threshold value: " + exitThresholdIt->second;
        return false;
      }
    }

    // ✅ OPTIONAL PARAMETER: capture_as (store result in context)
    std::string captureAs;
    auto captureAsIt = params.find("capture_as");
    if (captureAsIt != params.end()) {
      captureAs = captureAsIt->second;
    }

    // ✅ GET DATA STORE
    if (!m_dataStore) {
      errorMessage = "XCANExtended: DataStore not available";
      return false;
    }

    if (m_logger) {
      std::string axesLog;
      for (const auto& axis : axesToScan) axesLog += axis;

      std::string stepsLog;
      for (size_t i = 0; i < stepSizesMm.size(); ++i) {
        if (i > 0) stepsLog += ",";
        stepsLog += std::to_string(stepSizesMm[i] * 1000.0) + "um";
      }

      m_logger->Info(
        "XCANExtended: Starting scan on " + deviceName +
        " (axes=" + axesLog + ", steps=" + stepsLog +
        ", channel=" + channel +
        ", settling=" + std::to_string(settlingTimeMs) + "ms" +
        (hasExitThreshold ? ", exitThreshold=" + std::to_string(exitThreshold) : "") +
        (!captureAs.empty() ? ", captureAs=" + captureAs : "") + ")",
        "MotionHandler"
      );
    }

    // ✅ CREATE SCAN PARAMETERS
    ScanParameters scanParams;
    scanParams.axesToScan = axesToScan;
    scanParams.stepSizes = stepSizesMm;
    scanParams.motionSettleTimeMs = settlingTimeMs;
    scanParams.velocityPercent = 50.0;

    // Termination criteria
    scanParams.consecutiveDecreasesLimit = 3;
    scanParams.significantDecreaseThreshold = 0.05;  // 5%
    scanParams.maxTotalDistance = 0.2;  // 200µm per direction

    // Position optimization
    scanParams.improvementThreshold = 0.01;  // 1%

    // Measurement parameters
    scanParams.averagingCount = 3;
    scanParams.measurementTimeout = std::chrono::seconds(5);
    scanParams.minValidValue = -1000.0;
    scanParams.maxValidValue = 1000.0;

    // Safety limits
    scanParams.maxStepSize = 0.5;  // 500µm max
    scanParams.scanTimeout = std::chrono::minutes(10);

    // ✅ CREATE SCAN CONTROLLER
    AdaptiveScanController scanController(
      *motionManager,
      *m_dataStore,
      deviceName,
      channel
    );

    if (m_logger) {
      scanController.SetLogger(m_logger);
    }

    // ✅ COMPLETION TRACKING
    std::atomic<bool> scanComplete{false};
    std::atomic<bool> scanSuccess{false};
    std::atomic<bool> earlyExit{false};
    std::string scanErrorMsg;
    double peakValue = 0.0;
    std::mutex completionMutex;
    std::condition_variable completionCV;

    // Progress callback - check exit threshold
    scanController.SetDataPointCallback(
      [&](const ScanDataPoint& dataPoint) {
        if (hasExitThreshold && dataPoint.value > exitThreshold && !earlyExit.load()) {
          earlyExit.store(true);
          if (m_logger) {
            m_logger->Info(
              "XCANExtended: Exit threshold reached (" + std::to_string(dataPoint.value) +
              " > " + std::to_string(exitThreshold) + "), stopping scan",
              "MotionHandler"
            );
          }
          // The scan will halt and return to peak on next iteration
          scanController.HaltScan();
        }
      }
    );

    // Completion callback
    scanController.SetCompletionCallback(
      [&](const ScanResults& results) {
        std::lock_guard<std::mutex> lock(completionMutex);
        scanComplete = true;
        scanSuccess = results.globalPeak.valid;
        peakValue = results.globalPeak.value;
        if (m_logger) {
          if (results.globalPeak.valid) {
            m_logger->Info(
              "XCANExtended: Scan complete - Peak value: " + std::to_string(results.globalPeak.value),
              "MotionHandler"
            );
          } else {
            m_logger->Warning("XCANExtended: Scan complete - No peak found", "MotionHandler");
          }
        }
        completionCV.notify_one();
      }
    );

    // Error callback
    scanController.SetErrorCallback(
      [&](const ScanError& error) {
        std::lock_guard<std::mutex> lock(completionMutex);
        scanComplete = true;
        // Early exit due to threshold is not an error
        if (earlyExit.load() && error.type == ScanErrorType::UserHalt) {
          scanSuccess = true;  // Consider it a success
        } else {
          scanSuccess = false;
          scanErrorMsg = error.message;
          if (m_logger) {
            m_logger->Error("XCANExtended: Scan error - " + error.message, "MotionHandler");
          }
        }
        completionCV.notify_one();
      }
    );

    // ✅ START SCAN
    if (!scanController.StartScan(scanParams)) {
      errorMessage = "XCANExtended: Failed to start scan";
      return false;
    }

    // ✅ WAIT FOR COMPLETION WITH STOP FLAG MONITORING
    while (!scanComplete) {
      // Check if stop was requested
      if (context && context->IsStopRequestedAny()) {
        if (m_logger) {
          m_logger->Info("XCANExtended: Stop requested, halting scan", "MotionHandler");
        }
        scanController.HaltScan();
        errorMessage = "XCANExtended: Scan stopped by user";
        return false;
      }

      // Wait for 100ms before checking again
      std::unique_lock<std::mutex> lock(completionMutex);
      completionCV.wait_for(lock, std::chrono::milliseconds(100));
    }

    // Get final peak value from scan results
    ScanResults results = scanController.GetResults();
    if (results.globalPeak.valid) {
      peakValue = results.globalPeak.value;
    }

    // ✅ CAPTURE PEAK VALUE TO CONTEXT IF REQUESTED
    if (!captureAs.empty() && context) {
      context->SetValue(captureAs, static_cast<float>(peakValue), deviceName, "XCANExtended");
      if (m_logger) {
        m_logger->Info(
          "XCANExtended: Captured peak value " + std::to_string(peakValue) + " to context:" + captureAs,
          "MotionHandler"
        );
      }
    }

    // ✅ CHECK FINAL RESULT
    if (!scanSuccess && !earlyExit.load()) {
      if (!scanErrorMsg.empty()) {
        errorMessage = "XCANExtended: Scan failed - " + scanErrorMsg;
      } else {
        errorMessage = "XCANExtended: Scan completed but no peak found";
      }
      return false;
    }

    if (m_logger) {
      std::string completionReason = earlyExit.load() ? " (early exit - threshold reached)" : "";
      m_logger->Info("XCANExtended: Scan completed successfully" + completionReason, "MotionHandler");
    }

    return true;
  }

  // ============================================================================
  // GRID SCAN - 2D PLANAR GRID SCANNING WITH PRESET MODES
  // ============================================================================
  else if (methodName == "GridScan") {
    // ✅ VALIDATE DEVICE NAME (must match hex-*)
    std::regex hexPattern("^hex-.*");
    if (!std::regex_match(deviceName, hexPattern)) {
      errorMessage = "GridScan: Device must be a PI hexapod (hex-*), got: " + deviceName;
      return false;
    }

    // ✅ REQUIRED PARAMETER: mode (coarse/fine/ultrafine)
    auto modeIt = params.find("mode");
    if (modeIt == params.end()) {
      errorMessage = "GridScan: Missing required parameter 'mode' (coarse/fine/ultrafine)";
      return false;
    }
    std::string mode = modeIt->second;

    // Determine grid parameters from mode
    double rangeUm, stepUm;
    if (mode == "coarse") {
      rangeUm = 200.0;  // 200 µm total range
      stepUm = 20.0;    // 20 µm step -> 11x11 = 121 points
    } else if (mode == "fine") {
      rangeUm = 100.0;  // 100 µm total range
      stepUm = 10.0;    // 10 µm step -> 11x11 = 121 points
    } else if (mode == "ultrafine") {
      rangeUm = 50.0;   // 50 µm total range
      stepUm = 5.0;     // 5 µm step -> 11x11 = 121 points
    } else {
      errorMessage = "GridScan: Invalid mode '" + mode + "' (must be 'coarse', 'fine', or 'ultrafine')";
      return false;
    }

    // ✅ REQUIRED PARAMETER: plane (XY/XZ/YZ)
    auto planeIt = params.find("plane");
    if (planeIt == params.end()) {
      errorMessage = "GridScan: Missing required parameter 'plane' (XY/XZ/YZ)";
      return false;
    }
    std::string plane = planeIt->second;

    // Parse plane into axis names
    std::string axis1, axis2;
    if (plane == "XY" || plane == "xy") {
      axis1 = "X"; axis2 = "Y";
    } else if (plane == "XZ" || plane == "xz") {
      axis1 = "X"; axis2 = "Z";
    } else if (plane == "YZ" || plane == "yz") {
      axis1 = "Y"; axis2 = "Z";
    } else {
      errorMessage = "GridScan: Invalid plane '" + plane + "' (must be 'XY', 'XZ', or 'YZ')";
      return false;
    }

    // ✅ REQUIRED PARAMETER: channel
    auto channelIt = params.find("channel");
    if (channelIt == params.end()) {
      errorMessage = "GridScan: Missing required parameter 'channel'";
      return false;
    }
    std::string channel = channelIt->second;
    if (channel.empty()) {
      errorMessage = "GridScan: Data channel cannot be empty - please select a data channel in the recipe configuration";
      return false;
    }

    // ✅ OPTIONAL PARAMETER: settling_time_ms (default 300)
    int settlingTimeMs = 300;
    auto settlingIt = params.find("settling_time_ms");
    if (settlingIt != params.end() && !settlingIt->second.empty()) {
      settlingTimeMs = std::stoi(settlingIt->second);
    }

    // ✅ OPTIONAL PARAMETER: capture_as
    std::string captureAs;
    auto captureAsIt = params.find("capture_as");
    if (captureAsIt != params.end()) {
      captureAs = captureAsIt->second;
    }

    // ✅ GET DATA STORE
    if (!m_dataStore) {
      errorMessage = "GridScan: DataStore not available";
      return false;
    }

    // Convert to mm
    double rangeMm = rangeUm / 1000.0;
    double stepMm = stepUm / 1000.0;
    int gridPoints = static_cast<int>(rangeUm / stepUm) + 1;

    if (m_logger) {
      m_logger->Info(
        "GridScan: Starting " + mode + " scan on " + deviceName +
        " (plane=" + plane + ", range=" + std::to_string(rangeUm) + "um" +
        ", step=" + std::to_string(stepUm) + "um" +
        ", grid=" + std::to_string(gridPoints) + "x" + std::to_string(gridPoints) +
        ", channel='" + channel + "', settling=" + std::to_string(settlingTimeMs) + "ms)",
        "MotionHandler"
      );
    }

    // ✅ GET CURRENT POSITION (center of grid)
    double centerAxis1 = 0.0, centerAxis2 = 0.0;
    PositionStruct currentPos = motionManager->GetCurrentPosition(deviceName);
    if (axis1 == "X") centerAxis1 = currentPos.x;
    else if (axis1 == "Y") centerAxis1 = currentPos.y;
    else if (axis1 == "Z") centerAxis1 = currentPos.z;

    if (axis2 == "X") centerAxis2 = currentPos.x;
    else if (axis2 == "Y") centerAxis2 = currentPos.y;
    else if (axis2 == "Z") centerAxis2 = currentPos.z;

    // ✅ EXECUTE GRID SCAN
    double peakValue = std::numeric_limits<double>::lowest();
    double peakAxis1 = centerAxis1;
    double peakAxis2 = centerAxis2;
    int totalPoints = gridPoints * gridPoints;
    int currentPoint = 0;

    // Start position (corner of grid)
    double startAxis1 = centerAxis1 - rangeMm / 2.0;
    double startAxis2 = centerAxis2 - rangeMm / 2.0;

    for (int row = 0; row < gridPoints; ++row) {
      // Snake pattern: alternate direction each row
      bool reverse = (row % 2 == 1);

      for (int col = 0; col < gridPoints; ++col) {
        // Check stop flag
        if (context && context->IsStopRequestedAny()) {
          if (m_logger) {
            m_logger->Info("GridScan: Stop requested, halting scan", "MotionHandler");
          }
          errorMessage = "GridScan: Scan stopped by user";
          return false;
        }

        int actualCol = reverse ? (gridPoints - 1 - col) : col;

        double targetAxis1 = startAxis1 + actualCol * stepMm;
        double targetAxis2 = startAxis2 + row * stepMm;

        // Move to position (axis1 first, then axis2 for predictability)
        if (!motionManager->MoveToPosition(deviceName, axis1, targetAxis1, false)) {
          errorMessage = "GridScan: Failed to start move on " + axis1;
          return false;
        }
        // Wait for axis1 move
        auto start = std::chrono::steady_clock::now();
        while (motionManager->IsMoving(deviceName, axis1)) {
          if (context && context->IsStopRequestedAny()) {
            errorMessage = "GridScan: Scan stopped by user during motion";
            return false;
          }
          if (std::chrono::steady_clock::now() - start > std::chrono::seconds(10)) {
            errorMessage = "GridScan: Timeout waiting for " + axis1 + " motion";
            return false;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        if (!motionManager->MoveToPosition(deviceName, axis2, targetAxis2, false)) {
          errorMessage = "GridScan: Failed to start move on " + axis2;
          return false;
        }
        // Wait for axis2 move
        start = std::chrono::steady_clock::now();
        while (motionManager->IsMoving(deviceName, axis2)) {
          if (context && context->IsStopRequestedAny()) {
            errorMessage = "GridScan: Scan stopped by user during motion";
            return false;
          }
          if (std::chrono::steady_clock::now() - start > std::chrono::seconds(10)) {
            errorMessage = "GridScan: Timeout waiting for " + axis2 + " motion";
            return false;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Settling time
        std::this_thread::sleep_for(std::chrono::milliseconds(settlingTimeMs));

        // Read channel value
        double value = static_cast<double>(m_dataStore->GetValue(channel, 0.0f));

        // Track peak (maximize)
        if (value > peakValue) {
          peakValue = value;
          peakAxis1 = targetAxis1;
          peakAxis2 = targetAxis2;
          if (m_logger) {
            m_logger->Info(
              "GridScan: New peak at (" + std::to_string(targetAxis1) + ", " +
              std::to_string(targetAxis2) + ") = " + std::to_string(value),
              "MotionHandler"
            );
          }
        }

        currentPoint++;
        if (m_logger && (currentPoint % 20 == 0 || currentPoint == totalPoints)) {
          m_logger->Info(
            "GridScan: Progress " + std::to_string(currentPoint) + "/" + std::to_string(totalPoints) +
            " (" + std::to_string(100 * currentPoint / totalPoints) + "%)",
            "MotionHandler"
          );
        }
      }
    }

    // ✅ RETURN TO PEAK POSITION
    if (m_logger) {
      m_logger->Info(
        "GridScan: Returning to peak position (" + std::to_string(peakAxis1) + ", " +
        std::to_string(peakAxis2) + ") with value " + std::to_string(peakValue),
        "MotionHandler"
      );
    }

    if (!motionManager->MoveToPosition(deviceName, axis1, peakAxis1, false)) {
      errorMessage = "GridScan: Failed to return to peak on " + axis1;
      return false;
    }
    auto startReturn = std::chrono::steady_clock::now();
    while (motionManager->IsMoving(deviceName, axis1)) {
      if (std::chrono::steady_clock::now() - startReturn > std::chrono::seconds(10)) {
        errorMessage = "GridScan: Timeout returning to peak on " + axis1;
        return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!motionManager->MoveToPosition(deviceName, axis2, peakAxis2, false)) {
      errorMessage = "GridScan: Failed to return to peak on " + axis2;
      return false;
    }
    startReturn = std::chrono::steady_clock::now();
    while (motionManager->IsMoving(deviceName, axis2)) {
      if (std::chrono::steady_clock::now() - startReturn > std::chrono::seconds(10)) {
        errorMessage = "GridScan: Timeout returning to peak on " + axis2;
        return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // ✅ CAPTURE PEAK VALUE TO CONTEXT IF REQUESTED
    if (!captureAs.empty() && context) {
      context->SetValue(captureAs, static_cast<float>(peakValue), deviceName, "GridScan");
      if (m_logger) {
        m_logger->Info(
          "GridScan: Captured peak value " + std::to_string(peakValue) + " to context:" + captureAs,
          "MotionHandler"
        );
      }
    }

    if (m_logger) {
      m_logger->Info(
        "GridScan: Completed successfully - Peak value: " + std::to_string(peakValue) +
        " at (" + std::to_string(peakAxis1) + ", " + std::to_string(peakAxis2) + ")",
        "MotionHandler"
      );
    }

    return true;
  }

  // ============================================================================
  // GRID SCAN EXTENDED - FULL CONTROL 2D GRID SCANNING
  // ============================================================================
  else if (methodName == "GridScanExtended") {
    // ✅ REQUIRED PARAMETER: plane (XY/XZ/YZ)
    auto planeIt = params.find("plane");
    if (planeIt == params.end()) {
      errorMessage = "GridScanExtended: Missing required parameter 'plane' (XY/XZ/YZ)";
      return false;
    }
    std::string plane = planeIt->second;

    // Parse plane into axis names
    std::string axis1, axis2;
    if (plane == "XY" || plane == "xy") {
      axis1 = "X"; axis2 = "Y";
    } else if (plane == "XZ" || plane == "xz") {
      axis1 = "X"; axis2 = "Z";
    } else if (plane == "YZ" || plane == "yz") {
      axis1 = "Y"; axis2 = "Z";
    } else {
      errorMessage = "GridScanExtended: Invalid plane '" + plane + "' (must be 'XY', 'XZ', or 'YZ')";
      return false;
    }

    // ✅ REQUIRED PARAMETER: range_axis1_um
    auto range1It = params.find("range_axis1_um");
    if (range1It == params.end() || range1It->second.empty()) {
      errorMessage = "GridScanExtended: Missing required parameter 'range_axis1_um'";
      return false;
    }
    double rangeAxis1Um = std::stod(range1It->second);
    if (rangeAxis1Um <= 0 || rangeAxis1Um > 1000) {
      errorMessage = "GridScanExtended: range_axis1_um must be between 1 and 1000 um";
      return false;
    }

    // ✅ REQUIRED PARAMETER: range_axis2_um
    auto range2It = params.find("range_axis2_um");
    if (range2It == params.end() || range2It->second.empty()) {
      errorMessage = "GridScanExtended: Missing required parameter 'range_axis2_um'";
      return false;
    }
    double rangeAxis2Um = std::stod(range2It->second);
    if (rangeAxis2Um <= 0 || rangeAxis2Um > 1000) {
      errorMessage = "GridScanExtended: range_axis2_um must be between 1 and 1000 um";
      return false;
    }

    // ✅ REQUIRED PARAMETER: step_size_um
    auto stepIt = params.find("step_size_um");
    if (stepIt == params.end() || stepIt->second.empty()) {
      errorMessage = "GridScanExtended: Missing required parameter 'step_size_um'";
      return false;
    }
    double stepSizeUm = std::stod(stepIt->second);
    if (stepSizeUm < 1 || stepSizeUm > 100) {
      errorMessage = "GridScanExtended: step_size_um must be between 1 and 100 um";
      return false;
    }

    // ✅ REQUIRED PARAMETER: channel
    auto channelIt = params.find("channel");
    if (channelIt == params.end()) {
      errorMessage = "GridScanExtended: Missing required parameter 'channel'";
      return false;
    }
    std::string channel = channelIt->second;
    if (channel.empty()) {
      errorMessage = "GridScanExtended: Data channel cannot be empty";
      return false;
    }

    // ✅ OPTIONAL PARAMETER: settling_time_ms (default 300)
    int settlingTimeMs = 300;
    auto settlingIt = params.find("settling_time_ms");
    if (settlingIt != params.end() && !settlingIt->second.empty()) {
      settlingTimeMs = std::stoi(settlingIt->second);
      if (settlingTimeMs < 50 || settlingTimeMs > 2000) {
        errorMessage = "GridScanExtended: settling_time_ms must be between 50 and 2000";
        return false;
      }
    }

    // ✅ OPTIONAL PARAMETER: snake_pattern (default true)
    bool snakePattern = true;
    auto snakeIt = params.find("snake_pattern");
    if (snakeIt != params.end()) {
      snakePattern = (snakeIt->second == "true" || snakeIt->second == "1");
    }

    // ✅ OPTIONAL PARAMETER: return_to_peak (default true)
    bool returnToPeak = true;
    auto returnIt = params.find("return_to_peak");
    if (returnIt != params.end()) {
      returnToPeak = (returnIt->second == "true" || returnIt->second == "1");
    }

    // ✅ OPTIONAL PARAMETER: direction_axis1 (centered/positive/negative)
    std::string directionAxis1 = "centered";
    auto dir1It = params.find("direction_axis1");
    if (dir1It != params.end() && !dir1It->second.empty()) {
      directionAxis1 = dir1It->second;
      if (directionAxis1 != "centered" && directionAxis1 != "positive" && directionAxis1 != "negative") {
        errorMessage = "GridScanExtended: Invalid direction_axis1 '" + directionAxis1 + "' (must be centered/positive/negative)";
        return false;
      }
    }

    // ✅ OPTIONAL PARAMETER: direction_axis2 (centered/positive/negative)
    std::string directionAxis2 = "centered";
    auto dir2It = params.find("direction_axis2");
    if (dir2It != params.end() && !dir2It->second.empty()) {
      directionAxis2 = dir2It->second;
      if (directionAxis2 != "centered" && directionAxis2 != "positive" && directionAxis2 != "negative") {
        errorMessage = "GridScanExtended: Invalid direction_axis2 '" + directionAxis2 + "' (must be centered/positive/negative)";
        return false;
      }
    }

    // ✅ OPTIONAL PARAMETER: exit_threshold
    double exitThreshold = std::numeric_limits<double>::max();
    bool hasExitThreshold = false;
    auto exitIt = params.find("exit_threshold");
    if (exitIt != params.end() && !exitIt->second.empty()) {
      try {
        exitThreshold = std::stod(exitIt->second);
        hasExitThreshold = true;
      } catch (...) {
        errorMessage = "GridScanExtended: Invalid exit_threshold value: " + exitIt->second;
        return false;
      }
    }

    // ✅ OPTIONAL PARAMETER: capture_as
    std::string captureAs;
    auto captureAsIt = params.find("capture_as");
    if (captureAsIt != params.end()) {
      captureAs = captureAsIt->second;
    }

    // ✅ OPTIONAL PARAMETER: capture_position_as
    std::string capturePositionAs;
    auto capturePosIt = params.find("capture_position_as");
    if (capturePosIt != params.end()) {
      capturePositionAs = capturePosIt->second;
    }

    // ✅ GET DATA STORE
    if (!m_dataStore) {
      errorMessage = "GridScanExtended: DataStore not available";
      return false;
    }

    // Convert to mm
    double rangeAxis1Mm = rangeAxis1Um / 1000.0;
    double rangeAxis2Mm = rangeAxis2Um / 1000.0;
    double stepMm = stepSizeUm / 1000.0;

    // Calculate grid dimensions (asymmetric supported)
    int numPoints1 = static_cast<int>(rangeAxis1Um / stepSizeUm) + 1;
    int numPoints2 = static_cast<int>(rangeAxis2Um / stepSizeUm) + 1;
    int totalPoints = numPoints1 * numPoints2;

    if (m_logger) {
      m_logger->Info(
        "GridScanExtended: Starting scan on " + deviceName +
        " (plane=" + plane +
        ", axis1=" + std::to_string(rangeAxis1Um) + "um/" + directionAxis1 +
        ", axis2=" + std::to_string(rangeAxis2Um) + "um/" + directionAxis2 +
        ", step=" + std::to_string(stepSizeUm) + "um" +
        ", grid=" + std::to_string(numPoints1) + "x" + std::to_string(numPoints2) +
        " = " + std::to_string(totalPoints) + " points" +
        ", channel='" + channel + "'" +
        ", settling=" + std::to_string(settlingTimeMs) + "ms" +
        ", snake=" + (snakePattern ? "true" : "false") +
        ", returnToPeak=" + (returnToPeak ? "true" : "false") +
        (hasExitThreshold ? ", exitThreshold=" + std::to_string(exitThreshold) : "") + ")",
        "MotionHandler"
      );
    }

    // ✅ GET CURRENT POSITION (center of grid)
    double centerAxis1 = 0.0, centerAxis2 = 0.0;
    PositionStruct currentPosExt = motionManager->GetCurrentPosition(deviceName);
    if (axis1 == "X") centerAxis1 = currentPosExt.x;
    else if (axis1 == "Y") centerAxis1 = currentPosExt.y;
    else if (axis1 == "Z") centerAxis1 = currentPosExt.z;

    if (axis2 == "X") centerAxis2 = currentPosExt.x;
    else if (axis2 == "Y") centerAxis2 = currentPosExt.y;
    else if (axis2 == "Z") centerAxis2 = currentPosExt.z;

    // ✅ EXECUTE GRID SCAN
    double peakValue = std::numeric_limits<double>::lowest();
    double peakAxis1 = centerAxis1;
    double peakAxis2 = centerAxis2;
    int currentPoint = 0;
    bool earlyExit = false;

    // Calculate start position based on direction
    // centered: current - range/2 to current + range/2
    // positive: current to current + range
    // negative: current - range to current
    double startAxis1, startAxis2;
    if (directionAxis1 == "positive") {
      startAxis1 = centerAxis1;  // Scan from current to current + range
    } else if (directionAxis1 == "negative") {
      startAxis1 = centerAxis1 - rangeAxis1Mm;  // Scan from current - range to current
    } else {  // centered
      startAxis1 = centerAxis1 - rangeAxis1Mm / 2.0;
    }

    if (directionAxis2 == "positive") {
      startAxis2 = centerAxis2;  // Scan from current to current + range
    } else if (directionAxis2 == "negative") {
      startAxis2 = centerAxis2 - rangeAxis2Mm;  // Scan from current - range to current
    } else {  // centered
      startAxis2 = centerAxis2 - rangeAxis2Mm / 2.0;
    }

    if (m_logger) {
      m_logger->Info(
        "GridScanExtended: Axis1(" + axis1 + ") dir=" + directionAxis1 +
        " start=" + std::to_string(startAxis1) +
        ", Axis2(" + axis2 + ") dir=" + directionAxis2 +
        " start=" + std::to_string(startAxis2),
        "MotionHandler"
      );
    }

    for (int row = 0; row < numPoints2 && !earlyExit; ++row) {
      // Snake pattern: alternate direction each row
      bool reverse = snakePattern && (row % 2 == 1);

      for (int col = 0; col < numPoints1 && !earlyExit; ++col) {
        // Check stop flag
        if (context && context->IsStopRequestedAny()) {
          if (m_logger) {
            m_logger->Info("GridScanExtended: Stop requested, halting scan", "MotionHandler");
          }
          errorMessage = "GridScanExtended: Scan stopped by user";
          return false;
        }

        int actualCol = reverse ? (numPoints1 - 1 - col) : col;

        double targetAxis1 = startAxis1 + actualCol * stepMm;
        double targetAxis2 = startAxis2 + row * stepMm;

        // Move to position (axis1 first, then axis2 for predictability)
        if (!motionManager->MoveToPosition(deviceName, axis1, targetAxis1, false)) {
          errorMessage = "GridScanExtended: Failed to start move on " + axis1;
          return false;
        }
        // Wait for axis1 move
        auto startMove = std::chrono::steady_clock::now();
        while (motionManager->IsMoving(deviceName, axis1)) {
          if (context && context->IsStopRequestedAny()) {
            errorMessage = "GridScanExtended: Scan stopped by user during motion";
            return false;
          }
          if (std::chrono::steady_clock::now() - startMove > std::chrono::seconds(10)) {
            errorMessage = "GridScanExtended: Timeout waiting for " + axis1 + " motion";
            return false;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        if (!motionManager->MoveToPosition(deviceName, axis2, targetAxis2, false)) {
          errorMessage = "GridScanExtended: Failed to start move on " + axis2;
          return false;
        }
        // Wait for axis2 move
        startMove = std::chrono::steady_clock::now();
        while (motionManager->IsMoving(deviceName, axis2)) {
          if (context && context->IsStopRequestedAny()) {
            errorMessage = "GridScanExtended: Scan stopped by user during motion";
            return false;
          }
          if (std::chrono::steady_clock::now() - startMove > std::chrono::seconds(10)) {
            errorMessage = "GridScanExtended: Timeout waiting for " + axis2 + " motion";
            return false;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Settling time
        std::this_thread::sleep_for(std::chrono::milliseconds(settlingTimeMs));

        // Read channel value
        double value = static_cast<double>(m_dataStore->GetValue(channel, 0.0f));

        // Track peak (maximize)
        if (value > peakValue) {
          peakValue = value;
          peakAxis1 = targetAxis1;
          peakAxis2 = targetAxis2;
          if (m_logger) {
            m_logger->Info(
              "GridScanExtended: New peak at (" + std::to_string(targetAxis1) + ", " +
              std::to_string(targetAxis2) + ") = " + std::to_string(value),
              "MotionHandler"
            );
          }
        }

        // Check exit threshold
        if (hasExitThreshold && value > exitThreshold) {
          earlyExit = true;
          if (m_logger) {
            m_logger->Info(
              "GridScanExtended: Exit threshold reached (" + std::to_string(value) +
              " > " + std::to_string(exitThreshold) + "), stopping scan",
              "MotionHandler"
            );
          }
        }

        currentPoint++;
        if (m_logger && (currentPoint % 20 == 0 || currentPoint == totalPoints)) {
          m_logger->Info(
            "GridScanExtended: Progress " + std::to_string(currentPoint) + "/" + std::to_string(totalPoints) +
            " (" + std::to_string(100 * currentPoint / totalPoints) + "%)",
            "MotionHandler"
          );
        }
      }
    }

    // ✅ RETURN TO PEAK POSITION IF ENABLED
    if (returnToPeak) {
      if (m_logger) {
        m_logger->Info(
          "GridScanExtended: Returning to peak position (" + std::to_string(peakAxis1) + ", " +
          std::to_string(peakAxis2) + ") with value " + std::to_string(peakValue),
          "MotionHandler"
        );
      }

      if (!motionManager->MoveToPosition(deviceName, axis1, peakAxis1, false)) {
        errorMessage = "GridScanExtended: Failed to return to peak on " + axis1;
        return false;
      }
      auto startReturnExt = std::chrono::steady_clock::now();
      while (motionManager->IsMoving(deviceName, axis1)) {
        if (std::chrono::steady_clock::now() - startReturnExt > std::chrono::seconds(10)) {
          errorMessage = "GridScanExtended: Timeout returning to peak on " + axis1;
          return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }

      if (!motionManager->MoveToPosition(deviceName, axis2, peakAxis2, false)) {
        errorMessage = "GridScanExtended: Failed to return to peak on " + axis2;
        return false;
      }
      startReturnExt = std::chrono::steady_clock::now();
      while (motionManager->IsMoving(deviceName, axis2)) {
        if (std::chrono::steady_clock::now() - startReturnExt > std::chrono::seconds(10)) {
          errorMessage = "GridScanExtended: Timeout returning to peak on " + axis2;
          return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }

    // ✅ CAPTURE PEAK VALUE TO CONTEXT IF REQUESTED
    if (!captureAs.empty() && context) {
      context->SetValue(captureAs, static_cast<float>(peakValue), deviceName, "GridScanExtended");
      if (m_logger) {
        m_logger->Info(
          "GridScanExtended: Captured peak value " + std::to_string(peakValue) + " to context:" + captureAs,
          "MotionHandler"
        );
      }
    }

    // ✅ CAPTURE PEAK POSITION TO CONTEXT IF REQUESTED
    if (!capturePositionAs.empty() && context) {
      std::string positionStr = std::to_string(peakAxis1) + "," + std::to_string(peakAxis2);
      context->SetValue(capturePositionAs, positionStr, deviceName, "GridScanExtended");
      if (m_logger) {
        m_logger->Info(
          "GridScanExtended: Captured peak position " + positionStr + " to context:" + capturePositionAs,
          "MotionHandler"
        );
      }
    }

    if (m_logger) {
      std::string completionReason = earlyExit ? " (early exit - threshold reached)" : "";
      m_logger->Info(
        "GridScanExtended: Completed successfully" + completionReason +
        " - Peak value: " + std::to_string(peakValue) +
        " at (" + std::to_string(peakAxis1) + ", " + std::to_string(peakAxis2) + ")",
        "MotionHandler"
      );
    }

    return true;
  }

  // ============================================================================
  // MOVE TO GRAPH NODE - Execute graph-based path planning
  // ============================================================================
  else if (methodName == "MoveToGraphNode") {
    // ✅ VALIDATE: ProductPositionManager must be set
    if (!m_positionManager) {
      errorMessage = "MoveToGraphNode: ProductPositionManager not available";
      return false;
    }

    // ✅ REQUIRED PARAMETER: graphName
    auto graphNameIt = params.find("graphName");
    if (graphNameIt == params.end()) {
      errorMessage = "MoveToGraphNode: Missing required parameter 'graphName'";
      return false;
    }
    std::string graphName = graphNameIt->second;

    // ✅ REQUIRED PARAMETER: endNodeName
    auto endNodeIt = params.find("endNodeName");
    if (endNodeIt == params.end()) {
      errorMessage = "MoveToGraphNode: Missing required parameter 'endNodeName'";
      return false;
    }
    std::string endNodeName = endNodeIt->second;

    if (m_logger) {
      m_logger->Info("MoveToGraphNode: Device=" + deviceName + ", Graph=" + graphName + ", EndNode=" + endNodeName, "MotionHandler");
    }

    // ✅ GET GRAPH
    MotionGraphManager& graphManager = m_positionManager->GetGraphManager();
    const MotionGraph* graph = graphManager.getGraph(QString::fromStdString(deviceName), QString::fromStdString(graphName));

    if (!graph) {
      errorMessage = "MoveToGraphNode: Graph '" + graphName + "' not found for device '" + deviceName + "'";
      return false;
    }

    // ✅ FIND END NODE IN GRAPH
    const MotionNode* endNode = graph->getNodeByPosition(QString::fromStdString(endNodeName));
    if (!endNode) {
      errorMessage = "MoveToGraphNode: End node '" + endNodeName + "' not found in graph '" + graphName + "'";
      return false;
    }

    // ✅ GET CURRENT PHYSICAL POSITION
    PositionStruct currentPos = motionManager->GetCurrentPosition(deviceName);
    std::map<std::string, PositionStruct> allPositions = m_positionManager->GetAllPositions(deviceName);

    if (m_logger) {
      std::string startPosStr = "MoveToGraphNode: Starting position (" +
                                std::to_string(currentPos.x) + ", " +
                                std::to_string(currentPos.y) + ", " +
                                std::to_string(currentPos.z) + ")";
      m_logger->Info(startPosStr, "MotionHandler");
    }

    // ✅ FIND CURRENT POSITION IN GRAPH (within tolerance)
    // Tolerance depends on device type:
    // - Gantry systems: 0.2mm (allows for small mechanical errors)
    // - Hexapods: 0.1mm (high precision)
    const double HEXAPOD_TOLERANCE = 0.1;  // 0.1mm for hexapods
    const double GANTRY_TOLERANCE = 0.2;   // 0.2mm for gantry systems
    const double FALLBACK_TOLERANCE = 0.3; // 0.3mm max fallback distance

    bool isGantry = (deviceName.find("gantry") != std::string::npos);
    double positionTolerance = isGantry ? GANTRY_TOLERANCE : HEXAPOD_TOLERANCE;

    std::string currentPosName;
    std::string closestPosName;
    double closestDistance = std::numeric_limits<double>::max();

    // ✅ DEBUG: Log current position and all candidates
    if (m_logger) {
      std::string posStr = "MoveToGraphNode: Current position (" +
                          std::to_string(currentPos.x) + ", " +
                          std::to_string(currentPos.y) + ", " +
                          std::to_string(currentPos.z) + ") checking " +
                          std::to_string(allPositions.size()) + " positions";
      m_logger->Info(posStr, "MotionHandler");
    }

    for (const auto& pair : allPositions) {
      const std::string& posName = pair.first;
      const PositionStruct& savedPos = pair.second;

      double dx = currentPos.x - savedPos.x;
      double dy = currentPos.y - savedPos.y;
      double dz = currentPos.z - savedPos.z;
      double distance = std::sqrt(dx*dx + dy*dy + dz*dz);

      // ✅ DEBUG: Log each position distance
      if (m_logger && distance < 10.0) {  // Only log if within 10mm
        std::string distStr = "  - '" + posName + "': " + std::to_string(distance) + "mm";
        m_logger->Info(distStr, "MotionHandler");
      }

      if (distance < closestDistance) {
        closestDistance = distance;
        closestPosName = posName;

        if (distance < positionTolerance) {
          currentPosName = posName;
          break;
        }
      }
    }

    // ✅ FALLBACK: If no exact match, use closest position if within reasonable distance
    if (currentPosName.empty() && closestDistance < FALLBACK_TOLERANCE) {
      currentPosName = closestPosName;
      if (m_logger) {
        m_logger->Warning(
          "MoveToGraphNode: Using closest position '" + currentPosName +
          "' (distance: " + std::to_string(closestDistance) + "mm) as fallback",
          "MotionHandler"
        );
      }
    }

    if (currentPosName.empty()) {
      errorMessage = "MoveToGraphNode: Current position not at any named position (closest: " + std::to_string(closestDistance) + "mm) - cannot find start node in graph";
      return false;
    }

    // ✅ FIND START NODE IN GRAPH
    const MotionNode* startNode = graph->getNodeByPosition(QString::fromStdString(currentPosName));
    if (!startNode) {
      errorMessage = "MoveToGraphNode: Current position '" + currentPosName + "' not found in graph '" + graphName + "'";
      return false;
    }

    if (m_logger) {
      m_logger->Info("MoveToGraphNode: Start node '" + currentPosName + "' (id: " + startNode->id.toStdString() + ")", "MotionHandler");
      m_logger->Info("MoveToGraphNode: End node '" + endNodeName + "' (id: " + endNode->id.toStdString() + ")", "MotionHandler");
    }

    // ✅ SPECIAL CASE: Already at goal position
    if (startNode->id == endNode->id) {
      if (m_logger) {
        m_logger->Info("MoveToGraphNode: Already at goal position '" + endNodeName + "', no movement needed", "MotionHandler");
      }
      return true;  // Success - we're already where we need to be
    }

    // ✅ CALCULATE PATH
    GraphPath path = graphManager.findPath(
      QString::fromStdString(deviceName),
      QString::fromStdString(graphName),
      startNode->id,
      endNode->id
    );

    if (!path.isValid) {
      errorMessage = "MoveToGraphNode: " + path.errorMessage.toStdString();
      return false;
    }

    if (m_logger) {
      std::string pathStr = "MoveToGraphNode: Path found (";
      for (int i = 0; i < path.positionNames.size(); ++i) {
        if (i > 0) pathStr += " → ";
        pathStr += path.positionNames[i].toStdString();
      }
      pathStr += ") cost=" + std::to_string(path.totalDistance);
      m_logger->Info(pathStr, "MotionHandler");
    }

    // ✅ EXECUTE PATH: Move through each waypoint
    for (const QString& posName : path.positionNames) {
      // Check if we're already at this position (skip if so)
      if (allPositions.count(posName.toStdString()) > 0) {
        const PositionStruct& targetPos = allPositions[posName.toStdString()];
        double dx = currentPos.x - targetPos.x;
        double dy = currentPos.y - targetPos.y;
        double dz = currentPos.z - targetPos.z;
        double distance = std::sqrt(dx*dx + dy*dy + dz*dz);

        // ✅ Use device-appropriate tolerance (same as position detection)
        if (distance < positionTolerance) {
          if (m_logger) {
            m_logger->Info("MoveToGraphNode: Already at '" + posName.toStdString() + "', skipping move", "MotionHandler");
          }
          // ✅ CRITICAL FIX: Still update currentPos even if we skip the move
          // This ensures our position tracking stays accurate for the next waypoint
          currentPos = targetPos;
          continue;
        }
      }

      // Move to waypoint
      if (m_logger) {
        m_logger->Info("MoveToGraphNode: Moving to waypoint '" + posName.toStdString() + "'", "MotionHandler");
      }

      if (!motionManager->MoveToNamedPosition(deviceName, posName.toStdString(), false)) {
        errorMessage = "MoveToGraphNode: Failed to start move to '" + posName.toStdString() + "'";
        return false;
      }

      // Wait for motion to complete
      if (!WaitForMotionWithStopCheck(manager, deviceName, context, 30000)) {
        errorMessage = "MoveToGraphNode: Motion to '" + posName.toStdString() + "' stopped or timed out";
        return false;
      }

      // ✅ DEBUG: Read position immediately after motion completes
      if (m_logger) {
        PositionStruct preSettlePos = motionManager->GetCurrentPosition(deviceName);
        std::string preSettleStr = "MoveToGraphNode: Position before settling (" +
                                   std::to_string(preSettlePos.x) + ", " +
                                   std::to_string(preSettlePos.y) + ", " +
                                   std::to_string(preSettlePos.z) + ")";
        m_logger->Info(preSettleStr, "MotionHandler");
      }

      // ✅ SETTLING TIME: Wait for controller to stabilize position
      // Gantry systems need more settling time than hexapods
      int settlingTimeMs = isGantry ? 200 : 50;
      std::this_thread::sleep_for(std::chrono::milliseconds(settlingTimeMs));

      // Update current position for next iteration
      currentPos = motionManager->GetCurrentPosition(deviceName);

      if (m_logger) {
        std::string afterSettleStr = "MoveToGraphNode: Position after settling (" +
                                     std::to_string(currentPos.x) + ", " +
                                     std::to_string(currentPos.y) + ", " +
                                     std::to_string(currentPos.z) + ")";
        m_logger->Info(afterSettleStr, "MotionHandler");
        m_logger->Info("MoveToGraphNode: Reached waypoint '" + posName.toStdString() + "'", "MotionHandler");

        // ✅ VERIFY: Check if we actually reached the target position
        if (allPositions.count(posName.toStdString()) > 0) {
          const PositionStruct& targetPos = allPositions[posName.toStdString()];
          double dx = currentPos.x - targetPos.x;
          double dy = currentPos.y - targetPos.y;
          double dz = currentPos.z - targetPos.z;
          double distance = std::sqrt(dx*dx + dy*dy + dz*dz);

          std::string verifyStr = "MoveToGraphNode: Distance to target '" + posName.toStdString() + "': " +
                                 std::to_string(distance) + "mm (target: " +
                                 std::to_string(targetPos.x) + ", " +
                                 std::to_string(targetPos.y) + ", " +
                                 std::to_string(targetPos.z) + ")";
          m_logger->Info(verifyStr, "MotionHandler");
        }
      }
    }

    if (m_logger) {
      m_logger->Info("MoveToGraphNode: Path execution completed successfully", "MotionHandler");
    }

    return true;
  }

  // ============================================================================
  // MOVE TO CONTEXT POSITION - Move to world coordinates from vision detection results
  // ============================================================================
  else if (methodName == "MoveToContextPosition") {
    // ✅ REQUIRED PARAMETER: context_prefix (e.g., "loc_sled")
    auto prefixIt = params.find("context_prefix");
    if (prefixIt == params.end() || prefixIt->second.empty()) {
      errorMessage = "MoveToContextPosition: Missing required parameter 'context_prefix'";
      return false;
    }
    std::string contextPrefix = prefixIt->second;

    // ✅ OPTIONAL PARAMETER: keep_z (default: true)
    bool keepZ = true;
    auto keepZIt = params.find("keep_z");
    if (keepZIt != params.end()) {
      keepZ = (keepZIt->second == "true" || keepZIt->second == "1");
    }

    // ✅ OPTIONAL PARAMETERS: x_key, y_key (for custom coordinate source)
    auto xKeyIt = params.find("x_key");
    auto yKeyIt = params.find("y_key");
    bool useCustomKeys = (xKeyIt != params.end() && yKeyIt != params.end());

    // ✅ VALIDATE CONTEXT
    if (!context) {
      errorMessage = "MoveToContextPosition: No context available";
      return false;
    }

    // ✅ CHECK DETECTION SUCCESS
    std::string successKey = contextPrefix + ".result.success";
    if (!context->HasValue(successKey)) {
      errorMessage = "MoveToContextPosition: No detection result found for '" + contextPrefix + "'";
      return false;
    }

    bool detectionSuccess = context->GetBoolean(successKey, false);
    if (!detectionSuccess) {
      errorMessage = "MoveToContextPosition: Detection was not successful for '" + contextPrefix + "'";
      return false;
    }

    // ✅ GET WORLD COORDINATES FROM CONTEXT
    double targetX = 0.0, targetY = 0.0;

    if (useCustomKeys) {
      // Use custom pixel coordinate keys and convert to world coordinates
      std::string pixelXKey = xKeyIt->second;
      std::string pixelYKey = yKeyIt->second;

      if (!context->HasValue(pixelXKey) || !context->HasValue(pixelYKey)) {
        errorMessage = "MoveToContextPosition: Custom coordinates not found: " + pixelXKey + " or " + pixelYKey;
        return false;
      }

      double detectedX_px = context->GetNumeric(pixelXKey, 0.0);
      double detectedY_px = context->GetNumeric(pixelYKey, 0.0);

      // Get image size
      std::string widthKey = contextPrefix + ".result.image_width";
      std::string heightKey = contextPrefix + ".result.image_height";
      double imageWidth = context->GetNumeric(widthKey, 0.0);
      double imageHeight = context->GetNumeric(heightKey, 0.0);

      if (imageWidth == 0.0 || imageHeight == 0.0) {
        errorMessage = "MoveToContextPosition: Image size not found in context";
        return false;
      }

      // Calculate offset from image center
      double imageCenterX = imageWidth / 2.0;
      double imageCenterY = imageHeight / 2.0;
      double offsetX_px = detectedX_px - imageCenterX;
      double offsetY_px = detectedY_px - imageCenterY;

      // Convert to mm using calibration
      // VisionCommandHandler already sets per-camera calibration on VisionManager when loading profiles
      double pixelToMmX = VisionManager::instance().getPixelToMmX();
      double pixelToMmY = VisionManager::instance().getPixelToMmY();
      double offsetX_mm = offsetX_px * pixelToMmX;
      double offsetY_mm = offsetY_px * pixelToMmY;

      // Get device position at capture time
      std::string deviceXKey = contextPrefix + ".detected.device_x_mm";
      std::string deviceYKey = contextPrefix + ".detected.device_y_mm";

      if (!context->HasValue(deviceXKey) || !context->HasValue(deviceYKey)) {
        std::cout << "[MotionCommandHandler] ⚠️ WARNING: Device position not found in context!" << std::endl;
        std::cout << "[MotionCommandHandler]   Missing keys: " << deviceXKey << ", " << deviceYKey << std::endl;
      }

      double deviceX_mm = context->GetNumeric(deviceXKey, 0.0);
      double deviceY_mm = context->GetNumeric(deviceYKey, 0.0);

      std::cout << "[MotionCommandHandler] 📍 Device position at capture: (" << deviceX_mm << ", " << deviceY_mm << ") mm" << std::endl;
      std::cout << "[MotionCommandHandler] 📐 Pixel offset from center: (" << offsetX_px << ", " << offsetY_px << ") px" << std::endl;
      std::cout << "[MotionCommandHandler]    Calculation: pixel_offset = detected - image_center = (" << detectedX_px << " - " << (imageWidth/2.0) << ", " << detectedY_px << " - " << (imageHeight/2.0) << ")" << std::endl;
      std::cout << "[MotionCommandHandler] 📐 MM offset from center: (" << offsetX_mm << ", " << offsetY_mm << ") mm" << std::endl;
      std::cout << "[MotionCommandHandler]    Calculation: mm_offset = pixel_offset × calibration = (" << offsetX_px << " × " << pixelToMmX << ", " << offsetY_px << " × " << pixelToMmY << ")" << std::endl;

      // Calculate world position
      targetX = deviceX_mm + offsetX_mm;
      targetY = deviceY_mm + offsetY_mm;

      std::cout << "[MotionCommandHandler] 🌍 Target world position: (" << targetX << ", " << targetY << ") mm" << std::endl;
      std::cout << "[MotionCommandHandler]    Calculation: target = device + offset = (" << deviceX_mm << " + " << offsetX_mm << ", " << deviceY_mm << " + " << offsetY_mm << ")" << std::endl;
    } else {
      // Use default world coordinates
      std::string worldXKey = contextPrefix + ".detected.world_x_mm";
      std::string worldYKey = contextPrefix + ".detected.world_y_mm";

      if (!context->HasValue(worldXKey) || !context->HasValue(worldYKey)) {
        errorMessage = "MoveToContextPosition: World coordinates not found for '" + contextPrefix +
                      "'. Make sure GotoDetect was used with capture_as='" + contextPrefix + "'";
        return false;
      }

      targetX = context->GetNumeric(worldXKey, 0.0);
      targetY = context->GetNumeric(worldYKey, 0.0);
    }

    // ✅ SAFETY CHECK: Verify target is within FOV limits (5mm x 5mm from capture position)
    // Get device position at capture time
    std::string deviceXKey = contextPrefix + ".detected.device_x_mm";
    std::string deviceYKey = contextPrefix + ".detected.device_y_mm";

    if (context->HasValue(deviceXKey) && context->HasValue(deviceYKey)) {
      double captureDeviceX = context->GetNumeric(deviceXKey, 0.0);
      double captureDeviceY = context->GetNumeric(deviceYKey, 0.0);

      double deltaX = std::abs(targetX - captureDeviceX);
      double deltaY = std::abs(targetY - captureDeviceY);

      const double MAX_FOV_OFFSET_MM = 5.0;  // Safety limit: 5mm x 5mm FOV

      if (deltaX > MAX_FOV_OFFSET_MM || deltaY > MAX_FOV_OFFSET_MM) {
        errorMessage = "MoveToContextPosition: Target exceeds FOV safety limit!\n";
        errorMessage += "  Capture position: (" + std::to_string(captureDeviceX) + ", " + std::to_string(captureDeviceY) + ") mm\n";
        errorMessage += "  Target position: (" + std::to_string(targetX) + ", " + std::to_string(targetY) + ") mm\n";
        errorMessage += "  Offset: ΔX=" + std::to_string(deltaX) + "mm, ΔY=" + std::to_string(deltaY) + "mm\n";
        errorMessage += "  Maximum allowed: " + std::to_string(MAX_FOV_OFFSET_MM) + "mm\n";
        errorMessage += "  This likely indicates a calculation error. Check vision calibration.";

        std::cout << "[MotionCommandHandler] ⛔ " << errorMessage << std::endl;
        return false;
      }

      std::cout << "[MotionCommandHandler] ✅ FOV safety check passed: ΔX=" << deltaX
                << "mm, ΔY=" << deltaY << "mm (limit: " << MAX_FOV_OFFSET_MM << "mm)" << std::endl;
    }

    // ✅ DETERMINE TARGET Z POSITION
    PositionStruct currentPos = motionManager->GetCurrentPosition(deviceName);
    double targetZ = currentPos.z;  // Default: keep current Z

    if (!keepZ) {
      // If not keeping current Z, use the Z position from when detection was performed
      std::string deviceZKey = contextPrefix + ".detected.device_z_mm";
      if (context->HasValue(deviceZKey)) {
        targetZ = context->GetNumeric(deviceZKey, currentPos.z);
      } else {
        // Fallback: keep current Z if device_z not available
        if (m_logger) {
          m_logger->Warning("MoveToContextPosition: device_z_mm not found, keeping current Z", "MotionHandler");
        }
      }
    }

    // ✅ LOG WHAT WE'RE DOING
    bool usingAnchor = false;
    std::string usingAnchorKey = contextPrefix + ".detected.using_anchor";
    if (context->HasValue(usingAnchorKey)) {
      usingAnchor = context->GetBoolean(usingAnchorKey, false);
    }

    if (m_logger) {
      std::string moveInfo = "MoveToContextPosition: Moving " + deviceName + " to detected position from '" + contextPrefix + "'";
      m_logger->Info(moveInfo, "MotionHandler");

      std::string coordInfo = "  Target: X=" + std::to_string(targetX) + "mm, Y=" + std::to_string(targetY) + "mm";
      if (keepZ) {
        coordInfo += ", Z=" + std::to_string(targetZ) + "mm (kept current)";
      } else {
        coordInfo += ", Z=" + std::to_string(targetZ) + "mm (from detection time)";
      }
      m_logger->Info(coordInfo, "MotionHandler");

      if (usingAnchor) {
        m_logger->Info("  Reference: Custom anchor point (precise feature location)", "MotionHandler");
      } else {
        m_logger->Info("  Reference: Pattern center (no custom anchor)", "MotionHandler");
      }
    }

    std::cout << "[MotionCommandHandler] MoveToContextPosition: " << deviceName << " -> ("
              << targetX << ", " << targetY << ", " << targetZ << ") mm "
              << "(using " << (usingAnchor ? "anchor" : "center") << ")" << std::endl;

    // ✅ EXECUTE MOVE (XY with current Z)
    // Use MoveToAbs with current Z to keep Z position unchanged
    bool moveOk = motionManager->MoveToAbs(deviceName, targetX, targetY, targetZ, 100.0, false);  // false = non-blocking
    if (!moveOk) {
      errorMessage = "MoveToContextPosition: Failed to move to detected position";
      return false;
    }

    // ✅ WAIT FOR COMPLETION
    if (!WaitForMotionWithStopCheck(manager, deviceName, context)) {
      errorMessage = "MoveToContextPosition: Motion was interrupted or timed out";
      return false;
    }

    if (m_logger) {
      PositionStruct finalPos = motionManager->GetCurrentPosition(deviceName);
      std::string finalInfo = "MoveToContextPosition: Reached position (" +
                              std::to_string(finalPos.x) + ", " +
                              std::to_string(finalPos.y) + ", " +
                              std::to_string(finalPos.z) + ")";
      m_logger->Info(finalInfo, "MotionHandler");
    }

    return true;
  }

  // ============================================================================
  // PARALLEL MOVE - Move multiple devices simultaneously
  // ============================================================================
  else if (methodName == "ParallelMove") {
    // Parse the moves JSON array from parameters
    // Format: moves = [{"device":"gantry","type":"position","target":"home"},
    //                  {"device":"hex-left","type":"graphNode","graph":"align","target":"probe"}]

    auto movesIt = params.find("moves");
    if (movesIt == params.end()) {
      errorMessage = "ParallelMove: Missing required parameter 'moves'";
      return false;
    }

    // Parse JSON array of moves
    std::vector<ParallelMoveSpec> moveSpecs;
    try {
      nlohmann::json movesJson = nlohmann::json::parse(movesIt->second);

      if (!movesJson.is_array()) {
        errorMessage = "ParallelMove: 'moves' must be a JSON array";
        return false;
      }

      for (const auto& moveObj : movesJson) {
        ParallelMoveSpec spec;

        if (!moveObj.contains("device") || !moveObj.contains("type") || !moveObj.contains("target")) {
          errorMessage = "ParallelMove: Each move must have 'device', 'type', and 'target'";
          return false;
        }

        spec.deviceName = moveObj["device"].get<std::string>();
        spec.moveType = moveObj["type"].get<std::string>();
        spec.targetName = moveObj["target"].get<std::string>();

        if (spec.moveType == "graphNode") {
          if (!moveObj.contains("graph")) {
            errorMessage = "ParallelMove: graphNode type requires 'graph' parameter";
            return false;
          }
          spec.graphName = moveObj["graph"].get<std::string>();
        }

        // Validate device exists
        if (!motionManager->HasDevice(spec.deviceName)) {
          errorMessage = "ParallelMove: Device '" + spec.deviceName + "' not found";
          return false;
        }

        moveSpecs.push_back(spec);
      }
    } catch (const nlohmann::json::exception& e) {
      errorMessage = "ParallelMove: Failed to parse moves JSON: " + std::string(e.what());
      return false;
    }

    if (moveSpecs.empty()) {
      errorMessage = "ParallelMove: No moves specified";
      return false;
    }

    // ✅ Check for debug parameter to enable verbose logging
    bool debugMode = false;
    auto debugIt = params.find("debug");
    if (debugIt != params.end() && (debugIt->second == "true" || debugIt->second == "1")) {
      debugMode = true;
    }
    if (debugMode && motionManager) {
      motionManager->SetDebugLogging(true);
      if (m_logger) {
        m_logger->Info("ParallelMove: Debug logging ENABLED", "MotionHandler");
      }
    }

    if (m_logger) {
      std::string moveList = "ParallelMove: Starting " + std::to_string(moveSpecs.size()) + " parallel moves:\n";
      for (size_t i = 0; i < moveSpecs.size(); ++i) {
        moveList += "  [" + std::to_string(i+1) + "] " + moveSpecs[i].deviceName +
                    " -> " + moveSpecs[i].targetName +
                    " (type=" + moveSpecs[i].moveType +
                    ", graph=" + moveSpecs[i].graphName + ")\n";
      }
      m_logger->Info(moveList, "MotionHandler");

      // ✅ Also log current waypoint states before starting
      if (debugMode && motionManager) {
        std::string stateInfo = "ParallelMove: Current waypoint states before move:\n";
        for (const auto& spec : moveSpecs) {
          stateInfo += "  " + spec.deviceName + ": " + motionManager->GetWaypointStateString(spec.deviceName) + "\n";
        }
        m_logger->Info(stateInfo, "MotionHandler");
      }
    }

    // ✅ PHASE 1: Separate moves by type and validate
    std::vector<ParallelMoveSpec> positionMoves;
    std::vector<ParallelMoveSpec> graphNodeMoves;

    for (const auto& spec : moveSpecs) {
      if (spec.moveType == "position") {
        positionMoves.push_back(spec);
      }
      else if (spec.moveType == "graphNode") {
        graphNodeMoves.push_back(spec);
      }
      else {
        errorMessage = "ParallelMove: Unknown move type '" + spec.moveType + "'";
        return false;
      }
    }

    // Validate graphNode requirements
    if (!graphNodeMoves.empty() && !m_positionManager) {
      errorMessage = "ParallelMove: ProductPositionManager not available for graphNode moves";
      return false;
    }

    // ✅ PHASE 2: Create and start workers for all graphNode moves (TRUE PARALLEL!)
    std::vector<GraphMoveWorker*> graphWorkers;

    for (const auto& spec : graphNodeMoves) {
      if (m_logger) {
        m_logger->Info("ParallelMove: Creating worker for " + spec.deviceName +
                       " graphNode -> " + spec.targetName + " (graph: " + spec.graphName + ")",
                       "MotionHandler");
      }

      // Create worker (handles all path finding and execution internally)
      auto* worker = new GraphMoveWorker(
        motionManager,
        m_positionManager,
        spec.deviceName,
        spec.graphName,
        spec.targetName
      );

      // Connect log messages
      if (m_logger) {
        QObject::connect(worker, &GraphMoveWorker::logMessage,
          [this](QString msg) {
            m_logger->Info(msg.toStdString(), "MotionHandler");
          });
      }

      // Auto-delete when finished
      QObject::connect(worker, &GraphMoveWorker::finished,
                       worker, &QObject::deleteLater);

      graphWorkers.push_back(worker);

      // Start the worker thread (non-blocking!)
      worker->start();
    }

    if (m_logger && !graphWorkers.empty()) {
      m_logger->Info("ParallelMove: Started " + std::to_string(graphWorkers.size()) +
                     " graphNode workers in parallel", "MotionHandler");
    }

    // ✅ PHASE 3: Start all position moves (non-blocking)
    for (const auto& spec : positionMoves) {
      bool startSuccess = motionManager->MoveToNamedPosition(spec.deviceName, spec.targetName, false);
      if (!startSuccess) {
        errorMessage = "ParallelMove: Failed to start move for " + spec.deviceName + " to " + spec.targetName;
        // Stop all workers
        for (auto* w : graphWorkers) {
          w->requestStop();
        }
        // Emergency stop all devices
        for (const auto& s : moveSpecs) {
          (void)motionManager->EmergencyStop(s.deviceName);
        }
        // Wait for workers to finish
        for (auto* w : graphWorkers) {
          w->wait();
        }
        return false;
      }

      if (m_logger) {
        m_logger->Info("ParallelMove: Started position move for " + spec.deviceName + " -> " + spec.targetName, "MotionHandler");
      }
    }

    // ✅ PHASE 4: Unified wait loop for ALL moves (position and graphNode)
    auto startTime = std::chrono::steady_clock::now();
    const int OVERALL_TIMEOUT_MS = 60000;  // 60 seconds overall timeout
    const double POSITION_TOLERANCE = 0.5;  // mm - tolerance for position verification

    // Track starting positions and completion state for position moves
    struct PositionMoveState {
      std::string deviceName;
      std::string targetName;
      PositionStruct startPos;
      PositionStruct targetPos;
      bool motionStarted = false;
      bool completed = false;
    };
    std::vector<PositionMoveState> positionMoveStates;

    for (const auto& spec : positionMoves) {
      PositionMoveState state;
      state.deviceName = spec.deviceName;
      state.targetName = spec.targetName;
      state.startPos = motionManager->GetCurrentPosition(spec.deviceName);

      // Get target position
      if (motionManager->GetPositionWithCalculation(spec.deviceName, spec.targetName, state.targetPos)) {
        // Check if already at target (within tolerance)
        double dx = state.startPos.x - state.targetPos.x;
        double dy = state.startPos.y - state.targetPos.y;
        double dz = state.startPos.z - state.targetPos.z;
        double distToTarget = std::sqrt(dx*dx + dy*dy + dz*dz);

        if (distToTarget < POSITION_TOLERANCE) {
          state.motionStarted = true;
          state.completed = true;
          if (m_logger) {
            m_logger->Info("ParallelMove: " + state.deviceName + " already at target '" + state.targetName +
                         "' (dist: " + std::to_string(distToTarget) + "mm)", "MotionHandler");
          }
        }
        positionMoveStates.push_back(state);
      } else {
        if (m_logger) {
          m_logger->Warning("ParallelMove: Could not get target position for " + spec.deviceName + " -> " + spec.targetName, "MotionHandler");
        }
        // Still add it but without target verification
        state.targetPos = state.startPos;  // Will rely on IsDeviceInMotion
        positionMoveStates.push_back(state);
      }
    }

    constexpr int MAX_ITERATIONS_PARALLEL = 2400; // Safety bound: 60s timeout / 50ms poll * 2x margin
    for (int iteration = 0; iteration < MAX_ITERATIONS_PARALLEL; ++iteration) {
      // Check stop request
      if (context && context->IsStopRequestedAny()) {
        if (m_logger) {
          m_logger->Warning("ParallelMove: Stop requested, aborting all moves", "MotionHandler");
        }
        // Stop all workers
        for (auto* w : graphWorkers) {
          w->requestStop();
        }
        // Emergency stop all devices
        for (const auto& spec : moveSpecs) {
          (void)motionManager->EmergencyStop(spec.deviceName);
        }
        // Wait for workers to finish
        for (auto* w : graphWorkers) {
          w->wait();
        }
        errorMessage = "ParallelMove: Stopped by user request";
        return false;
      }

      // Check if all workers completed
      bool allWorkersComplete = true;
      for (auto* w : graphWorkers) {
        if (!w->isCompleted()) {
          allWorkersComplete = false;
          break;
        }
      }

      // Check if all position moves completed (with motion start detection)
      bool allPositionsComplete = true;
      for (auto& state : positionMoveStates) {
        if (state.completed) continue;

        PositionStruct currentPos = motionManager->GetCurrentPosition(state.deviceName);
        bool isMoving = motionManager->IsDeviceInMotion(state.deviceName);

        // Detect if motion has started (position changed from start)
        if (!state.motionStarted) {
          double dx = currentPos.x - state.startPos.x;
          double dy = currentPos.y - state.startPos.y;
          double dz = currentPos.z - state.startPos.z;
          double distFromStart = std::sqrt(dx*dx + dy*dy + dz*dz);

          if (distFromStart > 0.01 || isMoving) {  // 0.01mm threshold for start detection
            state.motionStarted = true;
          }
        }

        // Only mark complete if motion started AND device stopped AND at target
        if (state.motionStarted && !isMoving) {
          double dx = currentPos.x - state.targetPos.x;
          double dy = currentPos.y - state.targetPos.y;
          double dz = currentPos.z - state.targetPos.z;
          double distToTarget = std::sqrt(dx*dx + dy*dy + dz*dz);

          if (distToTarget < POSITION_TOLERANCE) {
            state.completed = true;
            if (m_logger) {
              m_logger->Info("ParallelMove: " + state.deviceName + " completed (dist to target: " +
                           std::to_string(distToTarget) + "mm)", "MotionHandler");
            }
            // Clear pending target
            motionManager->ClearPendingTarget(state.deviceName);
          }
        }

        if (!state.completed) {
          allPositionsComplete = false;
        }
      }

      // If all done, break
      if (allWorkersComplete && allPositionsComplete) {
        break;
      }

      // Check timeout
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime).count();
      if (elapsed > OVERALL_TIMEOUT_MS) {
        if (m_logger) {
          m_logger->Error("ParallelMove: Overall timeout exceeded", "MotionHandler");
        }
        // Stop all workers
        for (auto* w : graphWorkers) {
          w->requestStop();
        }
        // Emergency stop all devices
        for (const auto& spec : moveSpecs) {
          (void)motionManager->EmergencyStop(spec.deviceName);
        }
        // Wait for workers to finish
        for (auto* w : graphWorkers) {
          w->wait();
        }
        errorMessage = "ParallelMove: Overall timeout exceeded (" + std::to_string(OVERALL_TIMEOUT_MS) + "ms)";
        return false;
      }

      // Sleep to avoid busy waiting
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Safety bound exhausted - treat as timeout
    if (m_logger) {
      m_logger->Error("Loop safety bound reached in ParallelMove", "MotionHandler");
    }
    for (auto* w : graphWorkers) {
      w->requestStop();
    }
    for (const auto& spec : moveSpecs) {
      (void)motionManager->EmergencyStop(spec.deviceName);
    }
    for (auto* w : graphWorkers) {
      w->wait();
    }
    // Fall through to PHASE 5 to clean up and report

    // ✅ PHASE 5: Check results
    bool allSuccess = true;
    std::string failedDevices;

    // Check worker results
    for (auto* w : graphWorkers) {
      w->wait();  // Ensure thread finished
      if (!w->isSuccess()) {
        allSuccess = false;
        if (!failedDevices.empty()) failedDevices += ", ";
        failedDevices += w->getDeviceName() + " (" + w->getErrorMessage().toStdString() + ")";
      }
    }

    if (!allSuccess) {
      errorMessage = "ParallelMove: GraphNode moves failed for: " + failedDevices;
      return false;
    }

    if (m_logger) {
      m_logger->Info("ParallelMove: All " + std::to_string(moveSpecs.size()) + " moves completed successfully", "MotionHandler");
    }

    return true;
  }

  // ============================================================================
  // PARALLEL MOVE RELATIVE - Move multiple devices relative distances simultaneously
  // ============================================================================
  else if (methodName == "ParallelMoveRelative") {
    // Parse the moves JSON array from parameters
    // Format: moves = [{"device":"hex-left","axis":"X","distance":4.0},
    //                  {"device":"hex-right","axis":"Z","distance":-2.0}]

    auto movesIt = params.find("moves");
    if (movesIt == params.end()) {
      errorMessage = "ParallelMoveRelative: Missing required parameter 'moves'";
      return false;
    }

    // Parse JSON array of moves
    std::vector<ParallelRelativeMoveSpec> moveSpecs;
    std::vector<std::string> deviceNames;  // For waiting
    try {
      nlohmann::json movesJson = nlohmann::json::parse(movesIt->second);

      if (!movesJson.is_array()) {
        errorMessage = "ParallelMoveRelative: 'moves' must be a JSON array";
        return false;
      }

      for (const auto& moveObj : movesJson) {
        ParallelRelativeMoveSpec spec;

        if (!moveObj.contains("device") || !moveObj.contains("axis") || !moveObj.contains("distance")) {
          errorMessage = "ParallelMoveRelative: Each move must have 'device', 'axis', and 'distance'";
          return false;
        }

        spec.deviceName = moveObj["device"].get<std::string>();
        spec.axis = moveObj["axis"].get<std::string>();

        // Handle distance as either number or string
        if (moveObj["distance"].is_number()) {
          spec.distance = moveObj["distance"].get<double>();
        } else {
          spec.distance = std::stod(moveObj["distance"].get<std::string>());
        }

        // Validate device exists
        if (!motionManager->HasDevice(spec.deviceName)) {
          errorMessage = "ParallelMoveRelative: Device '" + spec.deviceName + "' not found";
          return false;
        }

        // Validate axis
        std::string axisUpper = spec.axis;
        std::transform(axisUpper.begin(), axisUpper.end(), axisUpper.begin(), ::toupper);
        if (axisUpper != "X" && axisUpper != "Y" && axisUpper != "Z" &&
            axisUpper != "U" && axisUpper != "V" && axisUpper != "W") {
          errorMessage = "ParallelMoveRelative: Invalid axis '" + spec.axis + "' (must be X, Y, Z, U, V, or W)";
          return false;
        }
        spec.axis = axisUpper;

        moveSpecs.push_back(spec);

        // Track unique device names for waiting
        if (std::find(deviceNames.begin(), deviceNames.end(), spec.deviceName) == deviceNames.end()) {
          deviceNames.push_back(spec.deviceName);
        }
      }
    } catch (const nlohmann::json::exception& e) {
      errorMessage = "ParallelMoveRelative: Failed to parse moves JSON: " + std::string(e.what());
      return false;
    }

    if (moveSpecs.empty()) {
      errorMessage = "ParallelMoveRelative: No moves specified";
      return false;
    }

    if (m_logger) {
      std::string moveList = "ParallelMoveRelative: Starting " + std::to_string(moveSpecs.size()) + " parallel relative moves:\n";
      for (size_t i = 0; i < moveSpecs.size(); ++i) {
        moveList += "  [" + std::to_string(i+1) + "] " + moveSpecs[i].deviceName +
                    " " + moveSpecs[i].axis + " by " + std::to_string(moveSpecs[i].distance) + "mm\n";
      }
      m_logger->Info(moveList, "MotionHandler");
    }

    // ✅ PHASE 1: Start all relative moves (non-blocking)
    for (const auto& spec : moveSpecs) {
      // Skip zero distance moves
      if (std::abs(spec.distance) < 1e-9) {
        if (m_logger) {
          m_logger->Info("ParallelMoveRelative: Skipping zero-distance move for " + spec.deviceName + " " + spec.axis, "MotionHandler");
        }
        continue;
      }

      if (!motionManager->MoveRelative(spec.deviceName, spec.axis, spec.distance, false)) {
        errorMessage = "ParallelMoveRelative: Failed to start move for " + spec.deviceName + " " + spec.axis;
        // Emergency stop all devices that already started
        for (const auto& stopDevice : deviceNames) {
          (void)motionManager->EmergencyStop(stopDevice);
        }
        return false;
      }

      if (m_logger) {
        m_logger->Info("ParallelMoveRelative: Started move for " + spec.deviceName + " " + spec.axis + " by " + std::to_string(spec.distance), "MotionHandler");
      }
    }

    // ✅ PHASE 2: Wait for all moves to complete
    if (!WaitForMultipleRelativeMotionsWithStopCheck(manager, deviceNames, context)) {
      errorMessage = "ParallelMoveRelative: One or more moves failed, timed out, or were stopped";
      return false;
    }

    if (m_logger) {
      m_logger->Info("ParallelMoveRelative: All " + std::to_string(moveSpecs.size()) + " moves completed successfully", "MotionHandler");
    }

    return true;
  }

  // ============================================================================
  // SAVE POSITION TO CONTEXT - Capture current device position into context variables
  // ============================================================================
  else if (methodName == "SavePositionToContext") {
    // Required: key_prefix - prefix for context keys (e.g., "corner1" -> corner1_x, corner1_y, etc.)
    auto prefixIt = params.find("key_prefix");
    if (prefixIt == params.end()) {
      errorMessage = "SavePositionToContext: Missing required parameter 'key_prefix'";
      return false;
    }
    std::string keyPrefix = prefixIt->second;

    if (!context) {
      errorMessage = "SavePositionToContext: No sequence context available";
      return false;
    }

    // Get current position from device
    PositionStruct currentPos = motionManager->GetCurrentPosition(deviceName);

    // Save XYZ to context
    context->SetValue(keyPrefix + "_x", static_cast<float>(currentPos.x), deviceName, methodName);
    context->SetValue(keyPrefix + "_y", static_cast<float>(currentPos.y), deviceName, methodName);
    context->SetValue(keyPrefix + "_z", static_cast<float>(currentPos.z), deviceName, methodName);

    // Check if UVW should also be saved (optional parameter)
    bool saveUVW = false;
    auto uvwIt = params.find("save_uvw");
    if (uvwIt != params.end() && (uvwIt->second == "true" || uvwIt->second == "1")) {
      saveUVW = true;
    }

    if (saveUVW) {
      context->SetValue(keyPrefix + "_u", static_cast<float>(currentPos.u), deviceName, methodName);
      context->SetValue(keyPrefix + "_v", static_cast<float>(currentPos.v), deviceName, methodName);
      context->SetValue(keyPrefix + "_w", static_cast<float>(currentPos.w), deviceName, methodName);
    }

    if (m_logger) {
      std::string logMsg = "SavePositionToContext: " + deviceName + " position saved as '" + keyPrefix + "' (X=" +
                           std::to_string(currentPos.x) + ", Y=" + std::to_string(currentPos.y) +
                           ", Z=" + std::to_string(currentPos.z);
      if (saveUVW) {
        logMsg += ", U=" + std::to_string(currentPos.u) + ", V=" + std::to_string(currentPos.v) +
                  ", W=" + std::to_string(currentPos.w);
      }
      logMsg += ")";
      m_logger->Info(logMsg, "MotionHandler");
    }

    std::cout << "[MotionCommandHandler] SavePositionToContext: " << deviceName
              << " -> " << keyPrefix << "_xyz" << (saveUVW ? "uvw" : "") << std::endl;

    return true;
  }

  // ============================================================================
  // MOVE TO ABS FROM CONTEXT - Move to position stored in context variables
  // ============================================================================
  else if (methodName == "MoveToAbsFromContext") {
    // Required: key_prefix - prefix for context keys (e.g., "corner1" -> reads corner1_x, corner1_y, corner1_z)
    auto prefixIt = params.find("key_prefix");
    if (prefixIt == params.end()) {
      errorMessage = "MoveToAbsFromContext: Missing required parameter 'key_prefix'";
      return false;
    }
    std::string keyPrefix = prefixIt->second;

    if (!context) {
      errorMessage = "MoveToAbsFromContext: No sequence context available";
      return false;
    }

    // Read position from context
    double x = static_cast<double>(context->GetNumeric(keyPrefix + "_x", 0.0f));
    double y = static_cast<double>(context->GetNumeric(keyPrefix + "_y", 0.0f));
    double z = static_cast<double>(context->GetNumeric(keyPrefix + "_z", 0.0f));

    // Optional: keep_z parameter - if true, use current Z instead of context Z
    bool keepZ = false;
    auto keepZIt = params.find("keep_z");
    if (keepZIt != params.end() && (keepZIt->second == "true" || keepZIt->second == "1")) {
      keepZ = true;
      double currentZ = 0.0;
      if (!motionManager->GetPosition(deviceName, "Z", currentZ)) {
        if (m_logger) {
          m_logger->Error("MoveToContextPosition: Failed to read Z position for " + deviceName, "MotionHandler");
        }
        return false;
      }
      z = currentZ;
    }

    // Optional: UVW from context
    double u = 0.0, v = 0.0, w = 0.0;
    bool hasUVW = false;
    auto uvwIt = params.find("use_uvw");
    if (uvwIt != params.end() && (uvwIt->second == "true" || uvwIt->second == "1")) {
      u = static_cast<double>(context->GetNumeric(keyPrefix + "_u", 0.0f));
      v = static_cast<double>(context->GetNumeric(keyPrefix + "_v", 0.0f));
      w = static_cast<double>(context->GetNumeric(keyPrefix + "_w", 0.0f));
      hasUVW = true;
    }

    // Check if non-blocking mode requested explicitly
    bool explicitNonBlocking = false;
    auto blockingIt = params.find("blocking");
    if (blockingIt != params.end() && blockingIt->second == "false") {
      explicitNonBlocking = true;
    }

    if (m_logger) {
      std::string logMsg = "MoveToAbsFromContext: " + deviceName + " moving to '" + keyPrefix + "' (X=" +
                           std::to_string(x) + ", Y=" + std::to_string(y) + ", Z=" + std::to_string(z);
      if (keepZ) logMsg += " [Z=current]";
      if (hasUVW) {
        logMsg += ", U=" + std::to_string(u) + ", V=" + std::to_string(v) + ", W=" + std::to_string(w);
      }
      logMsg += ")";
      m_logger->Info(logMsg, "MotionHandler");
    }

    // Execute move
    bool moveSuccess = true;
    if (hasUVW) {
      // For hexapods with UVW, move all 6 axes
      moveSuccess &= motionManager->MoveToPosition(deviceName, "X", x, false);
      moveSuccess &= motionManager->MoveToPosition(deviceName, "Y", y, false);
      moveSuccess &= motionManager->MoveToPosition(deviceName, "Z", z, false);
      moveSuccess &= motionManager->MoveToPosition(deviceName, "U", u, false);
      moveSuccess &= motionManager->MoveToPosition(deviceName, "V", v, false);
      moveSuccess &= motionManager->MoveToPosition(deviceName, "W", w, false);
    } else {
      moveSuccess = motionManager->MoveToAbs(deviceName, x, y, z, 100.0, false);
    }

    if (!moveSuccess) {
      errorMessage = "MoveToAbsFromContext: Failed to start move";
      return false;
    }

    // If explicit non-blocking requested, return immediately
    if (explicitNonBlocking) {
      return true;
    }

    // Wait for completion with stop flag monitoring
    TargetPosition target;
    target.hasX = true;
    target.hasY = true;
    target.hasZ = true;
    target.hasU = hasUVW;
    target.hasV = hasUVW;
    target.hasW = hasUVW;
    target.x = x;
    target.y = y;
    target.z = z;
    target.u = u;
    target.v = v;
    target.w = w;

    if (!WaitForMotionWithStopCheck(manager, deviceName, context, 30000, &target)) {
      errorMessage = "MoveToAbsFromContext: Motion stopped, timeout, or position verification failed";
      return false;
    }

    return true;
  }

  // ============================================================================
  // MOVE TO CONTEXT XY - Move X and Y to positions from context, keep current Z
  // ============================================================================
  // This command moves to X/Y coordinates stored in context variables while
  // keeping the current Z position unchanged. Useful for coordinate system
  // alignment workflows where target positions come from transformation results.
  //
  // Required params:
  //   x_key - Context key for X coordinate (e.g., "tgtSled.x" or "$tgtSled.x")
  //   y_key - Context key for Y coordinate (e.g., "tgtSled.y" or "$tgtSled.y")
  //
  // Optional params:
  //   blocking - "false" to return immediately without waiting (default: wait for completion)
  else if (methodName == "MoveToContextXY") {
    auto xKeyIt = params.find("x_key");
    auto yKeyIt = params.find("y_key");

    if (xKeyIt == params.end() || yKeyIt == params.end()) {
      errorMessage = "MoveToContextXY: Missing required parameters: x_key, y_key";
      return false;
    }

    if (!context) {
      errorMessage = "MoveToContextXY: No sequence context available";
      return false;
    }

    // Resolve context keys (strip $ prefix if present)
    std::string xKey = xKeyIt->second;
    std::string yKey = yKeyIt->second;
    if (!xKey.empty() && xKey[0] == '$') {
      xKey = xKey.substr(1);
    }
    if (!yKey.empty() && yKey[0] == '$') {
      yKey = yKey.substr(1);
    }

    // Read positions from context
    double targetX = static_cast<double>(context->GetNumeric(xKey, 0.0f));
    double targetY = static_cast<double>(context->GetNumeric(yKey, 0.0f));

    // Get current Z position
    double currentZ = 0.0;
    if (!motionManager->GetPosition(deviceName, "Z", currentZ)) {
      if (m_logger) {
        m_logger->Error("MoveToContextXY: Failed to read Z position for " + deviceName, "MotionHandler");
      }
      return false;
    }

    // Check if non-blocking mode requested
    bool explicitNonBlocking = false;
    auto blockingIt = params.find("blocking");
    if (blockingIt != params.end() && blockingIt->second == "false") {
      explicitNonBlocking = true;
    }

    if (m_logger) {
      m_logger->Info(
        "MoveToContextXY: " + deviceName + " moving to X=" + std::to_string(targetX) +
        " (from " + xKey + "), Y=" + std::to_string(targetY) +
        " (from " + yKey + "), keeping Z=" + std::to_string(currentZ),
        "MotionHandler"
      );
    }

    std::cout << "[MotionCommandHandler] MoveToContextXY: " << deviceName
              << " -> X=" << targetX << " (from " << xKey << "), Y=" << targetY
              << " (from " << yKey << "), Z=" << currentZ << " (current)" << std::endl;

    // Execute move (X and Y only, keep current Z)
    bool moveSuccess = motionManager->MoveToAbs(deviceName, targetX, targetY, currentZ, 100.0, false);

    if (!moveSuccess) {
      errorMessage = "MoveToContextXY: Failed to start move";
      return false;
    }

    // If explicit non-blocking requested, return immediately
    if (explicitNonBlocking) {
      return true;
    }

    // Wait for completion with stop flag monitoring
    TargetPosition target;
    target.hasX = true;
    target.hasY = true;
    target.hasZ = true;
    target.x = targetX;
    target.y = targetY;
    target.z = currentZ;

    if (!WaitForMotionWithStopCheck(manager, deviceName, context, 30000, &target)) {
      errorMessage = "MoveToContextXY: Motion stopped, timeout, or position verification failed";
      return false;
    }

    return true;
  }

  // ============================================================================
  // UPDATE POSITION AXIS FROM CONTEXT
  // ============================================================================
  // Updates a single axis of a named position with a value from context
  // Then saves the product file
  // params: position_name, axis (X/Y/Z/U/V/W), context_key, save_to_file (true/false)
  else if (methodName == "UpdatePositionAxis") {
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
    if (!m_positionManager) {
      errorMessage = "UpdatePositionAxis: ProductPositionManager not available";
      return false;
    }

    // Get current position
    PositionStruct currentPos;
    if (!m_positionManager->GetPosition(deviceName, positionName, currentPos)) {
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
    m_positionManager->AddPosition(deviceName, positionName, currentPos);

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
      if (m_positionManager->SaveProduct()) {
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
  else if (methodName == "UpdatePositionAll") {
    auto posNameIt = params.find("position_name");

    if (posNameIt == params.end()) {
      errorMessage = "UpdatePositionAll: Missing required parameter (position_name)";
      return false;
    }

    std::string positionName = posNameIt->second;

    // Check if ProductPositionManager is available
    if (!m_positionManager) {
      errorMessage = "UpdatePositionAll: ProductPositionManager not available";
      return false;
    }

    // Get current device position
    PositionStruct currentPos = motionManager->GetCurrentPosition(deviceName);

    // Get installed axes for this device
    std::string installedAxes = motionManager->GetInstalledAxes(deviceName);

    // Get existing position (if any) to preserve waypoints
    PositionStruct existingPos;
    bool hasExisting = m_positionManager->GetPosition(deviceName, positionName, existingPos);

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
    m_positionManager->AddPosition(deviceName, positionName, updatedPos);

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
      if (m_positionManager->SaveProduct()) {
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
  // REGISTER DEVICE LINK - Link a device to gantry-main for coordinated movement
  // ============================================================================
  else if (methodName == "RegisterDeviceLink") {
    if (!m_linkManager) {
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

    if (m_logger) {
      m_logger->Info("RegisterDeviceLink: Registering link " + primaryDevice + " -> " + linkedDevice, "MotionHandler");
    }

    // Register the link (captures current positions and calculates offset)
    if (!m_linkManager->RegisterLink(primaryDevice, linkedDevice)) {
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
      if (!m_linkManager->SaveToFile("config/device_links.json")) {
        if (m_logger) {
          m_logger->Warning("RegisterDeviceLink: Link registered but failed to save to file", "MotionHandler");
        }
      }
    }

    if (m_logger) {
      m_logger->Info("RegisterDeviceLink: Successfully registered link", "MotionHandler");
    }

    return true;
  }

  // ============================================================================
  // COMPENSATE DEVICE LINK - Move linked device to restore original relationship
  // ============================================================================
  else if (methodName == "CompensateDeviceLink") {
    if (!m_linkManager) {
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

    if (m_logger) {
      m_logger->Info("CompensateDeviceLink: Compensating " + linkedDevice + " relative to " + primaryDevice + " (axes: " + axes + ")", "MotionHandler");
    }

    // Check if link exists
    if (!m_linkManager->HasLink(primaryDevice, linkedDevice)) {
      errorMessage = "CompensateDeviceLink: No link registered for " + primaryDevice + " -> " + linkedDevice;
      return false;
    }

    // Get the compensation move (in linked device's local coordinates)
    CoordinateTransformer::Vector6D compensation;
    if (!m_linkManager->GetCompensationMove(primaryDevice, linkedDevice, compensation, axes)) {
      errorMessage = "CompensateDeviceLink: Failed to calculate compensation move";
      return false;
    }

    // Log the compensation values
    if (m_logger) {
      std::string compStr = "CompensateDeviceLink: Moving " + linkedDevice + " by X=" +
                            std::to_string(compensation.x) + ", Y=" +
                            std::to_string(compensation.y) + ", Z=" +
                            std::to_string(compensation.z);
      m_logger->Info(compStr, "MotionHandler");
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

    // Wait for motion to complete
    if (!WaitForMotionWithStopCheck(manager, linkedDevice, context, 30000)) {
      errorMessage = "CompensateDeviceLink: Motion stopped or timed out";
      return false;
    }

    if (m_logger) {
      m_logger->Info("CompensateDeviceLink: Compensation complete", "MotionHandler");
    }

    return true;
  }

  // ============================================================================
  // FAST ALIGN XY - PI hexapod FSM (Fast Scan Maximum) 2D alignment
  // ============================================================================
  else if (methodName == "FastAlignXY") {
    // Validate device is a PI hexapod (hex-* pattern)
    if (deviceName.find("hex-") != 0) {
      errorMessage = "FastAlignXY: Device must be a PI hexapod (hex-*), got: " + deviceName;
      return false;
    }

    // Get PI controller directly for FSM commands
    PIController* piController = motionManager->GetPIController(deviceName);
    if (!piController || !piController->IsConnected()) {
      errorMessage = "FastAlignXY: PI controller not connected for device: " + deviceName;
      return false;
    }

    // ✅ REQUIRED PARAMETER: axis1
    std::string axis1 = "X";
    auto axis1It = params.find("axis1");
    if (axis1It != params.end() && !axis1It->second.empty()) {
      axis1 = axis1It->second;
    }

    // ✅ REQUIRED PARAMETER: length1_um (scan length for axis1 in micrometers)
    double length1Um = 20.0;  // Default 20 um
    auto length1It = params.find("length1_um");
    if (length1It != params.end() && !length1It->second.empty()) {
      try {
        length1Um = std::stod(length1It->second);
      } catch (...) {
        errorMessage = "FastAlignXY: Invalid length1_um value: " + length1It->second;
        return false;
      }
    }

    // ✅ REQUIRED PARAMETER: axis2
    std::string axis2 = "Y";
    auto axis2It = params.find("axis2");
    if (axis2It != params.end() && !axis2It->second.empty()) {
      axis2 = axis2It->second;
    }

    // ✅ REQUIRED PARAMETER: length2_um (scan length for axis2 in micrometers)
    double length2Um = 20.0;  // Default 20 um
    auto length2It = params.find("length2_um");
    if (length2It != params.end() && !length2It->second.empty()) {
      try {
        length2Um = std::stod(length2It->second);
      } catch (...) {
        errorMessage = "FastAlignXY: Invalid length2_um value: " + length2It->second;
        return false;
      }
    }

    // ✅ REQUIRED PARAMETER: threshold (voltage threshold)
    double threshold = 0.01;  // Default 0.01 V
    auto thresholdIt = params.find("threshold");
    if (thresholdIt != params.end() && !thresholdIt->second.empty()) {
      try {
        threshold = std::stod(thresholdIt->second);
      } catch (...) {
        errorMessage = "FastAlignXY: Invalid threshold value: " + thresholdIt->second;
        return false;
      }
    }

    // ✅ REQUIRED PARAMETER: line_distance_um (spacing between scan lines)
    double lineDistanceUm = 1.0;  // Default 1 um
    auto lineDistIt = params.find("line_distance_um");
    if (lineDistIt != params.end() && !lineDistIt->second.empty()) {
      try {
        lineDistanceUm = std::stod(lineDistIt->second);
      } catch (...) {
        errorMessage = "FastAlignXY: Invalid line_distance_um value: " + lineDistIt->second;
        return false;
      }
    }

    // ✅ REQUIRED PARAMETER: analog_channel (ADC channel 1-6)
    int analogChannel = 5;  // Default channel 5
    auto channelIt = params.find("analog_channel");
    if (channelIt != params.end() && !channelIt->second.empty()) {
      try {
        analogChannel = std::stoi(channelIt->second);
        if (analogChannel < 1 || analogChannel > 6) {
          errorMessage = "FastAlignXY: analog_channel must be 1-6, got: " + channelIt->second;
          return false;
        }
      } catch (...) {
        errorMessage = "FastAlignXY: Invalid analog_channel value: " + channelIt->second;
        return false;
      }
    }

    // Convert micrometers to millimeters for PI controller
    double length1Mm = length1Um / 1000.0;
    double length2Mm = length2Um / 1000.0;
    double lineDistanceMm = lineDistanceUm / 1000.0;

    if (m_logger) {
      m_logger->Info(
        "FastAlignXY: Starting FSM on " + deviceName +
        " (axis1=" + axis1 + "/" + std::to_string(length1Um) + "um" +
        ", axis2=" + axis2 + "/" + std::to_string(length2Um) + "um" +
        ", threshold=" + std::to_string(threshold) + "V" +
        ", lineDistance=" + std::to_string(lineDistanceUm) + "um" +
        ", channel=" + std::to_string(analogChannel) + ")",
        "MotionHandler"
      );
    }

    // Execute FSM command
    bool fsmStarted = piController->FSM(
      axis1, length1Mm,
      axis2, length2Mm,
      threshold, lineDistanceMm,
      analogChannel);

    if (!fsmStarted) {
      errorMessage = "FastAlignXY: FSM command failed to start - check PI controller error";
      return false;
    }

    // ✅ POLL FOR COMPLETION
    const int TIMEOUT_MS = 60000;  // 60 second timeout
    const int POLL_INTERVAL_MS = 200;
    const int NOT_MOVING_THRESHOLD = 5;  // Consider done if not moving for 5 consecutive polls
    int elapsedMs = 0;
    int lastStatus = -999;
    int notMovingCount = 0;

    constexpr int MAX_ITERATIONS_FASTALIGN = 600; // Safety bound: 60s timeout / 200ms poll * 2x margin
    for (int iteration = 0; iteration < MAX_ITERATIONS_FASTALIGN; ++iteration) {
      // Check stop flags
      if (context && context->IsStopRequestedAny()) {
        errorMessage = "FastAlignXY: Cancelled by user";
        return false;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
      elapsedMs += POLL_INTERVAL_MS;

      // Check timeout
      if (elapsedMs >= TIMEOUT_MS) {
        errorMessage = "FastAlignXY: Timeout after " + std::to_string(TIMEOUT_MS/1000) + " seconds";
        return false;
      }

      // Query scan status
      int status = 0;
      if (!piController->GetFastScanStatus(status)) {
        // Don't fail immediately - might be temporary
        continue;
      }

      // Log status changes
      if (status != lastStatus) {
        if (m_logger) {
          m_logger->Info("FastAlignXY: Status=" + std::to_string(status) +
                         " (elapsed: " + std::to_string(elapsedMs/1000.0) + "s)", "MotionHandler");
        }
        lastStatus = status;
      }

      // Status interpretation: 0=idle (done), 1=running, negative=error
      if (status == 0) {
        // Scan completed successfully
        if (m_logger) {
          m_logger->Info("FastAlignXY: Completed successfully in " +
                         std::to_string(elapsedMs/1000.0) + "s", "MotionHandler");
        }
        return true;
      }
      else if (status < 0) {
        errorMessage = "FastAlignXY: Scan error, status=" + std::to_string(status);
        return false;
      }
      else if (status == 1) {
        // Status says running - but check if hexapod is actually moving
        // PI sometimes doesn't update FSS status after completion
        bool anyAxisMoving = piController->IsMoving("X") ||
                             piController->IsMoving("Y") ||
                             piController->IsMoving("Z");

        if (!anyAxisMoving) {
          notMovingCount++;
          if (notMovingCount >= NOT_MOVING_THRESHOLD) {
            // Motion stopped, consider done
            if (m_logger) {
              m_logger->Info("FastAlignXY: Completed (detected via motion stop, " +
                             std::to_string(notMovingCount) + " consecutive polls)", "MotionHandler");
            }
            return true;
          }
        } else {
          notMovingCount = 0;  // Reset counter if still moving
        }
      }
    }

    // Safety bound exhausted
    if (m_logger) {
      m_logger->Error("Loop safety bound reached in FastAlignXY", "MotionHandler");
    }
    errorMessage = "FastAlignXY: Loop safety bound reached";
    return false;
  }

  // ============================================================================
  // FAST ALIGN Z-XY - PI hexapod FSM with Z-iteration (3D alignment)
  // ============================================================================
  else if (methodName == "FastAlignZXY") {
    // Validate device is a PI hexapod (hex-* pattern)
    if (deviceName.find("hex-") != 0) {
      errorMessage = "FastAlignZXY: Device must be a PI hexapod (hex-*), got: " + deviceName;
      return false;
    }

    // Get PI controller directly for FSM commands
    PIController* piController = motionManager->GetPIController(deviceName);
    if (!piController || !piController->IsConnected()) {
      errorMessage = "FastAlignZXY: PI controller not connected for device: " + deviceName;
      return false;
    }

    // ✅ XY SCAN PARAMETERS (same as FastAlignXY)
    std::string axis1 = "X";
    auto axis1It = params.find("axis1");
    if (axis1It != params.end() && !axis1It->second.empty()) {
      axis1 = axis1It->second;
    }

    double length1Um = 20.0;
    auto length1It = params.find("length1_um");
    if (length1It != params.end() && !length1It->second.empty()) {
      try {
        length1Um = std::stod(length1It->second);
      } catch (...) {
        errorMessage = "FastAlignZXY: Invalid length1_um value: " + length1It->second;
        return false;
      }
    }

    std::string axis2 = "Y";
    auto axis2It = params.find("axis2");
    if (axis2It != params.end() && !axis2It->second.empty()) {
      axis2 = axis2It->second;
    }

    double length2Um = 20.0;
    auto length2It = params.find("length2_um");
    if (length2It != params.end() && !length2It->second.empty()) {
      try {
        length2Um = std::stod(length2It->second);
      } catch (...) {
        errorMessage = "FastAlignZXY: Invalid length2_um value: " + length2It->second;
        return false;
      }
    }

    double threshold = 0.01;
    auto thresholdIt = params.find("threshold");
    if (thresholdIt != params.end() && !thresholdIt->second.empty()) {
      try {
        threshold = std::stod(thresholdIt->second);
      } catch (...) {
        errorMessage = "FastAlignZXY: Invalid threshold value: " + thresholdIt->second;
        return false;
      }
    }

    double lineDistanceUm = 1.0;
    auto lineDistIt = params.find("line_distance_um");
    if (lineDistIt != params.end() && !lineDistIt->second.empty()) {
      try {
        lineDistanceUm = std::stod(lineDistIt->second);
      } catch (...) {
        errorMessage = "FastAlignZXY: Invalid line_distance_um value: " + lineDistIt->second;
        return false;
      }
    }

    int analogChannel = 5;
    auto channelIt = params.find("analog_channel");
    if (channelIt != params.end() && !channelIt->second.empty()) {
      try {
        analogChannel = std::stoi(channelIt->second);
        if (analogChannel < 1 || analogChannel > 6) {
          errorMessage = "FastAlignZXY: analog_channel must be 1-6, got: " + channelIt->second;
          return false;
        }
      } catch (...) {
        errorMessage = "FastAlignZXY: Invalid analog_channel value: " + channelIt->second;
        return false;
      }
    }

    // ✅ Z-ITERATION PARAMETERS
    // z_direction: "Z+" or "Z-"
    int zDirection = -1;  // Default Z- (move down)
    auto zDirIt = params.find("z_direction");
    if (zDirIt != params.end() && !zDirIt->second.empty()) {
      if (zDirIt->second == "Z+") {
        zDirection = 1;
      } else if (zDirIt->second == "Z-") {
        zDirection = -1;
      } else {
        errorMessage = "FastAlignZXY: Invalid z_direction '" + zDirIt->second + "' (must be Z+ or Z-)";
        return false;
      }
    }

    // z_range_um: Total Z range to scan
    double zRangeUm = 5.0;  // Default 5 um
    auto zRangeIt = params.find("z_range_um");
    if (zRangeIt != params.end() && !zRangeIt->second.empty()) {
      try {
        zRangeUm = std::stod(zRangeIt->second);
      } catch (...) {
        errorMessage = "FastAlignZXY: Invalid z_range_um value: " + zRangeIt->second;
        return false;
      }
    }

    // z_step_um: Z step between iterations
    double zStepUm = 1.0;  // Default 1 um
    auto zStepIt = params.find("z_step_um");
    if (zStepIt != params.end() && !zStepIt->second.empty()) {
      try {
        zStepUm = std::stod(zStepIt->second);
      } catch (...) {
        errorMessage = "FastAlignZXY: Invalid z_step_um value: " + zStepIt->second;
        return false;
      }
    }

    // Convert units
    double length1Mm = length1Um / 1000.0;
    double length2Mm = length2Um / 1000.0;
    double lineDistanceMm = lineDistanceUm / 1000.0;
    double zRangeMm = zRangeUm / 1000.0;
    double zStepMm = zStepUm / 1000.0;

    // Calculate number of Z iterations
    int numZIterations = static_cast<int>(zRangeMm / zStepMm);
    if (numZIterations < 1) numZIterations = 1;

    if (m_logger) {
      m_logger->Info(
        "FastAlignZXY: Starting Z-iterated FSM on " + deviceName +
        " (axis1=" + axis1 + "/" + std::to_string(length1Um) + "um" +
        ", axis2=" + axis2 + "/" + std::to_string(length2Um) + "um" +
        ", zDir=" + (zDirection > 0 ? "Z+" : "Z-") +
        ", zRange=" + std::to_string(zRangeUm) + "um" +
        ", zStep=" + std::to_string(zStepUm) + "um" +
        ", iterations=" + std::to_string(numZIterations + 1) + ")",
        "MotionHandler"
      );
    }

    // Get starting Z position
    double startZ = 0.0;
    if (!piController->GetPosition("Z", startZ)) {
      errorMessage = "FastAlignZXY: Failed to read starting Z position";
      return false;
    }

    if (m_logger) {
      m_logger->Info("FastAlignZXY: Starting Z position = " + std::to_string(startZ) + " mm", "MotionHandler");
    }

    // Track best position
    double bestZ = startZ;
    double bestVoltage = -9999.0;
    bool foundAnyValid = false;

    // FSM polling constants
    const int TIMEOUT_MS = 60000;
    const int POLL_INTERVAL_MS = 200;
    const int NOT_MOVING_THRESHOLD = 5;

    // Lambda to run single FSM and wait for completion
    auto runSingleFSM = [&]() -> bool {
      // Start FSM
      if (!piController->FSM(axis1, length1Mm, axis2, length2Mm, threshold, lineDistanceMm, analogChannel)) {
        return false;
      }

      int elapsedMs = 0;
      int lastStatus = -999;
      int notMovingCount = 0;

      constexpr int MAX_ITERATIONS_FSM = 600; // Safety bound: 60s timeout / 200ms poll * 2x margin
      for (int iteration = 0; iteration < MAX_ITERATIONS_FSM; ++iteration) {
        // Check stop flags
        if (context && context->IsStopRequestedAny()) {
          return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
        elapsedMs += POLL_INTERVAL_MS;

        if (elapsedMs >= TIMEOUT_MS) {
          return false;
        }

        int status = 0;
        if (!piController->GetFastScanStatus(status)) {
          continue;
        }

        if (status != lastStatus) {
          lastStatus = status;
        }

        if (status == 0) {
          return true;  // Completed
        } else if (status < 0) {
          return false;  // Error
        } else if (status == 1) {
          bool anyAxisMoving = piController->IsMoving("X") ||
                               piController->IsMoving("Y") ||
                               piController->IsMoving("Z");
          if (!anyAxisMoving) {
            notMovingCount++;
            if (notMovingCount >= NOT_MOVING_THRESHOLD) {
              return true;
            }
          } else {
            notMovingCount = 0;
          }
        }
      }
      return false; // Safety bound exhausted
    };

    // ✅ ITERATE THROUGH Z POSITIONS
    for (int i = 0; i <= numZIterations; i++) {
      // Check stop flags
      if (context && context->IsStopRequestedAny()) {
        errorMessage = "FastAlignZXY: Cancelled by user";
        return false;
      }

      double currentZ = startZ + (i * zStepMm * zDirection);

      if (m_logger) {
        m_logger->Info("FastAlignZXY: === Z Iteration " + std::to_string(i+1) + "/" +
                       std::to_string(numZIterations+1) + " at Z=" + std::to_string(currentZ) + " mm ===",
                       "MotionHandler");
      }

      // Move to this Z position (skip for first iteration - already there)
      if (i > 0) {
        if (!piController->MoveToPosition("Z", currentZ, true)) {
          if (m_logger) {
            m_logger->Warning("FastAlignZXY: Failed to move to Z=" + std::to_string(currentZ) + ", skipping", "MotionHandler");
          }
          continue;
        }
        // Wait for motion to settle
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }

      // Run FSM at this Z level
      if (!runSingleFSM()) {
        if (context && context->IsStopRequestedAny()) {
          errorMessage = "FastAlignZXY: Cancelled during FSM";
          return false;
        }
        // Continue to next Z level even if this one failed
        if (m_logger) {
          m_logger->Warning("FastAlignZXY: FSM failed at Z=" + std::to_string(currentZ) + ", continuing", "MotionHandler");
        }
        continue;
      }

      // Read analog voltage after FSM completes
      double voltage = 0.0;
      if (piController->GetAnalogVoltage(analogChannel, voltage)) {
        if (m_logger) {
          m_logger->Info("FastAlignZXY: Z=" + std::to_string(currentZ) +
                         " mm, Voltage=" + std::to_string(voltage) + " V", "MotionHandler");
        }

        // Track best position (highest voltage = best alignment)
        if (voltage > bestVoltage) {
          bestVoltage = voltage;
          bestZ = currentZ;
          foundAnyValid = true;
          if (m_logger) {
            m_logger->Info("FastAlignZXY: New best! Z=" + std::to_string(bestZ) +
                           " mm, V=" + std::to_string(bestVoltage) + " V", "MotionHandler");
          }
        }
      } else {
        if (m_logger) {
          m_logger->Warning("FastAlignZXY: Failed to read analog voltage at Z=" +
                            std::to_string(currentZ), "MotionHandler");
        }
      }
    }

    if (!foundAnyValid) {
      errorMessage = "FastAlignZXY: No valid readings found during Z-scan";
      return false;
    }

    // ✅ MOVE TO BEST Z POSITION
    if (m_logger) {
      m_logger->Info("FastAlignZXY: Moving to best Z=" + std::to_string(bestZ) +
                     " mm (V=" + std::to_string(bestVoltage) + " V)", "MotionHandler");
    }

    if (!piController->MoveToPosition("Z", bestZ, true)) {
      errorMessage = "FastAlignZXY: Failed to move to best Z position";
      return false;
    }

    // Wait for motion to settle
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // ✅ RUN FINAL FSM AT BEST Z
    if (m_logger) {
      m_logger->Info("FastAlignZXY: Running final FSM at best Z position", "MotionHandler");
    }

    if (!runSingleFSM()) {
      // Still report success since we found the best Z
      if (m_logger) {
        m_logger->Warning("FastAlignZXY: Final FSM failed, but best Z was found", "MotionHandler");
      }
    }

    if (m_logger) {
      m_logger->Info("FastAlignZXY: Completed! Best Z=" + std::to_string(bestZ) +
                     " mm, Best V=" + std::to_string(bestVoltage) + " V", "MotionHandler");
    }

    return true;
  }

  // ============================================================================
  // UNKNOWN METHOD
  // ============================================================================
  else {
    errorMessage = "Unknown motion method: " + methodName;
    return false;
  }
}

// =============================================================================
// HELPER: Wait for motion complete with safety stop checking and position verification
// =============================================================================
bool MotionCommandHandler::WaitForMotionWithStopCheck(
  void* manager,
  const std::string& deviceName,
  SequenceContext* context,
  int timeoutMs,
  const TargetPosition* targetPos)
{
  UnifiedMotionManager* motionManager = static_cast<UnifiedMotionManager*>(manager);
  auto startTime = std::chrono::steady_clock::now();

  // ✅ Get pending target from motion manager if no explicit target provided
  // This ensures we verify position even for MoveToNamedPosition commands
  std::string pendingTargetName = motionManager->GetPendingTarget(deviceName);
  TargetPosition pendingTarget;
  bool usePendingTarget = false;

  if (!targetPos && !pendingTargetName.empty()) {
    // Get the target position coordinates
    PositionStruct targetPosStruct;
    if (motionManager->GetPositionWithCalculation(deviceName, pendingTargetName, targetPosStruct)) {
      pendingTarget.x = targetPosStruct.x;
      pendingTarget.y = targetPosStruct.y;
      pendingTarget.z = targetPosStruct.z;
      pendingTarget.hasX = true;
      pendingTarget.hasY = true;
      pendingTarget.hasZ = true;
      usePendingTarget = true;

      if (m_logger) {
        m_logger->Info("WaitForMotion: Using pending target '" + pendingTargetName +
                       "' at [" + std::to_string(pendingTarget.x) + "," +
                       std::to_string(pendingTarget.y) + "," +
                       std::to_string(pendingTarget.z) + "]", "MotionHandler");
      }
    }
  }

  // Use pending target if no explicit target provided
  const TargetPosition* effectiveTarget = targetPos ? targetPos : (usePendingTarget ? &pendingTarget : nullptr);

  // ✅ Record starting position to detect actual movement
  PositionStruct startingPos = motionManager->GetCurrentPosition(deviceName);
  bool hasMovedFromStart = false;

  // ✅ FIX: Track "stuck" state - controller reports done but position is wrong
  std::chrono::steady_clock::time_point stuckDetectedTime;
  PositionStruct stuckPosition;
  bool isStuckDetected = false;
  const int STUCK_TIMEOUT_MS = 2000;  // Fail after 2 seconds of no position change

  // ✅ FIX: Small delay to ensure motion actually starts (avoid race condition)
  // When non-blocking motion is initiated, there's a brief window where the command
  // has been sent but the motor hasn't started moving yet
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Safety bound: timeoutMs / 50ms poll * 2x margin, minimum 400 iterations
  const int maxIterations = std::max(400, timeoutMs / 50 * 2);
  for (int iteration = 0; iteration < maxIterations; ++iteration) {
    // ✅ CRITICAL: Update waypoint state machine before checking status
    // This allows the waypoint system to transition from MovingToFinalTarget to Idle
    motionManager->Update();

    // ✅ SAFETY-CRITICAL: Check stop flags FIRST (before motion status)
    if (context && context->IsStopRequestedAny()) {
      if (m_logger) {
        m_logger->Warning("SAFETY STOP: Stop requested during motion - triggering emergency stop", "MotionHandler");
      }

      // Emergency stop the device immediately
      if (!motionManager->EmergencyStop(deviceName)) {
        if (m_logger) {
          m_logger->Error("Emergency stop failed for device: " + deviceName, "MotionHandler");
        }
      }

      // Clear pending target on stop
      motionManager->ClearPendingTarget(deviceName);
      return false;  // Stop sequence execution
    }

    // Check motion status
    bool isMoving = motionManager->IsDeviceInMotion(deviceName);
    bool waypointActive = motionManager->IsWaypointSystemActive(deviceName);

    // ✅ Check if device has moved from starting position
    PositionStruct currentPos = motionManager->GetCurrentPosition(deviceName);
    if (!hasMovedFromStart) {
      double dx = currentPos.x - startingPos.x;
      double dy = currentPos.y - startingPos.y;
      double dz = currentPos.z - startingPos.z;
      double distFromStart = std::sqrt(dx*dx + dy*dy + dz*dz);
      if (distFromStart > 0.1) {  // Consider moved if > 0.1mm from start
        hasMovedFromStart = true;
      }
    }

    if (!isMoving && !waypointActive) {
      // ✅ If we have a target and haven't moved yet, keep waiting (motor startup delay)
      if (effectiveTarget && !hasMovedFromStart) {
        // Check if we're already at target (maybe no move needed)
        double distToTarget = 0.0;
        if (effectiveTarget->hasX) distToTarget += (currentPos.x - effectiveTarget->x) * (currentPos.x - effectiveTarget->x);
        if (effectiveTarget->hasY) distToTarget += (currentPos.y - effectiveTarget->y) * (currentPos.y - effectiveTarget->y);
        if (effectiveTarget->hasZ) distToTarget += (currentPos.z - effectiveTarget->z) * (currentPos.z - effectiveTarget->z);
        distToTarget = std::sqrt(distToTarget);

        if (distToTarget > 0.5) {  // More than 0.5mm from target but haven't started moving
          // Check elapsed time to avoid infinite wait
          auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime
          ).count();

          if (elapsed < 500) {  // Wait up to 500ms for motion to start
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;  // Keep waiting for motion to start
          } else {
            if (m_logger) {
              m_logger->Warning("WaitForMotion: Motion did not start within 500ms, position still " +
                              std::to_string(distToTarget) + "mm from target", "MotionHandler");
            }
          }
        }
      }

      // ✅ Controller reports motion complete

      if (effectiveTarget) {
        double distanceFromTarget = 0.0;

        if (effectiveTarget->hasX) {
          double dx = currentPos.x - effectiveTarget->x;
          distanceFromTarget += dx * dx;
        }
        if (effectiveTarget->hasY) {
          double dy = currentPos.y - effectiveTarget->y;
          distanceFromTarget += dy * dy;
        }
        if (effectiveTarget->hasZ) {
          double dz = currentPos.z - effectiveTarget->z;
          distanceFromTarget += dz * dz;
        }
        if (effectiveTarget->hasU) {
          double du = currentPos.u - effectiveTarget->u;
          distanceFromTarget += du * du;
        }
        if (effectiveTarget->hasV) {
          double dv = currentPos.v - effectiveTarget->v;
          distanceFromTarget += dv * dv;
        }
        if (effectiveTarget->hasW) {
          double dw = currentPos.w - effectiveTarget->w;
          distanceFromTarget += dw * dw;
        }

        distanceFromTarget = std::sqrt(distanceFromTarget);

        const double TARGET_THRESHOLD = 0.05;  // Must be within 0.05mm of target
        if (distanceFromTarget > TARGET_THRESHOLD) {
          // ✅ FIX: Track stuck state to detect when controller is done but position won't change
          if (!isStuckDetected) {
            // First time detecting this stuck state
            isStuckDetected = true;
            stuckDetectedTime = std::chrono::steady_clock::now();
            stuckPosition = currentPos;
            if (m_logger) {
              m_logger->Warning("WaitForMotion: Controller reports done but still " + std::to_string(distanceFromTarget) +
                               "mm from target - monitoring for stuck condition", "MotionHandler");
            }
          } else {
            // Check if position has changed since we detected stuck state
            double dx = currentPos.x - stuckPosition.x;
            double dy = currentPos.y - stuckPosition.y;
            double dz = currentPos.z - stuckPosition.z;
            double positionChange = std::sqrt(dx*dx + dy*dy + dz*dz);

            if (positionChange > 0.01) {  // Position changed > 10 microns - reset stuck timer
              stuckPosition = currentPos;
              stuckDetectedTime = std::chrono::steady_clock::now();
            } else {
              // Position hasn't changed - check if we've been stuck too long
              auto stuckDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - stuckDetectedTime
              ).count();

              if (stuckDuration > STUCK_TIMEOUT_MS) {
                if (m_logger) {
                  m_logger->Error("WaitForMotion: STUCK - Controller reports done, position unchanged for " +
                                 std::to_string(stuckDuration) + "ms, still " + std::to_string(distanceFromTarget) +
                                 "mm from target. Motion may have hit a limit or encountered an error.", "MotionHandler");
                }
                motionManager->ClearPendingTarget(deviceName);
                return false;  // Fail - motion is stuck
              }
            }
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
          continue;  // Keep waiting briefly
        } else {
          // Position is now within threshold - clear stuck state
          isStuckDetected = false;
        }
      }

      if (m_logger) {
        std::string stableStr = "WaitForMotion: Motion complete at [" +
                               std::to_string(currentPos.x) + "," + std::to_string(currentPos.y) + "," + std::to_string(currentPos.z) + "]";
        m_logger->Info(stableStr, "MotionHandler");
      }

      // ✅ Clear pending target now that move is confirmed complete
      motionManager->ClearPendingTarget(deviceName);

      // Motion stopped and position is stable - now verify position if target was provided
      if (effectiveTarget) {
        const double TOLERANCE = 0.05;  // ±0.05mm tolerance

        bool positionOK = true;
        std::string errorDetail;

        // Verify X axis if specified
        if (effectiveTarget->hasX) {
          double actualX = 0.0;
          if (motionManager->GetPosition(deviceName, "X", actualX)) {
            double errorX = std::abs(actualX - effectiveTarget->x);
            if (errorX > TOLERANCE) {
              positionOK = false;
              errorDetail += "X: target=" + std::to_string(effectiveTarget->x) +
                             ", actual=" + std::to_string(actualX) +
                             ", error=" + std::to_string(errorX) + "mm; ";
            }
          }
        }

        // Verify Y axis if specified
        if (effectiveTarget->hasY) {
          double actualY = 0.0;
          if (motionManager->GetPosition(deviceName, "Y", actualY)) {
            double errorY = std::abs(actualY - effectiveTarget->y);
            if (errorY > TOLERANCE) {
              positionOK = false;
              errorDetail += "Y: target=" + std::to_string(effectiveTarget->y) +
                             ", actual=" + std::to_string(actualY) +
                             ", error=" + std::to_string(errorY) + "mm; ";
            }
          }
        }

        // Verify Z axis if specified
        if (effectiveTarget->hasZ) {
          double actualZ = 0.0;
          if (motionManager->GetPosition(deviceName, "Z", actualZ)) {
            double errorZ = std::abs(actualZ - effectiveTarget->z);
            if (errorZ > TOLERANCE) {
              positionOK = false;
              errorDetail += "Z: target=" + std::to_string(effectiveTarget->z) +
                             ", actual=" + std::to_string(actualZ) +
                             ", error=" + std::to_string(errorZ) + "mm; ";
            }
          }
        }

        // Verify U axis if specified (rotational, degrees)
        if (effectiveTarget->hasU) {
          double actualU = 0.0;
          if (motionManager->GetPosition(deviceName, "U", actualU)) {
            double errorU = std::abs(actualU - effectiveTarget->u);
            if (errorU > TOLERANCE) {
              positionOK = false;
              errorDetail += "U: target=" + std::to_string(effectiveTarget->u) +
                             ", actual=" + std::to_string(actualU) +
                             ", error=" + std::to_string(errorU) + "deg; ";
            }
          }
        }

        // Verify V axis if specified (rotational, degrees)
        if (effectiveTarget->hasV) {
          double actualV = 0.0;
          if (motionManager->GetPosition(deviceName, "V", actualV)) {
            double errorV = std::abs(actualV - effectiveTarget->v);
            if (errorV > TOLERANCE) {
              positionOK = false;
              errorDetail += "V: target=" + std::to_string(effectiveTarget->v) +
                             ", actual=" + std::to_string(actualV) +
                             ", error=" + std::to_string(errorV) + "deg; ";
            }
          }
        }

        // Verify W axis if specified (rotational, degrees)
        if (effectiveTarget->hasW) {
          double actualW = 0.0;
          if (motionManager->GetPosition(deviceName, "W", actualW)) {
            double errorW = std::abs(actualW - effectiveTarget->w);
            if (errorW > TOLERANCE) {
              positionOK = false;
              errorDetail += "W: target=" + std::to_string(effectiveTarget->w) +
                             ", actual=" + std::to_string(actualW) +
                             ", error=" + std::to_string(errorW) + "deg; ";
            }
          }
        }

        if (!positionOK) {
          if (m_logger) {
            m_logger->Error("Position verification FAILED for " + deviceName + ": " + errorDetail, "MotionHandler");
          }
          return false;  // Position error exceeds tolerance
        }

        if (m_logger) {
          m_logger->Info("Position verification OK for " + deviceName + " (tolerance ±" +
                         std::to_string(TOLERANCE) + "mm)", "MotionHandler");
        }
      }

      // Motion completed successfully (and position verified if required)
      return true;
    }

    // Check timeout
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - startTime
    ).count();

    if (elapsed > timeoutMs) {
      if (m_logger) {
        m_logger->Error("Timeout waiting for motion complete on: " + deviceName, "MotionHandler");
      }
      return false;
    }

    // Sleep briefly to avoid busy-waiting (50ms polling rate)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // Safety bound exhausted
  if (m_logger) {
    m_logger->Error("Loop safety bound reached in WaitForMotionWithStopCheck for " + deviceName, "MotionHandler");
  }
  return false;
}

// =============================================================================
// HELPER: Wait for multiple motions to complete with safety stop checking
// =============================================================================
bool MotionCommandHandler::WaitForMultipleMotionsWithStopCheck(
  void* manager,
  const std::vector<ParallelMoveSpec>& moves,
  SequenceContext* context,
  int timeoutMs)
{
  UnifiedMotionManager* motionManager = static_cast<UnifiedMotionManager*>(manager);
  auto startTime = std::chrono::steady_clock::now();

  // Track completion status for each device
  std::vector<bool> completed(moves.size(), false);
  int completedCount = 0;

  // Small delay to ensure all motions actually start
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  while (completedCount < static_cast<int>(moves.size())) {
    // ✅ CRITICAL: Update waypoint state machines for all devices
    motionManager->Update();

    // ✅ SAFETY-CRITICAL: Check stop flags FIRST
    if (context && context->IsStopRequestedAny()) {
      if (m_logger) {
        m_logger->Warning("SAFETY STOP: Stop requested during parallel motion - stopping all devices", "MotionHandler");
      }

      // Emergency stop ALL devices
      for (const auto& spec : moves) {
        (void)motionManager->EmergencyStop(spec.deviceName);
      }

      return false;
    }

    // Check each device's motion status
    for (size_t i = 0; i < moves.size(); ++i) {
      if (completed[i]) continue;  // Already done

      const auto& spec = moves[i];
      bool isMoving = motionManager->IsDeviceInMotion(spec.deviceName);
      bool waypointActive = motionManager->IsWaypointSystemActive(spec.deviceName);

      // ✅ DEBUG: Log state for each device check
      if (motionManager->IsDebugLoggingEnabled()) {
        std::cout << "[DEBUG] WaitForMultiple: " << spec.deviceName
          << " isMoving=" << (isMoving ? "true" : "false")
          << ", waypointActive=" << (waypointActive ? "true" : "false")
          << ", waypointState=" << motionManager->GetWaypointStateString(spec.deviceName)
          << std::endl;
      }

      if (!isMoving && !waypointActive) {
        // This device has stopped - verify position
        PositionStruct currentPos = motionManager->GetCurrentPosition(spec.deviceName);

        // Get target position for verification
        PositionStruct targetPos;
        bool hasTarget = false;

        if (m_positionManager) {
          std::map<std::string, PositionStruct> allPositions = m_positionManager->GetAllPositions(spec.deviceName);
          auto it = allPositions.find(spec.targetName);
          if (it != allPositions.end()) {
            targetPos = it->second;
            hasTarget = true;
          }
        }

        if (hasTarget) {
          // Verify position within tolerance
          double dx = currentPos.x - targetPos.x;
          double dy = currentPos.y - targetPos.y;
          double dz = currentPos.z - targetPos.z;
          double distance = std::sqrt(dx*dx + dy*dy + dz*dz);

          const double TOLERANCE = 0.05;  // 0.05mm tolerance
          if (distance > TOLERANCE) {
            // Still not at target - might be in waypoint transition
            if (m_logger) {
              m_logger->Info("ParallelMove: " + spec.deviceName + " stopped but " +
                            std::to_string(distance) + "mm from target, waiting...", "MotionHandler");
            }
            continue;
          }
        }

        // Device completed successfully
        completed[i] = true;
        completedCount++;

        if (m_logger) {
          m_logger->Info("ParallelMove: " + spec.deviceName + " reached " + spec.targetName +
                        " (" + std::to_string(completedCount) + "/" + std::to_string(moves.size()) + ")", "MotionHandler");
        }

        // ✅ DEBUG: Verify waypoint state is Idle after marking complete
        if (motionManager->IsDebugLoggingEnabled()) {
          std::cout << "[DEBUG] WaitForMultiple: MARKED COMPLETE " << spec.deviceName
            << " -> waypointState=" << motionManager->GetWaypointStateString(spec.deviceName)
            << std::endl;
        }
      }
    }

    // Check timeout
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - startTime
    ).count();

    if (elapsed > timeoutMs) {
      if (m_logger) {
        std::string pendingDevices;
        for (size_t i = 0; i < moves.size(); ++i) {
          if (!completed[i]) {
            if (!pendingDevices.empty()) pendingDevices += ", ";
            pendingDevices += moves[i].deviceName;
          }
        }
        m_logger->Error("ParallelMove: Timeout waiting for devices: " + pendingDevices, "MotionHandler");
      }

      // Emergency stop all devices that are still moving
      for (size_t i = 0; i < moves.size(); ++i) {
        if (!completed[i]) {
          (void)motionManager->EmergencyStop(moves[i].deviceName);
        }
      }

      return false;
    }

    // Sleep briefly to avoid busy-waiting
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // ✅ DEBUG: Log final states of all devices before returning
  if (motionManager->IsDebugLoggingEnabled()) {
    std::cout << "[DEBUG] WaitForMultiple: ALL MOVES COMPLETE - Final states:" << std::endl;
    for (const auto& spec : moves) {
      std::cout << "[DEBUG]   " << spec.deviceName
        << " -> waypointState=" << motionManager->GetWaypointStateString(spec.deviceName)
        << ", IsDeviceInMotion=" << (motionManager->IsDeviceInMotion(spec.deviceName) ? "true" : "false")
        << ", IsWaypointSystemActive=" << (motionManager->IsWaypointSystemActive(spec.deviceName) ? "true" : "false")
        << std::endl;
    }
  }

  return true;
}

// =============================================================================
// HELPER: Wait for multiple relative motions to complete with safety stop checking
// =============================================================================
bool MotionCommandHandler::WaitForMultipleRelativeMotionsWithStopCheck(
  void* manager,
  const std::vector<std::string>& deviceNames,
  SequenceContext* context,
  int timeoutMs)
{
  UnifiedMotionManager* motionManager = static_cast<UnifiedMotionManager*>(manager);
  auto startTime = std::chrono::steady_clock::now();

  // Track completion status for each device
  std::vector<bool> completed(deviceNames.size(), false);
  int completedCount = 0;

  // Small delay to ensure all motions actually start
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  while (completedCount < static_cast<int>(deviceNames.size())) {
    // ✅ CRITICAL: Update waypoint state machines for all devices
    motionManager->Update();

    // ✅ SAFETY-CRITICAL: Check stop flags FIRST
    if (context && context->IsStopRequestedAny()) {
      if (m_logger) {
        m_logger->Warning("SAFETY STOP: Stop requested during parallel relative motion - stopping all devices", "MotionHandler");
      }

      // Emergency stop ALL devices
      for (const auto& deviceName : deviceNames) {
        (void)motionManager->EmergencyStop(deviceName);
      }

      return false;
    }

    // Check each device's motion status
    for (size_t i = 0; i < deviceNames.size(); ++i) {
      if (completed[i]) continue;  // Already done

      const auto& deviceName = deviceNames[i];
      bool isMoving = motionManager->IsDeviceInMotion(deviceName);

      if (!isMoving) {
        // Device completed
        completed[i] = true;
        completedCount++;

        if (m_logger) {
          m_logger->Info("ParallelMoveRelative: " + deviceName + " completed (" +
                        std::to_string(completedCount) + "/" + std::to_string(deviceNames.size()) + ")", "MotionHandler");
        }
      }
    }

    // Check timeout
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - startTime
    ).count();

    if (elapsed > timeoutMs) {
      if (m_logger) {
        std::string pendingDevices;
        for (size_t i = 0; i < deviceNames.size(); ++i) {
          if (!completed[i]) {
            if (!pendingDevices.empty()) pendingDevices += ", ";
            pendingDevices += deviceNames[i];
          }
        }
        m_logger->Error("ParallelMoveRelative: Timeout waiting for devices: " + pendingDevices, "MotionHandler");
      }

      // Emergency stop all devices that are still moving
      for (size_t i = 0; i < deviceNames.size(); ++i) {
        if (!completed[i]) {
          (void)motionManager->EmergencyStop(deviceNames[i]);
        }
      }

      return false;
    }

    // Sleep briefly to avoid busy-waiting
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  return true;
}

// =============================================================================
// HELPER: Invoke retrace edit callback after motion completes
// =============================================================================
bool MotionCommandHandler::InvokeRetraceEditCallback(
  void* motionManager,
  const std::string& deviceName,
  const std::string& positionName,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  double originalX, double originalY, double originalZ,
  double originalU, double originalV, double originalW)
{
  if (!m_retraceEditCallback) {
    return true;  // No callback, continue
  }

  UnifiedMotionManager* manager = static_cast<UnifiedMotionManager*>(motionManager);

  // Build motion info
  RetraceMotionInfo info;
  info.deviceName = deviceName;
  info.positionName = positionName;
  info.methodName = methodName;
  info.originalX = originalX;
  info.originalY = originalY;
  info.originalZ = originalZ;
  info.originalU = originalU;
  info.originalV = originalV;
  info.originalW = originalW;
  info.params = params;

  // Get current position
  PositionStruct currentPos = manager->GetCurrentPosition(deviceName);
  RetraceCurrentPosition current;
  current.x = currentPos.x;
  current.y = currentPos.y;
  current.z = currentPos.z;
  current.u = currentPos.u;
  current.v = currentPos.v;
  current.w = currentPos.w;

  // Track statistics
  m_retracePositionsVisited++;

  if (m_logger) {
    m_logger->Info("Retrace: Invoking callback for " + deviceName + "/" + positionName +
                   " (position " + std::to_string(m_retracePositionsVisited) + ")", "MotionHandler");
  }

  // Invoke callback - this blocks until user responds
  bool saveRequested = false;
  bool continueExecution = m_retraceEditCallback(info, current, saveRequested);

  if (saveRequested) {
    // User wants to save the adjusted position
    m_retracePositionsSaved++;

    // Get updated current position (user may have jogged)
    PositionStruct updatedPos = manager->GetCurrentPosition(deviceName);

    // Save to file
    if (manager->SaveNamedPositionToFile(deviceName, positionName, updatedPos, true)) {
      if (m_logger) {
        m_logger->Info("Retrace: Saved position '" + positionName + "' for device " + deviceName +
                       " -> (" + std::to_string(updatedPos.x) + ", " + std::to_string(updatedPos.y) +
                       ", " + std::to_string(updatedPos.z) + ")", "MotionHandler");
      }
    } else {
      if (m_logger) {
        m_logger->Error("Retrace: Failed to save position '" + positionName + "'", "MotionHandler");
      }
    }
  }

  return continueExecution;
}