// SwirBeamDetector.h
// Real-time beam detection with d4sigma characterization for SWIR camera
#pragma once

#include "SwirPeakFinder.h"
#include <array>
#include <vector>
#include <string>
#include <cstdint>

#ifdef HAS_OPENCV
#include <opencv2/core.hpp>
#endif

struct Roi {
    int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
};

struct D4SigmaResult {
    double cx = 0, cy = 0;   // centroid (pixels)
    double dx = 0, dy = 0;   // d4sigma diameters (pixels)
    Roi roi;
};

struct BeamDetectionResult {
    D4SigmaResult d4;
    std::string label;             // "B0" or "B1"
    std::string gaLabel;           // "GA1", "GA2", or ""
    bool saturated = false;        // any pixel > saturationValue
    uint8_t peakIntensity = 0;     // max raw pixel value in beam ROI
    int saturatedPixelCount = 0;   // count of pixels at 255 in beam ROI
    double encircledEnergy = 0.0;  // EE within d4sigma/2 radius (0.0-1.0)
    double circleEnergy = 0.0;     // CE: energy inside the EE circle (arbitrary units)
    double totalEnergy = 0.0;      // TE: total energy in the ROI (arbitrary units)
    std::vector<double> xProfile;  // 1D cross-section at centroid row
    std::vector<double> yProfile;  // 1D cross-section at centroid col
    int profileXStart = 0;         // ROI x0 offset for chart alignment
    int profileYStart = 0;         // ROI y0 offset for chart alignment
};

/// GA quadrant label mapping.
/// Quadrants are in VISUAL orientation (after display Y-flip):
///   [0]=Q1 right-top, [1]=Q2 left-top, [2]=Q3 left-bottom, [3]=Q4 right-bottom.
/// Default: top half (Q1,Q2)=GA2, bottom half (Q3,Q4)=GA1.
struct GaQuadrantLabels {
    std::string q1 = "GA2";  // right-top
    std::string q2 = "GA2";  // left-top
    std::string q3 = "GA1";  // left-bottom
    std::string q4 = "GA1";  // right-bottom
};

struct BeamDetectionConfig {
    PeakFindParams xpeaks;
    PeakFindParams ypeaks;
    double combineXThresh = 20.0;
    double combineYThresh = 20.0;
    double d4sigmaThreshold = 0.05;
    int maxBeams = 2;
    uint8_t saturationValue = 255;
    double roiWidthFactor = 2.5;
    double minD4Sigma = 3.0;        // reject beams with max(dx,dy) < this
    double minPeakValue = 5.0;      // reject beams with ROI peak < this (after bg sub)
    double aspectRatioMax = 10.0;   // reject beams with max(dx,dy)/min(dx,dy) > this
    double roiSizeFactor = 3.0;     // reject beams with d4 > roi * this (diffuse noise)
    bool   rejectEdgeClipped = false; // skip candidates whose EE circle clips the image edge
    double micronsPerPixel = 0.17;  // SWIR camera calibration for overlay (um/px)
    bool   excludeSaturated = true; // mask saturated pixels from centroid moments
    GaQuadrantLabels gaLabels;
};

/// Config for Halcon connected-component beam detection.
/// Direct port of the HDevelop reference script. Only filter is the area
/// minimum from the source script — no max area, no D4sigma min, no aspect
/// ratio cap (script doesn't have those). maxBeams cap is an integration
/// constraint of the B0/B1 slot pipeline, not part of the algorithm.
struct HalconBeamDetectionConfig {
    int    bgKernelSize    = 51;     // mean_image kernel side (px); 0 = no bg sub
    int    noiseThreshold  = 20;     // gray threshold after bg subtract
    int    minBeamArea     = 10;     // px^2 minimum to keep a region
    int    maxBeams        = 2;      // cap on returned beams (B0/B1 slots)
    double widthFactor     = 2.5;    // ROI half-width factor for crop bounds
    uint8_t saturationValue = 255;
    double micronsPerPixel = 0.17;
    // Run mean/threshold/connection stage on a 1/N-resolution copy of the
    // image to cut CPU. Centroid coords are mapped back to full-res for
    // display and downstream measurements. 1 = full-res, 2 = half-res
    // (~4x cheaper, ~0.5px less centroid precision in source units).
    int detectionDownsample = 1;
    GaQuadrantLabels gaLabels;
};

/// Config for simple blob-based beam detection
struct BlobDetectionConfig {
    int maxBeams = 2;
    uint8_t saturationValue = 255;
    int gaussianKernel = 5;         // blur kernel size (odd number, 0=disabled)
    double thresholdValue = 180.0;  // fixed threshold (0-255), ignored if useOtsu=true
    bool useOtsu = false;           // use Otsu auto-threshold instead of fixed
    double minBlobArea = 50.0;      // reject blobs smaller than this (pixels^2)
    double maxBlobArea = 50000.0;   // reject blobs larger than this (pixels^2)
    double roiPadding = 1.5;        // bounding rect padding factor for display
    GaQuadrantLabels gaLabels;
};

#ifdef HAS_OPENCV

struct FindRoiResult {
    std::vector<Roi> rois;
    std::vector<std::array<int, 2>> peaks;
};

/// Merge adjacent ROIs whose centers are closer than threshold.
void combine_peaks(std::vector<std::array<int, 2>>& rois,
                   std::vector<int>& peaks,
                   double threshold);

/// Detect ROIs from an image using X/Y projection peak finding.
FindRoiResult find_roi(const cv::Mat& img,
                       double width_fact,
                       const PeakFindParams& xpeak_params,
                       const PeakFindParams& ypeak_params,
                       double combine_x_thresh,
                       double combine_y_thresh);

/// Fast fused d4sigma: single-pass threshold + second-moment calculation.
void d4sigma_fast(const cv::Mat& E, int x0, int y0, double threshold,
                  double& cx, double& cy, double& dx, double& dy);

/// Compute encircled energy: fraction of total ROI energy within a circle.
/// Also outputs circleEnergy and totalEnergy if pointers are non-null.
double ComputeEncircledEnergy(const cv::Mat& img, double cx, double cy,
                              double radius, const Roi& roi,
                              double* outCircleEnergy = nullptr,
                              double* outTotalEnergy = nullptr);

/// Run full beam detection pipeline on a grayscale uint8 image.
/// Returns up to config.maxBeams results, labeled B0/B1 with GA assignments.
std::vector<BeamDetectionResult> RunBeamDetection(
    const uint8_t* grayData, uint32_t width, uint32_t height,
    const BeamDetectionConfig& config);

/// Simple blob detection: threshold + contour finding.
/// Finds bright blobs, returns center + bounding rect as BeamDetectionResult.
/// Uses the same result struct so overlay and charts work unchanged.
std::vector<BeamDetectionResult> RunBlobDetection(
    const uint8_t* grayData, uint32_t width, uint32_t height,
    const BlobDetectionConfig& config);

#ifdef HAS_HALCON
/// Halcon connected-component beam detection (HDevelop port).
/// Local-mean background subtraction (rolling kernel) + threshold +
/// connection + per-region intensity-weighted centroid and D4sigma.
/// Falls back to empty result vector if Halcon throws.
std::vector<BeamDetectionResult> RunHalconBeamDetection(
    const uint8_t* grayData, uint32_t width, uint32_t height,
    const HalconBeamDetectionConfig& config);
#endif // HAS_HALCON

#endif // HAS_OPENCV
