// CoordAlignTransformCommands.cpp — Family D: apply transformations.
// Branches: TransformPoint, MoveToLocalXY.
//
// Verb-on-existing-data — owns no map; consumes live/taught systems via
// deps.parent->Get/Load*. Math via coord_align::*.
#include "CoordAlignTransformCommands.h"

#include "../CoordinateAlignCommandHandler.h"
#include "ApplicationContext.h"
#include "LogManager.h"
#include "SequenceContext.h"
#include "unified_motion_manager.h"
#include "CoordAlignMath.h"

#include <iostream>
#include <sstream>

std::optional<bool> CoordAlignTransformCommands::TryExecute(
    const std::string& /*deviceName*/,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const CoordAlignCommandDeps& deps)
{
  LogManager* logger = deps.app ? deps.app->logManager : nullptr;

  // ============================================================================
  // TRANSFORM POINT (raw local x/y -> world)
  // ============================================================================
  if (methodName == "TransformPoint") {
    auto liveUidIt      = params.find("live_uid");
    auto inputXIt       = params.find("input_x");
    auto inputYIt       = params.find("input_y");
    auto outputPrefixIt = params.find("output_prefix");

    if (liveUidIt == params.end()) {
      errorMessage = "TransformPoint: Missing required parameter 'live_uid'";
      return false;
    }
    if (inputXIt == params.end() || inputYIt == params.end()) {
      errorMessage = "TransformPoint: Missing required parameters 'input_x' and 'input_y'";
      return false;
    }
    if (outputPrefixIt == params.end()) {
      errorMessage = "TransformPoint: Missing required parameter 'output_prefix'";
      return false;
    }
    if (!context) {
      errorMessage = "TransformPoint: Sequence context is required";
      return false;
    }

    const std::string& liveUid = liveUidIt->second;

    LiveCoordinateSystem live;
    if (!deps.parent->GetLiveSystem(liveUid, live)) {
      errorMessage = "TransformPoint: Live system '" + liveUid + "' not found in memory or file";
      return false;
    }

    if (!live.transformValid) {
      errorMessage = "TransformPoint: Live system '" + liveUid + "' has no valid transformation";
      return false;
    }

    // Inputs are LOCAL coordinates relative to the taught origin.
    double localX = deps.parent->ResolveNumericValue(inputXIt->second, context);
    double localY = deps.parent->ResolveNumericValue(inputYIt->second, context);

    double outputX, outputY;
    coord_align::LocalToWorld(
        localX, localY,
        live.originX_mm, live.originY_mm,
        live.xAxisX, live.xAxisY,
        live.yAxisX, live.yAxisY,
        outputX, outputY);

    const std::string& outputPrefix = outputPrefixIt->second;
    context->SetValue(outputPrefix + ".x", static_cast<float>(outputX), "coordalign", "TransformPoint");
    context->SetValue(outputPrefix + ".y", static_cast<float>(outputY), "coordalign", "TransformPoint");

    if (logger) {
      std::ostringstream msg;
      msg << "TransformPoint: local(" << localX << ", " << localY << ") -> world(" << outputX << ", " << outputY << ")";
      logger->Info(msg.str(), "CoordAlign");
    }
    std::cout << "[CoordAlign] TransformPoint: local(" << localX << ", " << localY
              << ") -> world(" << outputX << ", " << outputY << ")" << std::endl;

    return true;
  }

  // ============================================================================
  // MOVE TO LOCAL XY (transform local + move device)
  // ============================================================================
  if (methodName == "MoveToLocalXY") {
    // Required: local_x, local_y, coord_system_uid, motion_device.
    // coord_system_uid resolves to a LIVE system first (preferred), falling
    // back to the taught system if no live entry has been persisted yet.
    auto localXIt   = params.find("local_x");
    auto localYIt   = params.find("local_y");
    auto coordUidIt = params.find("coord_system_uid");
    auto deviceIt   = params.find("motion_device");

    if (localXIt == params.end() || localYIt == params.end()) {
      errorMessage = "MoveToLocalXY: Missing required parameters: local_x, local_y";
      return false;
    }
    if (coordUidIt == params.end()) {
      errorMessage = "MoveToLocalXY: Missing required parameter: coord_system_uid";
      return false;
    }
    if (deviceIt == params.end()) {
      errorMessage = "MoveToLocalXY: Missing required parameter: motion_device";
      return false;
    }
    if (!deps.app || !deps.app->motionManager) {
      errorMessage = "MoveToLocalXY: Motion manager not available";
      return false;
    }

    double localX = deps.parent->ResolveNumericValue(localXIt->second, context);
    double localY = deps.parent->ResolveNumericValue(localYIt->second, context);
    const std::string& coordUid     = coordUidIt->second;
    const std::string& motionDevice = deviceIt->second;

    // Resolve the coordinate frame: live first, then taught.
    double originX = 0.0, originY = 0.0;
    double xAxisX = 1.0, xAxisY = 0.0;
    double yAxisX = 0.0, yAxisY = 1.0;
    bool coordSystemFound = false;

    LiveCoordinateSystem live;
    if (deps.parent->LoadLiveSystem(coordUid, live) && live.transformValid) {
      originX = live.originX_mm;
      originY = live.originY_mm;
      xAxisX = live.xAxisX;
      xAxisY = live.xAxisY;
      yAxisX = live.yAxisX;
      yAxisY = live.yAxisY;
      coordSystemFound = true;

      std::cout << "[CoordAlign] MoveToLocalXY: Using live coordinate system '" << coordUid << "'" << std::endl;
    }

    if (!coordSystemFound) {
      TaughtCoordinateSystem taught;
      if (deps.parent->LoadTaughtSystem(coordUid, taught)) {
        originX = taught.originX_mm;
        originY = taught.originY_mm;
        xAxisX = taught.xAxisX;
        xAxisY = taught.xAxisY;
        yAxisX = taught.yAxisX;
        yAxisY = taught.yAxisY;
        coordSystemFound = true;

        std::cout << "[CoordAlign] MoveToLocalXY: Using taught coordinate system '" << coordUid << "'" << std::endl;
      }
    }

    if (!coordSystemFound) {
      errorMessage = "MoveToLocalXY: Coordinate system '" + coordUid + "' not found (neither live nor taught)";
      return false;
    }

    double worldX, worldY;
    coord_align::LocalToWorld(
        localX, localY,
        originX, originY,
        xAxisX, xAxisY,
        yAxisX, yAxisY,
        worldX, worldY);

    double currentZ = 0.0;
    if (!deps.app->motionManager->GetPosition(motionDevice, "Z", currentZ)) {
      errorMessage = "MoveToLocalXY: Failed to read Z position for " + motionDevice;
      return false;
    }

    std::cout << "[CoordAlign] MoveToLocalXY: " << motionDevice
              << " local(" << localX << ", " << localY
              << ") -> world(" << worldX << ", " << worldY
              << "), keeping Z=" << currentZ << std::endl;

    if (logger) {
      std::ostringstream msg;
      msg << "MoveToLocalXY: " << motionDevice
          << " local(" << localX << ", " << localY
          << ") -> world(" << worldX << ", " << worldY
          << "), Z=" << currentZ;
      logger->Info(msg.str(), "CoordAlign");
    }

    bool moveOk = deps.app->motionManager->MoveToAbs(
        motionDevice, worldX, worldY, currentZ, 100.0, true);

    if (!moveOk) {
      errorMessage = "MoveToLocalXY: Move failed for device '" + motionDevice + "'";
      return false;
    }

    auto outputPrefixIt = params.find("output_prefix");
    if (outputPrefixIt != params.end() && context) {
      const std::string& prefix = outputPrefixIt->second;
      context->SetValue(prefix + ".world_x", static_cast<float>(worldX), "coordalign", "MoveToLocalXY");
      context->SetValue(prefix + ".world_y", static_cast<float>(worldY), "coordalign", "MoveToLocalXY");
      context->SetValue(prefix + ".z", static_cast<float>(currentZ), "coordalign", "MoveToLocalXY");
    }

    return true;
  }

  return std::nullopt;
}
