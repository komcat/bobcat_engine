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

struct BeamDetectionConfig {
    PeakFindParams xpeaks;
    PeakFindParams ypeaks;
    double combineXThresh = 20.0;
    double combineYThresh = 20.0;
    double d4sigmaThreshold = 0.05;
    int maxBeams = 2;
    uint8_t saturationValue = 200;
    double roiWidthFactor = 2.5;
    double minD4Sigma = 10.0;       // reject beams with max(dx,dy) < this
    double minPeakValue = 20.0;     // reject beams with ROI peak < this (after bg sub)
};

/// Config for simple blob-based beam detection
struct BlobDetectionConfig {
    int maxBeams = 2;
    uint8_t saturationValue = 200;
    int gaussianKernel = 5;         // blur kernel size (odd number, 0=disabled)
    double thresholdValue = 180.0;  // fixed threshold (0-255), ignored if useOtsu=true
    bool useOtsu = false;           // use Otsu auto-threshold instead of fixed
    double minBlobArea = 50.0;      // reject blobs smaller than this (pixels^2)
    double maxBlobArea = 50000.0;   // reject blobs larger than this (pixels^2)
    double roiPadding = 1.5;        // bounding rect padding factor for display
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
/// Uses the same result struct so overlay/EMA/charts work unchanged.
std::vector<BeamDetectionResult> RunBlobDetection(
    const uint8_t* grayData, uint32_t width, uint32_t height,
    const BlobDetectionConfig& config);

#endif // HAS_OPENCV
