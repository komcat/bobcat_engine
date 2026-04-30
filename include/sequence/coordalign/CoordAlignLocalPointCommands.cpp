// CoordAlignLocalPointCommands.cpp — Family C: TaughtLocalPoint lifecycle.
// Branches: TeachLocalPoint, TransformLocalPoint, ListLocalPoints,
//           ClearLocalPoint.
//
// State + persistence stay on the orchestrator; helper reaches them via
// deps.parent->Load/Store/Remove/ListLocalPoint*. Math is in
// coord_align::* (CoordAlignMath.h).
#include "CoordAlignLocalPointCommands.h"

#include "../CoordinateAlignCommandHandler.h"
#include "ApplicationContext.h"
#include "LogManager.h"
#include "SequenceContext.h"
#include "unified_motion_manager.h"
#include "CoordAlignMath.h"

#include <iostream>
#include <sstream>

std::optional<bool> CoordAlignLocalPointCommands::TryExecute(
    const std::string& /*deviceName*/,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const CoordAlignCommandDeps& deps)
{
  LogManager* logger = deps.app ? deps.app->logManager : nullptr;

  // ============================================================================
  // TEACH LOCAL POINT
  // ============================================================================
  if (methodName == "TeachLocalPoint") {
    auto uidIt            = params.find("uid");
    auto coordSystemUidIt = params.find("coord_system_uid");
    auto motionDeviceIt   = params.find("motion_device");

    if (uidIt == params.end()) {
      errorMessage = "TeachLocalPoint: Missing required parameter 'uid'";
      return false;
    }
    if (coordSystemUidIt == params.end()) {
      errorMessage = "TeachLocalPoint: Missing required parameter 'coord_system_uid'";
      return false;
    }
    if (motionDeviceIt == params.end()) {
      errorMessage = "TeachLocalPoint: Missing required parameter 'motion_device'";
      return false;
    }
    if (!context) {
      errorMessage = "TeachLocalPoint: Sequence context is required";
      return false;
    }
    if (!deps.app || !deps.app->motionManager) {
      errorMessage = "TeachLocalPoint: Motion manager not available";
      return false;
    }

    const std::string& uid             = uidIt->second;
    const std::string& coordSystemUid  = coordSystemUidIt->second;
    const std::string& motionDevice    = motionDeviceIt->second;

    // Surface "overwrite" to the user/log when uid already has a saved point.
    {
      TaughtLocalPoint existingPoint;
      if (deps.parent->LoadLocalPoint(uid, existingPoint)) {
        std::cout << "[CoordAlign] TeachLocalPoint: Overwriting existing local point '" << uid << "'" << std::endl;
        if (logger) {
          logger->Info("TeachLocalPoint: Overwriting existing local point '" + uid + "'", "CoordAlign");
        }
      }
    }

    TaughtCoordinateSystem taught;
    if (!deps.parent->LoadTaughtSystem(coordSystemUid, taught)) {
      errorMessage = "TeachLocalPoint: Coordinate system '" + coordSystemUid + "' not found in file";
      return false;
    }

    std::cout << "[CoordAlign] TeachLocalPoint: Please move " << motionDevice
              << " to center the camera on the desired point, then press Enter..." << std::endl;

    // Capture current device position. Caller is expected to have already
    // jogged the gantry to the desired point (no in-line prompt today).
    PositionStruct currentPos = deps.app->motionManager->GetCurrentPosition(motionDevice);
    double currentX = currentPos.x;
    double currentY = currentPos.y;
    double currentZ = currentPos.z;

    // World position: explicit world_x/world_y params win, otherwise default
    // to the device position (proxy for world when no vision step ran).
    double worldX = currentX;
    double worldY = currentY;

    auto worldXIt = params.find("world_x");
    auto worldYIt = params.find("world_y");

    bool hasWorldX = (worldXIt != params.end() && !worldXIt->second.empty());
    bool hasWorldY = (worldYIt != params.end() && !worldYIt->second.empty());

    if (hasWorldX && hasWorldY) {
      worldX = deps.parent->ResolveNumericValue(worldXIt->second, context);
      worldY = deps.parent->ResolveNumericValue(worldYIt->second, context);
    }

    double localX, localY;
    coord_align::WorldToLocal(
        worldX, worldY,
        taught.originX_mm, taught.originY_mm,
        taught.xAxisX, taught.xAxisY,
        taught.yAxisX, taught.yAxisY,
        localX, localY);

    TaughtLocalPoint localPoint;
    localPoint.uid             = uid;
    localPoint.coordSystemUid  = coordSystemUid;
    localPoint.localX_mm       = localX;
    localPoint.localY_mm       = localY;
    localPoint.taughtWorldX_mm = worldX;
    localPoint.taughtWorldY_mm = worldY;
    localPoint.deviceX_mm      = currentX;
    localPoint.deviceY_mm      = currentY;
    localPoint.deviceZ_mm      = currentZ;
    localPoint.createdAt       = deps.parent->GetCurrentTimestamp();
    localPoint.valid           = true;

    auto descIt = params.find("description");
    if (descIt != params.end()) {
      localPoint.description = descIt->second;
    }

    deps.parent->StoreLocalPoint(localPoint);

    std::ostringstream msg;
    msg << "Taught local point '" << uid << "'\n"
        << "  Coordinate system: " << coordSystemUid << "\n"
        << "  World position: (" << worldX << ", " << worldY << ")\n"
        << "  Local position: (" << localX << ", " << localY << ")\n"
        << "  Device position: (" << currentX << ", " << currentY << ", " << currentZ << ")";

    if (logger) {
      logger->Info(msg.str(), "CoordAlign");
    }
    std::cout << "[CoordAlign] " << msg.str() << std::endl;

    context->SetValue(uid + ".local_x", static_cast<float>(localX), "coordalign", "TeachLocalPoint");
    context->SetValue(uid + ".local_y", static_cast<float>(localY), "coordalign", "TeachLocalPoint");
    context->SetValue(uid + ".world_x", static_cast<float>(worldX), "coordalign", "TeachLocalPoint");
    context->SetValue(uid + ".world_y", static_cast<float>(worldY), "coordalign", "TeachLocalPoint");
    context->SetValue(uid + ".coord_system", coordSystemUid, "coordalign", "TeachLocalPoint");

    return true;
  }

  // ============================================================================
  // TRANSFORM LOCAL POINT
  // ============================================================================
  if (methodName == "TransformLocalPoint") {
    auto pointUidIt     = params.find("point_uid");
    auto liveUidIt      = params.find("live_uid");
    auto outputPrefixIt = params.find("output_prefix");

    if (pointUidIt == params.end()) {
      errorMessage = "TransformLocalPoint: Missing required parameter 'point_uid'";
      return false;
    }
    if (liveUidIt == params.end()) {
      errorMessage = "TransformLocalPoint: Missing required parameter 'live_uid'";
      return false;
    }
    if (outputPrefixIt == params.end()) {
      errorMessage = "TransformLocalPoint: Missing required parameter 'output_prefix'";
      return false;
    }
    if (!context) {
      errorMessage = "TransformLocalPoint: Sequence context is required";
      return false;
    }

    const std::string& pointUid     = pointUidIt->second;
    const std::string& liveUid      = liveUidIt->second;
    const std::string& outputPrefix = outputPrefixIt->second;

    TaughtLocalPoint localPoint;
    if (!deps.parent->LoadLocalPoint(pointUid, localPoint)) {
      errorMessage = "TransformLocalPoint: Local point '" + pointUid + "' not found in file";
      return false;
    }

    LiveCoordinateSystem live;
    if (!deps.parent->GetLiveSystem(liveUid, live)) {
      errorMessage = "TransformLocalPoint: Live system '" + liveUid + "' not found in memory or file";
      return false;
    }

    if (!live.transformValid) {
      errorMessage = "TransformLocalPoint: Live system '" + liveUid + "' has no valid transformation";
      return false;
    }

    double outputX, outputY;
    coord_align::LocalToWorld(
        localPoint.localX_mm, localPoint.localY_mm,
        live.originX_mm, live.originY_mm,
        live.xAxisX, live.xAxisY,
        live.yAxisX, live.yAxisY,
        outputX, outputY);

    context->SetValue(outputPrefix + ".x", static_cast<float>(outputX), "coordalign", "TransformLocalPoint");
    context->SetValue(outputPrefix + ".y", static_cast<float>(outputY), "coordalign", "TransformLocalPoint");
    context->SetValue(outputPrefix + ".point_uid", pointUid, "coordalign", "TransformLocalPoint");
    context->SetValue(outputPrefix + ".local_x", static_cast<float>(localPoint.localX_mm), "coordalign", "TransformLocalPoint");
    context->SetValue(outputPrefix + ".local_y", static_cast<float>(localPoint.localY_mm), "coordalign", "TransformLocalPoint");

    std::cout << "[CoordAlign] TransformLocalPoint: '" << pointUid << "' local("
              << localPoint.localX_mm << ", " << localPoint.localY_mm
              << ") -> world(" << outputX << ", " << outputY << ")" << std::endl;

    if (logger) {
      std::ostringstream msg;
      msg << "TransformLocalPoint: '" << pointUid << "' local("
          << localPoint.localX_mm << ", " << localPoint.localY_mm
          << ") -> world(" << outputX << ", " << outputY << ")";
      logger->Info(msg.str(), "CoordAlign");
    }

    return true;
  }

  // ============================================================================
  // LIST LOCAL POINTS
  // ============================================================================
  if (methodName == "ListLocalPoints") {
    auto pointList = deps.parent->ListLocalPointsSnapshot();

    std::ostringstream msg;
    msg << "Local Points (" << pointList.size() << "):\n";
    for (const auto& pt : pointList) {
      msg << "  - " << pt.uid << " (coord_system: " << pt.coordSystemUid
          << ", local: (" << pt.localX_mm << ", " << pt.localY_mm << ")"
          << ", created: " << pt.createdAt << ")\n";
    }

    if (logger) {
      logger->Info(msg.str(), "CoordAlign");
    }
    std::cout << "[CoordAlign] " << msg.str() << std::endl;

    if (context) {
      context->SetValue("coordalign.local_points_count", static_cast<float>(pointList.size()), "coordalign", "ListLocalPoints");
    }

    return true;
  }

  // ============================================================================
  // CLEAR LOCAL POINT
  // ============================================================================
  if (methodName == "ClearLocalPoint") {
    auto uidIt = params.find("uid");
    if (uidIt == params.end()) {
      errorMessage = "ClearLocalPoint: Missing required parameter 'uid'";
      return false;
    }

    const std::string& uid = uidIt->second;

    if (!deps.parent->RemoveLocalPoint(uid)) {
      errorMessage = "ClearLocalPoint: Local point '" + uid + "' not found";
      return false;
    }

    if (logger) {
      logger->Info("ClearLocalPoint: Removed '" + uid + "'", "CoordAlign");
    }
    std::cout << "[CoordAlign] Removed local point '" << uid << "'" << std::endl;

    return true;
  }

  return std::nullopt;
}
