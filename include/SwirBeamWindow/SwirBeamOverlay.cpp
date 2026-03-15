// SwirBeamOverlay.cpp
// GDI+ rendering of beam detection overlays
#include "SwirBeamOverlay.h"

#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>

namespace {

// Chart size = beamDiameter * CHART_SPAN_FACTOR (in image pixels)
static constexpr double CHART_SPAN_FACTOR = 2.5;

// Convert image-space coordinate to display-space
float imgToDispX(double imgX, int renderX, int renderW, uint32_t frameW) {
    return static_cast<float>(renderX + (imgX / frameW) * renderW);
}

float imgToDispY(double imgY, int renderY, int renderH, uint32_t frameH) {
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
                      uint32_t frameW, uint32_t frameH) {
    if (results.empty() || frameW == 0 || frameH == 0 || renderW <= 0 || renderH <= 0) return;

    Gdiplus::Font labelFont(L"Consolas", 8.0f, Gdiplus::FontStyleBold);

    for (const auto& beam : results) {
        const auto& d4 = beam.d4;

        // Compute chart dimension in image pixels: 2.5x the beam diameter
        double beamDiam = std::max(d4.dx, d4.dy);
        if (beamDiam < 1.0) beamDiam = 1.0;
        double chartDim = beamDiam * CHART_SPAN_FACTOR;

        // (a) Bounding box around d4sigma extent (1.25x padding) - not the ROI
        double boxPadFactor = 1.25;
        double boxHalfW = (d4.dx / 2.0) * boxPadFactor;
        double boxHalfH = (d4.dy / 2.0) * boxPadFactor;
        // Ensure minimum box size of 5 pixels
        if (boxHalfW < 2.5) boxHalfW = 2.5;
        if (boxHalfH < 2.5) boxHalfH = 2.5;

        Gdiplus::Color boxColor = beam.saturated
            ? Gdiplus::Color(220, 255, 60, 60)
            : Gdiplus::Color(220, 0, 255, 255);
        Gdiplus::Pen boxPen(boxColor, 2.0f);

        float bx0 = imgToDispX(d4.cx - boxHalfW, renderX, renderW, frameW);
        float by0 = imgToDispY(d4.cy - boxHalfH, renderY, renderH, frameH);
        float bx1 = imgToDispX(d4.cx + boxHalfW, renderX, renderW, frameW);
        float by1 = imgToDispY(d4.cy + boxHalfH, renderY, renderH, frameH);
        gfx.DrawRectangle(&boxPen, bx0, by0, bx1 - bx0, by1 - by0);

        // (b) Centroid crosshair - yellow 10px cross
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

        // (c) Text label - multi-row, grid-snapped position
        //     Snap anchor to a coarse grid so the label doesn't jitter frame-to-frame
        {
            // Grid snap: round centroid display position to nearest gridStep
            static constexpr float gridStep = 20.0f;
            float snapX = std::floor(ccx / gridStep) * gridStep;
            float snapY = std::floor(ccy / gridStep) * gridStep;

            // Build label rows
            std::wstring titleLine;
            {
                std::wostringstream ss;
                ss << std::wstring(beam.label.begin(), beam.label.end());
                if (!beam.gaLabel.empty())
                    ss << L" (" << std::wstring(beam.gaLabel.begin(), beam.gaLabel.end()) << L")";
                if (beam.saturated)
                    ss << L"  SAT!";
                titleLine = ss.str();
            }

            wchar_t cxLine[32], cyLine[32], dxLine[32], dyLine[32], eeLine[32];
            swprintf_s(cxLine, L"cx: %6.1f", d4.cx);
            swprintf_s(cyLine, L"cy: %6.1f", d4.cy);
            swprintf_s(dxLine, L"dx: %6.1f", d4.dx);
            swprintf_s(dyLine, L"dy: %6.1f", d4.dy);
            swprintf_s(eeLine, L"EE: %5.1f%%", beam.encircledEnergy * 100.0);

            const wchar_t* rows[] = { titleLine.c_str(), cxLine, cyLine, dxLine, dyLine, eeLine };
            static constexpr int numRows = 6;
            static constexpr float rowH = 14.0f;
            float totalH = rowH * numRows + 4.0f;

            // Measure widest row for background width
            float maxW = 0.0f;
            for (int r = 0; r < numRows; ++r) {
                Gdiplus::RectF m;
                gfx.MeasureString(rows[r], -1, &labelFont, Gdiplus::PointF(0, 0), &m);
                if (m.Width > maxW) maxW = m.Width;
            }

            // Position: above the beam box, snapped to grid
            float labelX = snapX;
            float labelY = snapY - totalH - 6.0f;

            // If above viewport, place below
            if (labelY < static_cast<float>(renderY))
                labelY = snapY + imgToDispLenY(boxHalfH * 2.0 + 4.0, renderH, frameH);

            // Keep within viewport horizontally
            float maxRight = static_cast<float>(renderX + renderW) - maxW - 6.0f;
            if (labelX > maxRight) labelX = maxRight;
            if (labelX < static_cast<float>(renderX + 2)) labelX = static_cast<float>(renderX + 2);

            // Background
            Gdiplus::SolidBrush textBgBrush(Gdiplus::Color(180, 0, 0, 0));
            gfx.FillRectangle(&textBgBrush, labelX - 3.0f, labelY - 2.0f,
                              maxW + 6.0f, totalH + 2.0f);

            // Draw rows
            Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 220, 220, 220));
            Gdiplus::SolidBrush satBrush(Gdiplus::Color(255, 255, 80, 80));
            for (int r = 0; r < numRows; ++r) {
                Gdiplus::SolidBrush* brush = &textBrush;
                // Title row with SAT! in red
                if (r == 0 && beam.saturated) brush = &satBrush;
                gfx.DrawString(rows[r], -1, &labelFont,
                               Gdiplus::PointF(labelX, labelY + r * rowH), brush);
            }
        }

        // Chart display sizes (square: chartDim x chartDim in image pixels)
        float dChartW = imgToDispLenX(chartDim, renderW, frameW);
        float dChartH = imgToDispLenY(chartDim, renderH, frameH);

        // (d) X profile chart - above the beam, centered on cx
        //     Image-space: centered on cx horizontally, bottom edge at cy - chartDim
        {
            ProfileSubRange sub = extractSubRange(beam.xProfile, beam.profileXStart,
                                                  d4.cx, chartDim);
            // Display position: centered on ccx, bottom at ccy - chartDim (in display)
            float xChartLeft = ccx - dChartW / 2.0f;
            float xChartTop = ccy - imgToDispLenY(chartDim, renderH, frameH) - dChartH;
            DrawHorizProfileChart(gfx, sub, xChartLeft, xChartTop, dChartW, dChartH,
                                  d4.cx, d4.dx, renderX, renderW, frameW,
                                  Gdiplus::Color(220, 0, 255, 255));
        }

        // (e) Y profile chart - right of the beam, centered on cy
        //     Image-space: centered on cy vertically, left edge at cx + chartDim
        {
            ProfileSubRange sub = extractSubRange(beam.yProfile, beam.profileYStart,
                                                  d4.cy, chartDim);
            // Display position: left at ccx + chartDim (in display), centered on ccy
            float yChartLeft = ccx + imgToDispLenX(chartDim, renderW, frameW);
            float yChartTop = ccy - dChartH / 2.0f;
            DrawVertProfileChart(gfx, sub, yChartLeft, yChartTop, dChartW, dChartH,
                                 d4.cy, d4.dy, renderY, renderH, frameH,
                                 Gdiplus::Color(220, 255, 0, 255));
        }
    }
}
