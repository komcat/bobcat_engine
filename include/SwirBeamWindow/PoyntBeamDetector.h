// PoyntBeamDetector.h
// Faithful C++ port of the poynt `server_v2.py` live beam-tracking path.
//
// This is a swappable SWIR detection mode that mirrors the *verbatim* poynt
// algorithm: 4-corner background subtraction, X/Y projection peak finding
// (find_roi), and per-ROI d4sigma on the amplitude field — with NO
// post-detection rejection filters and NO merge/dedup. It exists alongside the
// legacy d4sigma / blob / halcon modes in SwirBeamDetector so the two can be
// A/B compared.
//
// It reuses the shared helpers exported by SwirBeamDetector.h (find_roi,
// d4sigma_fast, ComputeEncircledEnergy) and returns the same
// BeamDetectionResult so the window's tracking, overlay, charts, and broker are
// unchanged. Scope is live per-frame detection only — the z-scan FIT path
// (waist/M²/Poynting) is out of scope and stays on the Python beam_server.
#pragma once

#include "SwirBeamDetector.h"
#include <cstdint>
#include <vector>

/// Config for the poynt-faithful detection mode.
/// Defaults mirror `poynt/config.json` process_info.dip_kwargs and the
/// server_v2 hard-coded constants (width_fact=2.5, threshold=0.1, 50px BG box).
struct PoyntDetectionConfig {
    PeakFindParams xpeaks{ 1200.0, 7.5, -1.0 };  // height, width, distance(off)
    PeakFindParams ypeaks{  600.0, 7.5, -1.0 };
    double  combineXThresh   = 100.0;  // combine_xpeaks_threshold
    double  combineYThresh   = 50.0;   // combine_ypeaks_threshold
    double  d4sigmaThreshold = 0.1;    // threshold_field cutoff
    double  roiWidthFactor   = 2.5;    // width_fact
    int     bgCornerBox      = 50;     // 4-corner BG patch side (px)
    int     maxBeams         = 2;      // cap for B0/B1 slot pipeline (integration constraint)
    uint8_t saturationValue  = 255;
    bool    excludeSaturated = false;  // server_v2 does NOT mask saturated pixels
    double  micronsPerPixel  = 0.17;   // overlay calibration
    GaQuadrantLabels gaLabels;
};

#ifdef HAS_OPENCV

/// Run the poynt-faithful detection pipeline on a grayscale uint8 image.
/// Returns up to config.maxBeams results (ranked by intensity, then cy-sorted
/// B0=top/B1=bottom), labeled B0/B1 with GA assignments — same contract as
/// RunBeamDetection so the caller's tracking/overlay code is unchanged.
std::vector<BeamDetectionResult> RunPoyntDetection(
    const uint8_t* grayData, uint32_t width, uint32_t height,
    const PoyntDetectionConfig& config);

#endif // HAS_OPENCV
