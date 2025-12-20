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
#include "../vision/VisionManager.h"  // ✅ NEW: For pixel-to-mm calibration in MoveToContextPosition
#include <chrono>
#include <thread>
#include <iostream>                   // ✅ For debug logging
#include <regex>                      // ✅ NEW: For device name validation
#include <atomic>                     // ✅ NEW: For scan completion flag
#include <condition_variable>         // ✅ NEW: For scan completion signaling
#include <cmath>                      // ✅ NEW: For position distance calculation
#include <vector>                     // ✅ NEW: For parallel workers

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

    // ✅ SAFETY: Always use non-blocking mode + stop checking
    if (!motionManager->MoveRelative(deviceName, axis, distance, false)) {
      errorMessage = "MoveRelative failed to start";
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
          motionManager->EmergencyStop(s.deviceName);
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

    while (true) {
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
          motionManager->EmergencyStop(spec.deviceName);
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
          motionManager->EmergencyStop(spec.deviceName);
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
        for (const auto& deviceName : deviceNames) {
          motionManager->EmergencyStop(deviceName);
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
      motionManager->GetPosition(deviceName, "Z", currentZ);
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

  // ✅ FIX: Small delay to ensure motion actually starts (avoid race condition)
  // When non-blocking motion is initiated, there's a brief window where the command
  // has been sent but the motor hasn't started moving yet
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  while (true) {
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
          if (m_logger) {
            std::string msg = "WaitForMotion: Controller reports done but still " + std::to_string(distanceFromTarget) +
                             "mm from target - continuing to wait";
            m_logger->Info(msg, "MotionHandler");
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
          continue;  // Keep waiting until closer to target
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
        motionManager->EmergencyStop(spec.deviceName);
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
          motionManager->EmergencyStop(moves[i].deviceName);
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
        motionManager->EmergencyStop(deviceName);
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
          motionManager->EmergencyStop(deviceNames[i]);
        }
      }

      return false;
    }

    // Sleep briefly to avoid busy-waiting
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  return true;
}