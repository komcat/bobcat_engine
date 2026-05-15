// SwirBeamConfigStore.cpp
#include "SwirBeamConfigStore.h"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace SwirBeamConfigStore {

std::string Load(BeamDetectionConfig& d4,
                 BlobDetectionConfig& blob,
                 HalconBeamDetectionConfig& halcon,
                 bool& useBlob,
                 bool& useHalcon,
                 bool& trackingEnabled,
                 double& trackingMaxJumpPx,
                 int& trackingStaleMax)
{
    // Try local override first, then default.
    std::string path = "config/beam_detection.local.json";
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        path = "config/beam_detection.json";
        ifs.open(path);
    }
    if (!ifs.is_open()) {
        std::cout << "[SwirBeamWindow] No beam_detection.json found, using defaults" << std::endl;
        return {};
    }

    auto j = nlohmann::json::parse(ifs, nullptr, false);
    if (j.is_discarded()) {
        std::cerr << "[SwirBeamWindow] Failed to parse " << path << std::endl;
        return {};
    }

    auto getD = [&](const char* key, double& out) {
        if (j.contains(key) && j[key].is_number()) out = j[key].get<double>();
    };
    auto getI = [&](const char* key, int& out) {
        if (j.contains(key) && j[key].is_number_integer()) out = j[key].get<int>();
    };
    auto getPeak = [&](const char* key, PeakFindParams& p) {
        if (!j.contains(key) || !j[key].is_object()) return;
        auto& sub = j[key];
        if (sub.contains("height") && sub["height"].is_number()) p.height = sub["height"].get<double>();
        if (sub.contains("width") && sub["width"].is_number()) p.width = sub["width"].get<double>();
        if (sub.contains("distance") && sub["distance"].is_number()) p.distance = sub["distance"].get<double>();
    };

    getPeak("xpeaks", d4.xpeaks);
    getPeak("ypeaks", d4.ypeaks);
    getD("combineXThresh", d4.combineXThresh);
    getD("combineYThresh", d4.combineYThresh);
    getD("d4sigmaThreshold", d4.d4sigmaThreshold);
    getI("maxBeams", d4.maxBeams);
    if (j.contains("saturationValue") && j["saturationValue"].is_number_integer()) {
        d4.saturationValue = static_cast<uint8_t>(j["saturationValue"].get<int>());
    }
    getD("roiWidthFactor", d4.roiWidthFactor);
    getD("minD4Sigma", d4.minD4Sigma);
    getD("minPeakValue", d4.minPeakValue);
    getD("aspectRatioMax", d4.aspectRatioMax);
    getD("roiSizeFactor", d4.roiSizeFactor);
    if (j.contains("rejectEdgeClipped") && j["rejectEdgeClipped"].is_boolean()) {
        d4.rejectEdgeClipped = j["rejectEdgeClipped"].get<bool>();
    }
    if (j.contains("excludeSaturated") && j["excludeSaturated"].is_boolean()) {
        d4.excludeSaturated = j["excludeSaturated"].get<bool>();
    }
    getD("micronsPerPixel", d4.micronsPerPixel);

    // Beam ID tracking (caller-side, applies to both d4sigma and blob paths).
    if (j.contains("tracking") && j["tracking"].is_object()) {
        auto& tj = j["tracking"];
        if (tj.contains("enabled") && tj["enabled"].is_boolean()) {
            trackingEnabled = tj["enabled"].get<bool>();
        }
        if (tj.contains("maxJumpPx") && tj["maxJumpPx"].is_number()) {
            trackingMaxJumpPx = tj["maxJumpPx"].get<double>();
        }
        if (tj.contains("staleFrames") && tj["staleFrames"].is_number_integer()) {
            trackingStaleMax = tj["staleFrames"].get<int>();
        }
    }

    // Quadrant -> GA label mapping (shared between d4sigma and blob modes).
    // Quadrants are in visual orientation (after display Y-flip):
    //   q1=right-top, q2=left-top, q3=left-bottom, q4=right-bottom.
    if (j.contains("gaQuadrantLabels") && j["gaQuadrantLabels"].is_object()) {
        auto& qj = j["gaQuadrantLabels"];
        auto getS = [&](const char* key, std::string& out) {
            if (qj.contains(key) && qj[key].is_string()) out = qj[key].get<std::string>();
        };
        getS("q1", d4.gaLabels.q1);
        getS("q2", d4.gaLabels.q2);
        getS("q3", d4.gaLabels.q3);
        getS("q4", d4.gaLabels.q4);
        blob.gaLabels = d4.gaLabels;
    }

    // Blob detection config.
    if (j.contains("useBlobDetection") && j["useBlobDetection"].is_boolean()) {
        useBlob = j["useBlobDetection"].get<bool>();
    }
    if (j.contains("blob") && j["blob"].is_object()) {
        auto& bj = j["blob"];
        auto getBD = [&](const char* key, double& out) {
            if (bj.contains(key) && bj[key].is_number()) out = bj[key].get<double>();
        };
        auto getBI = [&](const char* key, int& out) {
            if (bj.contains(key) && bj[key].is_number_integer()) out = bj[key].get<int>();
        };
        getBI("maxBeams", blob.maxBeams);
        if (bj.contains("saturationValue") && bj["saturationValue"].is_number_integer()) {
            blob.saturationValue = static_cast<uint8_t>(bj["saturationValue"].get<int>());
        }
        getBI("gaussianKernel", blob.gaussianKernel);
        getBD("thresholdValue", blob.thresholdValue);
        if (bj.contains("useOtsu") && bj["useOtsu"].is_boolean()) {
            blob.useOtsu = bj["useOtsu"].get<bool>();
        }
        getBD("minBlobArea", blob.minBlobArea);
        getBD("maxBlobArea", blob.maxBlobArea);
        getBD("roiPadding", blob.roiPadding);
    }

    // Halcon detection config (HDevelop port).
    if (j.contains("useHalconDetection") && j["useHalconDetection"].is_boolean()) {
        useHalcon = j["useHalconDetection"].get<bool>();
    }
    if (j.contains("halcon") && j["halcon"].is_object()) {
        auto& hj = j["halcon"];
        auto getHI = [&](const char* key, int& out) {
            if (hj.contains(key) && hj[key].is_number_integer()) out = hj[key].get<int>();
        };
        auto getHD = [&](const char* key, double& out) {
            if (hj.contains(key) && hj[key].is_number()) out = hj[key].get<double>();
        };
        getHI("bgKernelSize", halcon.bgKernelSize);
        getHI("noiseThreshold", halcon.noiseThreshold);
        getHI("minBeamArea", halcon.minBeamArea);
        getHI("maxBeams", halcon.maxBeams);
        getHD("widthFactor", halcon.widthFactor);
        if (hj.contains("saturationValue") && hj["saturationValue"].is_number_integer()) {
            halcon.saturationValue = static_cast<uint8_t>(hj["saturationValue"].get<int>());
        }
        getHD("micronsPerPixel", halcon.micronsPerPixel);
        getHI("detectionDownsample", halcon.detectionDownsample);
    }
    // Halcon shares the GA quadrant labels with the d4sigma path.
    halcon.gaLabels = d4.gaLabels;

    const char* modeStr = useHalcon ? "halcon" : (useBlob ? "blob" : "d4sigma");
    std::cout << "[SwirBeamWindow] Beam config loaded from " << path
              << " (mode: " << modeStr << ")" << std::endl;
    return path;
}

bool Save(const BeamDetectionConfig& d4,
          const BlobDetectionConfig& blob,
          const HalconBeamDetectionConfig& halcon,
          bool useBlob,
          bool useHalcon,
          bool trackingEnabled,
          double trackingMaxJumpPx,
          int trackingStaleMax)
{
    nlohmann::json j;
    j["xpeaks"] = {{"height", d4.xpeaks.height},
                    {"width",  d4.xpeaks.width},
                    {"distance", d4.xpeaks.distance}};
    j["ypeaks"] = {{"height", d4.ypeaks.height},
                    {"width",  d4.ypeaks.width},
                    {"distance", d4.ypeaks.distance}};
    j["combineXThresh"] = d4.combineXThresh;
    j["combineYThresh"] = d4.combineYThresh;
    j["d4sigmaThreshold"] = d4.d4sigmaThreshold;
    j["maxBeams"] = d4.maxBeams;
    j["saturationValue"] = static_cast<int>(d4.saturationValue);
    j["roiWidthFactor"] = d4.roiWidthFactor;
    j["minD4Sigma"] = d4.minD4Sigma;
    j["minPeakValue"] = d4.minPeakValue;
    j["aspectRatioMax"] = d4.aspectRatioMax;
    j["roiSizeFactor"] = d4.roiSizeFactor;
    j["rejectEdgeClipped"] = d4.rejectEdgeClipped;
    j["excludeSaturated"] = d4.excludeSaturated;
    j["micronsPerPixel"] = d4.micronsPerPixel;

    j["tracking"] = {
        {"enabled",     trackingEnabled},
        {"maxJumpPx",   trackingMaxJumpPx},
        {"staleFrames", trackingStaleMax}
    };

    j["gaQuadrantLabels"] = {
        {"q1", d4.gaLabels.q1},
        {"q2", d4.gaLabels.q2},
        {"q3", d4.gaLabels.q3},
        {"q4", d4.gaLabels.q4}
    };

    // Blob detection config.
    j["useBlobDetection"] = useBlob;
    j["blob"] = {
        {"maxBeams",        blob.maxBeams},
        {"saturationValue", static_cast<int>(blob.saturationValue)},
        {"gaussianKernel",  blob.gaussianKernel},
        {"thresholdValue",  blob.thresholdValue},
        {"useOtsu",         blob.useOtsu},
        {"minBlobArea",     blob.minBlobArea},
        {"maxBlobArea",     blob.maxBlobArea},
        {"roiPadding",      blob.roiPadding}
    };

    // Halcon detection config (HDevelop port).
    j["useHalconDetection"] = useHalcon;
    j["halcon"] = {
        {"bgKernelSize",        halcon.bgKernelSize},
        {"noiseThreshold",      halcon.noiseThreshold},
        {"minBeamArea",         halcon.minBeamArea},
        {"maxBeams",            halcon.maxBeams},
        {"widthFactor",         halcon.widthFactor},
        {"saturationValue",     static_cast<int>(halcon.saturationValue)},
        {"micronsPerPixel",     halcon.micronsPerPixel},
        {"detectionDownsample", halcon.detectionDownsample}
    };

    std::ofstream ofs("config/beam_detection.local.json");
    if (ofs.is_open()) {
        ofs << j.dump(4) << std::endl;
        std::cout << "[SwirBeamWindow] Beam config saved to beam_detection.local.json" << std::endl;
        return true;
    }
    std::cerr << "[SwirBeamWindow] Failed to save beam_detection.local.json" << std::endl;
    return false;
}

}  // namespace SwirBeamConfigStore
