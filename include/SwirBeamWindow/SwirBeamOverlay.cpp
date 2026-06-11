// SwirBeamOverlay.cpp
// GDI+ rendering of beam detection overlays
#include "SwirBeamOverlay.h"

#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <chrono>

namespace {

// Chart size = beamDiameter * CHART_SPAN_FACTOR (in image pixels)
static constexpr double CHART_SPAN_FACTOR = 2.5;

// Convert image-space coordinate to display-space
float imgToDispX(double imgX, int renderX, int renderW, uint32_t frameW) {
    return static_cast<float>(renderX + (imgX / frameW) * renderW);
}

float imgToDispY(double imgY, int renderY, int renderH, uint32_t frameH) {
    // m_frameBuffer is already pre-flipped on ingest (dstRow = h-1-row) and the
    // DIB has positive biHeight, so Windows renders the image upright. The
    // detector and the display therefore share the same top-down coord system —
    // no extra flip here. (Flipping was needed when the detector ran on the
    // bottom-up display buffer; it's wrong now that detection runs on the
    // top-down m_detectionBuffer.)
    return static_cast<float>(renderY + (imgY / frameH) * renderH);
}

// Convert image-space length to display-space length
float imgToDispLenX(double imgLen, int renderW, uint32_t frameW) {
    return static_cast<float>((imgLen / frameW) * renderW);
}

float imgToDispLenY(double imgLen, int renderH, uint32_t frameH) {
    return static_cast<float>((imgLen / frameH) * renderH);
}

// Extract sub-range of profile centered on centroid, spanning chartDim image pixels.
// Returns the sub-vector and the image-space start/end of the extracted range.
struct ProfileSubRange {
    std::vector<double> data;
    double imgStart = 0;  // image-space start of extracted range
    double imgEnd = 0;    // image-space end of extracted range
};

ProfileSubRange extractSubRange(const std::vector<double>& profile, int profileStart,
                                double center, double chartDim) {
    ProfileSubRange sub;
    if (profile.empty() || chartDim < 1.0) return sub;

    int n = static_cast<int>(profile.size());
    double halfSpan = chartDim / 2.0;
    double imgStart = center - halfSpan;
    double imgEnd = center + halfSpan;

    double profStart = static_cast<double>(profileStart);
    double profEnd = static_cast<double>(profileStart + n);
    if (imgStart < profStart) imgStart = profStart;
    if (imgEnd > profEnd) imgEnd = profEnd;
    if (imgEnd - imgStart < 1.0) return sub;

    int iStart = std::max(0, static_cast<int>(std::floor(imgStart - profStart)));
    int iEnd = std::min(n, static_cast<int>(std::ceil(imgEnd - profStart)));
    if (iEnd - iStart < 2) return sub;

    sub.data.assign(profile.begin() + iStart, profile.begin() + iEnd);
    sub.imgStart = profStart + iStart;
    sub.imgEnd = profStart + iEnd;
    return sub;
}

// Draw a horizontal profile chart into a display-space rect.
// Profile values map vertically (0 at bottom, max at top).
// Also draws d4sigma extent tick marks.
void DrawHorizProfileChart(Gdiplus::Graphics& gfx,
                           const ProfileSubRange& sub,
                           float dispLeft, float dispTop, float dispW, float dispH,
                           double cx, double dx,
                           int renderX, int renderW, uint32_t frameW,
                           const Gdiplus::Color& lineColor) {
    if (sub.data.empty() || dispW < 4.0f || dispH < 4.0f) return;

    // Background
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(160, 0, 0, 0));
    gfx.FillRectangle(&bgBrush, dispLeft, dispTop, dispW, dispH);

    double maxVal = *std::max_element(sub.data.begin(), sub.data.end());
    if (maxVal < 1e-6) return;

    // Polyline: X position mapped from image-space, Y from normalized value
    int subN = static_cast<int>(sub.data.size());
    std::vector<Gdiplus::PointF> pts(subN);
    double imgSpan = sub.imgEnd - sub.imgStart;
    for (int i = 0; i < subN; ++i) {
        float t = static_cast<float>((sub.imgStart + i - (sub.imgStart)) / imgSpan);
        float px = dispLeft + t * dispW;
        float py = dispTop + dispH - static_cast<float>(sub.data[i] / maxVal) * (dispH - 2.0f) - 1.0f;
        pts[i] = {px, py};
    }

    Gdiplus::Pen linePen(lineColor, 1.5f);
    gfx.DrawLines(&linePen, pts.data(), subN);

    // d4sigma extent ticks (vertical lines at cx +/- dx/2)
    float tickL = imgToDispX(cx - dx / 2.0, renderX, renderW, frameW);
    float tickR = imgToDispX(cx + dx / 2.0, renderX, renderW, frameW);
    Gdiplus::Pen tickPen(Gdiplus::Color(200, 255, 255, 0), 1.0f);
    if (tickL >= dispLeft && tickL <= dispLeft + dispW)
        gfx.DrawLine(&tickPen, tickL, dispTop, tickL, dispTop + dispH);
    if (tickR >= dispLeft && tickR <= dispLeft + dispW)
        gfx.DrawLine(&tickPen, tickR, dispTop, tickR, dispTop + dispH);
}

// Draw a vertical profile chart into a display-space rect.
// Profile values map horizontally (0 at left, max at right).
void DrawVertProfileChart(Gdiplus::Graphics& gfx,
                          const ProfileSubRange& sub,
                          float dispLeft, float dispTop, float dispW, float dispH,
                          double cy, double dy,
                          int renderY, int renderH, uint32_t frameH,
                          const Gdiplus::Color& lineColor) {
    if (sub.data.empty() || dispW < 4.0f || dispH < 4.0f) return;

    // Background
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(160, 0, 0, 0));
    gfx.FillRectangle(&bgBrush, dispLeft, dispTop, dispW, dispH);

    double maxVal = *std::max_element(sub.data.begin(), sub.data.end());
    if (maxVal < 1e-6) return;

    // Polyline: Y position mapped from image-space, X from normalized value
    int subN = static_cast<int>(sub.data.size());
    std::vector<Gdiplus::PointF> pts(subN);
    double imgSpan = sub.imgEnd - sub.imgStart;
    for (int i = 0; i < subN; ++i) {
        float t = static_cast<float>((sub.imgStart + i - (sub.imgStart)) / imgSpan);
        float py = dispTop + t * dispH;
        float px = dispLeft + static_cast<float>(sub.data[i] / maxVal) * (dispW - 2.0f) + 1.0f;
        pts[i] = {px, py};
    }

    Gdiplus::Pen linePen(lineColor, 1.5f);
    gfx.DrawLines(&linePen, pts.data(), subN);

    // d4sigma extent ticks (horizontal lines at cy +/- dy/2)
    float tickT = imgToDispY(cy - dy / 2.0, renderY, renderH, frameH);
    float tickB = imgToDispY(cy + dy / 2.0, renderY, renderH, frameH);
    Gdiplus::Pen tickPen(Gdiplus::Color(200, 255, 255, 0), 1.0f);
    if (tickT >= dispTop && tickT <= dispTop + dispH)
        gfx.DrawLine(&tickPen, dispLeft, tickT, dispLeft + dispW, tickT);
    if (tickB >= dispTop && tickB <= dispTop + dispH)
        gfx.DrawLine(&tickPen, dispLeft, tickB, dispLeft + dispW, tickB);
}

} // anonymous namespace

void DrawBeamOverlays(Gdiplus::Graphics& gfx,
                      const std::vector<BeamDetectionResult>& results,
                      int renderX, int renderY, int renderW, int renderH,
                      uint32_t frameW, uint32_t frameH,
                      bool /*blobMode*/,
                      double /*micronsPerPixel*/) {
    if (results.empty() || frameW == 0 || frameH == 0 || renderW <= 0 || renderH <= 0) return;

    static Gdiplus::Font labelFont(L"Consolas", 8.0f, Gdiplus::FontStyleBold);

    // Label text is throttled: rebuild strings at ~4 Hz to stop per-frame flicker.
    // Crosshair and EE ellipse update live on each frame using the raw centroid.
    static constexpr int LABEL_CACHE_MS = 250;
    static constexpr int MAX_CACHED_BEAMS = 2;
    static constexpr int MAX_LABEL_ROWS = 8;
    static wchar_t s_cachedRows[MAX_CACHED_BEAMS][MAX_LABEL_ROWS][40];
    static int s_cachedNumRows[MAX_CACHED_BEAMS] = {0, 0};
    static bool s_cachedSaturated[MAX_CACHED_BEAMS] = {false, false};
    static bool s_cachedValid[MAX_CACHED_BEAMS] = {false, false};
    static auto s_lastRebuild = std::chrono::steady_clock::time_point{};

    auto nowTp = std::chrono::steady_clock::now();
    auto sinceMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        nowTp - s_lastRebuild).count();
    const bool rebuildLabelCache = (sinceMs >= LABEL_CACHE_MS);
    if (rebuildLabelCache) s_lastRebuild = nowTp;

    // Per-quadrant label occupancy for this frame. Labels are anchored at the
    // center of the visual quadrant their beam falls in; when two beams share a
    // quadrant (e.g. TX/RX close together), stack the later ones downward so the
    // info blocks don't render on top of each other. Index: q0..q3 below.
    int quadrantLabelCount[4] = {0, 0, 0, 0};

    for (size_t bi = 0; bi < results.size(); ++bi) {
        const auto& beam = results[bi];
        const auto& d4 = beam.d4;

        // Centroid crosshair - yellow 10px cross
        float ccx = imgToDispX(d4.cx, renderX, renderW, frameW);
        float ccy = imgToDispY(d4.cy, renderY, renderH, frameH);
        Gdiplus::Pen crossPen(Gdiplus::Color(220, 255, 255, 0), 1.5f);
        gfx.DrawLine(&crossPen, ccx - 5.0f, ccy, ccx + 5.0f, ccy);
        gfx.DrawLine(&crossPen, ccx, ccy - 5.0f, ccx, ccy + 5.0f);

        // (b2) EE circle around centroid
        {
            double eeRadius = std::max(d4.dx, d4.dy) / 2.0;
            float eeDispRx = imgToDispLenX(eeRadius, renderW, frameW);
            float eeDispRy = imgToDispLenY(eeRadius, renderH, frameH);
            Gdiplus::Pen eePen(Gdiplus::Color(160, 0, 255, 0), 1.0f);
            gfx.DrawEllipse(&eePen, ccx - eeDispRx, ccy - eeDispRy,
                            eeDispRx * 2, eeDispRy * 2);
        }

        // (c) Text label — centered in the visual quadrant the beam falls in.
        //     Content is a compact 4-row block: title, AR, EE, PK.
        {
            // Refresh cached label strings for this beam (throttled). The crosshair,
            // EE ellipse, and label position update live above.
            if (rebuildLabelCache && bi < MAX_CACHED_BEAMS) {
                // Title line: B<bi> (<side>). The side label is user-selectable
                // per slot via B CONFIG (RX/TX/GA1/GA2), carried on the result
                // as sideLabel. Fall back to the legacy slot default (slot 0 =
                // RX, slot 1 = TX) when it hasn't been set.
                std::wstring titleLine;
                {
                    std::wostringstream ss;
                    ss << std::wstring(beam.label.begin(), beam.label.end());
                    std::wstring side(beam.sideLabel.begin(), beam.sideLabel.end());
                    if (side.empty())
                        side = (bi == 0) ? L"RX" : (bi == 1) ? L"TX" : L"";
                    if (!side.empty()) ss << L" (" << side << L")";
                    if (beam.saturated)
                        ss << L"  SAT!";
                    titleLine = ss.str();
                }

                int nr = 0;
                swprintf_s(s_cachedRows[bi][nr++], 40, L"%ls", titleLine.c_str());
                // Peak pixel value + count of fully-saturated (==255) pixels.
                swprintf_s(s_cachedRows[bi][nr++], 40, L"PK:  %5d", static_cast<int>(beam.peakIntensity));
                swprintf_s(s_cachedRows[bi][nr++], 40, L"SAT: %5d", beam.saturatedPixelCount);
                // Encircled energy within the d4sigma radius (0..1 -> percent).
                swprintf_s(s_cachedRows[bi][nr++], 40, L"EE: %5.1f%%", beam.encircledEnergy * 100.0);
                s_cachedNumRows[bi] = nr;
                s_cachedSaturated[bi] = beam.saturated;
                s_cachedValid[bi] = true;
            }

            // Skip if we have no cache yet for this beam (first frame before first rebuild)
            const int cacheIdx = static_cast<int>(bi);
            if (cacheIdx >= MAX_CACHED_BEAMS || !s_cachedValid[cacheIdx]) continue;
            const int numRows = s_cachedNumRows[cacheIdx];
            if (numRows == 0) continue;

            static constexpr float rowH = 14.0f;
            float totalH = rowH * numRows + 4.0f;

            // Cache the max width once (fixed-width font, content is always ~same width)
            static float cachedMaxW = 0.0f;
            if (cachedMaxW < 1.0f) {
                Gdiplus::RectF m;
                gfx.MeasureString(L"SAT: 99999", -1, &labelFont, Gdiplus::PointF(0, 0), &m);
                cachedMaxW = m.Width;
            }
            const float maxW = cachedMaxW;

            // Position: centered in the visual quadrant the beam falls in.
            //   Q1 right-top, Q2 left-top, Q3 left-bottom, Q4 right-bottom.
            const float halfDispX = static_cast<float>(renderX) + static_cast<float>(renderW) * 0.5f;
            const float halfDispY = static_cast<float>(renderY) + static_cast<float>(renderH) * 0.5f;
            const bool right = ccx >= halfDispX;
            const bool topVisual = ccy < halfDispY;
            const float qCenterX = right
                ? static_cast<float>(renderX) + static_cast<float>(renderW) * 0.75f
                : static_cast<float>(renderX) + static_cast<float>(renderW) * 0.25f;
            const float qCenterY = topVisual
                ? static_cast<float>(renderY) + static_cast<float>(renderH) * 0.25f
                : static_cast<float>(renderY) + static_cast<float>(renderH) * 0.75f;

            float labelX = qCenterX - maxW * 0.5f;
            float labelY = qCenterY - totalH * 0.5f;

            // If another beam already claimed this quadrant this frame, stack
            // this block below it so the two don't overlap exactly.
            const int qi = (right ? 1 : 0) + (topVisual ? 0 : 2);
            labelY += static_cast<float>(quadrantLabelCount[qi]) * (totalH + 4.0f);
            ++quadrantLabelCount[qi];

            // Clamp inside viewport
            const float maxRight = static_cast<float>(renderX + renderW) - maxW - 6.0f;
            if (labelX > maxRight) labelX = maxRight;
            if (labelX < static_cast<float>(renderX + 2)) labelX = static_cast<float>(renderX + 2);
            const float maxBottom = static_cast<float>(renderY + renderH) - totalH - 4.0f;
            if (labelY > maxBottom) labelY = maxBottom;
            if (labelY < static_cast<float>(renderY + 2)) labelY = static_cast<float>(renderY + 2);

            // Background
            Gdiplus::SolidBrush textBgBrush(Gdiplus::Color(180, 0, 0, 0));
            gfx.FillRectangle(&textBgBrush, labelX - 3.0f, labelY - 2.0f,
                              maxW + 6.0f, totalH + 2.0f);

            // Draw rows (from cached strings to avoid per-frame flicker)
            Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 220, 220, 220));
            Gdiplus::SolidBrush satBrush(Gdiplus::Color(255, 255, 80, 80));
            for (int r = 0; r < numRows; ++r) {
                Gdiplus::SolidBrush* brush = &textBrush;
                // Title row with SAT! in red
                if (r == 0 && s_cachedSaturated[cacheIdx]) brush = &satBrush;
                gfx.DrawString(s_cachedRows[cacheIdx][r], -1, &labelFont,
                               Gdiplus::PointF(labelX, labelY + r * rowH), brush);
            }
        }

        // Profile charts suppressed — polyline drawing + per-frame vector
        // allocation adds paint overhead, contributing to UI lag.
    }
}

// ============================================================================
// Real-time CE charts
// ============================================================================

void DrawCECharts(Gdiplus::Graphics& gfx,
                  const std::array<std::vector<double>, 2>& ceHistory,
                  const std::array<std::vector<double>, 2>& eeHistory,
                  const std::array<int, 2>& ceIndex,
                  const std::array<int, 2>& ceCount,
                  int historySize,
                  int renderX, int renderY, int renderW, int renderH,
                  bool blobMode) {

    static constexpr int CHART_W = 220;
    static constexpr int CHART_H = 80;
    static constexpr int MARGIN = 8;
    static constexpr int GAP = 6;

    static Gdiplus::Font labelFont(L"Consolas", 8.0f, Gdiplus::FontStyleBold);
    static Gdiplus::SolidBrush labelBrush(Gdiplus::Color(255, 220, 220, 220));

    // SAT line colors: B0 = cyan, B1 = green
    static const Gdiplus::Color ceColors[2] = {
        Gdiplus::Color(255, 0, 255, 255),
        Gdiplus::Color(255, 0, 255, 100)
    };
    (void)blobMode;  // SAT chart is identical for both detection modes
    (void)eeHistory; // no longer drawn — kept in signature for ABI stability

    int chartX = renderX + renderW - CHART_W - MARGIN;

    // Chart label text is throttled to ~4 Hz to stop per-frame flicker.
    // Polylines keep updating live.
    static constexpr int LABEL_CACHE_MS = 250;
    static wchar_t s_ceLabelBuf[2][48] = {};
    static wchar_t s_ceMaxBuf[2][24] = {};
    static wchar_t s_ceMinBuf[2][24] = {};
    static bool s_ceLabelValid[2] = {false, false};
    static auto s_ceLastRebuild = std::chrono::steady_clock::time_point{};
    auto ceNowTp = std::chrono::steady_clock::now();
    auto ceSinceMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        ceNowTp - s_ceLastRebuild).count();
    const bool rebuildCeCache = (ceSinceMs >= LABEL_CACHE_MS);
    if (rebuildCeCache) s_ceLastRebuild = ceNowTp;

    for (int bi = 0; bi < 2; ++bi) {
        int count = ceCount[bi];
        if (count == 0) continue;

        int chartY = renderY + MARGIN + bi * (CHART_H + GAP + 16);
        float fChartX = static_cast<float>(chartX);
        float fChartY = static_cast<float>(chartY);
        float fChartW = static_cast<float>(CHART_W);
        float fChartH = static_cast<float>(CHART_H);

        // Background
        Gdiplus::SolidBrush bgBrush(Gdiplus::Color(160, 0, 0, 0));
        gfx.FillRectangle(&bgBrush, fChartX, fChartY, fChartW, fChartH);

        // Border
        Gdiplus::Pen borderPen(Gdiplus::Color(100, 255, 255, 255), 1.0f);
        gfx.DrawRectangle(&borderPen, fChartX, fChartY, fChartW, fChartH);

        // Read ring buffer in chronological order
        int n = std::min(count, historySize);
        int startIdx = (ceIndex[bi] - n + historySize) % historySize;

        // --- SAT axis (auto-scaled) ---
        double ceMin = 1e30, ceMax = -1e30;
        for (int i = 0; i < n; ++i) {
            double v = ceHistory[bi][(startIdx + i) % historySize];
            if (v < ceMin) ceMin = v;
            if (v > ceMax) ceMax = v;
        }
        double ceRange = ceMax - ceMin;
        if (ceRange < 1.0) ceRange = 1.0;
        double cePad = ceRange * 0.1;
        double ceYMin = ceMin - cePad;
        double ceYMax = ceMax + cePad;
        double ceYRange = ceYMax - ceYMin;

        if (n >= 2) {
            float nMinus1 = static_cast<float>(n - 1);

            // Draw SAT polyline
            std::vector<Gdiplus::PointF> pts(n);
            for (int i = 0; i < n; ++i) {
                double v = ceHistory[bi][(startIdx + i) % historySize];
                float px = fChartX + static_cast<float>(i) / nMinus1 * fChartW;
                float py = fChartY + fChartH - static_cast<float>((v - ceYMin) / ceYRange) * fChartH;
                pts[i] = {px, py};
            }
            Gdiplus::Pen linePen(ceColors[bi], 1.5f);
            gfx.DrawLines(&linePen, pts.data(), n);
        }

        // Rebuild cached label strings at ~4 Hz to stop flicker.
        if (rebuildCeCache) {
            int lastIdx = (ceIndex[bi] - 1 + historySize) % historySize;
            swprintf_s(s_ceLabelBuf[bi], L"B%d SAT: %.0f", bi, ceHistory[bi][lastIdx]);
            swprintf_s(s_ceMaxBuf[bi], L"%.0f", ceMax);
            swprintf_s(s_ceMinBuf[bi], L"%.0f", ceMin);
            s_ceLabelValid[bi] = true;
        }

        if (!s_ceLabelValid[bi]) continue;

        // Labels (from cache)
        gfx.DrawString(s_ceLabelBuf[bi], -1, &labelFont,
                        Gdiplus::PointF(fChartX + 4.0f, fChartY + 2.0f),
                        &labelBrush);

        // Min/max on right side
        gfx.DrawString(s_ceMaxBuf[bi], -1, &labelFont,
                        Gdiplus::PointF(fChartX + fChartW - 50.0f, fChartY + 2.0f),
                        &labelBrush);
        gfx.DrawString(s_ceMinBuf[bi], -1, &labelFont,
                        Gdiplus::PointF(fChartX + fChartW - 50.0f, fChartY + fChartH - 14.0f),
                        &labelBrush);
    }
}

// ============================================================================
// Magnified beam thumbnails
// ============================================================================

void DrawBeamMagnifiers(Gdiplus::Graphics& gfx,
                        const std::vector<BeamDetectionResult>& results,
                        const uint8_t* frameData, uint32_t frameW, uint32_t frameH,
                        int renderX, int renderY, int renderW, int renderH) {
    if (!frameData || frameW == 0 || frameH == 0 || results.empty()) return;

    static constexpr int BOX_SIZE = 150;
    static constexpr int MARGIN = 8;
    static constexpr float BORDER_WIDTH = 2.0f;

    // Static buffer — avoids heap allocation every frame (150*150*4 = 90KB)
    static uint8_t thumbBuf[BOX_SIZE * BOX_SIZE * 4];
    static constexpr int stride = BOX_SIZE * 4;

    // Pre-compute column lookup table (reused per beam if crop changes)
    static int colLUT[BOX_SIZE];

    int boxX = renderX + renderW - BOX_SIZE - MARGIN;
    int imgStride = static_cast<int>(frameW) * 3;
    int maxRow = static_cast<int>(frameH) - 1;
    int maxCol = static_cast<int>(frameW) - 1;

    for (size_t bi = 0; bi < results.size() && bi < 2; ++bi) {
        const auto& beam = results[bi];
        const auto& d4 = beam.d4;

        int boxY = renderY + MARGIN + static_cast<int>(bi) * (BOX_SIZE + MARGIN);

        // Crop region: square centered on centroid, 3x beam diameter
        double span = std::max(d4.dx, d4.dy) * 3.0;
        if (span < 20.0) span = 20.0;

        int srcX0 = static_cast<int>(d4.cx - span * 0.5);
        int srcY0 = static_cast<int>(d4.cy - span * 0.5);
        int srcX1 = srcX0 + static_cast<int>(span);
        int srcY1 = srcY0 + static_cast<int>(span);

        srcX0 = std::max(srcX0, 0);
        srcY0 = std::max(srcY0, 0);
        srcX1 = std::min(srcX1, static_cast<int>(frameW));
        srcY1 = std::min(srcY1, static_cast<int>(frameH));

        int cropW = srcX1 - srcX0;
        int cropH = srcY1 - srcY0;
        if (cropW < 2 || cropH < 2) continue;

        // Build column lookup: integer fixed-point scaling (no per-pixel float math)
        for (int dx = 0; dx < BOX_SIZE; ++dx) {
            int c = srcX0 + (dx * cropW) / BOX_SIZE;
            colLUT[dx] = std::min(c, maxCol) * 3;
        }

        // Fill thumbnail buffer: BGR from DIB (bottom-up) -> BGRA top-down
        for (int dy = 0; dy < BOX_SIZE; ++dy) {
            int srcRow = srcY0 + (dy * cropH) / BOX_SIZE;
            if (srcRow > maxRow) srcRow = maxRow;

            // DIB bottom-up: row 0 = last image row
            const uint8_t* srcLine = frameData + static_cast<size_t>(maxRow - srcRow) * imgStride;
            uint8_t* dstLine = thumbBuf + dy * stride;

            for (int dx = 0; dx < BOX_SIZE; ++dx) {
                const uint8_t* sp = srcLine + colLUT[dx];
                uint8_t* dp = dstLine + dx * 4;
                dp[0] = sp[0];  // B
                dp[1] = sp[1];  // G
                dp[2] = sp[2];  // R
                dp[3] = 255;    // A
            }
        }

        Gdiplus::Bitmap thumbBmp(BOX_SIZE, BOX_SIZE, stride,
                                  PixelFormat32bppARGB, thumbBuf);

        gfx.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
        gfx.DrawImage(&thumbBmp, boxX, boxY, BOX_SIZE, BOX_SIZE);

        // Border
        Gdiplus::Pen borderPen(beam.saturated
            ? Gdiplus::Color(220, 255, 60, 60)
            : Gdiplus::Color(220, 0, 255, 255), BORDER_WIDTH);
        gfx.DrawRectangle(&borderPen, boxX, boxY, BOX_SIZE, BOX_SIZE);

        // Label
        {
            static Gdiplus::Font labelFont(L"Consolas", 8.0f, Gdiplus::FontStyleBold);
            static Gdiplus::SolidBrush labelBgBrush(Gdiplus::Color(180, 0, 0, 0));
            static Gdiplus::SolidBrush labelBrush(Gdiplus::Color(255, 220, 220, 220));

            const wchar_t* label = (bi == 0) ? L"B0" : L"B1";
            float lx = static_cast<float>(boxX) + 4.0f;
            float ly = static_cast<float>(boxY) + 2.0f;
            gfx.FillRectangle(&labelBgBrush, lx - 2.0f, ly, 22.0f, 14.0f);
            gfx.DrawString(label, -1, &labelFont, Gdiplus::PointF(lx, ly), &labelBrush);
        }

        // Crosshair at centroid position within thumbnail
        float cx = static_cast<float>(boxX) + static_cast<float>((d4.cx - srcX0) * BOX_SIZE) / cropW;
        float cy = static_cast<float>(boxY) + static_cast<float>((d4.cy - srcY0) * BOX_SIZE) / cropH;
        Gdiplus::Pen crossPen(Gdiplus::Color(200, 255, 255, 0), 1.0f);
        gfx.DrawLine(&crossPen, cx - 8.0f, cy, cx + 8.0f, cy);
        gfx.DrawLine(&crossPen, cx, cy - 8.0f, cx, cy + 8.0f);
    }
}

// ============================================================================
// Beam crop tiles (heatmap-coloured close-ups, aspect-preserving fit)
// ============================================================================

namespace {

// Jet-style heatmap LUT (blue -> cyan -> green -> yellow -> red),
// matches the HDevelop script's 'temperature' display style.
// Indexed by gray value [0..255]; entries are {B, G, R}.
const uint8_t* HeatLut() {
    static uint8_t HEAT_LUT[256][3];
    static bool s_ready = false;
    if (!s_ready) {
        auto clamp01 = [](float v) {
            return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        };
        for (int i = 0; i < 256; ++i) {
            float v = i / 255.0f;
            float r, g, b;
            if (v < 0.125f) {              // dark blue -> blue
                r = 0.0f; g = 0.0f; b = 0.5f + v * 4.0f;
            } else if (v < 0.375f) {       // blue -> cyan
                r = 0.0f; g = (v - 0.125f) * 4.0f; b = 1.0f;
            } else if (v < 0.625f) {       // cyan -> yellow
                r = (v - 0.375f) * 4.0f; g = 1.0f; b = 1.0f - (v - 0.375f) * 4.0f;
            } else if (v < 0.875f) {       // yellow -> red
                r = 1.0f; g = 1.0f - (v - 0.625f) * 4.0f; b = 0.0f;
            } else {                        // red -> dark red
                r = 1.0f - (v - 0.875f) * 2.0f; g = 0.0f; b = 0.0f;
            }
            HEAT_LUT[i][0] = static_cast<uint8_t>(clamp01(b) * 255.0f);
            HEAT_LUT[i][1] = static_cast<uint8_t>(clamp01(g) * 255.0f);
            HEAT_LUT[i][2] = static_cast<uint8_t>(clamp01(r) * 255.0f);
        }
        s_ready = true;
    }
    return &HEAT_LUT[0][0];
}

}  // anonymous namespace

void DrawSingleBeamCropTile(Gdiplus::Graphics& gfx,
                            const BeamDetectionResult& beam,
                            const uint8_t* frameData,
                            uint32_t frameW, uint32_t frameH,
                            int dstX, int dstY, int tileSize,
                            int slotIndex) {
    if (tileSize < 8) return;

    // Re-usable thumbnail buffer. Per-call allocation isn't on the hot path.
    std::vector<uint8_t> tileBuf(static_cast<size_t>(tileSize) * tileSize * 4);

    static Gdiplus::SolidBrush placeholderBrush(Gdiplus::Color(255, 35, 35, 35));
    static Gdiplus::SolidBrush labelBgBrush(Gdiplus::Color(180, 0, 0, 0));
    static Gdiplus::SolidBrush labelBrush(Gdiplus::Color(255, 220, 220, 220));
    static Gdiplus::Font labelFont(L"Consolas", 10.0f, Gdiplus::FontStyleBold);

    // Always paint the placeholder background first so letterbox bands and
    // edge clipping stay visually consistent with empty slots.
    gfx.FillRectangle(&placeholderBrush, dstX, dstY, tileSize, tileSize);

    const auto& d4 = beam.d4;
    const bool hasFrame = (frameData && frameW > 0 && frameH > 0);

    int srcX0 = std::max(d4.roi.x0, 0);
    int srcY0 = std::max(d4.roi.y0, 0);
    int srcX1 = std::min(d4.roi.x1, static_cast<int>(frameW));
    int srcY1 = std::min(d4.roi.y1, static_cast<int>(frameH));
    int cropW = srcX1 - srcX0;
    int cropH = srcY1 - srcY0;

    bool slotHasContent = hasFrame && cropW >= 2 && cropH >= 2;
    if (slotHasContent) {
        // Aspect-preserving fit into tileSize x tileSize.
        double srcAR = static_cast<double>(cropW) / cropH;
        int dstW, dstH;
        if (srcAR >= 1.0) {
            dstW = tileSize;
            dstH = std::max(1, static_cast<int>(tileSize / srcAR));
        } else {
            dstH = tileSize;
            dstW = std::max(1, static_cast<int>(tileSize * srcAR));
        }
        int activeX = dstX + (tileSize - dstW) / 2;
        int activeY = dstY + (tileSize - dstH) / 2;

        const int imgStride = static_cast<int>(frameW) * 3;
        const int maxRow = static_cast<int>(frameH) - 1;
        const int maxCol = static_cast<int>(frameW) - 1;
        const uint8_t* lutBase = HeatLut();

        std::vector<int> colLUT(dstW);
        for (int dx = 0; dx < dstW; ++dx) {
            int c = srcX0 + (dx * cropW) / dstW;
            if (c > maxCol) c = maxCol;
            colLUT[dx] = c * 3;
        }

        for (int dy = 0; dy < dstH; ++dy) {
            int srcRow = srcY0 + (dy * cropH) / dstH;
            if (srcRow > maxRow) srcRow = maxRow;
            // BGR DIB is bottom-up, so flip the row index.
            const uint8_t* srcLine =
                frameData + static_cast<size_t>(maxRow - srcRow) * imgStride;
            uint8_t* dstLine = tileBuf.data() + dy * (dstW * 4);
            for (int dx = 0; dx < dstW; ++dx) {
                const uint8_t v = srcLine[colLUT[dx]];
                const uint8_t* lut = lutBase + v * 3;
                uint8_t* dp = dstLine + dx * 4;
                dp[0] = lut[0]; dp[1] = lut[1]; dp[2] = lut[2]; dp[3] = 255;
            }
        }

        Gdiplus::Bitmap tileBmp(dstW, dstH, dstW * 4,
                                PixelFormat32bppARGB, tileBuf.data());
        gfx.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
        gfx.DrawImage(&tileBmp, activeX, activeY, dstW, dstH);

        // Centroid crosshair (white — stays visible across the full heatmap).
        float cx = static_cast<float>(activeX) +
                   static_cast<float>((d4.cx - srcX0) * dstW) / cropW;
        float cy = static_cast<float>(activeY) +
                   static_cast<float>((d4.cy - srcY0) * dstH) / cropH;
        Gdiplus::Pen crossPen(Gdiplus::Color(220, 255, 255, 255), 1.0f);
        gfx.DrawLine(&crossPen, cx - 10.0f, cy, cx + 10.0f, cy);
        gfx.DrawLine(&crossPen, cx, cy - 10.0f, cx, cy + 10.0f);
    }

    // Border colour: red on saturation, cyan otherwise, gray when no content.
    Gdiplus::Color borderColor =
        slotHasContent
            ? (beam.saturated ? Gdiplus::Color(220, 255, 60, 60)
                              : Gdiplus::Color(220, 0, 200, 200))
            : Gdiplus::Color(180, 90, 90, 90);
    Gdiplus::Pen borderPen(borderColor, 2.0f);
    gfx.DrawRectangle(&borderPen, dstX, dstY, tileSize, tileSize);

    // Corner label.
    if (slotIndex >= 0) {
        const wchar_t* label = (slotIndex == 0) ? L"B0" : L"B1";
        gfx.FillRectangle(&labelBgBrush,
                          static_cast<float>(dstX) + 4.0f,
                          static_cast<float>(dstY) + 4.0f,
                          26.0f, 16.0f);
        gfx.DrawString(label, -1, &labelFont,
                       Gdiplus::PointF(static_cast<float>(dstX) + 6.0f,
                                       static_cast<float>(dstY) + 4.0f),
                       &labelBrush);
    }
}

void DrawBeamCropStrip(Gdiplus::Graphics& gfx,
                       const std::vector<BeamDetectionResult>& results,
                       const uint8_t* frameData,
                       uint32_t frameW, uint32_t frameH,
                       const RECT& stripRect) {
    static constexpr int TILE_SIZE = 200;
    static constexpr int MARGIN = 10;
    static constexpr int MAX_TILES = 2;

    static Gdiplus::SolidBrush emptyBrush(Gdiplus::Color(255, 35, 35, 35));
    static Gdiplus::SolidBrush labelBgBrush(Gdiplus::Color(180, 0, 0, 0));
    static Gdiplus::SolidBrush labelBrush(Gdiplus::Color(255, 220, 220, 220));
    static Gdiplus::Font labelFont(L"Consolas", 10.0f, Gdiplus::FontStyleBold);
    static Gdiplus::Pen emptyBorderPen(Gdiplus::Color(180, 90, 90, 90), 2.0f);

    const int stripH = stripRect.bottom - stripRect.top;
    const int stripW = stripRect.right - stripRect.left;
    if (stripH < TILE_SIZE + 4 || stripW < TILE_SIZE + 2 * MARGIN) return;

    const int tileY = stripRect.top + (stripH - TILE_SIZE) / 2;

    for (int bi = 0; bi < MAX_TILES; ++bi) {
        const int tileX = stripRect.left + MARGIN + bi * (TILE_SIZE + MARGIN);
        if (tileX + TILE_SIZE > stripRect.right) break;

        if (static_cast<size_t>(bi) < results.size()) {
            DrawSingleBeamCropTile(gfx, results[bi], frameData, frameW, frameH,
                                   tileX, tileY, TILE_SIZE, bi);
        } else {
            // Empty slot — placeholder + faded label.
            gfx.FillRectangle(&emptyBrush, tileX, tileY, TILE_SIZE, TILE_SIZE);
            gfx.DrawRectangle(&emptyBorderPen, tileX, tileY, TILE_SIZE, TILE_SIZE);
            const wchar_t* label = (bi == 0) ? L"B0" : L"B1";
            gfx.FillRectangle(&labelBgBrush,
                              static_cast<float>(tileX) + 4.0f,
                              static_cast<float>(tileY) + 4.0f,
                              26.0f, 16.0f);
            gfx.DrawString(label, -1, &labelFont,
                           Gdiplus::PointF(static_cast<float>(tileX) + 6.0f,
                                           static_cast<float>(tileY) + 4.0f),
                           &labelBrush);
        }
    }
}
