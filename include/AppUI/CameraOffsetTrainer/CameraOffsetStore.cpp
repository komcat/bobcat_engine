// CameraOffsetStore.cpp - JSON persistence for camera offset calibration sets
#include "CameraOffsetStore.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <iomanip>

using json = nlohmann::json;

static constexpr int CURRENT_FILE_VERSION = 1;

// Generate a UUID v4 string without Qt dependency
static std::string GenerateUUID() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);

    uint32_t a = dist(gen);
    uint32_t b = dist(gen);
    uint32_t c = dist(gen);
    uint32_t d = dist(gen);

    // Set version (4) and variant (10xx) bits
    b = (b & 0xFFFF0FFF) | 0x00004000;  // version 4
    c = (c & 0x3FFFFFFF) | 0x80000000;  // variant 1

    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(8) << a << "-"
        << std::setw(4) << (b >> 16) << "-"
        << std::setw(4) << (b & 0xFFFF) << "-"
        << std::setw(4) << (c >> 16) << "-"
        << std::setw(4) << (c & 0xFFFF)
        << std::setw(8) << d;
    return oss.str();
}

CameraOffsetStore::CameraOffsetStore()
    : m_basePath("config/SYSTEM/PR_Camera_Offset")
{
}

void CameraOffsetStore::SetBasePath(const std::string& basePath)
{
    m_basePath = basePath;
}

void CameraOffsetStore::EnsureDirectory() const
{
    std::filesystem::path dir(m_basePath);
    if (!std::filesystem::exists(dir)) {
        std::filesystem::create_directories(dir);
    }
}

std::string CameraOffsetStore::MakeFilePath(const std::string& name) const
{
    return m_basePath + "/" + name + ".json";
}

bool CameraOffsetStore::SaveCalibrationSet(const CameraOffsetCalibrationSet& cs)
{
    EnsureDirectory();

    json j;
    j["version"] = CURRENT_FILE_VERSION;
    j["uid"] = cs.uid.empty() ? GenerateUUID() : cs.uid;
    j["name"] = cs.name;
    j["createdAt"] = cs.createdAt;
    j["lastModifiedAt"] = cs.lastModifiedAt;
    j["deviceName"] = cs.deviceName;
    j["lowMagCameraId"] = cs.lowMagCameraId;
    j["highMagCameraId"] = cs.highMagCameraId;

    // Low-mag position
    j["lowMagPosition"] = {
        {"x", cs.lowMagPosition.x},
        {"y", cs.lowMagPosition.y},
        {"z", cs.lowMagPosition.z},
        {"valid", cs.lowMagPosition.valid}
    };

    // High-mag position
    j["highMagPosition"] = {
        {"x", cs.highMagPosition.x},
        {"y", cs.highMagPosition.y},
        {"z", cs.highMagPosition.z},
        {"valid", cs.highMagPosition.valid}
    };

    // Computed offset
    j["offsetX"] = cs.offsetX;
    j["offsetY"] = cs.offsetY;
    j["offsetZ"] = cs.offsetZ;
    j["offsetValid"] = cs.offsetValid;

    // Vision profile paths
    j["lowMagVisionProfilePath"] = cs.lowMagVisionProfilePath;
    j["highMagVisionProfilePath"] = cs.highMagVisionProfilePath;

    // Last test result
    const auto& tr = cs.lastTestResult;
    json trj;
    trj["testedLowMagPos"] = {
        {"x", tr.testedLowMagPos.x}, {"y", tr.testedLowMagPos.y},
        {"z", tr.testedLowMagPos.z}, {"valid", tr.testedLowMagPos.valid}
    };
    trj["testedHighMagPos"] = {
        {"x", tr.testedHighMagPos.x}, {"y", tr.testedHighMagPos.y},
        {"z", tr.testedHighMagPos.z}, {"valid", tr.testedHighMagPos.valid}
    };
    trj["testedOffsetX"] = tr.testedOffsetX;
    trj["testedOffsetY"] = tr.testedOffsetY;
    trj["testedOffsetZ"] = tr.testedOffsetZ;
    trj["deviationX"] = tr.deviationX;
    trj["deviationY"] = tr.deviationY;
    trj["deviationZ"] = tr.deviationZ;
    trj["lowMagImagePath"] = tr.lowMagImagePath;
    trj["highMagImagePath"] = tr.highMagImagePath;
    trj["testedAt"] = tr.testedAt;
    trj["valid"] = tr.valid;
    j["lastTestResult"] = trj;

    // Save to source file if loaded from one, otherwise use internal name
    std::string saveAs = cs.sourceFilename.empty() ? cs.name : cs.sourceFilename;
    std::string filePath = MakeFilePath(saveAs);
    std::ofstream ofs(filePath);
    if (!ofs.is_open()) {
        std::cerr << "[CameraOffsetStore] Failed to open file for writing: " << filePath << std::endl;
        return false;
    }

    ofs << j.dump(2);
    ofs.close();

    std::cout << "[CameraOffsetStore] Saved calibration set: " << cs.name << " -> " << filePath << std::endl;
    return true;
}

std::optional<CameraOffsetCalibrationSet> CameraOffsetStore::LoadCalibrationSet(const std::string& name)
{
    // Try direct filename match first
    std::string filePath = MakeFilePath(name);
    std::ifstream ifs(filePath);

    // Fallback: search all files for matching internal name
    if (!ifs.is_open()) {
        std::filesystem::path dir(m_basePath);
        if (std::filesystem::exists(dir)) {
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
                std::ifstream probe(entry.path());
                if (!probe.is_open()) continue;
                json probeJ;
                try { probe >> probeJ; } catch (...) { continue; }
                probe.close();
                if (probeJ.contains("name") && probeJ["name"].get<std::string>() == name) {
                    filePath = entry.path().string();
                    ifs.open(filePath);
                    std::cout << "[CameraOffsetStore] Found '" << name << "' by internal name in: "
                              << filePath << std::endl;
                    break;
                }
            }
        }
    }

    if (!ifs.is_open()) {
        std::cerr << "[CameraOffsetStore] Failed to find calibration set: " << name << std::endl;
        return std::nullopt;
    }

    json j;
    try {
        ifs >> j;
    } catch (const json::parse_error& e) {
        std::cerr << "[CameraOffsetStore] JSON parse error in " << filePath << ": " << e.what() << std::endl;
        return std::nullopt;
    }

    CameraOffsetCalibrationSet cs;
    cs.uid = j.value("uid", "");
    if (cs.uid.empty()) {
        cs.uid = GenerateUUID();
    }
    cs.name = j.value("name", "");
    cs.createdAt = j.value("createdAt", "");
    cs.lastModifiedAt = j.value("lastModifiedAt", "");
    cs.deviceName = j.value("deviceName", "gantry-main");
    cs.lowMagCameraId = j.value("lowMagCameraId", "");
    cs.highMagCameraId = j.value("highMagCameraId", "");

    // Track source filename
    cs.sourceFilename = std::filesystem::path(filePath).stem().string();

    // Low-mag position
    if (j.contains("lowMagPosition")) {
        const auto& lp = j["lowMagPosition"];
        cs.lowMagPosition.x = lp.value("x", 0.0);
        cs.lowMagPosition.y = lp.value("y", 0.0);
        cs.lowMagPosition.z = lp.value("z", 0.0);
        cs.lowMagPosition.valid = lp.value("valid", false);
    }

    // High-mag position
    if (j.contains("highMagPosition")) {
        const auto& hp = j["highMagPosition"];
        cs.highMagPosition.x = hp.value("x", 0.0);
        cs.highMagPosition.y = hp.value("y", 0.0);
        cs.highMagPosition.z = hp.value("z", 0.0);
        cs.highMagPosition.valid = hp.value("valid", false);
    }

    // Offset
    cs.offsetX = j.value("offsetX", 0.0);
    cs.offsetY = j.value("offsetY", 0.0);
    cs.offsetZ = j.value("offsetZ", 0.0);
    cs.offsetValid = j.value("offsetValid", false);

    // Vision profile paths
    cs.lowMagVisionProfilePath = j.value("lowMagVisionProfilePath", "");
    cs.highMagVisionProfilePath = j.value("highMagVisionProfilePath", "");

    // Last test result
    if (j.contains("lastTestResult")) {
        const auto& trj = j["lastTestResult"];
        auto& tr = cs.lastTestResult;

        if (trj.contains("testedLowMagPos")) {
            const auto& tlp = trj["testedLowMagPos"];
            tr.testedLowMagPos.x = tlp.value("x", 0.0);
            tr.testedLowMagPos.y = tlp.value("y", 0.0);
            tr.testedLowMagPos.z = tlp.value("z", 0.0);
            tr.testedLowMagPos.valid = tlp.value("valid", false);
        }
        if (trj.contains("testedHighMagPos")) {
            const auto& thp = trj["testedHighMagPos"];
            tr.testedHighMagPos.x = thp.value("x", 0.0);
            tr.testedHighMagPos.y = thp.value("y", 0.0);
            tr.testedHighMagPos.z = thp.value("z", 0.0);
            tr.testedHighMagPos.valid = thp.value("valid", false);
        }

        tr.testedOffsetX = trj.value("testedOffsetX", 0.0);
        tr.testedOffsetY = trj.value("testedOffsetY", 0.0);
        tr.testedOffsetZ = trj.value("testedOffsetZ", 0.0);
        tr.deviationX = trj.value("deviationX", 0.0);
        tr.deviationY = trj.value("deviationY", 0.0);
        tr.deviationZ = trj.value("deviationZ", 0.0);
        tr.lowMagImagePath = trj.value("lowMagImagePath", "");
        tr.highMagImagePath = trj.value("highMagImagePath", "");
        tr.testedAt = trj.value("testedAt", "");
        tr.valid = trj.value("valid", false);
    }

    std::cout << "[CameraOffsetStore] Loaded calibration set: " << cs.name
              << " [uid=" << cs.uid << "]"
              << " offsetValid=" << cs.offsetValid << std::endl;
    return cs;
}

std::vector<std::string> CameraOffsetStore::ListCalibrationSets() const
{
    std::vector<std::string> names;
    std::filesystem::path dir(m_basePath);
    if (!std::filesystem::exists(dir)) {
        return names;
    }

    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            names.push_back(entry.path().stem().string());
        }
    }
    return names;
}

bool CameraOffsetStore::DeleteCalibrationSet(const std::string& name)
{
    std::string filePath = MakeFilePath(name);
    if (std::filesystem::exists(filePath)) {
        std::filesystem::remove(filePath);
        std::cout << "[CameraOffsetStore] Deleted calibration set: " << name << std::endl;
        return true;
    }
    std::cerr << "[CameraOffsetStore] File not found for deletion: " << filePath << std::endl;
    return false;
}
