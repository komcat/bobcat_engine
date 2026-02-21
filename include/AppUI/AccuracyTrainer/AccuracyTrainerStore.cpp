// AccuracyTrainerStore.cpp - JSON persistence for training sets
#include "AccuracyTrainerStore.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>

using json = nlohmann::json;

AccuracyTrainerStore::AccuracyTrainerStore()
    : m_basePath("config/accuracy_trainer")
{
}

void AccuracyTrainerStore::SetBasePath(const std::string& basePath)
{
    m_basePath = basePath;
}

void AccuracyTrainerStore::EnsureDirectory() const
{
    std::filesystem::path dir(m_basePath);
    if (!std::filesystem::exists(dir)) {
        std::filesystem::create_directories(dir);
    }
}

std::string AccuracyTrainerStore::MakeFilePath(const std::string& name) const
{
    return m_basePath + "/" + name + ".json";
}

bool AccuracyTrainerStore::SaveTrainingSet(const TrainingSet& ts)
{
    EnsureDirectory();

    json j;
    j["name"] = ts.name;
    j["deviceName"] = ts.deviceName;
    j["createdAt"] = ts.createdAt;
    j["valid"] = ts.valid;

    // Reference frame axes
    j["origin"] = { {"x", ts.originX}, {"y", ts.originY} };
    j["xAxis"] = { {"x", ts.xAxisX}, {"y", ts.xAxisY} };
    j["yAxis"] = { {"x", ts.yAxisX}, {"y", ts.yAxisY} };

    // Reference points
    json refArray = json::array();
    for (const auto& rp : ts.referencePoints) {
        json rj;
        rj["name"] = rp.name;
        rj["globalX"] = rp.globalX;
        rj["globalY"] = rp.globalY;
        rj["globalZ"] = rp.globalZ;
        rj["visionProfilePath"] = rp.visionProfilePath;
        rj["valid"] = rp.valid;
        refArray.push_back(rj);
    }
    j["referencePoints"] = refArray;

    // Bonding points
    json bondArray = json::array();
    for (const auto& bp : ts.bondingPoints) {
        json bj;
        bj["name"] = bp.name;
        bj["corners"] = {
            {"tl", {bp.cornerTL_X, bp.cornerTL_Y}},
            {"br", {bp.cornerBR_X, bp.cornerBR_Y}}
        };
        if (bp.hasThirdCorner) {
            bj["corners"]["tr"] = {bp.cornerTR_X, bp.cornerTR_Y};
        } else {
            bj["corners"]["tr"] = nullptr;
        }
        bj["hasThirdCorner"] = bp.hasThirdCorner;
        bj["globalX"] = bp.globalX;
        bj["globalY"] = bp.globalY;
        bj["globalZ"] = bp.globalZ;
        bj["localX"] = bp.localX;
        bj["localY"] = bp.localY;
        bj["valid"] = bp.valid;
        bondArray.push_back(bj);
    }
    j["bondingPoints"] = bondArray;

    // Hex device teach positions
    json hexArray = json::array();
    for (const auto& hd : ts.hexDevicePositions) {
        json hj;
        hj["deviceName"] = hd.deviceName;
        hj["hexPos"] = { {"x", hd.hexX}, {"y", hd.hexY}, {"z", hd.hexZ},
                         {"u", hd.hexU}, {"v", hd.hexV}, {"w", hd.hexW} };
        hj["gantryView"] = { {"x", hd.gantryViewX}, {"y", hd.gantryViewY}, {"z", hd.gantryViewZ} };
        hj["visionProfilePath"] = hd.visionProfilePath;
        hj["valid"] = hd.valid;
        hexArray.push_back(hj);
    }
    j["hexDevicePositions"] = hexArray;

    // Bonding device assignments
    json assignObj = json::object();
    for (const auto& [bondName, deviceName] : ts.bondingDeviceAssignments) {
        assignObj[bondName] = deviceName;
    }
    j["bondingDeviceAssignments"] = assignObj;

    // Save to source file if loaded from one, otherwise use internal name
    std::string saveAs = ts.sourceFilename.empty() ? ts.name : ts.sourceFilename;
    std::string filePath = MakeFilePath(saveAs);
    std::ofstream ofs(filePath);
    if (!ofs.is_open()) {
        std::cerr << "[AccuracyTrainerStore] Failed to open file for writing: " << filePath << std::endl;
        return false;
    }

    ofs << j.dump(2);
    ofs.close();

    std::cout << "[AccuracyTrainerStore] Saved training set: " << ts.name << " -> " << filePath << std::endl;
    return true;
}

std::optional<TrainingSet> AccuracyTrainerStore::LoadTrainingSet(const std::string& name)
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
                    std::cout << "[AccuracyTrainerStore] Found '" << name << "' by internal name in: "
                              << filePath << std::endl;
                    break;
                }
            }
        }
    }

    if (!ifs.is_open()) {
        std::cerr << "[AccuracyTrainerStore] Failed to find training set: " << name << std::endl;
        return std::nullopt;
    }

    json j;
    try {
        ifs >> j;
    } catch (const json::parse_error& e) {
        std::cerr << "[AccuracyTrainerStore] JSON parse error in " << filePath << ": " << e.what() << std::endl;
        return std::nullopt;
    }

    TrainingSet ts;
    ts.name = j.value("name", "");
    ts.deviceName = j.value("deviceName", "");
    ts.createdAt = j.value("createdAt", "");
    ts.valid = j.value("valid", false);

    // Track source filename so SaveTrainingSet writes back to the same file
    ts.sourceFilename = std::filesystem::path(filePath).stem().string();

    // Reference frame axes
    if (j.contains("origin")) {
        ts.originX = j["origin"].value("x", 0.0);
        ts.originY = j["origin"].value("y", 0.0);
    }
    if (j.contains("xAxis")) {
        ts.xAxisX = j["xAxis"].value("x", 1.0);
        ts.xAxisY = j["xAxis"].value("y", 0.0);
    }
    if (j.contains("yAxis")) {
        ts.yAxisX = j["yAxis"].value("x", 0.0);
        ts.yAxisY = j["yAxis"].value("y", 1.0);
    }

    // Reference points
    if (j.contains("referencePoints") && j["referencePoints"].is_array()) {
        for (const auto& rj : j["referencePoints"]) {
            ReferencePoint rp;
            rp.name = rj.value("name", "");
            rp.globalX = rj.value("globalX", 0.0);
            rp.globalY = rj.value("globalY", 0.0);
            rp.globalZ = rj.value("globalZ", 0.0);
            rp.visionProfilePath = rj.value("visionProfilePath", "");
            rp.valid = rj.value("valid", false);
            ts.referencePoints.push_back(rp);
        }
    }

    // Bonding points
    if (j.contains("bondingPoints") && j["bondingPoints"].is_array()) {
        for (const auto& bj : j["bondingPoints"]) {
            BondingPoint bp;
            bp.name = bj.value("name", "");
            bp.globalX = bj.value("globalX", 0.0);
            bp.globalY = bj.value("globalY", 0.0);
            bp.globalZ = bj.value("globalZ", 0.0);
            bp.localX = bj.value("localX", 0.0);
            bp.localY = bj.value("localY", 0.0);
            bp.valid = bj.value("valid", false);
            bp.hasThirdCorner = bj.value("hasThirdCorner", false);

            if (bj.contains("corners")) {
                const auto& corners = bj["corners"];
                if (corners.contains("tl") && corners["tl"].is_array() && corners["tl"].size() >= 2) {
                    bp.cornerTL_X = corners["tl"][0].get<double>();
                    bp.cornerTL_Y = corners["tl"][1].get<double>();
                }
                if (corners.contains("br") && corners["br"].is_array() && corners["br"].size() >= 2) {
                    bp.cornerBR_X = corners["br"][0].get<double>();
                    bp.cornerBR_Y = corners["br"][1].get<double>();
                }
                if (corners.contains("tr") && corners["tr"].is_array() && corners["tr"].size() >= 2) {
                    bp.cornerTR_X = corners["tr"][0].get<double>();
                    bp.cornerTR_Y = corners["tr"][1].get<double>();
                }
            }

            ts.bondingPoints.push_back(bp);
        }
    }

    // Hex device teach positions
    if (j.contains("hexDevicePositions") && j["hexDevicePositions"].is_array()) {
        for (const auto& hj : j["hexDevicePositions"]) {
            HexDeviceTeachData hd;
            hd.deviceName = hj.value("deviceName", "");
            hd.valid = hj.value("valid", false);
            if (hj.contains("hexPos")) {
                hd.hexX = hj["hexPos"].value("x", 0.0);
                hd.hexY = hj["hexPos"].value("y", 0.0);
                hd.hexZ = hj["hexPos"].value("z", 0.0);
                hd.hexU = hj["hexPos"].value("u", 0.0);
                hd.hexV = hj["hexPos"].value("v", 0.0);
                hd.hexW = hj["hexPos"].value("w", 0.0);
            }
            if (hj.contains("gantryView")) {
                hd.gantryViewX = hj["gantryView"].value("x", 0.0);
                hd.gantryViewY = hj["gantryView"].value("y", 0.0);
                hd.gantryViewZ = hj["gantryView"].value("z", 0.0);
            }
            hd.visionProfilePath = hj.value("visionProfilePath", "");
            ts.hexDevicePositions.push_back(hd);
        }
    }

    // Bonding device assignments
    if (j.contains("bondingDeviceAssignments") && j["bondingDeviceAssignments"].is_object()) {
        for (auto& [key, val] : j["bondingDeviceAssignments"].items()) {
            if (val.is_string()) {
                ts.bondingDeviceAssignments[key] = val.get<std::string>();
            }
        }
    }

    std::cout << "[AccuracyTrainerStore] Loaded training set: " << ts.name
              << " (" << ts.referencePoints.size() << " refs, "
              << ts.bondingPoints.size() << " bonds, "
              << ts.hexDevicePositions.size() << " hex teach, "
              << ts.bondingDeviceAssignments.size() << " assignments)" << std::endl;
    return ts;
}

std::vector<std::string> AccuracyTrainerStore::ListTrainingSets() const
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

bool AccuracyTrainerStore::DeleteTrainingSet(const std::string& name)
{
    std::string filePath = MakeFilePath(name);
    if (std::filesystem::exists(filePath)) {
        std::filesystem::remove(filePath);
        std::cout << "[AccuracyTrainerStore] Deleted training set: " << name << std::endl;
        return true;
    }
    std::cerr << "[AccuracyTrainerStore] File not found for deletion: " << filePath << std::endl;
    return false;
}
