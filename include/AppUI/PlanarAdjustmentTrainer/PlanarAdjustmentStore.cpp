// PlanarAdjustmentStore.cpp - JSON persistence for planar adjustment sets
#include "PlanarAdjustmentStore.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <iomanip>

using json = nlohmann::json;

static constexpr int CURRENT_FILE_VERSION = 1;

static std::string GenerateUUID() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);

    uint32_t a = dist(gen);
    uint32_t b = dist(gen);
    uint32_t c = dist(gen);
    uint32_t d = dist(gen);

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

PlanarAdjustmentStore::PlanarAdjustmentStore()
    : m_basePath("config/planar_adjustment")
{
}

void PlanarAdjustmentStore::SetBasePath(const std::string& basePath)
{
    m_basePath = basePath;
}

void PlanarAdjustmentStore::EnsureDirectory() const
{
    std::filesystem::path dir(m_basePath);
    if (!std::filesystem::exists(dir)) {
        std::filesystem::create_directories(dir);
    }
}

std::string PlanarAdjustmentStore::MakeFilePath(const std::string& name) const
{
    return m_basePath + "/" + name + ".json";
}

bool PlanarAdjustmentStore::SaveSet(const PlanarAdjustmentSet& set)
{
    EnsureDirectory();

    json j;
    j["version"] = CURRENT_FILE_VERSION;
    j["uid"] = set.uid.empty() ? GenerateUUID() : set.uid;
    j["name"] = set.name;
    j["deviceName"] = set.deviceName;
    j["createdAt"] = set.createdAt;

    // Point 1
    j["point1"] = {
        {"name", set.point1.name},
        {"x", set.point1.x},
        {"y", set.point1.y},
        {"z", set.point1.z},
        {"valid", set.point1.valid}
    };

    // Point 2
    j["point2"] = {
        {"name", set.point2.name},
        {"x", set.point2.x},
        {"y", set.point2.y},
        {"z", set.point2.z},
        {"valid", set.point2.valid}
    };

    // Correction config
    j["correction"] = {
        {"sourceAngle", set.correction.sourceAngle},
        {"targetDevice", set.correction.targetDevice},
        {"targetAxis", set.correction.targetAxis},
        {"reverseSign", set.correction.reverseSign},
        {"applied", set.correction.applied},
        {"appliedValue", set.correction.appliedValue}
    };

    // Save to source file if loaded from one, otherwise use internal name
    std::string saveAs = set.sourceFilename.empty() ? set.name : set.sourceFilename;
    std::string filePath = MakeFilePath(saveAs);
    std::ofstream ofs(filePath);
    if (!ofs.is_open()) {
        std::cerr << "[PlanarAdjustmentStore] Failed to open file for writing: " << filePath << std::endl;
        return false;
    }

    ofs << j.dump(2);
    ofs.close();

    std::cout << "[PlanarAdjustmentStore] Saved set: " << set.name << " -> " << filePath << std::endl;
    return true;
}

std::optional<PlanarAdjustmentSet> PlanarAdjustmentStore::LoadSet(const std::string& name)
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
                    std::cout << "[PlanarAdjustmentStore] Found '" << name << "' by internal name in: "
                              << filePath << std::endl;
                    break;
                }
            }
        }
    }

    if (!ifs.is_open()) {
        std::cerr << "[PlanarAdjustmentStore] Failed to find set: " << name << std::endl;
        return std::nullopt;
    }

    json j;
    try {
        ifs >> j;
    } catch (const json::parse_error& e) {
        std::cerr << "[PlanarAdjustmentStore] JSON parse error in " << filePath << ": " << e.what() << std::endl;
        return std::nullopt;
    }

    PlanarAdjustmentSet set;
    set.uid = j.value("uid", "");
    if (set.uid.empty()) {
        set.uid = GenerateUUID();
    }
    set.name = j.value("name", "");
    set.deviceName = j.value("deviceName", "gantry-main");
    set.createdAt = j.value("createdAt", "");
    set.sourceFilename = std::filesystem::path(filePath).stem().string();

    // Point 1
    if (j.contains("point1")) {
        const auto& p = j["point1"];
        set.point1.name = p.value("name", "Point1");
        set.point1.x = p.value("x", 0.0);
        set.point1.y = p.value("y", 0.0);
        set.point1.z = p.value("z", 0.0);
        set.point1.valid = p.value("valid", false);
    }

    // Point 2
    if (j.contains("point2")) {
        const auto& p = j["point2"];
        set.point2.name = p.value("name", "Point2");
        set.point2.x = p.value("x", 0.0);
        set.point2.y = p.value("y", 0.0);
        set.point2.z = p.value("z", 0.0);
        set.point2.valid = p.value("valid", false);
    }

    // Correction config
    if (j.contains("correction")) {
        const auto& c = j["correction"];
        set.correction.sourceAngle = c.value("sourceAngle", "");
        set.correction.targetDevice = c.value("targetDevice", "");
        set.correction.targetAxis = c.value("targetAxis", "");
        set.correction.reverseSign = c.value("reverseSign", false);
        set.correction.applied = c.value("applied", false);
        set.correction.appliedValue = c.value("appliedValue", 0.0);
    }

    // Recalculate angles from points
    set.RecalculateAngles();
    set.valid = set.point1.valid && set.point2.valid;

    std::cout << "[PlanarAdjustmentStore] Loaded set: " << set.name
              << " [uid=" << set.uid << "]"
              << " p1=" << (set.point1.valid ? "valid" : "empty")
              << " p2=" << (set.point2.valid ? "valid" : "empty")
              << " angles=" << (set.anglesValid ? "valid" : "N/A") << std::endl;
    return set;
}

std::vector<std::string> PlanarAdjustmentStore::ListSets() const
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

bool PlanarAdjustmentStore::DeleteSet(const std::string& name)
{
    std::string filePath = MakeFilePath(name);
    if (std::filesystem::exists(filePath)) {
        std::filesystem::remove(filePath);
        std::cout << "[PlanarAdjustmentStore] Deleted set: " << name << std::endl;
        return true;
    }
    std::cerr << "[PlanarAdjustmentStore] File not found for deletion: " << filePath << std::endl;
    return false;
}
