// YawTrainerStore.cpp - JSON persistence for yaw trainer sets
#include "YawTrainerStore.h"
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

static json LinePointsToJson(const LinePoints& lp) {
    return {
        {"name", lp.name},
        {"p1x", lp.p1x}, {"p1y", lp.p1y}, {"p1z", lp.p1z},
        {"p2x", lp.p2x}, {"p2y", lp.p2y}, {"p2z", lp.p2z},
        {"valid", lp.valid}
    };
}

static LinePoints LinePointsFromJson(const json& j, const std::string& defaultName) {
    LinePoints lp;
    lp.name = j.value("name", defaultName);
    lp.p1x = j.value("p1x", 0.0);
    lp.p1y = j.value("p1y", 0.0);
    lp.p1z = j.value("p1z", 0.0);
    lp.p2x = j.value("p2x", 0.0);
    lp.p2y = j.value("p2y", 0.0);
    lp.p2z = j.value("p2z", 0.0);
    lp.valid = j.value("valid", false);
    return lp;
}

YawTrainerStore::YawTrainerStore()
    : m_basePath("config/yaw_trainer")
{
}

void YawTrainerStore::SetBasePath(const std::string& basePath)
{
    m_basePath = basePath;
}

void YawTrainerStore::EnsureDirectory() const
{
    std::filesystem::path dir(m_basePath);
    if (!std::filesystem::exists(dir)) {
        std::filesystem::create_directories(dir);
    }
}

std::string YawTrainerStore::MakeFilePath(const std::string& name) const
{
    return m_basePath + "/" + name + ".json";
}

bool YawTrainerStore::SaveSet(const YawTrainerSet& set)
{
    EnsureDirectory();

    json j;
    j["version"] = CURRENT_FILE_VERSION;
    j["uid"] = set.uid.empty() ? GenerateUUID() : set.uid;
    j["name"] = set.name;
    j["deviceName"] = set.deviceName;
    j["createdAt"] = set.createdAt;

    j["referenceLine"] = LinePointsToJson(set.referenceLine);
    j["targetLine"] = LinePointsToJson(set.targetLine);

    j["correction"] = {
        {"targetDevice", set.correction.targetDevice},
        {"targetAxis", set.correction.targetAxis},
        {"reverseSign", set.correction.reverseSign},
        {"applied", set.correction.applied},
        {"appliedValue", set.correction.appliedValue}
    };

    std::string saveAs = set.sourceFilename.empty() ? set.name : set.sourceFilename;
    std::string filePath = MakeFilePath(saveAs);
    std::ofstream ofs(filePath);
    if (!ofs.is_open()) {
        std::cerr << "[YawTrainerStore] Failed to open file for writing: " << filePath << std::endl;
        return false;
    }

    ofs << j.dump(2);
    ofs.close();

    std::cout << "[YawTrainerStore] Saved set: " << set.name << " -> " << filePath << std::endl;
    return true;
}

std::optional<YawTrainerSet> YawTrainerStore::LoadSet(const std::string& name)
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
                    std::cout << "[YawTrainerStore] Found '" << name << "' by internal name in: "
                              << filePath << std::endl;
                    break;
                }
            }
        }
    }

    if (!ifs.is_open()) {
        std::cerr << "[YawTrainerStore] Failed to find set: " << name << std::endl;
        return std::nullopt;
    }

    json j;
    try {
        ifs >> j;
    } catch (const json::parse_error& e) {
        std::cerr << "[YawTrainerStore] JSON parse error in " << filePath << ": " << e.what() << std::endl;
        return std::nullopt;
    }

    YawTrainerSet set;
    set.uid = j.value("uid", "");
    if (set.uid.empty()) {
        set.uid = GenerateUUID();
    }
    set.name = j.value("name", "");
    set.deviceName = j.value("deviceName", "gantry-main");
    set.createdAt = j.value("createdAt", "");
    set.sourceFilename = std::filesystem::path(filePath).stem().string();

    if (j.contains("referenceLine")) {
        set.referenceLine = LinePointsFromJson(j["referenceLine"], "Reference");
    }
    if (j.contains("targetLine")) {
        set.targetLine = LinePointsFromJson(j["targetLine"], "Target");
    }

    if (j.contains("correction")) {
        const auto& c = j["correction"];
        set.correction.targetDevice = c.value("targetDevice", "");
        set.correction.targetAxis = c.value("targetAxis", "");
        set.correction.reverseSign = c.value("reverseSign", false);
        set.correction.applied = c.value("applied", false);
        set.correction.appliedValue = c.value("appliedValue", 0.0);
    }

    // Recalculate delta from line points
    set.RecalculateDelta();
    set.valid = set.referenceLine.valid && set.targetLine.valid;

    std::cout << "[YawTrainerStore] Loaded set: " << set.name
              << " [uid=" << set.uid << "]"
              << " ref=" << (set.referenceLine.valid ? "valid" : "empty")
              << " tgt=" << (set.targetLine.valid ? "valid" : "empty")
              << " delta=" << (set.deltaValid ? "valid" : "N/A") << std::endl;
    return set;
}

std::vector<std::string> YawTrainerStore::ListSets() const
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

bool YawTrainerStore::DeleteSet(const std::string& name)
{
    std::string filePath = MakeFilePath(name);
    if (std::filesystem::exists(filePath)) {
        std::filesystem::remove(filePath);
        std::cout << "[YawTrainerStore] Deleted set: " << name << std::endl;
        return true;
    }
    std::cerr << "[YawTrainerStore] File not found for deletion: " << filePath << std::endl;
    return false;
}
