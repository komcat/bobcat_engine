#include "CoordinateAlignCommandHandler.h"
#include "SequenceContext.h"
#include "LogManager.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#ifdef HAS_OPENCV
#include "VisionCommandHandler.h"
#endif
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cmath>

using json = nlohmann::json;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

bool CoordinateAlignCommandHandler::ExecuteCommand(
    void* manager,
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context)
{
    (void)manager;
    (void)deviceName;

    if (methodName == "TeachCoordinateSystem") {
        return HandleTeachCoordinateSystem(params, errorMessage, context);
    }
    else if (methodName == "AlignLiveCoordinateSystem") {
        return HandleAlignLiveCoordinateSystem(params, errorMessage, context);
    }
    else if (methodName == "TransformPoint") {
        return HandleTransformPoint(params, errorMessage, context);
    }
    else if (methodName == "TeachLocalPoint") {
        return HandleTeachLocalPoint(params, errorMessage, context);
    }
    else if (methodName == "TransformLocalPoint") {
        return HandleTransformLocalPoint(params, errorMessage, context);
    }
    else if (methodName == "GetTransform") {
        return HandleGetTransform(params, errorMessage, context);
    }
    else if (methodName == "ClearCoordinateSystem") {
        return HandleClearCoordinateSystem(params, errorMessage);
    }
    else if (methodName == "ListCoordinateSystems") {
        return HandleListCoordinateSystems(errorMessage, context);
    }
    else if (methodName == "ListLocalPoints") {
        return HandleListLocalPoints(errorMessage, context);
    }
    else if (methodName == "ClearLocalPoint") {
        return HandleClearLocalPoint(params, errorMessage);
    }
    else if (methodName == "MoveToLocalXY") {
        return HandleMoveToLocalXY(params, errorMessage, context);
    }

    errorMessage = "Unknown method: " + methodName +
                   ". Supported: TeachCoordinateSystem, AlignLiveCoordinateSystem, "
                   "TransformPoint, TeachLocalPoint, TransformLocalPoint, GetTransform, "
                   "ClearCoordinateSystem, ListCoordinateSystems, ListLocalPoints, ClearLocalPoint, MoveToLocalXY";
    return false;
}

bool CoordinateAlignCommandHandler::HandleTeachCoordinateSystem(
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context)
{
    // Validate required parameters
    auto uidIt = params.find("uid");
    auto fid1PrefixIt = params.find("fid1_prefix");
    auto fid2PrefixIt = params.find("fid2_prefix");
    auto fid3PrefixIt = params.find("fid3_prefix");
    auto fid1ProfileIt = params.find("fid1_profile");
    auto fid2ProfileIt = params.find("fid2_profile");
    auto fid3ProfileIt = params.find("fid3_profile");
    auto motionDeviceIt = params.find("motion_device");
    auto cameraNameIt = params.find("camera_name");

    if (uidIt == params.end()) {
        errorMessage = "TeachCoordinateSystem: Missing required parameter 'uid'";
        return false;
    }
    if (fid1PrefixIt == params.end() || fid2PrefixIt == params.end() || fid3PrefixIt == params.end()) {
        errorMessage = "TeachCoordinateSystem: Missing required parameters 'fid1_prefix', 'fid2_prefix', 'fid3_prefix'";
        return false;
    }
    if (fid1ProfileIt == params.end() || fid2ProfileIt == params.end() || fid3ProfileIt == params.end()) {
        errorMessage = "TeachCoordinateSystem: Missing required parameters 'fid1_profile', 'fid2_profile', 'fid3_profile'";
        return false;
    }
    if (motionDeviceIt == params.end()) {
        errorMessage = "TeachCoordinateSystem: Missing required parameter 'motion_device'";
        return false;
    }
    if (cameraNameIt == params.end()) {
        errorMessage = "TeachCoordinateSystem: Missing required parameter 'camera_name'";
        return false;
    }

    if (!context) {
        errorMessage = "TeachCoordinateSystem: Sequence context is required";
        return false;
    }

    const std::string& uid = uidIt->second;

    // Get process unique ID for file naming (allows each process instance to have its own file)
    auto processUidIt = params.find("process_uid");
    std::string storageKey = uid;
    if (processUidIt != params.end() && !processUidIt->second.empty()) {
        storageKey = uid + "_" + processUidIt->second;
    }

    // Read fiducial data from context
    TaughtCoordinateSystem taught;
    taught.uid = storageKey;  // Use combined key as the storage UID
    taught.motionDevice = motionDeviceIt->second;
    taught.cameraName = cameraNameIt->second;
    taught.createdAt = GetCurrentTimestamp();

    // Optional description
    auto descIt = params.find("description");
    if (descIt != params.end()) {
        taught.description = descIt->second;
    }

    // Read fiducial 1
    if (!ReadFiducialFromContext(fid1PrefixIt->second, taught.fid1, errorMessage, context)) {
        return false;
    }
    taught.fid1.visionProfilePath = fid1ProfileIt->second;

    // Read fiducial 2
    if (!ReadFiducialFromContext(fid2PrefixIt->second, taught.fid2, errorMessage, context)) {
        return false;
    }
    taught.fid2.visionProfilePath = fid2ProfileIt->second;

    // Read fiducial 3
    if (!ReadFiducialFromContext(fid3PrefixIt->second, taught.fid3, errorMessage, context)) {
        return false;
    }
    taught.fid3.visionProfilePath = fid3ProfileIt->second;

    // Check for collinearity
    if (ArePointsCollinear(
            taught.fid1.worldX_mm, taught.fid1.worldY_mm,
            taught.fid2.worldX_mm, taught.fid2.worldY_mm,
            taught.fid3.worldX_mm, taught.fid3.worldY_mm)) {
        errorMessage = "TeachCoordinateSystem: Fiducial points are collinear, cannot create coordinate system";
        return false;
    }

    // Calculate reference distances
    taught.refDistance12_mm = CalculateDistance(
        taught.fid1.worldX_mm, taught.fid1.worldY_mm,
        taught.fid2.worldX_mm, taught.fid2.worldY_mm);
    taught.refDistance23_mm = CalculateDistance(
        taught.fid2.worldX_mm, taught.fid2.worldY_mm,
        taught.fid3.worldX_mm, taught.fid3.worldY_mm);
    taught.refDistance31_mm = CalculateDistance(
        taught.fid3.worldX_mm, taught.fid3.worldY_mm,
        taught.fid1.worldX_mm, taught.fid1.worldY_mm);

    // Compute coordinate system axes (Fid1 = origin, Fid1->Fid2 = X-axis)
    ComputeCoordinateAxes(
        taught.fid1.worldX_mm, taught.fid1.worldY_mm,
        taught.fid2.worldX_mm, taught.fid2.worldY_mm,
        taught.originX_mm, taught.originY_mm,
        taught.xAxisX, taught.xAxisY,
        taught.yAxisX, taught.yAxisY);

    // Store taught system in memory (overwrites existing if present)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_taughtSystems[storageKey] = taught;
    }

    // Save to disk for persistence
    SaveTaughtSystemToDisk(taught);

    // Log success
    std::ostringstream msg;
    msg << "Created Taught coordinate system '" << storageKey << "'\n"
        << "  Origin (F1): (" << taught.originX_mm << ", " << taught.originY_mm << ")\n"
        << "  X-axis: (" << taught.xAxisX << ", " << taught.xAxisY << ")\n"
        << "  Y-axis: (" << taught.yAxisX << ", " << taught.yAxisY << ")\n"
        << "  F1: world(" << taught.fid1.worldX_mm << ", " << taught.fid1.worldY_mm << ") "
        << "device(" << taught.fid1.deviceX_mm << ", " << taught.fid1.deviceY_mm << ", " << taught.fid1.deviceZ_mm << ")\n"
        << "  F2: world(" << taught.fid2.worldX_mm << ", " << taught.fid2.worldY_mm << ") "
        << "device(" << taught.fid2.deviceX_mm << ", " << taught.fid2.deviceY_mm << ", " << taught.fid2.deviceZ_mm << ")\n"
        << "  F3: world(" << taught.fid3.worldX_mm << ", " << taught.fid3.worldY_mm << ") "
        << "device(" << taught.fid3.deviceX_mm << ", " << taught.fid3.deviceY_mm << ", " << taught.fid3.deviceZ_mm << ")\n"
        << "  Distances: F1-F2=" << taught.refDistance12_mm << "mm, F2-F3=" << taught.refDistance23_mm
        << "mm, F3-F1=" << taught.refDistance31_mm << "mm";

    if (m_logger) {
        m_logger->Info(msg.str(), "CoordAlign");
    }
    std::cout << "[CoordAlign] " << msg.str() << std::endl;

    // Store taught system info in context (use storageKey for unique per-process values)
    context->SetValue(storageKey + ".taught", true, "coordalign", "TeachCoordinateSystem");
    context->SetValue(storageKey + ".fid1_world_x", static_cast<float>(taught.fid1.worldX_mm), "coordalign", "TeachCoordinateSystem");
    context->SetValue(storageKey + ".fid1_world_y", static_cast<float>(taught.fid1.worldY_mm), "coordalign", "TeachCoordinateSystem");
    context->SetValue(storageKey + ".fid2_world_x", static_cast<float>(taught.fid2.worldX_mm), "coordalign", "TeachCoordinateSystem");
    context->SetValue(storageKey + ".fid2_world_y", static_cast<float>(taught.fid2.worldY_mm), "coordalign", "TeachCoordinateSystem");
    context->SetValue(storageKey + ".fid3_world_x", static_cast<float>(taught.fid3.worldX_mm), "coordalign", "TeachCoordinateSystem");
    context->SetValue(storageKey + ".fid3_world_y", static_cast<float>(taught.fid3.worldY_mm), "coordalign", "TeachCoordinateSystem");

    return true;
}

bool CoordinateAlignCommandHandler::HandleAlignLiveCoordinateSystem(
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context)
{
    // Validate required parameters
    auto taughtUidIt = params.find("taught_uid");
    auto liveUidIt = params.find("live_uid");

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
    if (!m_appContext) {
        errorMessage = "AlignLiveCoordinateSystem: Application context not set";
        return false;
    }

    const std::string& taughtUid = taughtUidIt->second;
    const std::string& liveUid = liveUidIt->second;

    // Get output prefix
    std::string outputPrefix = "transform";
    auto prefixIt = params.find("output_prefix");
    if (prefixIt != params.end()) {
        outputPrefix = prefixIt->second;
    }

    // Load taught system from file (real-time read)
    TaughtCoordinateSystem taught;
    if (!LoadTaughtSystemFromDisk(taughtUid, taught)) {
        errorMessage = "AlignLiveCoordinateSystem: Taught system '" + taughtUid + "' not found in file";
        return false;
    }

    LiveCoordinateSystem live;
    live.uid = liveUid;
    live.taughtUid = taughtUid;
    live.createdAt = GetCurrentTimestamp();

    // Execute vision at each taught device position
    std::string capturePrefix1 = "live_fid1_" + liveUid;
    std::string capturePrefix2 = "live_fid2_" + liveUid;
    std::string capturePrefix3 = "live_fid3_" + liveUid;

    if (m_logger) {
        m_logger->Info("AlignLiveCoordinateSystem: Starting alignment for taught='" + taughtUid +
                       "', live='" + liveUid + "'", "CoordAlign");
    }

    // Fiducial 1
    std::cout << "[CoordAlign] Moving to F1 position and executing vision..." << std::endl;
    if (!ExecuteVisionAtPosition(
            taught.motionDevice, taught.cameraName, taught.fid1.visionProfilePath,
            taught.fid1.deviceX_mm, taught.fid1.deviceY_mm, taught.fid1.deviceZ_mm,
            capturePrefix1, errorMessage, context)) {
        errorMessage = "AlignLiveCoordinateSystem: Failed at fiducial 1 - " + errorMessage;
        return false;
    }
    if (!ReadFiducialFromContext(capturePrefix1, live.fid1, errorMessage, context)) {
        errorMessage = "AlignLiveCoordinateSystem: Failed to read fiducial 1 results - " + errorMessage;
        return false;
    }
    live.fid1.visionProfilePath = taught.fid1.visionProfilePath;

    // Fiducial 2
    std::cout << "[CoordAlign] Moving to F2 position and executing vision..." << std::endl;
    if (!ExecuteVisionAtPosition(
            taught.motionDevice, taught.cameraName, taught.fid2.visionProfilePath,
            taught.fid2.deviceX_mm, taught.fid2.deviceY_mm, taught.fid2.deviceZ_mm,
            capturePrefix2, errorMessage, context)) {
        errorMessage = "AlignLiveCoordinateSystem: Failed at fiducial 2 - " + errorMessage;
        return false;
    }
    if (!ReadFiducialFromContext(capturePrefix2, live.fid2, errorMessage, context)) {
        errorMessage = "AlignLiveCoordinateSystem: Failed to read fiducial 2 results - " + errorMessage;
        return false;
    }
    live.fid2.visionProfilePath = taught.fid2.visionProfilePath;

    // Fiducial 3
    std::cout << "[CoordAlign] Moving to F3 position and executing vision..." << std::endl;
    if (!ExecuteVisionAtPosition(
            taught.motionDevice, taught.cameraName, taught.fid3.visionProfilePath,
            taught.fid3.deviceX_mm, taught.fid3.deviceY_mm, taught.fid3.deviceZ_mm,
            capturePrefix3, errorMessage, context)) {
        errorMessage = "AlignLiveCoordinateSystem: Failed at fiducial 3 - " + errorMessage;
        return false;
    }
    if (!ReadFiducialFromContext(capturePrefix3, live.fid3, errorMessage, context)) {
        errorMessage = "AlignLiveCoordinateSystem: Failed to read fiducial 3 results - " + errorMessage;
        return false;
    }
    live.fid3.visionProfilePath = taught.fid3.visionProfilePath;

    // Check for collinearity in live points
    if (ArePointsCollinear(
            live.fid1.worldX_mm, live.fid1.worldY_mm,
            live.fid2.worldX_mm, live.fid2.worldY_mm,
            live.fid3.worldX_mm, live.fid3.worldY_mm)) {
        errorMessage = "AlignLiveCoordinateSystem: Live fiducial points are collinear";
        return false;
    }

    // Compute live coordinate system axes (same convention: Fid1 = origin, Fid1->Fid2 = X-axis)
    ComputeCoordinateAxes(
        live.fid1.worldX_mm, live.fid1.worldY_mm,
        live.fid2.worldX_mm, live.fid2.worldY_mm,
        live.originX_mm, live.originY_mm,
        live.xAxisX, live.xAxisY,
        live.yAxisX, live.yAxisY);

    // Compute rotation angle between taught and live X-axes (for diagnostics)
    double taughtAngle = std::atan2(taught.xAxisY, taught.xAxisX);
    double liveAngle = std::atan2(live.xAxisY, live.xAxisX);
    live.rotationRad = liveAngle - taughtAngle;
    live.rotationDeg = live.rotationRad * 180.0 / M_PI;

    // Compute scale (ratio of live F1-F2 distance to taught F1-F2 distance)
    double taughtDist12 = taught.refDistance12_mm;
    double liveDist12 = CalculateDistance(
        live.fid1.worldX_mm, live.fid1.worldY_mm,
        live.fid2.worldX_mm, live.fid2.worldY_mm);
    live.scale = (taughtDist12 > 1e-10) ? (liveDist12 / taughtDist12) : 1.0;

    // Compute quality metrics by checking Fid3 prediction error
    // Transform taught Fid3 local coords to live world and compare
    double fid3LocalX, fid3LocalY;
    WorldToLocal(
        taught.fid3.worldX_mm, taught.fid3.worldY_mm,
        taught.originX_mm, taught.originY_mm,
        taught.xAxisX, taught.xAxisY,
        taught.yAxisX, taught.yAxisY,
        fid3LocalX, fid3LocalY);

    double fid3PredictedX, fid3PredictedY;
    LocalToWorld(
        fid3LocalX, fid3LocalY,
        live.originX_mm, live.originY_mm,
        live.xAxisX, live.xAxisY,
        live.yAxisX, live.yAxisY,
        fid3PredictedX, fid3PredictedY);

    double fid3ErrorX = fid3PredictedX - live.fid3.worldX_mm;
    double fid3ErrorY = fid3PredictedY - live.fid3.worldY_mm;
    live.maxError_mm = std::sqrt(fid3ErrorX * fid3ErrorX + fid3ErrorY * fid3ErrorY);
    live.rmsError_mm = live.maxError_mm;  // For 3-point, RMS = max since F1 and F2 are exact by construction

    live.transformValid = true;

    // Store live system in memory
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_liveSystems[liveUid] = live;
    }

    // Save live system to disk for persistence across sessions
    SaveLiveSystemToDisk(live);

    // Log success
    std::ostringstream msg;
    msg << "Created Live coordinate system '" << liveUid << "' (based on taught='" << taughtUid << "')\n"
        << "  Live Origin (F1): (" << live.originX_mm << ", " << live.originY_mm << ")\n"
        << "  Live X-axis: (" << live.xAxisX << ", " << live.xAxisY << ")\n"
        << "  Live Y-axis: (" << live.yAxisX << ", " << live.yAxisY << ")\n"
        << "  Rotation from taught: " << live.rotationDeg << " deg\n"
        << "  Scale: " << live.scale << "\n"
        << "  Fid3 prediction error: " << live.maxError_mm << " mm";

    if (m_logger) {
        m_logger->Info(msg.str(), "CoordAlign");
    }
    std::cout << "[CoordAlign] " << msg.str() << std::endl;

    // Store results in context
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

bool CoordinateAlignCommandHandler::HandleTransformPoint(
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context)
{
    auto liveUidIt = params.find("live_uid");
    auto inputXIt = params.find("input_x");
    auto inputYIt = params.find("input_y");
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

    // Find live system (try memory first, then disk)
    LiveCoordinateSystem live;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_liveSystems.find(liveUid);
        if (it != m_liveSystems.end()) {
            live = it->second;
        }
    }

    // If not in memory, try loading from disk
    if (live.uid.empty()) {
        if (!LoadLiveSystemFromDisk(liveUid, live)) {
            errorMessage = "TransformPoint: Live system '" + liveUid + "' not found in memory or file";
            return false;
        }
    }

    if (!live.transformValid) {
        errorMessage = "TransformPoint: Live system '" + liveUid + "' has no valid transformation";
        return false;
    }

    // Resolve input values (these are LOCAL coordinates relative to taught origin)
    double localX = ResolveNumericValue(inputXIt->second, context);
    double localY = ResolveNumericValue(inputYIt->second, context);

    // Convert local coordinates to live world coordinates
    double outputX, outputY;
    LocalToWorld(
        localX, localY,
        live.originX_mm, live.originY_mm,
        live.xAxisX, live.xAxisY,
        live.yAxisX, live.yAxisY,
        outputX, outputY);

    // Store results
    const std::string& outputPrefix = outputPrefixIt->second;
    context->SetValue(outputPrefix + ".x", static_cast<float>(outputX), "coordalign", "TransformPoint");
    context->SetValue(outputPrefix + ".y", static_cast<float>(outputY), "coordalign", "TransformPoint");

    if (m_logger) {
        std::ostringstream msg;
        msg << "TransformPoint: local(" << localX << ", " << localY << ") -> world(" << outputX << ", " << outputY << ")";
        m_logger->Info(msg.str(), "CoordAlign");
    }
    std::cout << "[CoordAlign] TransformPoint: local(" << localX << ", " << localY
              << ") -> world(" << outputX << ", " << outputY << ")" << std::endl;

    return true;
}

bool CoordinateAlignCommandHandler::HandleGetTransform(
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context)
{
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

    // Find live system (try memory first, then disk)
    LiveCoordinateSystem live;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_liveSystems.find(liveUid);
        if (it != m_liveSystems.end()) {
            live = it->second;
        }
    }

    // If not in memory, try loading from disk
    if (live.uid.empty()) {
        if (!LoadLiveSystemFromDisk(liveUid, live)) {
            errorMessage = "GetTransform: Live system '" + liveUid + "' not found in memory or file";
            return false;
        }
    }

    // Store in context
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

bool CoordinateAlignCommandHandler::HandleClearCoordinateSystem(
    const std::map<std::string, std::string>& params,
    std::string& errorMessage)
{
    auto uidIt = params.find("uid");
    if (uidIt == params.end()) {
        errorMessage = "ClearCoordinateSystem: Missing required parameter 'uid'";
        return false;
    }

    const std::string& uid = uidIt->second;

    std::string type = "both";
    auto typeIt = params.find("type");
    if (typeIt != params.end()) {
        type = typeIt->second;
    }

    bool clearedTaught = false;
    bool clearedLive = false;

    // For taught systems: delete from file (primary storage)
    if (type == "taught" || type == "both") {
        if (DeleteTaughtSystemFromDisk(uid)) {
            clearedTaught = true;
            // Also remove from memory cache if present
            std::lock_guard<std::mutex> lock(m_mutex);
            m_taughtSystems.erase(uid);
        }
    }

    // For live systems: delete from file first, then memory
    if (type == "live" || type == "both") {
        if (DeleteLiveSystemFromDisk(uid)) {
            clearedLive = true;
        }
        // Also remove from memory cache if present
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_liveSystems.find(uid);
        if (it != m_liveSystems.end()) {
            m_liveSystems.erase(it);
            clearedLive = true;
        }
    }

    if (!clearedTaught && !clearedLive) {
        errorMessage = "ClearCoordinateSystem: System '" + uid + "' not found";
        return false;
    }

    std::string clearedMsg;
    if (clearedTaught && clearedLive) {
        clearedMsg = "Cleared taught and live systems";
    } else if (clearedTaught) {
        clearedMsg = "Cleared taught system";
    } else {
        clearedMsg = "Cleared live system";
    }

    if (m_logger) {
        m_logger->Info("ClearCoordinateSystem: " + clearedMsg + " '" + uid + "'", "CoordAlign");
    }
    std::cout << "[CoordAlign] " << clearedMsg << " '" << uid << "'" << std::endl;

    return true;
}

bool CoordinateAlignCommandHandler::HandleListCoordinateSystems(
    std::string& errorMessage,
    SequenceContext* context)
{
    (void)errorMessage;

    std::ostringstream msg;
    msg << "Coordinate Systems:\n";

    // List taught systems from files
    std::vector<TaughtCoordinateSystem> taughtList;
    auto taughtDir = GetTaughtSystemsDir();
    if (std::filesystem::exists(taughtDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(taughtDir)) {
            if (entry.path().extension() == ".json") {
                std::string uid = entry.path().stem().string();
                TaughtCoordinateSystem sys;
                if (LoadTaughtSystemFromDisk(uid, sys)) {
                    taughtList.push_back(sys);
                }
            }
        }
    }

    msg << "  Taught (" << taughtList.size() << "):\n";
    for (const auto& sys : taughtList) {
        msg << "    - " << sys.uid << " (device: " << sys.motionDevice
            << ", created: " << sys.createdAt << ")\n";
    }

    // Live systems are session-only (memory)
    std::lock_guard<std::mutex> lock(m_mutex);
    msg << "  Live (" << m_liveSystems.size() << "):\n";
    for (const auto& [uid, sys] : m_liveSystems) {
        msg << "    - " << uid << " (taught: " << sys.taughtUid
            << ", rot: " << sys.rotationDeg << "deg, rms: " << sys.rmsError_mm << "mm)\n";
    }

    if (m_logger) {
        m_logger->Info(msg.str(), "CoordAlign");
    }
    std::cout << "[CoordAlign] " << msg.str() << std::endl;

    if (context) {
        context->SetValue("coordalign.taught_count", static_cast<float>(taughtList.size()), "coordalign", "ListCoordinateSystems");
        context->SetValue("coordalign.live_count", static_cast<float>(m_liveSystems.size()), "coordalign", "ListCoordinateSystems");
    }

    return true;
}

bool CoordinateAlignCommandHandler::HandleTeachLocalPoint(
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context)
{
    // Required parameters
    auto uidIt = params.find("uid");
    auto coordSystemUidIt = params.find("coord_system_uid");
    auto motionDeviceIt = params.find("motion_device");

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
    if (!m_appContext || !m_appContext->motionManager) {
        errorMessage = "TeachLocalPoint: Motion manager not available";
        return false;
    }

    const std::string& uid = uidIt->second;
    const std::string& coordSystemUid = coordSystemUidIt->second;
    const std::string& motionDevice = motionDeviceIt->second;

    // Check if local point UID already exists (will overwrite)
    {
        TaughtLocalPoint existingPoint;
        if (LoadLocalPointFromDisk(uid, existingPoint)) {
            std::cout << "[CoordAlign] TeachLocalPoint: Overwriting existing local point '" << uid << "'" << std::endl;
            if (m_logger) {
                m_logger->Info("TeachLocalPoint: Overwriting existing local point '" + uid + "'", "CoordAlign");
            }
        }
    }

    // Load taught coordinate system from file (real-time read)
    TaughtCoordinateSystem taught;
    if (!LoadTaughtSystemFromDisk(coordSystemUid, taught)) {
        errorMessage = "TeachLocalPoint: Coordinate system '" + coordSystemUid + "' not found in file";
        return false;
    }

    // Prompt user to move gantry to desired position
    std::cout << "[CoordAlign] TeachLocalPoint: Please move " << motionDevice
              << " to center the camera on the desired point, then press Enter..." << std::endl;

    // Use SystemCommandHandler's prompt mechanism if available, otherwise just proceed
    // For now, we'll just capture the current position (assumes user has positioned it)

    // Get current device position
    PositionStruct currentPos = m_appContext->motionManager->GetCurrentPosition(motionDevice);
    double currentX = currentPos.x;
    double currentY = currentPos.y;
    double currentZ = currentPos.z;

    // We need the world position at camera center. For simplicity, we'll use the device position
    // as a proxy for world position. In a full implementation, you would run vision here.
    // For now, the user should have already run a vision step to get the world position.
    // Let's check for a "world_x" and "world_y" parameter or context key.

    double worldX = currentX;
    double worldY = currentY;

    auto worldXIt = params.find("world_x");
    auto worldYIt = params.find("world_y");

    // Check if explicit world coordinates were provided (non-empty)
    bool hasWorldX = (worldXIt != params.end() && !worldXIt->second.empty());
    bool hasWorldY = (worldYIt != params.end() && !worldYIt->second.empty());

    if (hasWorldX && hasWorldY) {
        worldX = ResolveNumericValue(worldXIt->second, context);
        worldY = ResolveNumericValue(worldYIt->second, context);
    }
    // else: worldX/worldY already set to device position (currentX/currentY)

    // Convert world position to local coordinates
    double localX, localY;
    WorldToLocal(
        worldX, worldY,
        taught.originX_mm, taught.originY_mm,
        taught.xAxisX, taught.xAxisY,
        taught.yAxisX, taught.yAxisY,
        localX, localY);

    // Create and store the local point
    TaughtLocalPoint localPoint;
    localPoint.uid = uid;
    localPoint.coordSystemUid = coordSystemUid;
    localPoint.localX_mm = localX;
    localPoint.localY_mm = localY;
    localPoint.taughtWorldX_mm = worldX;
    localPoint.taughtWorldY_mm = worldY;
    localPoint.deviceX_mm = currentX;
    localPoint.deviceY_mm = currentY;
    localPoint.deviceZ_mm = currentZ;
    localPoint.createdAt = GetCurrentTimestamp();
    localPoint.valid = true;

    // Optional description
    auto descIt = params.find("description");
    if (descIt != params.end()) {
        localPoint.description = descIt->second;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_localPoints[uid] = localPoint;
    }

    // Save to disk for persistence
    SaveLocalPointToDisk(localPoint);

    // Log success
    std::ostringstream msg;
    msg << "Taught local point '" << uid << "'\n"
        << "  Coordinate system: " << coordSystemUid << "\n"
        << "  World position: (" << worldX << ", " << worldY << ")\n"
        << "  Local position: (" << localX << ", " << localY << ")\n"
        << "  Device position: (" << currentX << ", " << currentY << ", " << currentZ << ")";

    if (m_logger) {
        m_logger->Info(msg.str(), "CoordAlign");
    }
    std::cout << "[CoordAlign] " << msg.str() << std::endl;

    // Store in context
    context->SetValue(uid + ".local_x", static_cast<float>(localX), "coordalign", "TeachLocalPoint");
    context->SetValue(uid + ".local_y", static_cast<float>(localY), "coordalign", "TeachLocalPoint");
    context->SetValue(uid + ".world_x", static_cast<float>(worldX), "coordalign", "TeachLocalPoint");
    context->SetValue(uid + ".world_y", static_cast<float>(worldY), "coordalign", "TeachLocalPoint");
    context->SetValue(uid + ".coord_system", coordSystemUid, "coordalign", "TeachLocalPoint");

    return true;
}

bool CoordinateAlignCommandHandler::HandleTransformLocalPoint(
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context)
{
    auto pointUidIt = params.find("point_uid");
    auto liveUidIt = params.find("live_uid");
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

    const std::string& pointUid = pointUidIt->second;
    const std::string& liveUid = liveUidIt->second;
    const std::string& outputPrefix = outputPrefixIt->second;

    // Load local point from file (real-time read)
    TaughtLocalPoint localPoint;
    if (!LoadLocalPointFromDisk(pointUid, localPoint)) {
        errorMessage = "TransformLocalPoint: Local point '" + pointUid + "' not found in file";
        return false;
    }

    // Find the live coordinate system (try memory first, then disk)
    LiveCoordinateSystem live;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_liveSystems.find(liveUid);
        if (it != m_liveSystems.end()) {
            live = it->second;
        }
    }

    // If not in memory, try loading from disk
    if (live.uid.empty()) {
        if (!LoadLiveSystemFromDisk(liveUid, live)) {
            errorMessage = "TransformLocalPoint: Live system '" + liveUid + "' not found in memory or file";
            return false;
        }
    }

    if (!live.transformValid) {
        errorMessage = "TransformLocalPoint: Live system '" + liveUid + "' has no valid transformation";
        return false;
    }

    // Convert local coordinates to live world coordinates
    double outputX, outputY;
    LocalToWorld(
        localPoint.localX_mm, localPoint.localY_mm,
        live.originX_mm, live.originY_mm,
        live.xAxisX, live.xAxisY,
        live.yAxisX, live.yAxisY,
        outputX, outputY);

    // Store results
    context->SetValue(outputPrefix + ".x", static_cast<float>(outputX), "coordalign", "TransformLocalPoint");
    context->SetValue(outputPrefix + ".y", static_cast<float>(outputY), "coordalign", "TransformLocalPoint");
    context->SetValue(outputPrefix + ".point_uid", pointUid, "coordalign", "TransformLocalPoint");
    context->SetValue(outputPrefix + ".local_x", static_cast<float>(localPoint.localX_mm), "coordalign", "TransformLocalPoint");
    context->SetValue(outputPrefix + ".local_y", static_cast<float>(localPoint.localY_mm), "coordalign", "TransformLocalPoint");

    std::cout << "[CoordAlign] TransformLocalPoint: '" << pointUid << "' local("
              << localPoint.localX_mm << ", " << localPoint.localY_mm
              << ") -> world(" << outputX << ", " << outputY << ")" << std::endl;

    if (m_logger) {
        std::ostringstream msg;
        msg << "TransformLocalPoint: '" << pointUid << "' local("
            << localPoint.localX_mm << ", " << localPoint.localY_mm
            << ") -> world(" << outputX << ", " << outputY << ")";
        m_logger->Info(msg.str(), "CoordAlign");
    }

    return true;
}

bool CoordinateAlignCommandHandler::HandleMoveToLocalXY(
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context)
{
    // Required params: local_x, local_y, coord_system_uid, motion_device
    // coord_system_uid can be either live or taught - we try live first, then taught
    auto localXIt = params.find("local_x");
    auto localYIt = params.find("local_y");
    auto coordUidIt = params.find("coord_system_uid");
    auto deviceIt = params.find("motion_device");

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

    if (!m_appContext || !m_appContext->motionManager) {
        errorMessage = "MoveToLocalXY: Motion manager not available";
        return false;
    }

    // Parse local coordinates (support context variables)
    double localX = ResolveNumericValue(localXIt->second, context);
    double localY = ResolveNumericValue(localYIt->second, context);
    const std::string& coordUid = coordUidIt->second;
    const std::string& motionDevice = deviceIt->second;

    // Try to load coordinate system (live first, then taught)
    double originX = 0.0, originY = 0.0;
    double xAxisX = 1.0, xAxisY = 0.0;
    double yAxisX = 0.0, yAxisY = 1.0;
    bool coordSystemFound = false;

    // Try live system first
    LiveCoordinateSystem live;
    if (LoadLiveSystemFromDisk(coordUid, live) && live.transformValid) {
        originX = live.originX_mm;
        originY = live.originY_mm;
        xAxisX = live.xAxisX;
        xAxisY = live.xAxisY;
        yAxisX = live.yAxisX;
        yAxisY = live.yAxisY;
        coordSystemFound = true;

        std::cout << "[CoordAlign] MoveToLocalXY: Using live coordinate system '" << coordUid << "'" << std::endl;
    }

    // If no live system, try taught system
    if (!coordSystemFound) {
        TaughtCoordinateSystem taught;
        if (LoadTaughtSystemFromDisk(coordUid, taught)) {
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

    // Transform local to world
    double worldX, worldY;
    LocalToWorld(
        localX, localY,
        originX, originY,
        xAxisX, xAxisY,
        yAxisX, yAxisY,
        worldX, worldY);

    // Get current Z position
    double currentZ = 0.0;
    m_appContext->motionManager->GetPosition(motionDevice, "Z", currentZ);

    std::cout << "[CoordAlign] MoveToLocalXY: " << motionDevice
              << " local(" << localX << ", " << localY
              << ") -> world(" << worldX << ", " << worldY
              << "), keeping Z=" << currentZ << std::endl;

    if (m_logger) {
        std::ostringstream msg;
        msg << "MoveToLocalXY: " << motionDevice
            << " local(" << localX << ", " << localY
            << ") -> world(" << worldX << ", " << worldY
            << "), Z=" << currentZ;
        m_logger->Info(msg.str(), "CoordAlign");
    }

    // Move to world position (X, Y, current Z)
    bool moveOk = m_appContext->motionManager->MoveToAbs(
        motionDevice, worldX, worldY, currentZ, 100.0, true);  // velocity=100, blocking=true

    if (!moveOk) {
        errorMessage = "MoveToLocalXY: Move failed for device '" + motionDevice + "'";
        return false;
    }

    // Store results in context if requested
    auto outputPrefixIt = params.find("output_prefix");
    if (outputPrefixIt != params.end() && context) {
        const std::string& prefix = outputPrefixIt->second;
        context->SetValue(prefix + ".world_x", static_cast<float>(worldX), "coordalign", "MoveToLocalXY");
        context->SetValue(prefix + ".world_y", static_cast<float>(worldY), "coordalign", "MoveToLocalXY");
        context->SetValue(prefix + ".z", static_cast<float>(currentZ), "coordalign", "MoveToLocalXY");
    }

    return true;
}

bool CoordinateAlignCommandHandler::HandleListLocalPoints(
    std::string& errorMessage,
    SequenceContext* context)
{
    (void)errorMessage;

    // List local points from files
    std::vector<TaughtLocalPoint> pointList;
    auto pointsDir = GetLocalPointsDir();
    if (std::filesystem::exists(pointsDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(pointsDir)) {
            if (entry.path().extension() == ".json") {
                std::string uid = entry.path().stem().string();
                TaughtLocalPoint pt;
                if (LoadLocalPointFromDisk(uid, pt)) {
                    pointList.push_back(pt);
                }
            }
        }
    }

    std::ostringstream msg;
    msg << "Local Points (" << pointList.size() << "):\n";
    for (const auto& pt : pointList) {
        msg << "  - " << pt.uid << " (coord_system: " << pt.coordSystemUid
            << ", local: (" << pt.localX_mm << ", " << pt.localY_mm << ")"
            << ", created: " << pt.createdAt << ")\n";
    }

    if (m_logger) {
        m_logger->Info(msg.str(), "CoordAlign");
    }
    std::cout << "[CoordAlign] " << msg.str() << std::endl;

    if (context) {
        context->SetValue("coordalign.local_points_count", static_cast<float>(pointList.size()), "coordalign", "ListLocalPoints");
    }

    return true;
}

bool CoordinateAlignCommandHandler::HandleClearLocalPoint(
    const std::map<std::string, std::string>& params,
    std::string& errorMessage)
{
    auto uidIt = params.find("uid");
    if (uidIt == params.end()) {
        errorMessage = "ClearLocalPoint: Missing required parameter 'uid'";
        return false;
    }

    const std::string& uid = uidIt->second;

    // Delete from disk (file-based storage)
    if (!DeleteLocalPointFromDisk(uid)) {
        errorMessage = "ClearLocalPoint: Local point '" + uid + "' not found";
        return false;
    }

    // Also remove from memory cache if present
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_localPoints.erase(uid);
    }

    if (m_logger) {
        m_logger->Info("ClearLocalPoint: Removed '" + uid + "'", "CoordAlign");
    }
    std::cout << "[CoordAlign] Removed local point '" << uid << "'" << std::endl;

    return true;
}

bool CoordinateAlignCommandHandler::ExecuteVisionAtPosition(
    const std::string& motionDevice,
    const std::string& cameraName,
    const std::string& visionProfilePath,
    double deviceX, double deviceY, double deviceZ,
    const std::string& captureAs,
    std::string& errorMessage,
    SequenceContext* context)
{
    if (!m_appContext) {
        errorMessage = "Application context not available";
        return false;
    }

    if (!m_appContext->motionManager) {
        errorMessage = "Motion manager not available";
        return false;
    }

    // Get current position BEFORE move for debugging
    PositionStruct posBefore = m_appContext->motionManager->GetCurrentPosition(motionDevice);
    std::cout << "[DEBUG] [CoordAlign] ExecuteVisionAtPosition START" << std::endl;
    std::cout << "[DEBUG] [CoordAlign]   Device: " << motionDevice << std::endl;
    std::cout << "[DEBUG] [CoordAlign]   Current position BEFORE move: ("
              << posBefore.x << ", " << posBefore.y << ", " << posBefore.z << ")" << std::endl;
    std::cout << "[DEBUG] [CoordAlign]   Target position: ("
              << deviceX << ", " << deviceY << ", " << deviceZ << ")" << std::endl;

    // Calculate distance to target
    double dx = deviceX - posBefore.x;
    double dy = deviceY - posBefore.y;
    double dz = deviceZ - posBefore.z;
    double distanceToTarget = std::sqrt(dx*dx + dy*dy + dz*dz);
    std::cout << "[DEBUG] [CoordAlign]   Distance to target: " << distanceToTarget << " mm" << std::endl;

    // 1. Move to device position (blocking)
    if (m_logger) {
        std::ostringstream msg;
        msg << "Moving " << motionDevice << " to (" << deviceX << ", " << deviceY << ", " << deviceZ << ")";
        m_logger->Info(msg.str(), "CoordAlign");
    }

    auto moveStartTime = std::chrono::steady_clock::now();
    std::cout << "[DEBUG] [CoordAlign] Calling MoveToAbs with blocking=true..." << std::endl;

    bool moveOk = m_appContext->motionManager->MoveToAbs(
        motionDevice, deviceX, deviceY, deviceZ, 100.0, true);  // velocity=100, blocking=true

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

    // Verify position AFTER move
    PositionStruct posAfter = m_appContext->motionManager->GetCurrentPosition(motionDevice);
    double dxAfter = deviceX - posAfter.x;
    double dyAfter = deviceY - posAfter.y;
    double dzAfter = deviceZ - posAfter.z;
    double distanceAfterMove = std::sqrt(dxAfter*dxAfter + dyAfter*dyAfter + dzAfter*dzAfter);

    std::cout << "[DEBUG] [CoordAlign]   Position AFTER move: ("
              << posAfter.x << ", " << posAfter.y << ", " << posAfter.z << ")" << std::endl;
    std::cout << "[DEBUG] [CoordAlign]   Distance from target after move: " << distanceAfterMove << " mm" << std::endl;

    // Check if we're actually at target (within tolerance)
    const double positionTolerance = 0.5;  // 0.5mm tolerance
    if (distanceAfterMove > positionTolerance) {
        std::cout << "[WARNING] [CoordAlign] Motion returned success but device is "
                  << distanceAfterMove << " mm from target (tolerance: "
                  << positionTolerance << " mm)" << std::endl;
        std::cout << "[WARNING] [CoordAlign] This may indicate premature motion completion!" << std::endl;
    }

    std::cout << "[DEBUG] [CoordAlign] Starting vision execution for capture_as='" << captureAs << "'" << std::endl;

    // 2. Execute vision profile using VisionCommandHandler directly
#ifdef HAS_OPENCV
    VisionCommandHandler visionHandler;
    visionHandler.SetContext(m_appContext);
    visionHandler.SetLogger(m_logger);

    std::map<std::string, std::string> visionParams;
    visionParams["profile_path"] = visionProfilePath;
    visionParams["camera"] = cameraName;  // Note: VisionCommandHandler expects "camera", not "camera_name"
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

    // VisionCommandHandler always returns true, so check context for actual result
    // First check vision.result.success (base key before capture_as copy)
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

    // Verify captured results exist with the prefix
    std::string worldXKey = captureAs + ".detected.world_x_mm";
    std::string worldYKey = captureAs + ".detected.world_y_mm";

    std::cout << "[DEBUG] [CoordAlign] Checking for vision results with prefix: " << captureAs << std::endl;
    std::cout << "[DEBUG] [CoordAlign]   Looking for key: " << worldXKey << std::endl;
    std::cout << "[DEBUG] [CoordAlign]   HasValue(worldXKey) = " << (context->HasValue(worldXKey) ? "true" : "false") << std::endl;
    std::cout << "[DEBUG] [CoordAlign]   HasValue(worldYKey) = " << (context->HasValue(worldYKey) ? "true" : "false") << std::endl;

    // Debug: List all keys that start with the capture_as prefix
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

        // Also check for 'vision.result' keys
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

bool CoordinateAlignCommandHandler::ReadFiducialFromContext(
    const std::string& prefix,
    FiducialPoint& outFiducial,
    std::string& errorMessage,
    SequenceContext* context)
{
    if (!context) {
        errorMessage = "Context is null";
        return false;
    }

    // Keys for world position (from vision detection)
    std::string worldXKey = prefix + ".detected.world_x_mm";
    std::string worldYKey = prefix + ".detected.world_y_mm";
    std::string deviceXKey = prefix + ".detected.device_x_mm";
    std::string deviceYKey = prefix + ".detected.device_y_mm";
    std::string deviceZKey = prefix + ".detected.device_z_mm";

    if (!context->HasValue(worldXKey) || !context->HasValue(worldYKey)) {
        errorMessage = "Missing world position for prefix: " + prefix +
                       " (expected keys: " + worldXKey + ", " + worldYKey + ")";
        return false;
    }

    outFiducial.worldX_mm = context->GetNumeric(worldXKey, 0.0f);
    outFiducial.worldY_mm = context->GetNumeric(worldYKey, 0.0f);
    outFiducial.deviceX_mm = context->GetNumeric(deviceXKey, 0.0f);
    outFiducial.deviceY_mm = context->GetNumeric(deviceYKey, 0.0f);
    outFiducial.deviceZ_mm = context->GetNumeric(deviceZKey, 0.0f);
    outFiducial.contextPrefix = prefix;
    outFiducial.timestamp = GetCurrentTimestamp();
    outFiducial.valid = true;

    return true;
}

bool CoordinateAlignCommandHandler::ComputeBestFitTransform(
    const std::array<std::pair<double, double>, 3>& taught,
    const std::array<std::pair<double, double>, 3>& live,
    double& rotation,
    double& translateX,
    double& translateY,
    double& scale,
    double& rmsError,
    double& maxError)
{
    // Step 1: Compute centroids
    double taughtCx = (taught[0].first + taught[1].first + taught[2].first) / 3.0;
    double taughtCy = (taught[0].second + taught[1].second + taught[2].second) / 3.0;
    double liveCx = (live[0].first + live[1].first + live[2].first) / 3.0;
    double liveCy = (live[0].second + live[1].second + live[2].second) / 3.0;

    // Step 2: Center the point sets
    std::array<std::pair<double, double>, 3> taughtCentered;
    std::array<std::pair<double, double>, 3> liveCentered;
    for (int i = 0; i < 3; i++) {
        taughtCentered[i] = {taught[i].first - taughtCx, taught[i].second - taughtCy};
        liveCentered[i] = {live[i].first - liveCx, live[i].second - liveCy};
    }

    // Step 3: Compute cross-covariance matrix H (2x2)
    double H[2][2] = {{0, 0}, {0, 0}};
    for (int i = 0; i < 3; i++) {
        H[0][0] += taughtCentered[i].first * liveCentered[i].first;
        H[0][1] += taughtCentered[i].first * liveCentered[i].second;
        H[1][0] += taughtCentered[i].second * liveCentered[i].first;
        H[1][1] += taughtCentered[i].second * liveCentered[i].second;
    }

    // Step 4: Compute rotation from H
    // For 2D, rotation = atan2(H[1][0] - H[0][1], H[0][0] + H[1][1])
    rotation = std::atan2(H[1][0] - H[0][1], H[0][0] + H[1][1]);

    // Step 5: Compute scale
    double taughtNorm = 0, liveNorm = 0;
    for (int i = 0; i < 3; i++) {
        taughtNorm += taughtCentered[i].first * taughtCentered[i].first +
                      taughtCentered[i].second * taughtCentered[i].second;
        liveNorm += liveCentered[i].first * liveCentered[i].first +
                    liveCentered[i].second * liveCentered[i].second;
    }
    scale = (taughtNorm > 1e-10) ? std::sqrt(liveNorm / taughtNorm) : 1.0;

    // Step 6: Compute translation
    double cosR = std::cos(rotation);
    double sinR = std::sin(rotation);
    double rotatedTaughtCx = scale * (cosR * taughtCx - sinR * taughtCy);
    double rotatedTaughtCy = scale * (sinR * taughtCx + cosR * taughtCy);
    translateX = liveCx - rotatedTaughtCx;
    translateY = liveCy - rotatedTaughtCy;

    // Step 7: Compute RMS and max error
    double sumSqError = 0;
    maxError = 0;
    for (int i = 0; i < 3; i++) {
        // Transform taught point
        double transformedX = scale * (cosR * taught[i].first - sinR * taught[i].second) + translateX;
        double transformedY = scale * (sinR * taught[i].first + cosR * taught[i].second) + translateY;

        // Error
        double errX = transformedX - live[i].first;
        double errY = transformedY - live[i].second;
        double err = std::sqrt(errX * errX + errY * errY);
        sumSqError += err * err;
        if (err > maxError) {
            maxError = err;
        }
    }
    rmsError = std::sqrt(sumSqError / 3.0);

    return true;
}

void CoordinateAlignCommandHandler::TransformPointInternal(
    double inputX, double inputY,
    double rotation, double translateX, double translateY, double scale,
    double& outputX, double& outputY) const
{
    double cosR = std::cos(rotation);
    double sinR = std::sin(rotation);

    // Apply transformation: T(p) = scale * R * p + t
    outputX = scale * (cosR * inputX - sinR * inputY) + translateX;
    outputY = scale * (sinR * inputX + cosR * inputY) + translateY;
}

double CoordinateAlignCommandHandler::ResolveNumericValue(
    const std::string& valueStr, SequenceContext* context) const
{
    if (valueStr.empty()) {
        return 0.0;
    }

    // Check if it's a context reference (starts with $ or "context:")
    if (valueStr[0] == '$') {
        std::string key = valueStr.substr(1);
        if (context) {
            return static_cast<double>(context->GetNumeric(key, 0.0f));
        }
        return 0.0;
    }
    if (valueStr.find("context:") == 0) {
        std::string key = valueStr.substr(8);
        if (context) {
            return static_cast<double>(context->GetNumeric(key, 0.0f));
        }
        return 0.0;
    }

    // Parse as numeric value
    try {
        return std::stod(valueStr);
    } catch (...) {
        return 0.0;
    }
}

std::string CoordinateAlignCommandHandler::GetCurrentTimestamp() const
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time);
#else
    localtime_r(&time, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

double CoordinateAlignCommandHandler::CalculateDistance(
    double x1, double y1, double x2, double y2) const
{
    double dx = x2 - x1;
    double dy = y2 - y1;
    return std::sqrt(dx * dx + dy * dy);
}

bool CoordinateAlignCommandHandler::ArePointsCollinear(
    double x1, double y1,
    double x2, double y2,
    double x3, double y3,
    double threshold_mm2) const
{
    // Calculate area of triangle formed by the three points
    // Area = 0.5 * |x1(y2 - y3) + x2(y3 - y1) + x3(y1 - y2)|
    double area = std::abs(x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2)) / 2.0;
    return area < threshold_mm2;
}

void CoordinateAlignCommandHandler::ComputeCoordinateAxes(
    double fid1X, double fid1Y,
    double fid2X, double fid2Y,
    double& originX, double& originY,
    double& xAxisX, double& xAxisY,
    double& yAxisX, double& yAxisY) const
{
    // Origin is at Fid1
    originX = fid1X;
    originY = fid1Y;

    // X-axis is unit vector from Fid1 to Fid2
    double dx = fid2X - fid1X;
    double dy = fid2Y - fid1Y;
    double len = std::sqrt(dx * dx + dy * dy);

    if (len < 1e-10) {
        // Degenerate case: Fid1 and Fid2 are same point
        xAxisX = 1.0;
        xAxisY = 0.0;
        yAxisX = 0.0;
        yAxisY = 1.0;
        return;
    }

    xAxisX = dx / len;
    xAxisY = dy / len;

    // Y-axis is perpendicular to X-axis (counter-clockwise 90 degrees)
    // Rotate (xAxisX, xAxisY) by 90 degrees: (x, y) -> (-y, x)
    yAxisX = -xAxisY;
    yAxisY = xAxisX;
}

void CoordinateAlignCommandHandler::WorldToLocal(
    double worldX, double worldY,
    double originX, double originY,
    double xAxisX, double xAxisY,
    double yAxisX, double yAxisY,
    double& localX, double& localY) const
{
    // Translate to origin
    double dx = worldX - originX;
    double dy = worldY - originY;

    // Project onto axes (dot product)
    // The axes are orthonormal, so the inverse transform is just the transpose
    localX = dx * xAxisX + dy * xAxisY;
    localY = dx * yAxisX + dy * yAxisY;
}

void CoordinateAlignCommandHandler::LocalToWorld(
    double localX, double localY,
    double originX, double originY,
    double xAxisX, double xAxisY,
    double yAxisX, double yAxisY,
    double& worldX, double& worldY) const
{
    // Combine axis vectors with local coordinates and add origin
    // world = origin + localX * xAxis + localY * yAxis
    worldX = originX + localX * xAxisX + localY * yAxisX;
    worldY = originY + localX * xAxisY + localY * yAxisY;
}

// =============================================================================
// Persistence Implementation
// =============================================================================

void CoordinateAlignCommandHandler::SetConfigPath(const std::string& configPath)
{
    m_configPath = configPath;
    // Ensure trailing slash
    if (!m_configPath.empty() && m_configPath.back() != '/' && m_configPath.back() != '\\') {
        m_configPath += "/";
    }
}

std::filesystem::path CoordinateAlignCommandHandler::GetTaughtSystemsDir() const
{
    return std::filesystem::path(m_configPath) / "coordalign" / "taught_systems";
}

std::filesystem::path CoordinateAlignCommandHandler::GetLocalPointsDir() const
{
    return std::filesystem::path(m_configPath) / "coordalign" / "local_points";
}

void CoordinateAlignCommandHandler::LoadAllFromDisk()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Load taught systems
    auto taughtDir = GetTaughtSystemsDir();
    if (std::filesystem::exists(taughtDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(taughtDir)) {
            if (entry.path().extension() == ".json") {
                std::string uid = entry.path().stem().string();
                TaughtCoordinateSystem system;
                if (LoadTaughtSystemFromDisk(uid, system)) {
                    m_taughtSystems[uid] = system;
                    std::cout << "[CoordAlign] Loaded taught system: " << uid << std::endl;
                }
            }
        }
    }

    // Load local points
    auto pointsDir = GetLocalPointsDir();
    if (std::filesystem::exists(pointsDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(pointsDir)) {
            if (entry.path().extension() == ".json") {
                std::string uid = entry.path().stem().string();
                TaughtLocalPoint point;
                if (LoadLocalPointFromDisk(uid, point)) {
                    m_localPoints[uid] = point;
                    std::cout << "[CoordAlign] Loaded local point: " << uid << std::endl;
                }
            }
        }
    }

    std::cout << "[CoordAlign] Loaded " << m_taughtSystems.size() << " taught systems, "
              << m_localPoints.size() << " local points from disk" << std::endl;
}

bool CoordinateAlignCommandHandler::SaveTaughtSystemToDisk(const TaughtCoordinateSystem& system)
{
    try {
        auto dir = GetTaughtSystemsDir();
        std::filesystem::create_directories(dir);

        auto filePath = dir / (system.uid + ".json");

        json j;
        j["uid"] = system.uid;
        j["motionDevice"] = system.motionDevice;
        j["cameraName"] = system.cameraName;
        j["createdAt"] = system.createdAt;
        j["description"] = system.description;

        // Fiducial 1
        j["fid1"]["worldX_mm"] = system.fid1.worldX_mm;
        j["fid1"]["worldY_mm"] = system.fid1.worldY_mm;
        j["fid1"]["deviceX_mm"] = system.fid1.deviceX_mm;
        j["fid1"]["deviceY_mm"] = system.fid1.deviceY_mm;
        j["fid1"]["deviceZ_mm"] = system.fid1.deviceZ_mm;
        j["fid1"]["visionProfilePath"] = system.fid1.visionProfilePath;
        j["fid1"]["contextPrefix"] = system.fid1.contextPrefix;

        // Fiducial 2
        j["fid2"]["worldX_mm"] = system.fid2.worldX_mm;
        j["fid2"]["worldY_mm"] = system.fid2.worldY_mm;
        j["fid2"]["deviceX_mm"] = system.fid2.deviceX_mm;
        j["fid2"]["deviceY_mm"] = system.fid2.deviceY_mm;
        j["fid2"]["deviceZ_mm"] = system.fid2.deviceZ_mm;
        j["fid2"]["visionProfilePath"] = system.fid2.visionProfilePath;
        j["fid2"]["contextPrefix"] = system.fid2.contextPrefix;

        // Fiducial 3
        j["fid3"]["worldX_mm"] = system.fid3.worldX_mm;
        j["fid3"]["worldY_mm"] = system.fid3.worldY_mm;
        j["fid3"]["deviceX_mm"] = system.fid3.deviceX_mm;
        j["fid3"]["deviceY_mm"] = system.fid3.deviceY_mm;
        j["fid3"]["deviceZ_mm"] = system.fid3.deviceZ_mm;
        j["fid3"]["visionProfilePath"] = system.fid3.visionProfilePath;
        j["fid3"]["contextPrefix"] = system.fid3.contextPrefix;

        // Reference distances
        j["refDistance12_mm"] = system.refDistance12_mm;
        j["refDistance23_mm"] = system.refDistance23_mm;
        j["refDistance31_mm"] = system.refDistance31_mm;

        // Coordinate axes
        j["originX_mm"] = system.originX_mm;
        j["originY_mm"] = system.originY_mm;
        j["xAxisX"] = system.xAxisX;
        j["xAxisY"] = system.xAxisY;
        j["yAxisX"] = system.yAxisX;
        j["yAxisY"] = system.yAxisY;

        std::ofstream file(filePath);
        file << j.dump(2);
        file.close();

        std::cout << "[CoordAlign] Saved taught system to: " << filePath.string() << std::endl;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[CoordAlign] Error saving taught system: " << e.what() << std::endl;
        return false;
    }
}

bool CoordinateAlignCommandHandler::LoadTaughtSystemFromDisk(const std::string& uid, TaughtCoordinateSystem& outSystem)
{
    try {
        auto filePath = GetTaughtSystemsDir() / (uid + ".json");
        if (!std::filesystem::exists(filePath)) {
            return false;
        }

        std::ifstream file(filePath);
        json j = json::parse(file);
        file.close();

        outSystem.uid = j.value("uid", uid);
        outSystem.motionDevice = j.value("motionDevice", "");
        outSystem.cameraName = j.value("cameraName", "");
        outSystem.createdAt = j.value("createdAt", "");
        outSystem.description = j.value("description", "");

        // Fiducial 1
        if (j.contains("fid1")) {
            outSystem.fid1.worldX_mm = j["fid1"].value("worldX_mm", 0.0);
            outSystem.fid1.worldY_mm = j["fid1"].value("worldY_mm", 0.0);
            outSystem.fid1.deviceX_mm = j["fid1"].value("deviceX_mm", 0.0);
            outSystem.fid1.deviceY_mm = j["fid1"].value("deviceY_mm", 0.0);
            outSystem.fid1.deviceZ_mm = j["fid1"].value("deviceZ_mm", 0.0);
            outSystem.fid1.visionProfilePath = j["fid1"].value("visionProfilePath", "");
            outSystem.fid1.contextPrefix = j["fid1"].value("contextPrefix", "");
            outSystem.fid1.valid = true;
        }

        // Fiducial 2
        if (j.contains("fid2")) {
            outSystem.fid2.worldX_mm = j["fid2"].value("worldX_mm", 0.0);
            outSystem.fid2.worldY_mm = j["fid2"].value("worldY_mm", 0.0);
            outSystem.fid2.deviceX_mm = j["fid2"].value("deviceX_mm", 0.0);
            outSystem.fid2.deviceY_mm = j["fid2"].value("deviceY_mm", 0.0);
            outSystem.fid2.deviceZ_mm = j["fid2"].value("deviceZ_mm", 0.0);
            outSystem.fid2.visionProfilePath = j["fid2"].value("visionProfilePath", "");
            outSystem.fid2.contextPrefix = j["fid2"].value("contextPrefix", "");
            outSystem.fid2.valid = true;
        }

        // Fiducial 3
        if (j.contains("fid3")) {
            outSystem.fid3.worldX_mm = j["fid3"].value("worldX_mm", 0.0);
            outSystem.fid3.worldY_mm = j["fid3"].value("worldY_mm", 0.0);
            outSystem.fid3.deviceX_mm = j["fid3"].value("deviceX_mm", 0.0);
            outSystem.fid3.deviceY_mm = j["fid3"].value("deviceY_mm", 0.0);
            outSystem.fid3.deviceZ_mm = j["fid3"].value("deviceZ_mm", 0.0);
            outSystem.fid3.visionProfilePath = j["fid3"].value("visionProfilePath", "");
            outSystem.fid3.contextPrefix = j["fid3"].value("contextPrefix", "");
            outSystem.fid3.valid = true;
        }

        // Reference distances
        outSystem.refDistance12_mm = j.value("refDistance12_mm", 0.0);
        outSystem.refDistance23_mm = j.value("refDistance23_mm", 0.0);
        outSystem.refDistance31_mm = j.value("refDistance31_mm", 0.0);

        // Coordinate axes
        outSystem.originX_mm = j.value("originX_mm", 0.0);
        outSystem.originY_mm = j.value("originY_mm", 0.0);
        outSystem.xAxisX = j.value("xAxisX", 1.0);
        outSystem.xAxisY = j.value("xAxisY", 0.0);
        outSystem.yAxisX = j.value("yAxisX", 0.0);
        outSystem.yAxisY = j.value("yAxisY", 1.0);

        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[CoordAlign] Error loading taught system '" << uid << "': " << e.what() << std::endl;
        return false;
    }
}

bool CoordinateAlignCommandHandler::SaveLocalPointToDisk(const TaughtLocalPoint& point)
{
    try {
        auto dir = GetLocalPointsDir();
        std::filesystem::create_directories(dir);

        auto filePath = dir / (point.uid + ".json");

        json j;
        j["uid"] = point.uid;
        j["coordSystemUid"] = point.coordSystemUid;
        j["localX_mm"] = point.localX_mm;
        j["localY_mm"] = point.localY_mm;
        j["taughtWorldX_mm"] = point.taughtWorldX_mm;
        j["taughtWorldY_mm"] = point.taughtWorldY_mm;
        j["deviceX_mm"] = point.deviceX_mm;
        j["deviceY_mm"] = point.deviceY_mm;
        j["deviceZ_mm"] = point.deviceZ_mm;
        j["description"] = point.description;
        j["createdAt"] = point.createdAt;

        std::ofstream file(filePath);
        file << j.dump(2);
        file.close();

        std::cout << "[CoordAlign] Saved local point to: " << filePath.string() << std::endl;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[CoordAlign] Error saving local point: " << e.what() << std::endl;
        return false;
    }
}

bool CoordinateAlignCommandHandler::LoadLocalPointFromDisk(const std::string& uid, TaughtLocalPoint& outPoint)
{
    try {
        auto filePath = GetLocalPointsDir() / (uid + ".json");
        if (!std::filesystem::exists(filePath)) {
            return false;
        }

        std::ifstream file(filePath);
        json j = json::parse(file);
        file.close();

        outPoint.uid = j.value("uid", uid);
        outPoint.coordSystemUid = j.value("coordSystemUid", "");
        outPoint.localX_mm = j.value("localX_mm", 0.0);
        outPoint.localY_mm = j.value("localY_mm", 0.0);
        outPoint.taughtWorldX_mm = j.value("taughtWorldX_mm", 0.0);
        outPoint.taughtWorldY_mm = j.value("taughtWorldY_mm", 0.0);
        outPoint.deviceX_mm = j.value("deviceX_mm", 0.0);
        outPoint.deviceY_mm = j.value("deviceY_mm", 0.0);
        outPoint.deviceZ_mm = j.value("deviceZ_mm", 0.0);
        outPoint.description = j.value("description", "");
        outPoint.createdAt = j.value("createdAt", "");
        outPoint.valid = true;

        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[CoordAlign] Error loading local point '" << uid << "': " << e.what() << std::endl;
        return false;
    }
}

bool CoordinateAlignCommandHandler::DeleteTaughtSystemFromDisk(const std::string& uid)
{
    try {
        auto filePath = GetTaughtSystemsDir() / (uid + ".json");
        if (std::filesystem::exists(filePath)) {
            std::filesystem::remove(filePath);
            std::cout << "[CoordAlign] Deleted taught system file: " << filePath.string() << std::endl;
            return true;
        }
        return false;
    }
    catch (const std::exception& e) {
        std::cerr << "[CoordAlign] Error deleting taught system file: " << e.what() << std::endl;
        return false;
    }
}

bool CoordinateAlignCommandHandler::DeleteLocalPointFromDisk(const std::string& uid)
{
    try {
        auto filePath = GetLocalPointsDir() / (uid + ".json");
        if (std::filesystem::exists(filePath)) {
            std::filesystem::remove(filePath);
            std::cout << "[CoordAlign] Deleted local point file: " << filePath.string() << std::endl;
            return true;
        }
        return false;
    }
    catch (const std::exception& e) {
        std::cerr << "[CoordAlign] Error deleting local point file: " << e.what() << std::endl;
        return false;
    }
}

std::filesystem::path CoordinateAlignCommandHandler::GetLiveSystemsDir() const
{
    return std::filesystem::path(m_configPath) / "coordalign" / "live_systems";
}

bool CoordinateAlignCommandHandler::SaveLiveSystemToDisk(const LiveCoordinateSystem& system)
{
    try {
        auto dir = GetLiveSystemsDir();
        std::filesystem::create_directories(dir);

        auto filePath = dir / (system.uid + ".json");

        json j;
        j["uid"] = system.uid;
        j["taughtUid"] = system.taughtUid;
        j["createdAt"] = system.createdAt;

        // Fiducial 1 (live positions)
        j["fid1"]["worldX_mm"] = system.fid1.worldX_mm;
        j["fid1"]["worldY_mm"] = system.fid1.worldY_mm;
        j["fid1"]["deviceX_mm"] = system.fid1.deviceX_mm;
        j["fid1"]["deviceY_mm"] = system.fid1.deviceY_mm;
        j["fid1"]["deviceZ_mm"] = system.fid1.deviceZ_mm;
        j["fid1"]["visionProfilePath"] = system.fid1.visionProfilePath;
        j["fid1"]["contextPrefix"] = system.fid1.contextPrefix;

        // Fiducial 2
        j["fid2"]["worldX_mm"] = system.fid2.worldX_mm;
        j["fid2"]["worldY_mm"] = system.fid2.worldY_mm;
        j["fid2"]["deviceX_mm"] = system.fid2.deviceX_mm;
        j["fid2"]["deviceY_mm"] = system.fid2.deviceY_mm;
        j["fid2"]["deviceZ_mm"] = system.fid2.deviceZ_mm;
        j["fid2"]["visionProfilePath"] = system.fid2.visionProfilePath;
        j["fid2"]["contextPrefix"] = system.fid2.contextPrefix;

        // Fiducial 3
        j["fid3"]["worldX_mm"] = system.fid3.worldX_mm;
        j["fid3"]["worldY_mm"] = system.fid3.worldY_mm;
        j["fid3"]["deviceX_mm"] = system.fid3.deviceX_mm;
        j["fid3"]["deviceY_mm"] = system.fid3.deviceY_mm;
        j["fid3"]["deviceZ_mm"] = system.fid3.deviceZ_mm;
        j["fid3"]["visionProfilePath"] = system.fid3.visionProfilePath;
        j["fid3"]["contextPrefix"] = system.fid3.contextPrefix;

        // Live coordinate axes
        j["originX_mm"] = system.originX_mm;
        j["originY_mm"] = system.originY_mm;
        j["xAxisX"] = system.xAxisX;
        j["xAxisY"] = system.xAxisY;
        j["yAxisX"] = system.yAxisX;
        j["yAxisY"] = system.yAxisY;

        // Transformation metrics
        j["rotationRad"] = system.rotationRad;
        j["rotationDeg"] = system.rotationDeg;
        j["scale"] = system.scale;
        j["rmsError_mm"] = system.rmsError_mm;
        j["maxError_mm"] = system.maxError_mm;
        j["transformValid"] = system.transformValid;

        std::ofstream file(filePath);
        file << j.dump(2);
        file.close();

        std::cout << "[CoordAlign] Saved live system to: " << filePath.string() << std::endl;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[CoordAlign] Error saving live system: " << e.what() << std::endl;
        return false;
    }
}

bool CoordinateAlignCommandHandler::LoadLiveSystemFromDisk(const std::string& uid, LiveCoordinateSystem& outSystem)
{
    try {
        auto filePath = GetLiveSystemsDir() / (uid + ".json");
        if (!std::filesystem::exists(filePath)) {
            return false;
        }

        std::ifstream file(filePath);
        json j = json::parse(file);
        file.close();

        outSystem.uid = j.value("uid", uid);
        outSystem.taughtUid = j.value("taughtUid", "");
        outSystem.createdAt = j.value("createdAt", "");

        // Fiducial 1
        if (j.contains("fid1")) {
            outSystem.fid1.worldX_mm = j["fid1"].value("worldX_mm", 0.0);
            outSystem.fid1.worldY_mm = j["fid1"].value("worldY_mm", 0.0);
            outSystem.fid1.deviceX_mm = j["fid1"].value("deviceX_mm", 0.0);
            outSystem.fid1.deviceY_mm = j["fid1"].value("deviceY_mm", 0.0);
            outSystem.fid1.deviceZ_mm = j["fid1"].value("deviceZ_mm", 0.0);
            outSystem.fid1.visionProfilePath = j["fid1"].value("visionProfilePath", "");
            outSystem.fid1.contextPrefix = j["fid1"].value("contextPrefix", "");
            outSystem.fid1.valid = true;
        }

        // Fiducial 2
        if (j.contains("fid2")) {
            outSystem.fid2.worldX_mm = j["fid2"].value("worldX_mm", 0.0);
            outSystem.fid2.worldY_mm = j["fid2"].value("worldY_mm", 0.0);
            outSystem.fid2.deviceX_mm = j["fid2"].value("deviceX_mm", 0.0);
            outSystem.fid2.deviceY_mm = j["fid2"].value("deviceY_mm", 0.0);
            outSystem.fid2.deviceZ_mm = j["fid2"].value("deviceZ_mm", 0.0);
            outSystem.fid2.visionProfilePath = j["fid2"].value("visionProfilePath", "");
            outSystem.fid2.contextPrefix = j["fid2"].value("contextPrefix", "");
            outSystem.fid2.valid = true;
        }

        // Fiducial 3
        if (j.contains("fid3")) {
            outSystem.fid3.worldX_mm = j["fid3"].value("worldX_mm", 0.0);
            outSystem.fid3.worldY_mm = j["fid3"].value("worldY_mm", 0.0);
            outSystem.fid3.deviceX_mm = j["fid3"].value("deviceX_mm", 0.0);
            outSystem.fid3.deviceY_mm = j["fid3"].value("deviceY_mm", 0.0);
            outSystem.fid3.deviceZ_mm = j["fid3"].value("deviceZ_mm", 0.0);
            outSystem.fid3.visionProfilePath = j["fid3"].value("visionProfilePath", "");
            outSystem.fid3.contextPrefix = j["fid3"].value("contextPrefix", "");
            outSystem.fid3.valid = true;
        }

        // Live coordinate axes
        outSystem.originX_mm = j.value("originX_mm", 0.0);
        outSystem.originY_mm = j.value("originY_mm", 0.0);
        outSystem.xAxisX = j.value("xAxisX", 1.0);
        outSystem.xAxisY = j.value("xAxisY", 0.0);
        outSystem.yAxisX = j.value("yAxisX", 0.0);
        outSystem.yAxisY = j.value("yAxisY", 1.0);

        // Transformation metrics
        outSystem.rotationRad = j.value("rotationRad", 0.0);
        outSystem.rotationDeg = j.value("rotationDeg", 0.0);
        outSystem.scale = j.value("scale", 1.0);
        outSystem.rmsError_mm = j.value("rmsError_mm", 0.0);
        outSystem.maxError_mm = j.value("maxError_mm", 0.0);
        outSystem.transformValid = j.value("transformValid", false);

        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[CoordAlign] Error loading live system: " << e.what() << std::endl;
        return false;
    }
}

bool CoordinateAlignCommandHandler::DeleteLiveSystemFromDisk(const std::string& uid)
{
    try {
        auto filePath = GetLiveSystemsDir() / (uid + ".json");
        if (std::filesystem::exists(filePath)) {
            std::filesystem::remove(filePath);
            std::cout << "[CoordAlign] Deleted live system file: " << filePath.string() << std::endl;
            return true;
        }
        return false;
    }
    catch (const std::exception& e) {
        std::cerr << "[CoordAlign] Error deleting live system file: " << e.what() << std::endl;
        return false;
    }
}
