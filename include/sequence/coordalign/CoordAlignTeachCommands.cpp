// CoordAlignTeachCommands.cpp — Family A: TaughtCoordinateSystem lifecycle.
// Branches: TeachCoordinateSystem, ClearCoordinateSystem, ListCoordinateSystems.
//
// State and persistence stay on the orchestrator behind three high-level
// state ops (StoreTaughtSystem, ClearCoordinateSystemEntries,
// ListCoordinateSystemsSnapshot) so the lock + map + disk-write semantics
// are encapsulated. Helper just does parameter parsing, fid reads, and
// coordinate-axis computation, then forwards.
#include "CoordAlignTeachCommands.h"

#include "../CoordinateAlignCommandHandler.h"
#include "ApplicationContext.h"
#include "LogManager.h"
#include "SequenceContext.h"
#include "CoordAlignMath.h"

#include <iostream>
#include <sstream>

std::optional<bool> CoordAlignTeachCommands::TryExecute(
    const std::string& /*deviceName*/,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const CoordAlignCommandDeps& deps)
{
  LogManager* logger = deps.app ? deps.app->logManager : nullptr;

  // ============================================================================
  // TEACH COORDINATE SYSTEM
  // ============================================================================
  if (methodName == "TeachCoordinateSystem") {
    auto uidIt          = params.find("uid");
    auto fid1PrefixIt   = params.find("fid1_prefix");
    auto fid2PrefixIt   = params.find("fid2_prefix");
    auto fid3PrefixIt   = params.find("fid3_prefix");
    auto fid1ProfileIt  = params.find("fid1_profile");
    auto fid2ProfileIt  = params.find("fid2_profile");
    auto fid3ProfileIt  = params.find("fid3_profile");
    auto motionDeviceIt = params.find("motion_device");
    auto cameraNameIt   = params.find("camera_name");

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

    // Optional process_uid for per-instance file naming (uid_<process_uid>).
    auto processUidIt = params.find("process_uid");
    std::string storageKey = uid;
    if (processUidIt != params.end() && !processUidIt->second.empty()) {
      storageKey = uid + "_" + processUidIt->second;
    }

    TaughtCoordinateSystem taught;
    taught.uid           = storageKey;
    taught.motionDevice  = motionDeviceIt->second;
    taught.cameraName    = cameraNameIt->second;
    taught.createdAt     = deps.parent->GetCurrentTimestamp();

    auto descIt = params.find("description");
    if (descIt != params.end()) {
      taught.description = descIt->second;
    }

    if (!deps.parent->ReadFiducialFromContext(fid1PrefixIt->second, taught.fid1, errorMessage, context)) {
      return false;
    }
    taught.fid1.visionProfilePath = fid1ProfileIt->second;

    if (!deps.parent->ReadFiducialFromContext(fid2PrefixIt->second, taught.fid2, errorMessage, context)) {
      return false;
    }
    taught.fid2.visionProfilePath = fid2ProfileIt->second;

    if (!deps.parent->ReadFiducialFromContext(fid3PrefixIt->second, taught.fid3, errorMessage, context)) {
      return false;
    }
    taught.fid3.visionProfilePath = fid3ProfileIt->second;

    if (coord_align::ArePointsCollinear(
            taught.fid1.worldX_mm, taught.fid1.worldY_mm,
            taught.fid2.worldX_mm, taught.fid2.worldY_mm,
            taught.fid3.worldX_mm, taught.fid3.worldY_mm)) {
      errorMessage = "TeachCoordinateSystem: Fiducial points are collinear, cannot create coordinate system";
      return false;
    }

    taught.refDistance12_mm = coord_align::CalculateDistance(
        taught.fid1.worldX_mm, taught.fid1.worldY_mm,
        taught.fid2.worldX_mm, taught.fid2.worldY_mm);
    taught.refDistance23_mm = coord_align::CalculateDistance(
        taught.fid2.worldX_mm, taught.fid2.worldY_mm,
        taught.fid3.worldX_mm, taught.fid3.worldY_mm);
    taught.refDistance31_mm = coord_align::CalculateDistance(
        taught.fid3.worldX_mm, taught.fid3.worldY_mm,
        taught.fid1.worldX_mm, taught.fid1.worldY_mm);

    coord_align::ComputeCoordinateAxes(
        taught.fid1.worldX_mm, taught.fid1.worldY_mm,
        taught.fid2.worldX_mm, taught.fid2.worldY_mm,
        taught.originX_mm, taught.originY_mm,
        taught.xAxisX, taught.xAxisY,
        taught.yAxisX, taught.yAxisY);

    deps.parent->StoreTaughtSystem(taught);

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

    if (logger) {
      logger->Info(msg.str(), "CoordAlign");
    }
    std::cout << "[CoordAlign] " << msg.str() << std::endl;

    context->SetValue(storageKey + ".taught", true, "coordalign", "TeachCoordinateSystem");
    context->SetValue(storageKey + ".fid1_world_x", static_cast<float>(taught.fid1.worldX_mm), "coordalign", "TeachCoordinateSystem");
    context->SetValue(storageKey + ".fid1_world_y", static_cast<float>(taught.fid1.worldY_mm), "coordalign", "TeachCoordinateSystem");
    context->SetValue(storageKey + ".fid2_world_x", static_cast<float>(taught.fid2.worldX_mm), "coordalign", "TeachCoordinateSystem");
    context->SetValue(storageKey + ".fid2_world_y", static_cast<float>(taught.fid2.worldY_mm), "coordalign", "TeachCoordinateSystem");
    context->SetValue(storageKey + ".fid3_world_x", static_cast<float>(taught.fid3.worldX_mm), "coordalign", "TeachCoordinateSystem");
    context->SetValue(storageKey + ".fid3_world_y", static_cast<float>(taught.fid3.worldY_mm), "coordalign", "TeachCoordinateSystem");

    return true;
  }

  // ============================================================================
  // CLEAR COORDINATE SYSTEM
  // ============================================================================
  if (methodName == "ClearCoordinateSystem") {
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

    auto result = deps.parent->ClearCoordinateSystemEntries(uid, type);

    if (!result.clearedTaught && !result.clearedLive) {
      errorMessage = "ClearCoordinateSystem: System '" + uid + "' not found";
      return false;
    }

    std::string clearedMsg;
    if (result.clearedTaught && result.clearedLive) {
      clearedMsg = "Cleared taught and live systems";
    } else if (result.clearedTaught) {
      clearedMsg = "Cleared taught system";
    } else {
      clearedMsg = "Cleared live system";
    }

    if (logger) {
      logger->Info("ClearCoordinateSystem: " + clearedMsg + " '" + uid + "'", "CoordAlign");
    }
    std::cout << "[CoordAlign] " << clearedMsg << " '" << uid << "'" << std::endl;

    return true;
  }

  // ============================================================================
  // LIST COORDINATE SYSTEMS
  // ============================================================================
  if (methodName == "ListCoordinateSystems") {
    auto listing = deps.parent->ListCoordinateSystemsSnapshot();

    std::ostringstream msg;
    msg << "Coordinate Systems:\n";

    msg << "  Taught (" << listing.taught.size() << "):\n";
    for (const auto& sys : listing.taught) {
      msg << "    - " << sys.uid << " (device: " << sys.motionDevice
          << ", created: " << sys.createdAt << ")\n";
    }

    msg << "  Live (" << listing.liveByUid.size() << "):\n";
    for (const auto& [uid, sys] : listing.liveByUid) {
      msg << "    - " << uid << " (taught: " << sys.taughtUid
          << ", rot: " << sys.rotationDeg << "deg, rms: " << sys.rmsError_mm << "mm)\n";
    }

    if (logger) {
      logger->Info(msg.str(), "CoordAlign");
    }
    std::cout << "[CoordAlign] " << msg.str() << std::endl;

    if (context) {
      context->SetValue("coordalign.taught_count", static_cast<float>(listing.taught.size()), "coordalign", "ListCoordinateSystems");
      context->SetValue("coordalign.live_count", static_cast<float>(listing.liveByUid.size()), "coordalign", "ListCoordinateSystems");
    }

    return true;
  }

  return std::nullopt;
}
