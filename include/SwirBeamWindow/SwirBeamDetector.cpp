// SwirBeamDetector.cpp
// Real-time beam detection with d4sigma characterization
#include "SwirBeamDetector.h"

#ifdef HAS_OPENCV

#include <algorithm>
#include <cmath>
#include <numeric>
#include <array>
#include <iostream>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

// Diagnostic logging (disabled by default, uncomment to enable)
// static int s_bdFrameCounter = 0;
// static constexpr int BD_LOG_INTERVAL = 150;
// #define BD_LOG(x) do { if (s_bdFrameCounter == 0) { std::cout << x << std::endl; } } while(0)
#define BD_LOG(x) do {} while(0)

// ============================================================================
// combine_peaks - merge adjacent ROIs
// ============================================================================

void combine_peaks(std::vector<std::array<int, 2>>& rois,
                   std::vector<int>& peaks,
                   double threshold) {
    std::vector<int> drop_idx;
    for (size_t idx = 0; idx + 1 < rois.size(); ++idx) {
        double mean_curr = (rois[idx][0] + rois[idx][1]) / 2.0;
        double mean_next = (rois[idx + 1][0] + rois[idx + 1][1]) / 2.0;
        if (std::abs(mean_next - mean_curr) < threshold) {
            rois[idx][0] = std::min(rois[idx][0], rois[idx + 1][0]);
            rois[idx][1] = std::max(rois[idx][1], rois[idx + 1][1]);
            drop_idx.push_back(static_cast<int>(idx + 1));
        }
    }
    for (auto it = drop_idx.rbegin(); it != drop_idx.rend(); ++it) {
        rois.erase(rois.begin() + *it);
        peaks.erase(peaks.begin() + *it);
    }
}

// ============================================================================
// find_roi - X/Y projection peak detection to find beam ROIs
// ============================================================================

namespace {

template <typename T>
FindRoiResult find_roi_impl(const cv::Mat& img,
                             double width_fact,
                             const PeakFindParams& xpeak_params,
                             const PeakFindParams& ypeak_params,
                             double combine_x_thresh,
                             double combine_y_thresh) {
    int rows = img.rows;
    int cols = img.cols;

    // X-projection: sum along rows (column sums)
    std::vector<double> x_project(cols, 0.0);
    for (int r = 0; r < rows; ++r) {
        const T* row_ptr = img.ptr<T>(r);
        for (int c = 0; c < cols; ++c) {
            x_project[c] += row_ptr[c];
        }
    }

    PeakResult x_peaks_result = find_peaks(x_project,
                                            xpeak_params.height,
                                            xpeak_params.distance,
                                            xpeak_params.width);

    // Build x ROIs
    std::vector<std::array<int, 2>> x_rois;
    std::vector<int> x_peak_indices = x_peaks_result.indices;
    for (size_t i = 0; i < x_peak_indices.size(); ++i) {
        int pk = x_peak_indices[i];
        double w = x_peaks_result.widths[i];
        int lo = std::max(static_cast<int>(pk - w * width_fact), 0);
        int hi = std::min(static_cast<int>(pk + w * width_fact), cols - 1);
        x_rois.push_back({lo, hi});
    }

    combine_peaks(x_rois, x_peak_indices, combine_x_thresh);

    // For each x-band, find y peaks
    FindRoiResult result;
    for (size_t idx_x = 0; idx_x < x_rois.size(); ++idx_x) {
        int x0 = x_rois[idx_x][0];
        int x1 = x_rois[idx_x][1];

        std::vector<double> y_project(rows, 0.0);
        for (int r = 0; r < rows; ++r) {
            const T* row_ptr = img.ptr<T>(r);
            for (int c = x0; c < x1; ++c) {
                y_project[r] += row_ptr[c];
            }
        }

        PeakResult y_peaks_result = find_peaks(y_project,
                                                ypeak_params.height,
                                                ypeak_params.distance,
                                                ypeak_params.width);

        std::vector<std::array<int, 2>> y_rois;
        std::vector<int> y_peak_indices = y_peaks_result.indices;
        for (size_t i = 0; i < y_peak_indices.size(); ++i) {
            int pk = y_peak_indices[i];
            double w = y_peaks_result.widths[i];
            int lo = std::max(static_cast<int>(pk - w * width_fact), 0);
            int hi = std::min(static_cast<int>(pk + w * width_fact), rows - 1);
            y_rois.push_back({lo, hi});
        }

        combine_peaks(y_rois, y_peak_indices, combine_y_thresh);

        for (size_t iy = 0; iy < y_rois.size(); ++iy) {
            Roi roi;
            roi.x0 = x0;
            roi.x1 = x1;
            roi.y0 = y_rois[iy][0];
            roi.y1 = y_rois[iy][1];

            result.rois.push_back(roi);
            result.peaks.push_back({x_peak_indices[idx_x], y_peak_indices[iy]});
        }
    }

    return result;
}

} // anonymous namespace

FindRoiResult find_roi(const cv::Mat& img,
                        double width_fact,
                        const PeakFindParams& xpeak_params,
                        const PeakFindParams& ypeak_params,
                        double combine_x_thresh,
                        double combine_y_thresh) {
    if (img.depth() == CV_32F)
        return find_roi_impl<float>(img, width_fact, xpeak_params, ypeak_params, combine_x_thresh, combine_y_thresh);
    return find_roi_impl<double>(img, width_fact, xpeak_params, ypeak_params, combine_x_thresh, combine_y_thresh);
}

// ============================================================================
// d4sigma_fast - single-pass beam diameter calculation
// ============================================================================

namespace {

template <typename T>
void d4sigma_fast_impl(const cv::Mat& E, int x0, int y0, double threshold,
                       double& cx, double& cy, double& dx, double& dy) {
    int rows = E.rows;
    int cols = E.cols;

    // Pass 1: find max(E^2) for thresholding
    T max_e2 = 0;
    for (int r = 0; r < rows; ++r) {
        const T* row = E.ptr<T>(r);
        for (int c = 0; c < cols; ++c) {
            T e2 = row[c] * row[c];
            if (e2 > max_e2) max_e2 = e2;
        }
    }

    if (max_e2 <= 0) {
        cx = cy = dx = dy = 0;
        return;
    }

    double thresh_abs = threshold * static_cast<double>(max_e2);

    // Pass 2: compute centroid + second moments
    double sum_sz = 0, sum_x = 0, sum_y = 0, sum_x2 = 0, sum_y2 = 0;
    for (int r = 0; r < rows; ++r) {
        const T* row = E.ptr<T>(r);
        double gy = static_cast<double>(y0 + r);
        for (int c = 0; c < cols; ++c) {
            double e2 = static_cast<double>(row[c]) * static_cast<double>(row[c]);
            if (e2 < thresh_abs) continue;
            double gx = static_cast<double>(x0 + c);
            sum_sz += e2;
            sum_x += e2 * gx;
            sum_y += e2 * gy;
            sum_x2 += e2 * gx * gx;
            sum_y2 += e2 * gy * gy;
        }
    }

    if (sum_sz < 1e-30) {
        cx = cy = dx = dy = 0;
        return;
    }

    cx = sum_x / sum_sz;
    cy = sum_y / sum_sz;
    double var_x = sum_x2 / sum_sz - cx * cx;
    double var_y = sum_y2 / sum_sz - cy * cy;
    dx = 4.0 * std::sqrt(std::max(var_x, 0.0));
    dy = 4.0 * std::sqrt(std::max(var_y, 0.0));
}

} // anonymous namespace

void d4sigma_fast(const cv::Mat& E, int x0, int y0, double threshold,
                  double& cx, double& cy, double& dx, double& dy) {
    if (E.depth() == CV_32F)
        d4sigma_fast_impl<float>(E, x0, y0, threshold, cx, cy, dx, dy);
    else
        d4sigma_fast_impl<double>(E, x0, y0, threshold, cx, cy, dx, dy);
}

// ============================================================================
// ComputeEncircledEnergy - fraction of total ROI energy within a circle
// ============================================================================

double ComputeEncircledEnergy(const cv::Mat& img, double cx, double cy,
                              double radius, const Roi& roi,
                              double* outCircleEnergy,
                              double* outTotalEnergy) {
    if (outCircleEnergy) *outCircleEnergy = 0.0;
    if (outTotalEnergy) *outTotalEnergy = 0.0;

    if (img.empty() || radius <= 0.0) return 0.0;

    int x0 = std::max(roi.x0, 0);
    int y0 = std::max(roi.y0, 0);
    int x1 = std::min(roi.x1, img.cols);
    int y1 = std::min(roi.y1, img.rows);
    if (x0 >= x1 || y0 >= y1) return 0.0;

    double r2 = radius * radius;
    double totalEnergy = 0.0;
    double circleEnergy = 0.0;

    for (int r = y0; r < y1; ++r) {
        const float* row = img.ptr<float>(r);
        double dy = static_cast<double>(r) - cy;
        double dy2 = dy * dy;
        for (int c = x0; c < x1; ++c) {
            double val = static_cast<double>(row[c]);
            if (val <= 0.0) continue;
            totalEnergy += val;
            double dxv = static_cast<double>(c) - cx;
            if (dxv * dxv + dy2 <= r2) {
                circleEnergy += val;
            }
        }
    }

    if (outCircleEnergy) *outCircleEnergy = circleEnergy;
    if (outTotalEnergy) *outTotalEnergy = totalEnergy;

    if (totalEnergy < 1e-30) return 0.0;
    return circleEnergy / totalEnergy;
}

// ============================================================================
// RunBeamDetection - full pipeline
// ============================================================================

std::vector<BeamDetectionResult> RunBeamDetection(
    const uint8_t* grayData, uint32_t width, uint32_t height,
    const BeamDetectionConfig& config) {

    if (!grayData || width == 0 || height == 0) return {};

    // s_bdFrameCounter = (s_bdFrameCounter + 1) % BD_LOG_INTERVAL;

    int w = static_cast<int>(width);
    int h = static_cast<int>(height);

    // Convert uint8 grayscale to CV_32F
    cv::Mat grayMat(h, w, CV_8UC1, const_cast<uint8_t*>(grayData));
    cv::Mat floatMat;
    grayMat.convertTo(floatMat, CV_32F);

    // Background subtract: average of 2 corner patches (top-left + top-right, 20x20)
    int patchSize = 20;
    if (patchSize > w / 4) patchSize = w / 4;
    if (patchSize > h / 4) patchSize = h / 4;
    if (patchSize < 2) patchSize = 2;

    double bgSum = 0.0;
    int bgCount = 0;
    // Top-left corner
    for (int r = 0; r < patchSize; ++r) {
        const float* row = floatMat.ptr<float>(r);
        for (int c = 0; c < patchSize; ++c) {
            bgSum += row[c];
            ++bgCount;
        }
    }
    // Top-right corner
    for (int r = 0; r < patchSize; ++r) {
        const float* row = floatMat.ptr<float>(r);
        for (int c = w - patchSize; c < w; ++c) {
            bgSum += row[c];
            ++bgCount;
        }
    }
    float bgAvg = (bgCount > 0) ? static_cast<float>(bgSum / bgCount) : 0.0f;

    // Subtract background and clamp negatives
    for (int r = 0; r < h; ++r) {
        float* row = floatMat.ptr<float>(r);
        for (int c = 0; c < w; ++c) {
            row[c] = std::max(row[c] - bgAvg, 0.0f);
        }
    }

    // Find ROIs
    FindRoiResult roiResult = find_roi(floatMat, config.roiWidthFactor,
                                        config.xpeaks, config.ypeaks,
                                        config.combineXThresh, config.combineYThresh);

    BD_LOG("[BeamDetect] ROIs found: " << roiResult.rois.size()
           << "  bgAvg=" << bgAvg << "  image=" << w << "x" << h);
    for (size_t ri = 0; ri < roiResult.rois.size(); ++ri) {
        const auto& rr = roiResult.rois[ri];
        BD_LOG("  ROI[" << ri << "] x=" << rr.x0 << ".." << rr.x1
               << " y=" << rr.y0 << ".." << rr.y1);
    }

    if (roiResult.rois.empty()) return {};

    // d4sigma per ROI + compute intensity for sorting
    struct BeamCandidate {
        D4SigmaResult d4;
        Roi roi;
        double intensity;  // sum of E^2 in ROI
        size_t origIdx;
        double encircledEnergy = 0.0;
        double circleEnergy = 0.0;
        double totalEnergy = 0.0;
    };
    std::vector<BeamCandidate> candidates;

    for (size_t i = 0; i < roiResult.rois.size(); ++i) {
        const Roi& r = roiResult.rois[i];
        int roiW = r.x1 - r.x0;
        int roiH = r.y1 - r.y0;
        if (roiW <= 0 || roiH <= 0) continue;
        if (r.x0 < 0 || r.y0 < 0 || r.x1 > w || r.y1 > h) continue;

        cv::Rect rect(r.x0, r.y0, roiW, roiH);
        cv::Mat roiMat = floatMat(rect);

        // sqrt for amplitude (d4sigma on amplitude field)
        cv::Mat E;
        cv::sqrt(roiMat, E);

        D4SigmaResult d4;
        d4sigma_fast(E, r.x0, r.y0, config.d4sigmaThreshold,
                     d4.cx, d4.cy, d4.dx, d4.dy);
        d4.roi = r;

        BD_LOG("  Cand[" << i << "] cx=" << d4.cx << " cy=" << d4.cy
               << " dx=" << d4.dx << " dy=" << d4.dy
               << " roi=" << roiW << "x" << roiH);

        // Skip if d4sigma returned zero (no signal)
        if (d4.dx < 1e-6 && d4.dy < 1e-6) { BD_LOG("    REJECT: zero d4sigma"); continue; }

        // Skip fake beams: reject if diameter > half the image dimension
        if (d4.dx > w * 0.5 || d4.dy > h * 0.5) { BD_LOG("    REJECT: too large vs image"); continue; }

        // Skip if beam is much larger than its ROI (diffuse noise, not a real
        // beam). Tune roiSizeFactor in beam_detection.json.
        if (d4.dx > roiW * config.roiSizeFactor || d4.dy > roiH * config.roiSizeFactor) {
            BD_LOG("    REJECT: larger than ROI (factor " << config.roiSizeFactor << ")");
            continue;
        }

        // Skip beams that are too small (noise spikes). The 8px hardcode that
        // used to sit here was redundant with this check — tune minD4Sigma in
        // beam_detection.json if you need to discriminate smaller beams.
        if (std::max(d4.dx, d4.dy) < config.minD4Sigma) { BD_LOG("    REJECT: too small d4=" << std::max(d4.dx, d4.dy)); continue; }

        // Skip beams with extreme aspect ratio — likely a detection artifact.
        // Dim beams produce jittery d4sigma, so keep this permissive; tune
        // aspectRatioMax in beam_detection.json.
        {
            double minD = std::min(d4.dx, d4.dy);
            double maxD = std::max(d4.dx, d4.dy);
            if (minD > 1e-6 && (maxD / minD) > config.aspectRatioMax) { BD_LOG("    REJECT: aspect ratio " << (maxD/minD)); continue; }
        }

        // Optional: skip beams whose EE circle clips the image boundary.
        // Off by default so beams near the edge still register; enable by
        // setting rejectEdgeClipped=true in beam_detection.json.
        if (config.rejectEdgeClipped) {
            double eeRadius = std::max(d4.dx, d4.dy) / 2.0;
            if (d4.cx - eeRadius < 0 || d4.cx + eeRadius > w ||
                d4.cy - eeRadius < 0 || d4.cy + eeRadius > h) { BD_LOG("    REJECT: EE clipped edge"); continue; }
        }

        // Skip beams with insufficient peak signal (noise)
        {
            float peakVal = 0.0f;
            for (int rr = 0; rr < roiH; ++rr) {
                const float* rowPtr = roiMat.ptr<float>(rr);
                for (int cc = 0; cc < roiW; ++cc) {
                    if (rowPtr[cc] > peakVal) peakVal = rowPtr[cc];
                }
            }
            if (static_cast<double>(peakVal) < config.minPeakValue) { BD_LOG("    REJECT: low peak=" << peakVal); continue; }
        }

        BD_LOG("    ACCEPTED");

        // Compute intensity: sum of E^2 in ROI
        double intensity = 0.0;
        for (int rr = 0; rr < roiH; ++rr) {
            const float* rowPtr = roiMat.ptr<float>(rr);
            for (int cc = 0; cc < roiW; ++cc) {
                intensity += static_cast<double>(rowPtr[cc]);
            }
        }

        candidates.push_back({d4, r, intensity, i});
    }

    if (candidates.empty()) return {};

    // Compute encircled energy for each candidate
    for (auto& cand : candidates) {
        double eeRadius = std::max(cand.d4.dx, cand.d4.dy) / 2.0;
        cand.encircledEnergy = ComputeEncircledEnergy(floatMat, cand.d4.cx, cand.d4.cy,
                                                      eeRadius, cand.d4.roi,
                                                      &cand.circleEnergy, &cand.totalEnergy);
    }

    // Sort by intensity descending, take top maxBeams
    std::sort(candidates.begin(), candidates.end(),
              [](const BeamCandidate& a, const BeamCandidate& b) {
                  return a.intensity > b.intensity;
              });

    int numBeams = std::min(config.maxBeams, static_cast<int>(candidates.size()));
    candidates.resize(numBeams);

    // Sort by cy ascending: B0 (top), B1 (bottom)
    std::sort(candidates.begin(), candidates.end(),
              [](const BeamCandidate& a, const BeamCandidate& b) {
                  return a.d4.cy < b.d4.cy;
              });

    // Build results with labeling
    double halfX = static_cast<double>(width) / 2.0;
    double halfY = static_cast<double>(height) / 2.0;
    std::vector<BeamDetectionResult> results;
    results.reserve(numBeams);

    for (int bi = 0; bi < numBeams; ++bi) {
        const auto& cand = candidates[bi];
        BeamDetectionResult res;
        res.d4 = cand.d4;
        res.encircledEnergy = cand.encircledEnergy;
        res.circleEnergy = cand.circleEnergy;
        res.totalEnergy = cand.totalEnergy;
        res.label = "B" + std::to_string(bi);

        // GA labeling by visual quadrant. Display is Y-flipped from the
        // detector frame, so detector cy > halfY = visual top.
        const bool right = cand.d4.cx >= halfX;
        const bool visualTop = cand.d4.cy >= halfY;
        if (visualTop) {
            res.gaLabel = right ? config.gaLabels.q1 : config.gaLabels.q2;
        } else {
            res.gaLabel = right ? config.gaLabels.q4 : config.gaLabels.q3;
        }

        // Saturation check and peak intensity on original uint8 data within ROI
        uint8_t maxPixel = 0;
        const Roi& roi = cand.roi;
        for (int r = roi.y0; r < roi.y1; ++r) {
            for (int c = roi.x0; c < roi.x1; ++c) {
                uint8_t val = grayData[r * w + c];
                if (val > maxPixel) maxPixel = val;
            }
        }
        res.peakIntensity = maxPixel;
        res.saturated = (maxPixel > config.saturationValue);

        // Extract 1D profiles at centroid row/column
        int centroidRow = static_cast<int>(std::round(cand.d4.cy));
        int centroidCol = static_cast<int>(std::round(cand.d4.cx));
        centroidRow = std::clamp(centroidRow, roi.y0, roi.y1 - 1);
        centroidCol = std::clamp(centroidCol, roi.x0, roi.x1 - 1);

        // X profile: values along centroid row within ROI
        res.profileXStart = roi.x0;
        res.xProfile.resize(roi.x1 - roi.x0);
        for (int c = roi.x0; c < roi.x1; ++c) {
            res.xProfile[c - roi.x0] = static_cast<double>(grayData[centroidRow * w + c]);
        }

        // Y profile: values along centroid column within ROI
        res.profileYStart = roi.y0;
        res.yProfile.resize(roi.y1 - roi.y0);
        for (int r = roi.y0; r < roi.y1; ++r) {
            res.yProfile[r - roi.y0] = static_cast<double>(grayData[r * w + centroidCol]);
        }

        results.push_back(std::move(res));
    }

    return results;
}

// ============================================================================
// RunBlobDetection - simple threshold + contour blob detection
// ============================================================================

std::vector<BeamDetectionResult> RunBlobDetection(
    const uint8_t* grayData, uint32_t width, uint32_t height,
    const BlobDetectionConfig& config) {

    if (!grayData || width == 0 || height == 0) return {};

    int w = static_cast<int>(width);
    int h = static_cast<int>(height);

    cv::Mat grayMat(h, w, CV_8UC1, const_cast<uint8_t*>(grayData));

    // Optional Gaussian blur to reduce noise
    cv::Mat blurred;
    if (config.gaussianKernel >= 3) {
        int k = config.gaussianKernel | 1;  // ensure odd
        cv::GaussianBlur(grayMat, blurred, cv::Size(k, k), 0);
    } else {
        blurred = grayMat;
    }

    // Threshold to binary for blob finding
    cv::Mat binary;
    if (config.useOtsu) {
        cv::threshold(blurred, binary, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    } else {
        cv::threshold(blurred, binary, config.thresholdValue, 255, cv::THRESH_BINARY);
    }

    // Hard saturation mask: only pixels at exactly 255 survive (for stable SP count)
    cv::Mat satMask;
    cv::threshold(grayMat, satMask, 254, 255, cv::THRESH_BINARY);

    // Find contours
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    if (contours.empty()) return {};

    // Score each contour by area, filter by size
    struct BlobCandidate {
        double area;
        cv::Moments mom;
        cv::Rect bbox;
        double cx, cy;
        int contourIdx;  // index into contours vector for SP counting
    };
    std::vector<BlobCandidate> candidates;

    for (int ci = 0; ci < static_cast<int>(contours.size()); ++ci) {
        const auto& contour = contours[ci];
        double area = cv::contourArea(contour);
        if (area < config.minBlobArea || area > config.maxBlobArea) continue;

        cv::Moments mom = cv::moments(contour);
        if (mom.m00 < 1e-6) continue;

        BlobCandidate cand;
        cand.area = area;
        cand.mom = mom;
        cand.bbox = cv::boundingRect(contour);
        cand.cx = mom.m10 / mom.m00;
        cand.cy = mom.m01 / mom.m00;
        cand.contourIdx = ci;
        candidates.push_back(cand);
    }

    if (candidates.empty()) return {};

    // Sort by area descending, take top maxBeams
    std::sort(candidates.begin(), candidates.end(),
              [](const BlobCandidate& a, const BlobCandidate& b) {
                  return a.area > b.area;
              });

    int numBeams = std::min(config.maxBeams, static_cast<int>(candidates.size()));
    candidates.resize(numBeams);

    // Sort by cy ascending: B0 (top), B1 (bottom)
    std::sort(candidates.begin(), candidates.end(),
              [](const BlobCandidate& a, const BlobCandidate& b) {
                  return a.cy < b.cy;
              });

    // Build results
    double halfX = static_cast<double>(width) / 2.0;
    double halfY = static_cast<double>(height) / 2.0;
    std::vector<BeamDetectionResult> results;
    results.reserve(numBeams);

    for (int bi = 0; bi < numBeams; ++bi) {
        const auto& cand = candidates[bi];
        BeamDetectionResult res;

        // Use bounding rect dimensions as dx/dy (with padding)
        double bw = static_cast<double>(cand.bbox.width) * config.roiPadding;
        double bh = static_cast<double>(cand.bbox.height) * config.roiPadding;

        res.d4.cx = cand.cx;
        res.d4.cy = cand.cy;
        res.d4.dx = bw;
        res.d4.dy = bh;

        // ROI from padded bounding rect
        double padW = bw / 2.0;
        double padH = bh / 2.0;
        res.d4.roi.x0 = std::max(0, static_cast<int>(cand.cx - padW));
        res.d4.roi.y0 = std::max(0, static_cast<int>(cand.cy - padH));
        res.d4.roi.x1 = std::min(w, static_cast<int>(cand.cx + padW));
        res.d4.roi.y1 = std::min(h, static_cast<int>(cand.cy + padH));

        res.label = "B" + std::to_string(bi);

        // GA labeling by visual quadrant. Display is Y-flipped from the
        // detector frame, so detector cy > halfY = visual top.
        {
            const bool right = cand.cx >= halfX;
            const bool visualTop = cand.cy >= halfY;
            if (visualTop) {
                res.gaLabel = right ? config.gaLabels.q1 : config.gaLabels.q2;
            } else {
                res.gaLabel = right ? config.gaLabels.q4 : config.gaLabels.q3;
            }
        }

        // Peak intensity and total energy in ROI
        uint8_t maxPixel = 0;
        double totalEnergy = 0.0;
        for (int r = res.d4.roi.y0; r < res.d4.roi.y1; ++r) {
            for (int c = res.d4.roi.x0; c < res.d4.roi.x1; ++c) {
                uint8_t val = grayData[r * w + c];
                if (val > maxPixel) maxPixel = val;
                totalEnergy += static_cast<double>(val);
            }
        }
        res.peakIntensity = maxPixel;
        res.saturated = (maxPixel > config.saturationValue);
        res.totalEnergy = totalEnergy;

        // Saturated pixel count: count 255-only pixels within the blob contour
        // Uses satMask (binary threshold at 254) so only truly saturated pixels
        // are counted — stable regardless of camera position or ROI padding
        {
            cv::Rect bbox = cand.bbox;
            cv::Mat localMask = cv::Mat::zeros(bbox.height, bbox.width, CV_8UC1);
            // Shift contour points to local bbox coordinates
            std::vector<cv::Point> shifted;
            shifted.reserve(contours[cand.contourIdx].size());
            for (const auto& pt : contours[cand.contourIdx]) {
                shifted.emplace_back(pt.x - bbox.x, pt.y - bbox.y);
            }
            std::vector<std::vector<cv::Point>> shiftedContours = { shifted };
            cv::drawContours(localMask, shiftedContours, 0, cv::Scalar(255), cv::FILLED);
            cv::Mat localSat = satMask(bbox);
            cv::Mat maskedSat;
            cv::bitwise_and(localSat, localMask, maskedSat);
            res.saturatedPixelCount = cv::countNonZero(maskedSat);
        }

        // Encircled energy: use the blob area circle
        double eeRadius = std::max(bw, bh) / 2.0;
        double circleEnergy = 0.0;
        double r2 = eeRadius * eeRadius;
        for (int r = res.d4.roi.y0; r < res.d4.roi.y1; ++r) {
            double dy = static_cast<double>(r) - cand.cy;
            double dy2 = dy * dy;
            for (int c = res.d4.roi.x0; c < res.d4.roi.x1; ++c) {
                double dx = static_cast<double>(c) - cand.cx;
                if (dx * dx + dy2 <= r2) {
                    circleEnergy += static_cast<double>(grayData[r * w + c]);
                }
            }
        }
        res.circleEnergy = circleEnergy;
        res.encircledEnergy = (totalEnergy > 1e-6) ? circleEnergy / totalEnergy : 0.0;

        // 1D profiles at centroid
        int centroidRow = std::clamp(static_cast<int>(std::round(cand.cy)),
                                     res.d4.roi.y0, res.d4.roi.y1 - 1);
        int centroidCol = std::clamp(static_cast<int>(std::round(cand.cx)),
                                     res.d4.roi.x0, res.d4.roi.x1 - 1);

        res.profileXStart = res.d4.roi.x0;
        res.xProfile.resize(res.d4.roi.x1 - res.d4.roi.x0);
        for (int c = res.d4.roi.x0; c < res.d4.roi.x1; ++c) {
            res.xProfile[c - res.d4.roi.x0] = static_cast<double>(grayData[centroidRow * w + c]);
        }

        res.profileYStart = res.d4.roi.y0;
        res.yProfile.resize(res.d4.roi.y1 - res.d4.roi.y0);
        for (int r = res.d4.roi.y0; r < res.d4.roi.y1; ++r) {
            res.yProfile[r - res.d4.roi.y0] = static_cast<double>(grayData[r * w + centroidCol]);
        }

        results.push_back(std::move(res));
    }

    return results;
}

#endif // HAS_OPENCV
