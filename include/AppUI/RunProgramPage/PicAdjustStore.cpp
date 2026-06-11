#include "PicAdjustStore.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using nlohmann::json;

std::string PicAdjustStore::DefaultFilePath() {
    return "config/pic_adjust.local.json";
}

bool PicAdjustStore::Save(const PicAdjustSet& set,
                          const PicAdjustSettings& settings,
                          const std::string& filePath) {
    json j;
    j["version"] = 2;
    json& pts = j["points"];
    for (int i = 0; i < kPicPointCount; ++i) {
        const auto& p = set.points[i];
        const char* name = PicPointName(static_cast<PicPointId>(i));
        json o;
        o["captured"] = p.captured;
        o["x"] = p.x;
        o["y"] = p.y;
        o["z"] = p.z;
        pts[name] = o;
    }
    json& sj = j["settings"];
    sj["invertU"] = settings.invertU;
    sj["invertV"] = settings.invertV;
    sj["invertW"] = settings.invertW;
    sj["targetDevice"] = settings.targetDevice;
    sj["targetDistanceUm"] = settings.targetDistanceUm;
    sj["invertDistance"] = settings.invertDistance;
    sj["lockSigns"]      = settings.lockSigns;
    sj["overwriteOnComplete"] = settings.overwriteOnComplete;
    sj["autoFocusSwir"]       = settings.autoFocusSwir;
    try {
        std::filesystem::path path(filePath);
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        std::ofstream out(filePath);
        if (!out) return false;
        out << j.dump(2);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[PicAdjustStore] Save failed: " << e.what() << std::endl;
        return false;
    }
}

bool PicAdjustStore::Load(PicAdjustSet& outSet,
                          PicAdjustSettings& outSettings,
                          const std::string& filePath) {
    if (!std::filesystem::exists(filePath)) return false;
    try {
        std::ifstream in(filePath);
        if (!in) return false;
        json j;
        in >> j;
        const json& pts = j.value("points", json::object());
        for (int i = 0; i < kPicPointCount; ++i) {
            const char* name = PicPointName(static_cast<PicPointId>(i));
            if (!pts.contains(name)) continue;
            const json& o = pts[name];
            auto& p = outSet.points[i];
            p.captured = o.value("captured", false);
            p.x = o.value("x", 0.0);
            p.y = o.value("y", 0.0);
            p.z = o.value("z", 0.0);
        }
        if (j.contains("settings")) {
            const json& sj = j["settings"];
            outSettings.invertU          = sj.value("invertU", outSettings.invertU);
            outSettings.invertV          = sj.value("invertV", outSettings.invertV);
            outSettings.invertW          = sj.value("invertW", outSettings.invertW);
            outSettings.targetDevice     = sj.value("targetDevice", outSettings.targetDevice);
            outSettings.targetDistanceUm = sj.value("targetDistanceUm", outSettings.targetDistanceUm);
            outSettings.invertDistance   = sj.value("invertDistance", outSettings.invertDistance);
            outSettings.lockSigns        = sj.value("lockSigns",      outSettings.lockSigns);
            outSettings.overwriteOnComplete = sj.value("overwriteOnComplete", outSettings.overwriteOnComplete);
            outSettings.autoFocusSwir       = sj.value("autoFocusSwir",       outSettings.autoFocusSwir);
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[PicAdjustStore] Load failed: " << e.what() << std::endl;
        return false;
    }
}
