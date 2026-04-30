// CoordinateAlignCommandHandler.cpp
//
// Orchestrator for coordinate-alignment commands. Per-family logic lives in
// include/sequence/coordalign/CoordAlign*Commands.{h,cpp}; this file owns
// only the dispatch ladder, the in-memory maps + mutex, the cross-family
// utilities (GetCurrentTimestamp, ResolveNumericValue, ReadFiducialFromContext)
// and the high-level state ops the helpers call via deps.parent->...
#include "CoordinateAlignCommandHandler.h"
#include "SequenceContext.h"
#include "LogManager.h"
#include "ApplicationContext.h"

#include <nlohmann/json.hpp>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

// Per-family helpers (dispatch order — each TryExecute returns std::nullopt
// when methodName isn't theirs, otherwise true/false handled).
#include "coordalign/CoordAlignTeachCommands.h"        // Family A
#include "coordalign/CoordAlignAlignCommands.h"        // Family B
#include "coordalign/CoordAlignLocalPointCommands.h"   // Family C
#include "coordalign/CoordAlignTransformCommands.h"    // Family D

using json = nlohmann::json;

bool CoordinateAlignCommandHandler::ExecuteCommand(
    void* manager,
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context)
{
    (void)manager;

    // Shared dependency bundle for split helper families. Built once here
    // and reused across every helper TryExecute call below.
    CoordAlignCommandDeps deps{};
    deps.app    = m_appContext;
    deps.parent = this;

    if (auto r = CoordAlignTeachCommands::TryExecute(
          deviceName, methodName, params, errorMessage, context, deps)) return *r;
    if (auto r = CoordAlignAlignCommands::TryExecute(
          deviceName, methodName, params, errorMessage, context, deps)) return *r;
    if (auto r = CoordAlignLocalPointCommands::TryExecute(
          deviceName, methodName, params, errorMessage, context, deps)) return *r;
    if (auto r = CoordAlignTransformCommands::TryExecute(
          deviceName, methodName, params, errorMessage, context, deps)) return *r;

    errorMessage = "Unknown method: " + methodName +
                   ". Supported: TeachCoordinateSystem, AlignLiveCoordinateSystem, "
                   "TransformPoint, TeachLocalPoint, TransformLocalPoint, GetTransform, "
                   "ClearCoordinateSystem, ListCoordinateSystems, ListLocalPoints, ClearLocalPoint, MoveToLocalXY";
    return false;
}

// =============================================================================
// Family A high-level state ops — locking + persistence encapsulated for the
// CoordAlignTeachCommands helper. (Mirrors Calibration's parent-shim pattern.)
// =============================================================================

void CoordinateAlignCommandHandler::StoreTaughtSystem(const TaughtCoordinateSystem& sys)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_taughtSystems[sys.uid] = sys;
    }
    // Best-effort persist (return value intentionally discarded; matches
    // pre-refactor behaviour).
    SaveTaughtSystemToDisk(sys);
}

CoordinateAlignCommandHandler::ClearCoordinateSystemResult
CoordinateAlignCommandHandler::ClearCoordinateSystemEntries(
    const std::string& uid,
    const std::string& type)
{
    ClearCoordinateSystemResult result;

    if (type == "taught" || type == "both") {
        if (DeleteTaughtSystemFromDisk(uid)) {
            result.clearedTaught = true;
            std::lock_guard<std::mutex> lock(m_mutex);
            m_taughtSystems.erase(uid);
        }
    }

    if (type == "live" || type == "both") {
        if (DeleteLiveSystemFromDisk(uid)) {
            result.clearedLive = true;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_liveSystems.find(uid);
        if (it != m_liveSystems.end()) {
            m_liveSystems.erase(it);
            result.clearedLive = true;
        }
    }

    return result;
}

bool CoordinateAlignCommandHandler::LoadTaughtSystem(
    const std::string& uid, TaughtCoordinateSystem& out)
{
    return LoadTaughtSystemFromDisk(uid, out);
}

void CoordinateAlignCommandHandler::StoreLiveSystem(const LiveCoordinateSystem& live)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_liveSystems[live.uid] = live;
    }
    SaveLiveSystemToDisk(live);
}

bool CoordinateAlignCommandHandler::GetLiveSystem(
    const std::string& uid, LiveCoordinateSystem& out)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_liveSystems.find(uid);
        if (it != m_liveSystems.end()) {
            out = it->second;
            return true;
        }
    }
    // Fall back to disk.
    return LoadLiveSystemFromDisk(uid, out);
}

bool CoordinateAlignCommandHandler::LoadLiveSystem(
    const std::string& uid, LiveCoordinateSystem& out)
{
    return LoadLiveSystemFromDisk(uid, out);
}

bool CoordinateAlignCommandHandler::LoadLocalPoint(
    const std::string& uid, TaughtLocalPoint& out)
{
    return LoadLocalPointFromDisk(uid, out);
}

void CoordinateAlignCommandHandler::StoreLocalPoint(const TaughtLocalPoint& point)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_localPoints[point.uid] = point;
    }
    SaveLocalPointToDisk(point);
}

bool CoordinateAlignCommandHandler::RemoveLocalPoint(const std::string& uid)
{
    if (!DeleteLocalPointFromDisk(uid)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_localPoints.erase(uid);
    return true;
}

std::vector<TaughtLocalPoint>
CoordinateAlignCommandHandler::ListLocalPointsSnapshot() const
{
    std::vector<TaughtLocalPoint> points;
    auto pointsDir = GetLocalPointsDir();
    if (std::filesystem::exists(pointsDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(pointsDir)) {
            if (entry.path().extension() == ".json") {
                std::string uid = entry.path().stem().string();
                TaughtLocalPoint pt;
                if (const_cast<CoordinateAlignCommandHandler*>(this)
                        ->LoadLocalPointFromDisk(uid, pt)) {
                    points.push_back(pt);
                }
            }
        }
    }
    return points;
}

CoordinateAlignCommandHandler::CoordinateSystemListing
CoordinateAlignCommandHandler::ListCoordinateSystemsSnapshot() const
{
    CoordinateSystemListing listing;

    // Taught: read from disk (filesystem is authoritative).
    auto taughtDir = GetTaughtSystemsDir();
    if (std::filesystem::exists(taughtDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(taughtDir)) {
            if (entry.path().extension() == ".json") {
                std::string uid = entry.path().stem().string();
                TaughtCoordinateSystem sys;
                // const_cast preserves the original mutable disk-load API
                // shape; the operation is read-only from the caller's POV.
                if (const_cast<CoordinateAlignCommandHandler*>(this)
                        ->LoadTaughtSystemFromDisk(uid, sys)) {
                    listing.taught.push_back(sys);
                }
            }
        }
    }

    // Live: snapshot under lock.
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& [uid, sys] : m_liveSystems) {
            listing.liveByUid.emplace_back(uid, sys);
        }
    }

    return listing;
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
