// CoordAlignAlignCommands.cpp — Family B: LiveCoordinateSystem creation +
// transform retrieval. Branches:
//   - AlignLiveCoordinateSystem : re-execute vision at the 3 taught positions,
//                                 compute the live frame, store + persist
//   - GetTransform              : retrieve a live system's parameters by uid
//                                 (memory first, then disk)
//
// Owns ExecuteVisionAtPosition (~180 LOC) — the move + vision-execute
// pipeline used only by AlignLiveCoordinateSystem. Lives in an anon
// namespace; pulls deps.app->motionManager + deps.app->logManager.
#include "CoordAlignAlignCommands.h"

#include "../CoordinateAlignCommandHandler.h"
#include "ApplicationContext.h"
#include "LogManager.h"
#include "SequenceContext.h"
#include "unified_motion_manager.h"
#ifdef HAS_OPENCV
#include "../VisionCommandHandler.h"
#endif
#include "CoordAlignMath.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <sstream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

// Move the gantry to a target device position and execute the given vision
// profile, capturing results under `captureAs` in context. The verbose
// debug stdout traces match the pre-refactor behaviour — they are useful
// for diagnosing alignment failures and the pre-refactor code committed
// to keeping them.
bool ExecuteVisionAtPosition(
    const std::string& motionDevice,
    const std::string& cameraName,
    const std::string& visionProfilePath,
    double deviceX, double deviceY, double deviceZ,
    const std::string& captureAs,
    std::string& errorMessage,
    SequenceContext* context,
    const CoordAlignCommandDeps& deps)
{
    ApplicationContext* app = deps.app;
    LogManager* logger = app ? app->logManager : nullptr;

    if (!app) {
        errorMessage = "Application context not available";
        return false;
    }
    if (!app->motionManager) {
        errorMessage = "Motion manager not available";
        return false;
    }

    PositionStruct posBefore = app->motionManager->GetCurrentPosition(motionDevice);
    std::cout << "[DEBUG] [CoordAlign] ExecuteVisionAtPosition START" << std::endl;
    std::cout << "[DEBUG] [CoordAlign]   Device: " << motionDevice << std::endl;
    std::cout << "[DEBUG] [CoordAlign]   Current position BEFORE move: ("
              << posBefore.x << ", " << posBefore.y << ", " << posBefore.z << ")" << std::endl;
    std::cout << "[DEBUG] [CoordAlign]   Target position: ("
              << deviceX << ", " << deviceY << ", " << deviceZ << ")" << std::endl;

    double dx = deviceX - posBefore.x;
    double dy = deviceY - posBefore.y;
    double dz = deviceZ - posBefore.z;
    double distanceToTarget = std::sqrt(dx*dx + dy*dy + dz*dz);
    std::cout << "[DEBUG] [CoordAlign]   Distance to target: " << distanceToTarget << " mm" << std::endl;

    if (logger) {
        std::ostringstream msg;
        msg << "Moving " << motionDevice << " to (" << deviceX << ", " << deviceY << ", " << deviceZ << ")";
        logger->Info(msg.str(), "CoordAlign");
    }

    auto moveStartTime = std::chrono::steady_clock::now();
    std::cout << "[DEBUG] [CoordAlign] Calling MoveToAbs with blocking=true..." << std::endl;

    bool moveOk = app->motionManager->MoveToAbs(
        motionDevice, deviceX, deviceY, deviceZ, 100.0, true);

    auto moveEndTime = std::chrono::steady_clock::now();
    auto moveElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        moveEndTime - moveStartTime).count();

    std::cout << "[DEBUG] [CoordAlign] MoveToAbs returned: " << (moveOk ? "SUCCESS" : "FAILED")
              << " after " << moveElapsedMs << " ms" << std::endl;

    if (!moveOk) {
        errorMessage = "Failed to move to position (" +
            std::to_string(deviceX) + ", " +
            std::to_string(deviceY) + ", " +
            std::to_string(deviceZ) + ")";
        std::cout << "[ERROR] [CoordAlign] " << errorMessage << std::endl;
        return false;
    }

    PositionStruct posAfter = app->motionManager->GetCurrentPosition(motionDevice);
    double dxAfter = deviceX - posAfter.x;
    double dyAfter = deviceY - posAfter.y;
    double dzAfter = deviceZ - posAfter.z;
    double distanceAfterMove = std::sqrt(dxAfter*dxAfter + dyAfter*dyAfter + dzAfter*dzAfter);

    std::cout << "[DEBUG] [CoordAlign]   Position AFTER move: ("
              << posAfter.x << ", " << posAfter.y << ", " << posAfter.z << ")" << std::endl;
    std::cout << "[DEBUG] [CoordAlign]   Distance from target after move: " << distanceAfterMove << " mm" << std::endl;

    const double positionTolerance = 0.5;  // mm
    if (distanceAfterMove > positionTolerance) {
        std::cout << "[WARNING] [CoordAlign] Motion returned success but device is "
                  << distanceAfterMove << " mm from target (tolerance: "
                  << positionTolerance << " mm)" << std::endl;
        std::cout << "[WARNING] [CoordAlign] This may indicate premature motion completion!" << std::endl;
    }

    std::cout << "[DEBUG] [CoordAlign] Starting vision execution for capture_as='" << captureAs << "'" << std::endl;

#ifdef HAS_OPENCV
    VisionCommandHandler visionHandler;
    visionHandler.SetContext(app);
    visionHandler.SetLogger(logger);

    std::map<std::string, std::string> visionParams;
    visionParams["profile_path"] = visionProfilePath;
    visionParams["camera"] = cameraName;
    visionParams["capture_as"] = captureAs;
    visionParams["calculate_world_position"] = "true";
    visionParams["motion_device"] = motionDevice;

    std::string visionError;
    auto visionStartTime = std::chrono::steady_clock::now();

    bool visionOk = visionHandler.ExecuteCommand(
        nullptr, "vision", "execute_profile",
        visionParams, visionError, context);

    auto visionEndTime = std::chrono::steady_clock::now();
    auto visionElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        visionEndTime - visionStartTime).count();

    std::cout << "[DEBUG] [CoordAlign] VisionCommandHandler.ExecuteCommand returned: "
              << (visionOk ? "SUCCESS" : "FAILED") << " after " << visionElapsedMs << " ms" << std::endl;

    if (!visionOk) {
        errorMessage = "Vision execution failed: " + visionError;
        std::cout << "[ERROR] [CoordAlign] Vision execution failed: " << visionError << std::endl;
        return false;
    }

    bool visionSuccess = context->GetBoolean("vision.result.success", false);
    std::cout << "[DEBUG] [CoordAlign] vision.result.success = " << (visionSuccess ? "true" : "false") << std::endl;

    if (!visionSuccess) {
        std::string visionErr = context->GetString("vision.result.error", "Unknown vision error");
        errorMessage = "Vision detection failed: " + visionErr;
        std::cout << "[ERROR] [CoordAlign] Vision detection failed: " << visionErr << std::endl;
        return false;
    }

    std::cout << "[DEBUG] [CoordAlign] Vision succeeded for " << captureAs << std::endl;
#else
    (void)cameraName;
    (void)visionProfilePath;
    (void)captureAs;
    errorMessage = "Vision support requires OpenCV (HAS_OPENCV not defined)";
    return false;
#endif

    std::string worldXKey = captureAs + ".detected.world_x_mm";
    std::string worldYKey = captureAs + ".detected.world_y_mm";

    std::cout << "[DEBUG] [CoordAlign] Checking for vision results with prefix: " << captureAs << std::endl;
    std::cout << "[DEBUG] [CoordAlign]   Looking for key: " << worldXKey << std::endl;
    std::cout << "[DEBUG] [CoordAlign]   HasValue(worldXKey) = " << (context->HasValue(worldXKey) ? "true" : "false") << std::endl;
    std::cout << "[DEBUG] [CoordAlign]   HasValue(worldYKey) = " << (context->HasValue(worldYKey) ? "true" : "false") << std::endl;

    std::cout << "[DEBUG] [CoordAlign] Searching context for keys starting with '" << captureAs << "'..." << std::endl;
    auto allKeys = context->GetAllKeys();
    bool foundAny = false;
    for (const auto& key : allKeys) {
        if (key.find(captureAs) == 0) {
            std::cout << "[DEBUG] [CoordAlign]   Found key: " << key << std::endl;
            foundAny = true;
        }
    }
    if (!foundAny) {
        std::cout << "[DEBUG] [CoordAlign]   NO keys found with prefix '" << captureAs << "'" << std::endl;
        std::cout << "[DEBUG] [CoordAlign] Checking 'vision.result' keys..." << std::endl;
        for (const auto& key : allKeys) {
            if (key.find("vision.result") == 0 || key.find("vision.") == 0) {
                std::cout << "[DEBUG] [CoordAlign]   Found key: " << key << std::endl;
            }
        }
    }

    if (!context->HasValue(worldXKey)) {
        errorMessage = "Vision results not found with prefix: " + captureAs;
        std::cout << "[ERROR] [CoordAlign] " << errorMessage << std::endl;
        return false;
    }

    std::cout << "[DEBUG] [CoordAlign] ExecuteVisionAtPosition completed successfully" << std::endl;
    return true;
}

}  // namespace

std::optional<bool> CoordAlignAlignCommands::TryExecute(
    const std::string& /*deviceName*/,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const CoordAlignCommandDeps& deps)
{
  LogManager* logger = deps.app ? deps.app->logManager : nullptr;

  // ============================================================================
  // ALIGN LIVE COORDINATE SYSTEM
  // ============================================================================
  if (methodName == "AlignLiveCoordinateSystem") {
    auto taughtUidIt = params.find("taught_uid");
    auto liveUidIt   = params.find("live_uid");

    if (taughtUidIt == params.end()) {
      errorMessage = "AlignLiveCoordinateSystem: Missing required parameter 'taught_uid'";
      return false;
    }
    if (liveUidIt == params.end()) {
      errorMessage = "AlignLiveCoordinateSystem: Missing required parameter 'live_uid'";
      return false;
    }
    if (!context) {
      errorMessage = "AlignLiveCoordinateSystem: Sequence context is required";
      return false;
    }
    if (!deps.app) {
      errorMessage = "AlignLiveCoordinateSystem: Application context not set";
      return false;
    }

    const std::string& taughtUid = taughtUidIt->second;
    const std::string& liveUid   = liveUidIt->second;

    std::string outputPrefix = "transform";
    auto prefixIt = params.find("output_prefix");
    if (prefixIt != params.end()) {
      outputPrefix = prefixIt->second;
    }

    TaughtCoordinateSystem taught;
    if (!deps.parent->LoadTaughtSystem(taughtUid, taught)) {
      errorMessage = "AlignLiveCoordinateSystem: Taught system '" + taughtUid + "' not found in file";
      return false;
    }

    LiveCoordinateSystem live;
    live.uid       = liveUid;
    live.taughtUid = taughtUid;
    live.createdAt = deps.parent->GetCurrentTimestamp();

    std::string capturePrefix1 = "live_fid1_" + liveUid;
    std::string capturePrefix2 = "live_fid2_" + liveUid;
    std::string capturePrefix3 = "live_fid3_" + liveUid;

    if (logger) {
      logger->Info("AlignLiveCoordinateSystem: Starting alignment for taught='" + taughtUid +
                   "', live='" + liveUid + "'", "CoordAlign");
    }

    // Fiducial 1
    std::cout << "[CoordAlign] Moving to F1 position and executing vision..." << std::endl;
    if (!ExecuteVisionAtPosition(
            taught.motionDevice, taught.cameraName, taught.fid1.visionProfilePath,
            taught.fid1.deviceX_mm, taught.fid1.deviceY_mm, taught.fid1.deviceZ_mm,
            capturePrefix1, errorMessage, context, deps)) {
      errorMessage = "AlignLiveCoordinateSystem: Failed at fiducial 1 - " + errorMessage;
      return false;
    }
    if (!deps.parent->ReadFiducialFromContext(capturePrefix1, live.fid1, errorMessage, context)) {
      errorMessage = "AlignLiveCoordinateSystem: Failed to read fiducial 1 results - " + errorMessage;
      return false;
    }
    live.fid1.visionProfilePath = taught.fid1.visionProfilePath;

    // Fiducial 2
    std::cout << "[CoordAlign] Moving to F2 position and executing vision..." << std::endl;
    if (!ExecuteVisionAtPosition(
            taught.motionDevice, taught.cameraName, taught.fid2.visionProfilePath,
            taught.fid2.deviceX_mm, taught.fid2.deviceY_mm, taught.fid2.deviceZ_mm,
            capturePrefix2, errorMessage, context, deps)) {
      errorMessage = "AlignLiveCoordinateSystem: Failed at fiducial 2 - " + errorMessage;
      return false;
    }
    if (!deps.parent->ReadFiducialFromContext(capturePrefix2, live.fid2, errorMessage, context)) {
      errorMessage = "AlignLiveCoordinateSystem: Failed to read fiducial 2 results - " + errorMessage;
      return false;
    }
    live.fid2.visionProfilePath = taught.fid2.visionProfilePath;

    // Fiducial 3
    std::cout << "[CoordAlign] Moving to F3 position and executing vision..." << std::endl;
    if (!ExecuteVisionAtPosition(
            taught.motionDevice, taught.cameraName, taught.fid3.visionProfilePath,
            taught.fid3.deviceX_mm, taught.fid3.deviceY_mm, taught.fid3.deviceZ_mm,
            capturePrefix3, errorMessage, context, deps)) {
      errorMessage = "AlignLiveCoordinateSystem: Failed at fiducial 3 - " + errorMessage;
      return false;
    }
    if (!deps.parent->ReadFiducialFromContext(capturePrefix3, live.fid3, errorMessage, context)) {
      errorMessage = "AlignLiveCoordinateSystem: Failed to read fiducial 3 results - " + errorMessage;
      return false;
    }
    live.fid3.visionProfilePath = taught.fid3.visionProfilePath;

    if (coord_align::ArePointsCollinear(
            live.fid1.worldX_mm, live.fid1.worldY_mm,
            live.fid2.worldX_mm, live.fid2.worldY_mm,
            live.fid3.worldX_mm, live.fid3.worldY_mm)) {
      errorMessage = "AlignLiveCoordinateSystem: Live fiducial points are collinear";
      return false;
    }

    coord_align::ComputeCoordinateAxes(
        live.fid1.worldX_mm, live.fid1.worldY_mm,
        live.fid2.worldX_mm, live.fid2.worldY_mm,
        live.originX_mm, live.originY_mm,
        live.xAxisX, live.xAxisY,
        live.yAxisX, live.yAxisY);

    // Rotation between taught and live X-axes (diagnostics).
    double taughtAngle = std::atan2(taught.xAxisY, taught.xAxisX);
    double liveAngle   = std::atan2(live.xAxisY, live.xAxisX);
    live.rotationRad = liveAngle - taughtAngle;
    live.rotationDeg = live.rotationRad * 180.0 / M_PI;

    // Scale = ratio of F1-F2 live distance to taught distance.
    double taughtDist12 = taught.refDistance12_mm;
    double liveDist12   = coord_align::CalculateDistance(
        live.fid1.worldX_mm, live.fid1.worldY_mm,
        live.fid2.worldX_mm, live.fid2.worldY_mm);
    live.scale = (taughtDist12 > 1e-10) ? (liveDist12 / taughtDist12) : 1.0;

    // Quality: predict Fid3 from taught local coords + live frame, compare to actual.
    double fid3LocalX, fid3LocalY;
    coord_align::WorldToLocal(
        taught.fid3.worldX_mm, taught.fid3.worldY_mm,
        taught.originX_mm, taught.originY_mm,
        taught.xAxisX, taught.xAxisY,
        taught.yAxisX, taught.yAxisY,
        fid3LocalX, fid3LocalY);

    double fid3PredictedX, fid3PredictedY;
    coord_align::LocalToWorld(
        fid3LocalX, fid3LocalY,
        live.originX_mm, live.originY_mm,
        live.xAxisX, live.xAxisY,
        live.yAxisX, live.yAxisY,
        fid3PredictedX, fid3PredictedY);

    double fid3ErrorX = fid3PredictedX - live.fid3.worldX_mm;
    double fid3ErrorY = fid3PredictedY - live.fid3.worldY_mm;
    live.maxError_mm = std::sqrt(fid3ErrorX * fid3ErrorX + fid3ErrorY * fid3ErrorY);
    // For 3-point, F1 and F2 are exact by construction → RMS == max.
    live.rmsError_mm = live.maxError_mm;

    live.transformValid = true;

    deps.parent->StoreLiveSystem(live);

    std::ostringstream msg;
    msg << "Created Live coordinate system '" << liveUid << "' (based on taught='" << taughtUid << "')\n"
        << "  Live Origin (F1): (" << live.originX_mm << ", " << live.originY_mm << ")\n"
        << "  Live X-axis: (" << live.xAxisX << ", " << live.xAxisY << ")\n"
        << "  Live Y-axis: (" << live.yAxisX << ", " << live.yAxisY << ")\n"
        << "  Rotation from taught: " << live.rotationDeg << " deg\n"
        << "  Scale: " << live.scale << "\n"
        << "  Fid3 prediction error: " << live.maxError_mm << " mm";

    if (logger) {
      logger->Info(msg.str(), "CoordAlign");
    }
    std::cout << "[CoordAlign] " << msg.str() << std::endl;

    context->SetValue(outputPrefix + ".origin_x_mm", static_cast<float>(live.originX_mm), "coordalign", "AlignLiveCoordinateSystem");
    context->SetValue(outputPrefix + ".origin_y_mm", static_cast<float>(live.originY_mm), "coordalign", "AlignLiveCoordinateSystem");
    context->SetValue(outputPrefix + ".rotation_deg", static_cast<float>(live.rotationDeg), "coordalign", "AlignLiveCoordinateSystem");
    context->SetValue(outputPrefix + ".rotation_rad", static_cast<float>(live.rotationRad), "coordalign", "AlignLiveCoordinateSystem");
    context->SetValue(outputPrefix + ".scale", static_cast<float>(live.scale), "coordalign", "AlignLiveCoordinateSystem");
    context->SetValue(outputPrefix + ".fid3_error_mm", static_cast<float>(live.maxError_mm), "coordalign", "AlignLiveCoordinateSystem");
    context->SetValue(outputPrefix + ".success", true, "coordalign", "AlignLiveCoordinateSystem");
    context->SetValue(outputPrefix + ".taught_uid", taughtUid, "coordalign", "AlignLiveCoordinateSystem");
    context->SetValue(outputPrefix + ".live_uid", liveUid, "coordalign", "AlignLiveCoordinateSystem");

    return true;
  }

  // ============================================================================
  // GET TRANSFORM
  // ============================================================================
  if (methodName == "GetTransform") {
    auto liveUidIt = params.find("live_uid");
    if (liveUidIt == params.end()) {
      errorMessage = "GetTransform: Missing required parameter 'live_uid'";
      return false;
    }
    if (!context) {
      errorMessage = "GetTransform: Sequence context is required";
      return false;
    }

    const std::string& liveUid = liveUidIt->second;

    std::string outputPrefix = "transform";
    auto prefixIt = params.find("output_prefix");
    if (prefixIt != params.end()) {
      outputPrefix = prefixIt->second;
    }

    LiveCoordinateSystem live;
    if (!deps.parent->GetLiveSystem(liveUid, live)) {
      errorMessage = "GetTransform: Live system '" + liveUid + "' not found in memory or file";
      return false;
    }

    context->SetValue(outputPrefix + ".origin_x_mm", static_cast<float>(live.originX_mm), "coordalign", "GetTransform");
    context->SetValue(outputPrefix + ".origin_y_mm", static_cast<float>(live.originY_mm), "coordalign", "GetTransform");
    context->SetValue(outputPrefix + ".x_axis_x", static_cast<float>(live.xAxisX), "coordalign", "GetTransform");
    context->SetValue(outputPrefix + ".x_axis_y", static_cast<float>(live.xAxisY), "coordalign", "GetTransform");
    context->SetValue(outputPrefix + ".y_axis_x", static_cast<float>(live.yAxisX), "coordalign", "GetTransform");
    context->SetValue(outputPrefix + ".y_axis_y", static_cast<float>(live.yAxisY), "coordalign", "GetTransform");
    context->SetValue(outputPrefix + ".rotation_deg", static_cast<float>(live.rotationDeg), "coordalign", "GetTransform");
    context->SetValue(outputPrefix + ".rotation_rad", static_cast<float>(live.rotationRad), "coordalign", "GetTransform");
    context->SetValue(outputPrefix + ".scale", static_cast<float>(live.scale), "coordalign", "GetTransform");
    context->SetValue(outputPrefix + ".rms_error_mm", static_cast<float>(live.rmsError_mm), "coordalign", "GetTransform");
    context->SetValue(outputPrefix + ".max_error_mm", static_cast<float>(live.maxError_mm), "coordalign", "GetTransform");
    context->SetValue(outputPrefix + ".valid", live.transformValid, "coordalign", "GetTransform");

    return true;
  }

  return std::nullopt;
}
