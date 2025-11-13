// MotionCommandHandler.cpp
#include "MotionCommandHandler.h"
#include "unified_motion_manager.h"
#include "LogManager.h"
#include "SequenceContext.h"
#include "AdaptiveScanController.h"  // ✅ NEW: For XCAN command
#include "DataStore.h"                // ✅ NEW: For XCAN command
#include "ScanTypes.h"                // ✅ NEW: For XCAN command
#include "../graph/MotionGraphManager.h"  // ✅ NEW: For MoveToGraphNode command
#include "../products/product_position_manager.h"  // ✅ NEW: For MoveToGraphNode command
#include <chrono>
#include <thread>
#include <regex>                      // ✅ NEW: For device name validation
#include <atomic>                     // ✅ NEW: For scan completion flag
#include <condition_variable>         // ✅ NEW: For scan completion signaling
#include <cmath>                      // ✅ NEW: For position distance calculation

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
      m_logger->Info(
        "Moving " + deviceName + " to (" +
        std::to_string(x) + ", " +
        std::to_string(y) + ", " +
        std::to_string(z) + ")",
        "MotionHandler"
      );
    }

    // ✅ SAFETY: Always use non-blocking mode + stop checking
    if (!motionManager->MoveToAbs(deviceName, x, y, z, velocity, false)) {
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
    target.x = x;
    target.y = y;
    target.z = z;

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

      return false;  // Stop sequence execution
    }

    // Check motion status
    bool isMoving = motionManager->IsDeviceInMotion(deviceName);
    bool waypointActive = motionManager->IsWaypointSystemActive(deviceName);

    if (!isMoving && !waypointActive) {
      // ✅ CRITICAL FIX: Verify position is stable before declaring motion complete
      // The controller might report "not moving" while the motor is still settling
      // Read position twice with 100ms delay and ensure they're the same
      PositionStruct pos1 = motionManager->GetCurrentPosition(deviceName);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      PositionStruct pos2 = motionManager->GetCurrentPosition(deviceName);

      double dx = std::abs(pos2.x - pos1.x);
      double dy = std::abs(pos2.y - pos1.y);
      double dz = std::abs(pos2.z - pos1.z);
      double posDrift = std::sqrt(dx*dx + dy*dy + dz*dz);

      if (posDrift > 0.05) {  // Position changed by more than 0.05mm - still settling
        if (m_logger) {
          std::string driftStr = "WaitForMotion: Position still drifting " + std::to_string(posDrift) +
                                "mm (from [" + std::to_string(pos1.x) + "," + std::to_string(pos1.y) + "," + std::to_string(pos1.z) +
                                "] to [" + std::to_string(pos2.x) + "," + std::to_string(pos2.y) + "," + std::to_string(pos2.z) + "]) - waiting for stability";
          m_logger->Info(driftStr, "MotionHandler");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        continue;  // Keep waiting
      }

      // ✅ CRITICAL FIX: For slow velocities, drift might appear small but motor is still far from target
      // If we have a target position, verify we're actually close to it before declaring stable
      // This catches cases where motor moves at 0.2mm/s (only 0.02mm per 100ms) and appears stable
      if (targetPos) {
        double distanceFromTarget = 0.0;
        PositionStruct currentPos = motionManager->GetCurrentPosition(deviceName);

        if (targetPos->hasX) {
          double dx = currentPos.x - targetPos->x;
          distanceFromTarget += dx * dx;
        }
        if (targetPos->hasY) {
          double dy = currentPos.y - targetPos->y;
          distanceFromTarget += dy * dy;
        }
        if (targetPos->hasZ) {
          double dz = currentPos.z - targetPos->z;
          distanceFromTarget += dz * dz;
        }

        distanceFromTarget = std::sqrt(distanceFromTarget);

        const double TARGET_THRESHOLD = 0.01;  // Must be within 0.01mm of target before declaring stable (tight tolerance for slow speed precision)
        if (distanceFromTarget > TARGET_THRESHOLD) {
          if (m_logger) {
            std::string msg = "WaitForMotion: Position appears stable (drift=" + std::to_string(posDrift) +
                             "mm) but still " + std::to_string(distanceFromTarget) +
                             "mm from target - continuing to wait";
            m_logger->Info(msg, "MotionHandler");
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          continue;  // Keep waiting until closer to target
        }
      }

      if (m_logger) {
        std::string stableStr = "WaitForMotion: Position stable at [" +
                               std::to_string(pos2.x) + "," + std::to_string(pos2.y) + "," + std::to_string(pos2.z) +
                               "], drift=" + std::to_string(posDrift) + "mm";
        m_logger->Info(stableStr, "MotionHandler");
      }

      // Motion stopped and position is stable - now verify position if target was provided
      if (targetPos) {
        const double TOLERANCE = 0.05;  // ±0.05mm tolerance

        bool positionOK = true;
        std::string errorDetail;

        // Verify X axis if specified
        if (targetPos->hasX) {
          double actualX = 0.0;
          if (motionManager->GetPosition(deviceName, "X", actualX)) {
            double errorX = std::abs(actualX - targetPos->x);
            if (errorX > TOLERANCE) {
              positionOK = false;
              errorDetail += "X: target=" + std::to_string(targetPos->x) +
                             ", actual=" + std::to_string(actualX) +
                             ", error=" + std::to_string(errorX) + "mm; ";
            }
          }
        }

        // Verify Y axis if specified
        if (targetPos->hasY) {
          double actualY = 0.0;
          if (motionManager->GetPosition(deviceName, "Y", actualY)) {
            double errorY = std::abs(actualY - targetPos->y);
            if (errorY > TOLERANCE) {
              positionOK = false;
              errorDetail += "Y: target=" + std::to_string(targetPos->y) +
                             ", actual=" + std::to_string(actualY) +
                             ", error=" + std::to_string(errorY) + "mm; ";
            }
          }
        }

        // Verify Z axis if specified
        if (targetPos->hasZ) {
          double actualZ = 0.0;
          if (motionManager->GetPosition(deviceName, "Z", actualZ)) {
            double errorZ = std::abs(actualZ - targetPos->z);
            if (errorZ > TOLERANCE) {
              positionOK = false;
              errorDetail += "Z: target=" + std::to_string(targetPos->z) +
                             ", actual=" + std::to_string(actualZ) +
                             ", error=" + std::to_string(errorZ) + "mm; ";
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