// PlacementRetraceLogger.cpp — see header for design notes.
#include "PlacementRetraceLogger.h"
#include "product_position_manager.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

using json = nlohmann::json;

static constexpr std::uintmax_t kRotationSizeBytes = 10 * 1024 * 1024;  // 10 MB

static json PointMetaToJson(const RetracePointMeta& m) {
    return {
        {"autoDetected",    m.autoDetected},
        {"visionIters",     m.visionIters},
        {"finalResidualMm", m.finalResidualMm},
        {"reason",          m.reason}
    };
}

static json EntryToJson(const RetraceLogEntry& e) {
    json j;
    j["timestamp"]           = e.timestamp;
    j["setName"]             = e.setName;
    j["setUid"]              = e.setUid;
    j["mode"]                = e.mode;
    j["outcome"]             = e.outcome;
    j["measureDevice"]       = e.measureDevice;
    j["targetDevice"]        = e.targetDevice;
    j["masterPositionName"]  = e.masterPositionName;
    j["targetPositionName"]  = e.targetPositionName;

    j["retracedRefP1"] = { {"x", e.retracedRefP1x}, {"y", e.retracedRefP1y}, {"z", e.retracedRefP1z} };
    j["retracedRefP2"] = { {"x", e.retracedRefP2x}, {"y", e.retracedRefP2y}, {"z", e.retracedRefP2z} };
    j["retracedNrmP1"] = { {"x", e.retracedNrmP1x}, {"y", e.retracedNrmP1y}, {"z", e.retracedNrmP1z} };
    j["retracedNrmP2"] = { {"x", e.retracedNrmP2x}, {"y", e.retracedNrmP2y}, {"z", e.retracedNrmP2z} };
    j["retracedRefAngleDeg"] = e.retracedRefAngleDeg;
    j["retracedNrmAngleDeg"] = e.retracedNrmAngleDeg;

    j["taughtRefAngleDeg"]   = e.taughtRefAngleDeg;
    j["frameDeltaThetaDeg"]  = e.frameDeltaThetaDeg;
    j["uCorrectionDeg"]      = e.uCorrectionDeg;

    j["designOffsetTaught"]  = { {"dx", e.designOffsetX},  {"dy", e.designOffsetY} };
    j["designOffsetRotated"] = { {"dx", e.rotatedOffsetX}, {"dy", e.rotatedOffsetY} };
    j["idealTargetGlobal"]   = { {"x",  e.idealTargetX},   {"y",  e.idealTargetY} };
    j["correctionGlobal"]    = { {"dx", e.correctionGlobalX}, {"dy", e.correctionGlobalY} };
    j["correctionLocal"]     = { {"dx", e.correctionLocalX}, {"dy", e.correctionLocalY},
                                 {"dz", e.correctionLocalZ} };

    j["liveHex"]   = { {"x", e.liveHexX}, {"y", e.liveHexY}, {"z", e.liveHexZ},
                       {"u", e.liveHexU}, {"v", e.liveHexV}, {"w", e.liveHexW} };
    j["tuneOffset"] = { {"x", e.tuneOffsetX}, {"y", e.tuneOffsetY}, {"z", e.tuneOffsetZ},
                        {"u", e.tuneOffsetU}, {"v", e.tuneOffsetV}, {"w", e.tuneOffsetW} };
    j["applyUCorrection"] = e.applyUCorrection;

    j["corrected"] = { {"x", e.correctedX}, {"y", e.correctedY}, {"z", e.correctedZ},
                       {"u", e.correctedU}, {"v", e.correctedV}, {"w", e.correctedW} };

    j["perStepMeta"] = {
        {"refP1", PointMetaToJson(e.steps[0])},
        {"refP2", PointMetaToJson(e.steps[1])},
        {"nrmP1", PointMetaToJson(e.steps[2])},
        {"nrmP2", PointMetaToJson(e.steps[3])}
    };

    if (!e.failureStep.empty())   j["failureStep"]   = e.failureStep;
    if (!e.failureDetail.empty()) j["failureDetail"] = e.failureDetail;
    return j;
}

static void RotateIfNeeded(const std::string& path) {
    std::error_code ec;
    auto sz = std::filesystem::file_size(path, ec);
    if (ec) return;   // file may not exist yet — fine
    if (sz < kRotationSizeBytes) return;

    std::string backup = path + ".1";
    std::filesystem::remove(backup, ec);                  // ignore failure
    std::filesystem::rename(path, backup, ec);
    if (ec) {
        std::cerr << "[PlacementRetraceLogger] rotate FAILED: " << ec.message() << std::endl;
    } else {
        std::cout << "[PlacementRetraceLogger] rotated " << path << " → " << backup << std::endl;
    }
}

std::string PlacementRetraceLogger::MakeLogPath(const std::string& productDir,
                                                  const std::string& setNameOrSourceFile)
{
    std::filesystem::path base;
    if (!productDir.empty()) {
        base = std::filesystem::path(productDir) / "placement_correction";
    } else {
        base = std::filesystem::path("config") / "placement_correction";
    }
    // Sanitize the filename piece — disallow path separators.
    std::string safe = setNameOrSourceFile;
    for (char& c : safe) {
        if (c == '/' || c == '\\') c = '_';
    }
    return (base / (safe + ".retrace_log.jsonl")).string();
}

bool PlacementRetraceLogger::Append(const std::string& logPath,
                                     const RetraceLogEntry& entry)
{
    try {
        std::filesystem::path p(logPath);
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        // ec ignored: open will fail visibly if the dir really can't be made.

        RotateIfNeeded(logPath);

        std::ofstream ofs(logPath, std::ios::app | std::ios::binary);
        if (!ofs.is_open()) {
            std::cerr << "[PlacementRetraceLogger] open for append FAILED: "
                      << logPath << std::endl;
            return false;
        }
        json j = EntryToJson(entry);
        ofs << j.dump() << "\n";
        ofs.close();
        std::cout << "[PlacementRetraceLogger] appended " << entry.outcome
                  << " entry to " << logPath << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[PlacementRetraceLogger] Append exception: "
                  << e.what() << std::endl;
        return false;
    }
}

std::vector<json> PlacementRetraceLogger::ReadAll(const std::string& logPath)
{
    std::vector<json> out;
    std::ifstream ifs(logPath);
    if (!ifs.is_open()) return out;

    std::string line;
    int lineNo = 0;
    while (std::getline(ifs, line)) {
        ++lineNo;
        if (line.empty()) continue;
        try {
            out.push_back(json::parse(line));
        } catch (const json::parse_error& e) {
            std::cerr << "[PlacementRetraceLogger] parse error on line "
                      << lineNo << " of " << logPath << ": " << e.what() << std::endl;
        }
    }
    return out;
}

std::string PlacementRetraceLogger::ResolvePathForSet(
    const PlacementCorrectionSet& set,
    void* productPositionMgr)
{
    std::string productDir;
    auto* ppm = reinterpret_cast<ProductPositionManager*>(productPositionMgr);
    if (ppm && ppm->HasLoadedProduct()) {
        std::filesystem::path productPath(ppm->GetCurrentProductPath());
        productDir = productPath.parent_path().string();
    }
    std::string nameKey = !set.sourceFilename.empty() ? set.sourceFilename : set.name;
    return MakeLogPath(productDir, nameKey);
}
