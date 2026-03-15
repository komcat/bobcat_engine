// SwirBeamDetector.cpp
// Real-time beam detection with d4sigma characterization
#include "SwirBeamDetector.h"

#ifdef HAS_OPENCV

#include <algorithm>
#include <cmath>
#include <numeric>
#include <array>
#include <opencv2/core.hpp>

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
// RunBeamDetection - full pipeline
// ============================================================================

std::vector<BeamDetectionResult> RunBeamDetection(
    const uint8_t* grayData, uint32_t width, uint32_t height,
    const BeamDetectionConfig& config) {

    if (!grayData || width == 0 || height == 0) return {};

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

    if (roiResult.rois.empty()) return {};

    // d4sigma per ROI + compute intensity for sorting
    struct BeamCandidate {
        D4SigmaResult d4;
        Roi roi;
        double intensity;  // sum of E^2 in ROI
        size_t origIdx;
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

        // Skip if d4sigma returned zero (no signal)
        if (d4.dx < 1e-6 && d4.dy < 1e-6) continue;

        // Skip fake beams: reject if diameter > half the image dimension
        // (real beams are localized, not spread across the entire frame)
        if (d4.dx > w * 0.5 || d4.dy > h * 0.5) continue;

        // Skip if beam is larger than its ROI (diffuse noise, not a real beam)
        if (d4.dx > roiW * 1.5 || d4.dy > roiH * 1.5) continue;

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
    double halfY = static_cast<double>(height) / 2.0;
    std::vector<BeamDetectionResult> results;
    results.reserve(numBeams);

    for (int bi = 0; bi < numBeams; ++bi) {
        const auto& cand = candidates[bi];
        BeamDetectionResult res;
        res.d4 = cand.d4;
        res.label = "B" + std::to_string(bi);

        // GA labeling
        if (numBeams == 2) {
            bool b0InTop = (candidates[0].d4.cy < halfY);
            bool b1InBottom = (candidates[1].d4.cy >= halfY);
            if (b0InTop && b1InBottom) {
                if (bi == 0) res.gaLabel = "GA2";
                if (bi == 1) res.gaLabel = "GA1";
            }
            // If both in same half, leave gaLabel empty
        }

        // Saturation check on original uint8 data within ROI
        const Roi& roi = cand.roi;
        for (int r = roi.y0; r < roi.y1 && !res.saturated; ++r) {
            for (int c = roi.x0; c < roi.x1; ++c) {
                if (grayData[r * w + c] > config.saturationValue) {
                    res.saturated = true;
                    break;
                }
            }
        }

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

#endif // HAS_OPENCV
