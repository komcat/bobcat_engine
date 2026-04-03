// PlacementCorrectionStore.cpp - JSON persistence for placement correction trainer sets
#include "PlacementCorrectionStore.h"
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

static json LinePointsToJson(const PlacementLinePoints& lp) {
    return {
        {"name", lp.name},
        {"p1x", lp.p1x}, {"p1y", lp.p1y}, {"p1z", lp.p1z}, {"p1Exposure", lp.p1Exposure}, {"p1ImageFile", lp.p1ImageFile},
        {"p2x", lp.p2x}, {"p2y", lp.p2y}, {"p2z", lp.p2z}, {"p2Exposure", lp.p2Exposure}, {"p2ImageFile", lp.p2ImageFile},
        {"valid", lp.valid}
    };
}

static PlacementLinePoints LinePointsFromJson(const json& j, const std::string& defaultName) {
    PlacementLinePoints lp;
    lp.name = j.value("name", defaultName);
    lp.p1x = j.value("p1x", 0.0);
    lp.p1y = j.value("p1y", 0.0);
    lp.p1z = j.value("p1z", 0.0);
    lp.p1Exposure = j.value("p1Exposure", 800.0);
    lp.p1ImageFile = j.value("p1ImageFile", std::string(""));
    lp.p2x = j.value("p2x", 0.0);
    lp.p2y = j.value("p2y", 0.0);
    lp.p2z = j.value("p2z", 0.0);
    lp.p2Exposure = j.value("p2Exposure", 800.0);
    lp.p2ImageFile = j.value("p2ImageFile", std::string(""));
    lp.valid = j.value("valid", false);
    return lp;
}

PlacementCorrectionStore::PlacementCorrectionStore()
    : m_basePath("config/placement_correction")
{
}

void PlacementCorrectionStore::SetBasePath(const std::string& basePath)
{
    m_basePath = basePath;
}

void PlacementCorrectionStore::EnsureDirectory() const
{
    std::filesystem::path dir(m_basePath);
    if (!std::filesystem::exists(dir)) {
        std::filesystem::create_directories(dir);
    }
}

std::string PlacementCorrectionStore::MakeFilePath(const std::string& name) const
{
    return m_basePath + "/" + name + ".json";
}

bool PlacementCorrectionStore::SaveSet(const PlacementCorrectionSet& set)
{
    EnsureDirectory();

    json j;
    j["version"] = CURRENT_FILE_VERSION;
    j["uid"] = set.uid.empty() ? GenerateUUID() : set.uid;
    j["name"] = set.name;
    j["measureDevice"] = set.measureDevice;
    j["createdAt"] = set.createdAt;

    j["referenceLine"] = LinePointsToJson(set.referenceLine);
    j["targetLine"] = LinePointsToJson(set.targetLine);

    j["config"] = {
        {"targetDevice", set.config.targetDevice},
        {"masterPositionName", set.config.masterPositionName},
        {"masterX", set.config.masterX},
        {"masterY", set.config.masterY},
        {"masterZ", set.config.masterZ},
        {"masterU", set.config.masterU},
        {"masterV", set.config.masterV},
        {"masterW", set.config.masterW},
        {"targetPositionName", set.config.targetPositionName},
        {"targetX", set.config.targetX},
        {"targetY", set.config.targetY},
        {"targetZ", set.config.targetZ},
        {"targetU", set.config.targetU},
        {"targetV", set.config.targetV},
        {"targetW", set.config.targetW},
        {"reverseSign", set.config.reverseSign},
        {"applyUCorrection", set.config.applyUCorrection},
        {"cameraId", set.config.cameraId}
    };

    // Summary diagram thumbnail offsets
    json thumbOffsets = json::array();
    for (int i = 0; i < 4; ++i) {
        thumbOffsets.push_back({
            {"dx", set.summaryThumbOffsets[i].dx},
            {"dy", set.summaryThumbOffsets[i].dy}
        });
    }
    j["summaryThumbOffsets"] = thumbOffsets;

    std::string saveAs = set.sourceFilename.empty() ? set.name : set.sourceFilename;
    std::string filePath = MakeFilePath(saveAs);
    std::ofstream ofs(filePath);
    if (!ofs.is_open()) {
        std::cerr << "[PlacementCorrectionStore] Failed to open file for writing: " << filePath << std::endl;
        return false;
    }

    ofs << j.dump(2);
    ofs.close();

    std::cout << "[PlacementCorrectionStore] Saved set: " << set.name << " -> " << filePath << std::endl;
    return true;
}

std::optional<PlacementCorrectionSet> PlacementCorrectionStore::LoadSet(const std::string& name)
{
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
                    std::cout << "[PlacementCorrectionStore] Found '" << name << "' by internal name in: "
                              << filePath << std::endl;
                    break;
                }
            }
        }
    }

    if (!ifs.is_open()) {
        std::cerr << "[PlacementCorrectionStore] Failed to find set: " << name << std::endl;
        return std::nullopt;
    }

    json j;
    try {
        ifs >> j;
    } catch (const json::parse_error& e) {
        std::cerr << "[PlacementCorrectionStore] JSON parse error in " << filePath << ": " << e.what() << std::endl;
        return std::nullopt;
    }

    PlacementCorrectionSet set;
    set.uid = j.value("uid", "");
    if (set.uid.empty()) {
        set.uid = GenerateUUID();
    }
    set.name = j.value("name", "");
    set.measureDevice = j.value("measureDevice", "gantry-main");
    set.createdAt = j.value("createdAt", "");
    set.sourceFilename = std::filesystem::path(filePath).stem().string();

    if (j.contains("referenceLine")) {
        set.referenceLine = LinePointsFromJson(j["referenceLine"], "Reference");
    }
    if (j.contains("targetLine")) {
        set.targetLine = LinePointsFromJson(j["targetLine"], "Target");
    }

    if (j.contains("config")) {
        const auto& c = j["config"];
        set.config.targetDevice = c.value("targetDevice", "");
        set.config.masterPositionName = c.value("masterPositionName",
            c.value("targetPositionName", "PLACE"));  // backward compat
        set.config.masterX = c.value("masterX", c.value("baselineX", 0.0));
        set.config.masterY = c.value("masterY", c.value("baselineY", 0.0));
        set.config.masterZ = c.value("masterZ", c.value("baselineZ", 0.0));
        set.config.masterU = c.value("masterU", c.value("baselineU", 0.0));
        set.config.masterV = c.value("masterV", c.value("baselineV", 0.0));
        set.config.masterW = c.value("masterW", c.value("baselineW", 0.0));
        set.config.targetPositionName = c.value("targetPositionName", "");
        set.config.targetX = c.value("targetX", 0.0);
        set.config.targetY = c.value("targetY", 0.0);
        set.config.targetZ = c.value("targetZ", 0.0);
        set.config.targetU = c.value("targetU", 0.0);
        set.config.targetV = c.value("targetV", 0.0);
        set.config.targetW = c.value("targetW", 0.0);
        set.config.reverseSign = c.value("reverseSign", false);
        set.config.applyUCorrection = c.value("applyUCorrection", true);
        set.config.cameraId = c.value("cameraId", "");
    }

    // Load summary thumbnail offsets (backward-compatible: defaults if absent)
    if (j.contains("summaryThumbOffsets") && j["summaryThumbOffsets"].is_array()) {
        const auto& arr = j["summaryThumbOffsets"];
        for (int i = 0; i < 4 && i < static_cast<int>(arr.size()); ++i) {
            set.summaryThumbOffsets[i].dx = arr[i].value("dx", ThumbOffset{}.dx);
            set.summaryThumbOffsets[i].dy = arr[i].value("dy", ThumbOffset{}.dy);
        }
    }

    // Recalculate derived values from line points
    set.Recalculate();
    set.valid = set.referenceLine.valid && set.targetLine.valid;

    std::cout << "[PlacementCorrectionStore] Loaded set: " << set.name
              << " [uid=" << set.uid << "]"
              << " ref=" << (set.referenceLine.valid ? "valid" : "empty")
              << " tgt=" << (set.targetLine.valid ? "valid" : "empty")
              << " delta=" << (set.deltaValid ? "valid" : "N/A") << std::endl;
    return set;
}

std::vector<std::string> PlacementCorrectionStore::ListSets() const
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

bool PlacementCorrectionStore::DeleteSet(const std::string& name)
{
    std::string filePath = MakeFilePath(name);
    if (std::filesystem::exists(filePath)) {
        std::filesystem::remove(filePath);
        std::cout << "[PlacementCorrectionStore] Deleted set: " << name << std::endl;
        return true;
    }
    std::cerr << "[PlacementCorrectionStore] File not found for deletion: " << filePath << std::endl;
    return false;
}
