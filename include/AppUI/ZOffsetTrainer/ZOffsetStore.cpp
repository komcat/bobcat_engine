// ZOffsetStore.cpp - JSON persistence for Z offset training sets
#include "ZOffsetStore.h"
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

ZOffsetStore::ZOffsetStore()
    : m_basePath("config/z_offset")
{
}

void ZOffsetStore::SetBasePath(const std::string& basePath)
{
    m_basePath = basePath;
}

void ZOffsetStore::EnsureDirectory() const
{
    std::filesystem::path dir(m_basePath);
    if (!std::filesystem::exists(dir)) {
        std::filesystem::create_directories(dir);
    }
}

std::string ZOffsetStore::MakeFilePath(const std::string& name) const
{
    return m_basePath + "/" + name + ".json";
}

bool ZOffsetStore::SaveSet(const ZOffsetSet& set)
{
    EnsureDirectory();

    json j;
    j["version"] = CURRENT_FILE_VERSION;
    j["uid"] = set.uid.empty() ? GenerateUUID() : set.uid;
    j["name"] = set.name;
    j["deviceName"] = set.deviceName;
    j["createdAt"] = set.createdAt;

    // Reference point (Point 1)
    j["reference"] = {
        {"name", set.reference.name},
        {"x", set.reference.x},
        {"y", set.reference.y},
        {"z", set.reference.z},
        {"valid", set.reference.valid}
    };

    // Target point (Point 2)
    j["target"] = {
        {"name", set.target.name},
        {"x", set.target.x},
        {"y", set.target.y},
        {"z", set.target.z},
        {"valid", set.target.valid}
    };

    // Correction config
    j["correction"] = {
        {"targetDevice", set.correction.targetDevice},
        {"targetZDiffUm", set.correction.targetZDiffUm},
        {"reverseSign", set.correction.reverseSign},
        {"applied", set.correction.applied},
        {"appliedValueMm", set.correction.appliedValueMm}
    };

    // Save to source file if loaded from one, otherwise use internal name
    std::string saveAs = set.sourceFilename.empty() ? set.name : set.sourceFilename;
    std::string filePath = MakeFilePath(saveAs);
    std::ofstream ofs(filePath);
    if (!ofs.is_open()) {
        std::cerr << "[ZOffsetStore] Failed to open file for writing: " << filePath << std::endl;
        return false;
    }

    ofs << j.dump(2);
    ofs.close();

    std::cout << "[ZOffsetStore] Saved set: " << set.name << " -> " << filePath << std::endl;
    return true;
}

std::optional<ZOffsetSet> ZOffsetStore::LoadSet(const std::string& name)
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
                    std::cout << "[ZOffsetStore] Found '" << name << "' by internal name in: "
                              << filePath << std::endl;
                    break;
                }
            }
        }
    }

    if (!ifs.is_open()) {
        std::cerr << "[ZOffsetStore] Failed to find set: " << name << std::endl;
        return std::nullopt;
    }

    json j;
    try {
        ifs >> j;
    } catch (const json::parse_error& e) {
        std::cerr << "[ZOffsetStore] JSON parse error in " << filePath << ": " << e.what() << std::endl;
        return std::nullopt;
    }

    ZOffsetSet set;
    set.uid = j.value("uid", "");
    if (set.uid.empty()) {
        set.uid = GenerateUUID();
    }
    set.name = j.value("name", "");
    set.deviceName = j.value("deviceName", "gantry-main");
    set.createdAt = j.value("createdAt", "");
    set.sourceFilename = std::filesystem::path(filePath).stem().string();

    // Reference point
    if (j.contains("reference")) {
        const auto& p = j["reference"];
        set.reference.name = p.value("name", "Reference");
        set.reference.x = p.value("x", 0.0);
        set.reference.y = p.value("y", 0.0);
        set.reference.z = p.value("z", 0.0);
        set.reference.valid = p.value("valid", false);
    }

    // Target point
    if (j.contains("target")) {
        const auto& p = j["target"];
        set.target.name = p.value("name", "Target");
        set.target.x = p.value("x", 0.0);
        set.target.y = p.value("y", 0.0);
        set.target.z = p.value("z", 0.0);
        set.target.valid = p.value("valid", false);
    }

    // Correction config
    if (j.contains("correction")) {
        const auto& c = j["correction"];
        set.correction.targetDevice = c.value("targetDevice", "");
        set.correction.targetZDiffUm = c.value("targetZDiffUm", 150.0);
        set.correction.reverseSign = c.value("reverseSign", false);
        set.correction.applied = c.value("applied", false);
        set.correction.appliedValueMm = c.value("appliedValueMm", 0.0);
    }

    // Recalculate Z difference
    set.RecalculateZDiff();
    set.valid = set.reference.valid && set.target.valid;

    std::cout << "[ZOffsetStore] Loaded set: " << set.name
              << " [uid=" << set.uid << "]"
              << " ref=" << (set.reference.valid ? "valid" : "empty")
              << " tgt=" << (set.target.valid ? "valid" : "empty")
              << " zDiff=" << (set.zDiffValid ? "valid" : "N/A") << std::endl;
    return set;
}

std::vector<std::string> ZOffsetStore::ListSets() const
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

bool ZOffsetStore::DeleteSet(const std::string& name)
{
    std::string filePath = MakeFilePath(name);
    if (std::filesystem::exists(filePath)) {
        std::filesystem::remove(filePath);
        std::cout << "[ZOffsetStore] Deleted set: " << name << std::endl;
        return true;
    }
    std::cerr << "[ZOffsetStore] File not found for deletion: " << filePath << std::endl;
    return false;
}
