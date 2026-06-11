// PoyntBeamDetector.cpp
// Faithful C++ port of the poynt server_v2.py live beam-tracking path.
// See PoyntBeamDetector.h for the rationale. This reuses find_roi /
// d4sigma_fast / ComputeEncircledEnergy from SwirBeamDetector and differs from
// the legacy RunBeamDetection in exactly the ways that make server_v2 track
// better: 4-corner background subtraction and NO post-detection filters.
#include "PoyntBeamDetector.h"

#ifdef HAS_OPENCV

#include <algorithm>
#include <cmath>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace {

// 4-corner background subtract — mirrors server_v2.bg_subtract():
// average the mean of four `box`x`box` corner patches (TL, TR, BL, BR),
// subtract, clamp >= 0. (The legacy d4sigma mode used only TL+TR, which biases
// the estimate when illumination is uneven.)
float corner_background(const cv::Mat& f, int box) {
    int w = f.cols, h = f.rows;
    int b = box;
    if (b > w / 2) b = w / 2;
    if (b > h / 2) b = h / 2;
    if (b < 1) b = 1;

    auto patchMean = [&](int r0, int c0) {
        double sum = 0.0;
        for (int r = r0; r < r0 + b; ++r) {
            const float* row = f.ptr<float>(r);
            for (int c = c0; c < c0 + b; ++c) sum += row[c];
        }
        return sum / (static_cast<double>(b) * b);
    };

    double tl = patchMean(0, 0);
    double tr = patchMean(0, w - b);
    double bl = patchMean(h - b, 0);
    double br = patchMean(h - b, w - b);
    return static_cast<float>((tl + tr + bl + br) / 4.0);
}

} // anonymous namespace

std::vector<BeamDetectionResult> RunPoyntDetection(
    const uint8_t* grayData, uint32_t width, uint32_t height,
    const PoyntDetectionConfig& config) {

    if (!grayData || width == 0 || height == 0) return {};

    int w = static_cast<int>(width);
    int h = static_cast<int>(height);

    cv::Mat grayMat(h, w, CV_8UC1, const_cast<uint8_t*>(grayData));
    cv::Mat floatMat;
    grayMat.convertTo(floatMat, CV_32F);

    // 4-corner background subtract + clamp negatives.
    const float bgAvg = corner_background(floatMat, config.bgCornerBox);
    for (int r = 0; r < h; ++r) {
        float* row = floatMat.ptr<float>(r);
        for (int c = 0; c < w; ++c) row[c] = std::max(row[c] - bgAvg, 0.0f);
    }

    // ROI detection (shared with the legacy path; structurally identical to
    // server_v2.find_roi — peak +/- width*width_fact, then combine_peaks).
    FindRoiResult roiResult = find_roi(floatMat, config.roiWidthFactor,
                                       config.xpeaks, config.ypeaks,
                                       config.combineXThresh, config.combineYThresh);
    if (roiResult.rois.empty()) return {};

    struct BeamCandidate {
        D4SigmaResult d4;
        Roi roi;
        double encircledEnergy = 0.0;
        double circleEnergy = 0.0;
        double totalEnergy = 0.0;
    };
    std::vector<BeamCandidate> candidates;
    candidates.reserve(roiResult.rois.size());

    for (size_t i = 0; i < roiResult.rois.size(); ++i) {
        const Roi& r = roiResult.rois[i];
        int roiW = r.x1 - r.x0;
        int roiH = r.y1 - r.y0;
        // Bounds sanity only — NO beam-quality rejection filters (this is the
        // key behavioural difference vs RunBeamDetection: server_v2 keeps every
        // detected beam, so dim/odd-shaped beams are not silently dropped).
        if (roiW <= 0 || roiH <= 0) continue;
        if (r.x0 < 0 || r.y0 < 0 || r.x1 > w || r.y1 > h) continue;

        cv::Rect rect(r.x0, r.y0, roiW, roiH);
        cv::Mat roiMat = floatMat(rect);

        // server_v2 does NOT mask saturated pixels; honour excludeSaturated as
        // an optional bobcat extension (default off for fidelity).
        cv::Mat roiForMoments = roiMat;
        cv::Mat roiMasked;
        if (config.excludeSaturated) {
            roiMasked = roiMat.clone();
            cv::Mat roiGray = grayMat(rect);
            for (int rr = 0; rr < roiH; ++rr) {
                const uint8_t* gp = roiGray.ptr<uint8_t>(rr);
                float* fp = roiMasked.ptr<float>(rr);
                for (int cc = 0; cc < roiW; ++cc) {
                    if (gp[cc] >= config.saturationValue) fp[cc] = 0.0f;
                }
            }
            roiForMoments = roiMasked;
        }

        // d4sigma operates on the amplitude field (sqrt of intensity); Sz=E^2
        // recovers the intensity weighting — matches server_v2 d4sigma(sqrt(img_roi)).
        cv::Mat E;
        cv::sqrt(roiForMoments, E);

        D4SigmaResult d4;
        d4sigma_fast(E, r.x0, r.y0, config.d4sigmaThreshold, d4.cx, d4.cy, d4.dx, d4.dy);
        d4.roi = r;

        // NO rejection filters at all — server_v2 keeps every ROI find_roi
        // produced. (d4sigma_fast already returns 0s, not NaN, for an empty ROI,
        // so an unlikely degenerate ROI can't poison the sort.)
        candidates.push_back({d4, r, 0.0, 0.0, 0.0});
    }

    if (candidates.empty()) return {};

    // Slot assignment mirrors poynt server_v2_ui.py exactly: sort every
    // detection by centroid Y, then take the TOPMOST as B0/RX (sorted_b[0]) and
    // the BOTTOMMOST as B1/TX (sorted_b[-1]). No intensity ranking, no
    // nearest-neighbour tracking — pure per-frame position. This drops any
    // spurious middle detection without an explicit reject filter, exactly like
    // the server. Verified on hardware: smaller detector cy = top of display.
    std::sort(candidates.begin(), candidates.end(),
              [](const BeamCandidate& a, const BeamCandidate& b) {
                  return a.d4.cy < b.d4.cy;
              });
    std::vector<BeamCandidate> picked;
    picked.push_back(candidates.front());            // top    -> B0 / RX
    if (candidates.size() > 1)
        picked.push_back(candidates.back());         // bottom -> B1 / TX
    candidates.swap(picked);
    const int numBeams = static_cast<int>(candidates.size());

    // Encircled energy for the selected beams (bobcat overlay/chart input).
    for (auto& cand : candidates) {
        double eeRadius = std::max(cand.d4.dx, cand.d4.dy) / 2.0;
        cand.encircledEnergy = ComputeEncircledEnergy(floatMat, cand.d4.cx, cand.d4.cy,
                                                      eeRadius, cand.d4.roi,
                                                      &cand.circleEnergy, &cand.totalEnergy);
    }

    const double halfX = static_cast<double>(width) / 2.0;
    const double halfY = static_cast<double>(height) / 2.0;
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

        // GA labeling by visual quadrant. Display is Y-flipped from the detector
        // frame, so detector cy > halfY = visual top. (Same convention as the
        // other modes.)
        const bool right = cand.d4.cx >= halfX;
        const bool visualTop = cand.d4.cy >= halfY;
        if (visualTop) {
            res.gaLabel = right ? config.gaLabels.q1 : config.gaLabels.q2;
        } else {
            res.gaLabel = right ? config.gaLabels.q4 : config.gaLabels.q3;
        }

        // Peak intensity + saturated-pixel count on raw uint8 within ROI.
        uint8_t maxPixel = 0;
        int satCount = 0;
        const Roi& roi = cand.roi;
        for (int r = roi.y0; r < roi.y1; ++r) {
            for (int c = roi.x0; c < roi.x1; ++c) {
                uint8_t val = grayData[r * w + c];
                if (val > maxPixel) maxPixel = val;
                if (val >= config.saturationValue) ++satCount;
            }
        }
        res.peakIntensity = maxPixel;
        res.saturated = (maxPixel >= config.saturationValue);
        res.saturatedPixelCount = satCount;

        // 1D profiles at centroid row/col for chart rendering.
        int centroidRow = std::clamp(static_cast<int>(std::round(cand.d4.cy)), roi.y0, roi.y1 - 1);
        int centroidCol = std::clamp(static_cast<int>(std::round(cand.d4.cx)), roi.x0, roi.x1 - 1);

        res.profileXStart = roi.x0;
        res.xProfile.resize(roi.x1 - roi.x0);
        for (int c = roi.x0; c < roi.x1; ++c) {
            res.xProfile[c - roi.x0] = static_cast<double>(grayData[centroidRow * w + c]);
        }
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
