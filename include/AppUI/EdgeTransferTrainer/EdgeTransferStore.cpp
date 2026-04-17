// EdgeTransferStore.cpp
#include "EdgeTransferStore.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>

using json = nlohmann::json;

static constexpr int CURRENT_FILE_VERSION = 1;

static json EdgeToJson(const EdgePoints& e) {
    return {
        {"name", e.name},
        {"p1x", e.p1x}, {"p1y", e.p1y}, {"p1z", e.p1z},
        {"p2x", e.p2x}, {"p2y", e.p2y}, {"p2z", e.p2z},
        {"p1ImageFile", e.p1ImageFile},
        {"p2ImageFile", e.p2ImageFile},
        {"valid", e.valid}
    };
}

static EdgePoints EdgeFromJson(const json& j, const std::string& defaultName) {
    EdgePoints e;
    e.name = j.value("name", defaultName);
    e.p1x = j.value("p1x", 0.0);
    e.p1y = j.value("p1y", 0.0);
    e.p1z = j.value("p1z", 0.0);
    e.p2x = j.value("p2x", 0.0);
    e.p2y = j.value("p2y", 0.0);
    e.p2z = j.value("p2z", 0.0);
    e.p1ImageFile = j.value("p1ImageFile", "");
    e.p2ImageFile = j.value("p2ImageFile", "");
    e.valid = j.value("valid", false);
    if (e.valid) EdgeTransferMath::Recalc(e);
    return e;
}

EdgeTransferStore::EdgeTransferStore()
    : m_basePath("config/edge_transfer")
{
}

void EdgeTransferStore::SetBasePath(const std::string& basePath)
{
    m_basePath = basePath;
}

void EdgeTransferStore::EnsureDirectory() const
{
    std::filesystem::path dir(m_basePath);
    if (!std::filesystem::exists(dir)) {
        std::filesystem::create_directories(dir);
    }
}

std::string EdgeTransferStore::MakeFilePath(const std::string& name) const
{
    return m_basePath + "/" + name + ".json";
}

bool EdgeTransferStore::SaveSet(const EdgeTransferSet& set)
{
    EnsureDirectory();

    json j;
    j["version"] = CURRENT_FILE_VERSION;
    j["name"] = set.name;
    j["measureDevice"] = set.measureDevice;
    j["referenceEdge"] = EdgeToJson(set.referenceEdge);
    j["targetEdge"]    = EdgeToJson(set.targetEdge);

    j["config"] = {
        {"targetDevice", set.config.targetDevice},
        {"cameraId",     set.config.cameraId},
        {"mapping", {
            {"u", EdgeAxisRoleToString(set.config.mapping.u)},
            {"v", EdgeAxisRoleToString(set.config.mapping.v)},
            {"w", EdgeAxisRoleToString(set.config.mapping.w)},
            {"reverseU", set.config.mapping.reverseU},
            {"reverseV", set.config.mapping.reverseV},
            {"reverseW", set.config.mapping.reverseW},
            {"applyU",   set.config.mapping.applyU},
            {"applyV",   set.config.mapping.applyV},
            {"applyW",   set.config.mapping.applyW}
        }}
    };

    std::string saveAs = set.name;
    std::string filePath = MakeFilePath(saveAs);
    std::ofstream ofs(filePath);
    if (!ofs.is_open()) {
        std::cerr << "[EdgeTransferStore] Failed to open for writing: "
                  << filePath << std::endl;
        return false;
    }
    ofs << j.dump(2);
    ofs.close();

    std::cout << "[EdgeTransferStore] Saved set: " << set.name
              << " -> " << filePath << std::endl;
    return true;
}

std::optional<EdgeTransferSet> EdgeTransferStore::LoadSet(const std::string& name)
{
    std::string filePath = MakeFilePath(name);
    std::ifstream ifs(filePath);

    // Fallback: search all files for internal-name match
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
                    break;
                }
            }
        }
    }

    if (!ifs.is_open()) {
        std::cerr << "[EdgeTransferStore] Failed to find set: " << name << std::endl;
        return std::nullopt;
    }

    json j;
    try {
        ifs >> j;
    } catch (const json::parse_error& e) {
        std::cerr << "[EdgeTransferStore] JSON parse error: " << e.what() << std::endl;
        return std::nullopt;
    }

    EdgeTransferSet set;
    set.name = j.value("name", "");
    set.measureDevice = j.value("measureDevice", "gantry-main");

    if (j.contains("referenceEdge"))
        set.referenceEdge = EdgeFromJson(j["referenceEdge"], "Reference");
    if (j.contains("targetEdge"))
        set.targetEdge = EdgeFromJson(j["targetEdge"], "Target");

    if (j.contains("config")) {
        const auto& c = j["config"];
        set.config.targetDevice = c.value("targetDevice", "hex-left");
        set.config.cameraId     = c.value("cameraId", "");
        if (c.contains("mapping")) {
            const auto& m = c["mapping"];
            set.config.mapping.u = EdgeAxisRoleFromString(m.value("u", "None"));
            set.config.mapping.v = EdgeAxisRoleFromString(m.value("v", "None"));
            set.config.mapping.w = EdgeAxisRoleFromString(m.value("w", "None"));
            set.config.mapping.reverseU = m.value("reverseU", false);
            set.config.mapping.reverseV = m.value("reverseV", false);
            set.config.mapping.reverseW = m.value("reverseW", false);
            set.config.mapping.applyU   = m.value("applyU",   true);
            set.config.mapping.applyV   = m.value("applyV",   true);
            set.config.mapping.applyW   = m.value("applyW",   true);
        }
    }

    set.Recalculate();

    std::cout << "[EdgeTransferStore] Loaded set: " << set.name
              << " ref=" << (set.referenceEdge.valid ? "valid" : "empty")
              << " tgt=" << (set.targetEdge.valid ? "valid" : "empty") << std::endl;
    return set;
}

std::vector<std::string> EdgeTransferStore::ListSets() const
{
    std::vector<std::string> names;
    std::filesystem::path dir(m_basePath);
    if (!std::filesystem::exists(dir)) return names;

    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            names.push_back(entry.path().stem().string());
        }
    }
    return names;
}

bool EdgeTransferStore::DeleteSet(const std::string& name)
{
    std::string filePath = MakeFilePath(name);
    if (std::filesystem::exists(filePath)) {
        std::filesystem::remove(filePath);
        std::cout << "[EdgeTransferStore] Deleted set: " << name << std::endl;
        return true;
    }
    return false;
}
