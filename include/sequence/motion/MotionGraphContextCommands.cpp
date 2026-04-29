// MotionGraphContextCommands.cpp
// Family E implementation. See MotionGraphContextCommands.h.
#include "MotionGraphContextCommands.h"

#include "MotionCommandHandler.h"        // for parent->WaitForMotionWithStopCheck + TargetPosition
#include "unified_motion_manager.h"
#include "LogManager.h"
#include "SequenceContext.h"
#include "../../graph/MotionGraphManager.h"   // include/graph not in CMake search path
#include "product_position_manager.h"
#include "VisionManager.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <string>
#include <thread>

std::optional<bool> MotionGraphContextCommands::TryExecute(
  const std::string& deviceName,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context,
  const MotionCommandDeps& deps)
{
  UnifiedMotionManager* motionManager = deps.motion;

  // ============================================================================
  // MOVE TO GRAPH NODE - Execute graph-based path planning
  // ============================================================================
  if (methodName == "MoveToGraphNode") {
    // ✅ VALIDATE: ProductPositionManager must be set
    if (!deps.posMgr) {
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

    if (deps.logger) {
      deps.logger->Info("MoveToGraphNode: Device=" + deviceName + ", Graph=" + graphName + ", EndNode=" + endNodeName, "MotionHandler");
    }

    // ✅ GET GRAPH
    MotionGraphManager& graphManager = deps.posMgr->GetGraphManager();
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
    std::map<std::string, PositionStruct> allPositions = deps.posMgr->GetAllPositions(deviceName);

    if (deps.logger) {
      std::string startPosStr = "MoveToGraphNode: Starting position (" +
                                std::to_string(currentPos.x) + ", " +
                                std::to_string(currentPos.y) + ", " +
                                std::to_string(currentPos.z) + ")";
      deps.logger->Info(startPosStr, "MotionHandler");
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
    if (deps.logger) {
      std::string posStr = "MoveToGraphNode: Current position (" +
                          std::to_string(currentPos.x) + ", " +
                          std::to_string(currentPos.y) + ", " +
                          std::to_string(currentPos.z) + ") checking " +
                          std::to_string(allPositions.size()) + " positions";
      deps.logger->Info(posStr, "MotionHandler");
    }

    for (const auto& pair : allPositions) {
      const std::string& posName = pair.first;
      const PositionStruct& savedPos = pair.second;

      double dx = currentPos.x - savedPos.x;
      double dy = currentPos.y - savedPos.y;
      double dz = currentPos.z - savedPos.z;
      double distance = std::sqrt(dx*dx + dy*dy + dz*dz);

      // ✅ DEBUG: Log each position distance
      if (deps.logger && distance < 10.0) {  // Only log if within 10mm
        std::string distStr = "  - '" + posName + "': " + std::to_string(distance) + "mm";
        deps.logger->Info(distStr, "MotionHandler");
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
      if (deps.logger) {
        deps.logger->Warning(
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

    if (deps.logger) {
      deps.logger->Info("MoveToGraphNode: Start node '" + currentPosName + "' (id: " + startNode->id.toStdString() + ")", "MotionHandler");
      deps.logger->Info("MoveToGraphNode: End node '" + endNodeName + "' (id: " + endNode->id.toStdString() + ")", "MotionHandler");
    }

    // ✅ SPECIAL CASE: Already at goal position
    if (startNode->id == endNode->id) {
      if (deps.logger) {
        deps.logger->Info("MoveToGraphNode: Already at goal position '" + endNodeName + "', no movement needed", "MotionHandler");
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

    if (deps.logger) {
      std::string pathStr = "MoveToGraphNode: Path found (";
      for (int i = 0; i < path.positionNames.size(); ++i) {
        if (i > 0) pathStr += " → ";
        pathStr += path.positionNames[i].toStdString();
      }
      pathStr += ") cost=" + std::to_string(path.totalDistance);
      deps.logger->Info(pathStr, "MotionHandler");
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
          if (deps.logger) {
            deps.logger->Info("MoveToGraphNode: Already at '" + posName.toStdString() + "', skipping move", "MotionHandler");
          }
          // ✅ CRITICAL FIX: Still update currentPos even if we skip the move
          // This ensures our position tracking stays accurate for the next waypoint
          currentPos = targetPos;
          continue;
        }
      }

      // Move to waypoint
      if (deps.logger) {
        deps.logger->Info("MoveToGraphNode: Moving to waypoint '" + posName.toStdString() + "'", "MotionHandler");
      }

      if (!motionManager->MoveToNamedPosition(deviceName, posName.toStdString(), false)) {
        errorMessage = "MoveToGraphNode: Failed to start move to '" + posName.toStdString() + "'";
        return false;
      }

      // Wait for motion to complete
      if (!deps.parent->WaitForMotionWithStopCheck(motionManager, deviceName, context, 30000)) {
        errorMessage = "MoveToGraphNode: Motion to '" + posName.toStdString() + "' stopped or timed out";
        return false;
      }

      // ✅ DEBUG: Read position immediately after motion completes
      if (deps.logger) {
        PositionStruct preSettlePos = motionManager->GetCurrentPosition(deviceName);
        std::string preSettleStr = "MoveToGraphNode: Position before settling (" +
                                   std::to_string(preSettlePos.x) + ", " +
                                   std::to_string(preSettlePos.y) + ", " +
                                   std::to_string(preSettlePos.z) + ")";
        deps.logger->Info(preSettleStr, "MotionHandler");
      }

      // ✅ SETTLING TIME: Wait for controller to stabilize position
      // Gantry systems need more settling time than hexapods
      int settlingTimeMs = isGantry ? 200 : 50;
      std::this_thread::sleep_for(std::chrono::milliseconds(settlingTimeMs));

      // Update current position for next iteration
      currentPos = motionManager->GetCurrentPosition(deviceName);

      if (deps.logger) {
        std::string afterSettleStr = "MoveToGraphNode: Position after settling (" +
                                     std::to_string(currentPos.x) + ", " +
                                     std::to_string(currentPos.y) + ", " +
                                     std::to_string(currentPos.z) + ")";
        deps.logger->Info(afterSettleStr, "MotionHandler");
        deps.logger->Info("MoveToGraphNode: Reached waypoint '" + posName.toStdString() + "'", "MotionHandler");

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
          deps.logger->Info(verifyStr, "MotionHandler");
        }
      }
    }

    if (deps.logger) {
      deps.logger->Info("MoveToGraphNode: Path execution completed successfully", "MotionHandler");
    }

    return true;
  }
  // ============================================================================
  // MOVE TO CONTEXT POSITION - Move to world coordinates from vision detection results
  // ============================================================================
  if (methodName == "MoveToContextPosition") {
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
        if (deps.logger) {
          deps.logger->Warning("MoveToContextPosition: device_z_mm not found, keeping current Z", "MotionHandler");
        }
      }
    }

    // ✅ LOG WHAT WE'RE DOING
    bool usingAnchor = false;
    std::string usingAnchorKey = contextPrefix + ".detected.using_anchor";
    if (context->HasValue(usingAnchorKey)) {
      usingAnchor = context->GetBoolean(usingAnchorKey, false);
    }

    if (deps.logger) {
      std::string moveInfo = "MoveToContextPosition: Moving " + deviceName + " to detected position from '" + contextPrefix + "'";
      deps.logger->Info(moveInfo, "MotionHandler");

      std::string coordInfo = "  Target: X=" + std::to_string(targetX) + "mm, Y=" + std::to_string(targetY) + "mm";
      if (keepZ) {
        coordInfo += ", Z=" + std::to_string(targetZ) + "mm (kept current)";
      } else {
        coordInfo += ", Z=" + std::to_string(targetZ) + "mm (from detection time)";
      }
      deps.logger->Info(coordInfo, "MotionHandler");

      if (usingAnchor) {
        deps.logger->Info("  Reference: Custom anchor point (precise feature location)", "MotionHandler");
      } else {
        deps.logger->Info("  Reference: Pattern center (no custom anchor)", "MotionHandler");
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
    if (!deps.parent->WaitForMotionWithStopCheck(motionManager, deviceName, context)) {
      errorMessage = "MoveToContextPosition: Motion was interrupted or timed out";
      return false;
    }

    if (deps.logger) {
      PositionStruct finalPos = motionManager->GetCurrentPosition(deviceName);
      std::string finalInfo = "MoveToContextPosition: Reached position (" +
                              std::to_string(finalPos.x) + ", " +
                              std::to_string(finalPos.y) + ", " +
                              std::to_string(finalPos.z) + ")";
      deps.logger->Info(finalInfo, "MotionHandler");
    }

    return true;
  }
  // ============================================================================
  // SAVE POSITION TO CONTEXT - Capture current device position into context variables
  // ============================================================================
  if (methodName == "SavePositionToContext") {
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

    if (deps.logger) {
      std::string logMsg = "SavePositionToContext: " + deviceName + " position saved as '" + keyPrefix + "' (X=" +
                           std::to_string(currentPos.x) + ", Y=" + std::to_string(currentPos.y) +
                           ", Z=" + std::to_string(currentPos.z);
      if (saveUVW) {
        logMsg += ", U=" + std::to_string(currentPos.u) + ", V=" + std::to_string(currentPos.v) +
                  ", W=" + std::to_string(currentPos.w);
      }
      logMsg += ")";
      deps.logger->Info(logMsg, "MotionHandler");
    }

    std::cout << "[MotionCommandHandler] SavePositionToContext: " << deviceName
              << " -> " << keyPrefix << "_xyz" << (saveUVW ? "uvw" : "") << std::endl;

    return true;
  }
  // ============================================================================
  // MOVE TO ABS FROM CONTEXT - Move to position stored in context variables
  // ============================================================================
  if (methodName == "MoveToAbsFromContext") {
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
        if (deps.logger) {
          deps.logger->Error("MoveToContextPosition: Failed to read Z position for " + deviceName, "MotionHandler");
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

    if (deps.logger) {
      std::string logMsg = "MoveToAbsFromContext: " + deviceName + " moving to '" + keyPrefix + "' (X=" +
                           std::to_string(x) + ", Y=" + std::to_string(y) + ", Z=" + std::to_string(z);
      if (keepZ) logMsg += " [Z=current]";
      if (hasUVW) {
        logMsg += ", U=" + std::to_string(u) + ", V=" + std::to_string(v) + ", W=" + std::to_string(w);
      }
      logMsg += ")";
      deps.logger->Info(logMsg, "MotionHandler");
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
    MotionCommandHandler::TargetPosition target;
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

    if (!deps.parent->WaitForMotionWithStopCheck(motionManager, deviceName, context, 30000, &target)) {
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
  if (methodName == "MoveToContextXY") {
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
      if (deps.logger) {
        deps.logger->Error("MoveToContextXY: Failed to read Z position for " + deviceName, "MotionHandler");
      }
      return false;
    }

    // Check if non-blocking mode requested
    bool explicitNonBlocking = false;
    auto blockingIt = params.find("blocking");
    if (blockingIt != params.end() && blockingIt->second == "false") {
      explicitNonBlocking = true;
    }

    if (deps.logger) {
      deps.logger->Info(
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
    MotionCommandHandler::TargetPosition target;
    target.hasX = true;
    target.hasY = true;
    target.hasZ = true;
    target.x = targetX;
    target.y = targetY;
    target.z = currentZ;

    if (!deps.parent->WaitForMotionWithStopCheck(motionManager, deviceName, context, 30000, &target)) {
      errorMessage = "MoveToContextXY: Motion stopped, timeout, or position verification failed";
      return false;
    }

    return true;
  }

  return std::nullopt;
}
