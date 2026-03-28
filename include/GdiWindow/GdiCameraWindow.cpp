// GdiCameraWindow.cpp
#include "GdiCameraWindow.h"
#include "customserver/BeamServerClient.h"
#include "data/DataStore.h"
#include "motions/unified_motion_manager.h"
#include "camera/CameraManager.h"
#include "camera/CameraConfigManager.h"
#include <algorithm>
#include <iostream>
#include <cmath>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <fstream>
#include <set>

#include <QSettings>

#ifdef HAS_OPENCV
#include "vision/VisionManager.h"
#include "vision/VisionROI.h"
#include <opencv2/imgproc.hpp>
#include <QString>
#endif

// Static members
bool GdiCameraWindow::s_classRegistered = false;
const wchar_t* GdiCameraWindow::s_className = L"GdiCameraWindowClass";
ULONG_PTR GdiCameraWindow::s_gdiplusToken = 0;
bool GdiCameraWindow::s_gdiplusInitialized = false;

// ============================================================================
// ChartOverlay Implementation
// ============================================================================

void ChartOverlay::AddDataPoint(const std::string& channelId, double value) {
    std::lock_guard<std::mutex> lock(m_dataMutex);

    auto& ch = m_channels[channelId];
    ch.values.push_back(value);
    if (ch.values.size() > MAX_POINTS)
        ch.values.pop_front();

    if (value < ch.runningMin) ch.runningMin = value;
    if (value > ch.runningMax) ch.runningMax = value;
}

void ChartOverlay::Clear() {
    std::lock_guard<std::mutex> lock(m_dataMutex);
    m_channels.clear();
}

void ChartOverlay::SetChannelEnabled(const std::string& channelId, bool enabled) {
    std::lock_guard<std::mutex> lock(m_dataMutex);
    if (enabled) {
        m_enabledChannels.insert(channelId);
    } else {
        m_enabledChannels.erase(channelId);
    }
}

bool ChartOverlay::IsChannelEnabled(const std::string& channelId) const {
    std::lock_guard<std::mutex> lock(m_dataMutex);
    return m_enabledChannels.find(channelId) != m_enabledChannels.end();
}

std::vector<std::string> ChartOverlay::GetEnabledChannels() const {
    std::lock_guard<std::mutex> lock(m_dataMutex);
    return std::vector<std::string>(m_enabledChannels.begin(), m_enabledChannels.end());
}

void ChartOverlay::SetScale(float scale) {
    if (scale < MIN_SCALE) scale = MIN_SCALE;
    if (scale > MAX_SCALE) scale = MAX_SCALE;
    m_scale = scale;
    std::cout << "[ChartOverlay] Scale set to " << m_scale << std::endl;
}

void ChartOverlay::IncreaseScale() {
    SetScale(m_scale + SCALE_STEP);
}

void ChartOverlay::DecreaseScale() {
    SetScale(m_scale - SCALE_STEP);
}

void ChartOverlay::Draw(HDC hDC, const RECT& windowRect) {
    if (!m_enabled) return;

    std::lock_guard<std::mutex> lock(m_dataMutex);

    // Only draw channels that are enabled and have data
    bool hasVisibleData = false;
    for (const auto& channelId : m_enabledChannels) {
        auto it = m_channels.find(channelId);
        if (it != m_channels.end() && !it->second.values.empty()) {
            hasVisibleData = true;
            break;
        }
    }
    if (!hasVisibleData) return;

    Gdiplus::Graphics gfx(hDC);
    gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    gfx.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    int winW = windowRect.right - windowRect.left;

    // Get scaled dimensions
    int chartWidth = GetChartWidth();
    int chartHeight = GetChartHeight();

    // Chart rect: top-right corner
    int chartLeft = winW - chartWidth - CHART_MARGIN;
    int chartTop = CHART_MARGIN;
    if (chartLeft < CHART_MARGIN) chartLeft = CHART_MARGIN;

    Gdiplus::RectF chartRect(
        static_cast<Gdiplus::REAL>(chartLeft),
        static_cast<Gdiplus::REAL>(chartTop),
        static_cast<Gdiplus::REAL>(chartWidth),
        static_cast<Gdiplus::REAL>(chartHeight));

    // Semi-transparent dark background
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(180, 20, 20, 30));
    gfx.FillRectangle(&bgBrush, chartRect);

    // Border
    Gdiplus::Pen borderPen(Gdiplus::Color(200, 80, 80, 80), 1.0f);
    gfx.DrawRectangle(&borderPen, chartRect);

    // Compute global Y range across all enabled channels
    double globalMin = 1e30;
    double globalMax = -1e30;
    int colorIndex = 0;
    for (const auto& channelId : m_enabledChannels) {
        auto it = m_channels.find(channelId);
        if (it == m_channels.end() || it->second.values.empty()) continue;
        for (double v : it->second.values) {
            if (v < globalMin) globalMin = v;
            if (v > globalMax) globalMax = v;
        }
    }

    if (globalMin > globalMax) return;

    // Add 10% padding
    double range = globalMax - globalMin;
    if (range < 1e-15) range = 1e-12;
    double padding = range * 0.10;
    double yMin = globalMin - padding;
    double yMax = globalMax + padding;
    double yRange = yMax - yMin;

    // Scaled font sizes and layout dimensions
    float scaledLegendFontSize = 8.0f * m_scale;
    float scaledValueFontSize = 14.0f * m_scale;
    float legendH = 20.0f * m_scale;
    float valuesH = 28.0f * m_scale;
    float plotLeft = chartRect.X + 4.0f * m_scale;
    float plotTop = chartRect.Y + legendH;
    float plotW = chartRect.Width - 8.0f * m_scale;
    float plotH = chartRect.Height - legendH - valuesH - 4.0f * m_scale;

    // Fonts (scaled)
    Gdiplus::Font legendFont(L"Segoe UI", scaledLegendFontSize, Gdiplus::FontStyleBold);
    Gdiplus::Font valueFont(L"Segoe UI", scaledValueFontSize, Gdiplus::FontStyleBold);

    // Draw legend and curves per channel
    float legendX = chartRect.X + 6.0f * m_scale;
    float legendY = chartRect.Y + 2.0f * m_scale;
    float valueX = chartRect.X + 6.0f * m_scale;
    float valueY = chartRect.Y + chartRect.Height - valuesH - 1.0f;

    colorIndex = 0;
    for (const auto& channelId : m_enabledChannels) {
        auto it = m_channels.find(channelId);
        if (it == m_channels.end() || it->second.values.empty()) continue;
        const auto& data = it->second;

        Gdiplus::Color color = GetChannelColor(colorIndex++);
        Gdiplus::SolidBrush textBrush(color);
        Gdiplus::Pen linePen(color, 1.5f * m_scale);

        // Legend: colored square + channel name (truncated)
        float squareSize = 8.0f * m_scale;
        Gdiplus::SolidBrush squareBrush(color);
        gfx.FillRectangle(&squareBrush, legendX, legendY + 4.0f * m_scale, squareSize, squareSize);

        // Truncate channel name to fit (adjust length based on scale)
        std::wstring wChannelId(channelId.begin(), channelId.end());
        size_t maxChars = static_cast<size_t>(12 * m_scale);
        if (maxChars < 6) maxChars = 6;
        if (wChannelId.length() > maxChars) {
            wChannelId = wChannelId.substr(0, maxChars - 2) + L"..";
        }
        gfx.DrawString(wChannelId.c_str(), -1, &legendFont,
            Gdiplus::PointF(legendX + 11.0f * m_scale, legendY), &textBrush);
        legendX += 90.0f * m_scale;

        // Build PointF array for the curve
        size_t count = data.values.size();
        std::vector<Gdiplus::PointF> pts(count);
        for (size_t i = 0; i < count; i++) {
            float xFrac = (count > 1) ? static_cast<float>(i) / (count - 1) : 0.5f;
            float yFrac = static_cast<float>((data.values[i] - yMin) / yRange);
            yFrac = 1.0f - yFrac;  // invert: higher values at top

            pts[i].X = plotLeft + xFrac * plotW;
            pts[i].Y = plotTop + yFrac * plotH;
        }

        // Draw smooth cardinal spline
        if (count >= 2) {
            gfx.DrawCurve(&linePen, pts.data(), static_cast<INT>(count));
        } else if (count == 1) {
            float dotSize = 4.0f * m_scale;
            Gdiplus::RectF dot(pts[0].X - dotSize/2, pts[0].Y - dotSize/2, dotSize, dotSize);
            gfx.DrawEllipse(&linePen, dot);
        }

        // Current value text at bottom (larger font)
        std::wstring valStr = FormatValue(data.values.back());
        gfx.DrawString(valStr.c_str(), -1, &valueFont,
            Gdiplus::PointF(valueX, valueY), &textBrush);
        valueX += 120.0f * m_scale;  // Scaled spacing for value display
    }
}

Gdiplus::Color ChartOverlay::GetChannelColor(int index) {
    switch (index % 6) {
    case 0: return Gdiplus::Color(255, 0, 200, 255);    // Cyan
    case 1: return Gdiplus::Color(255, 255, 220, 0);    // Yellow
    case 2: return Gdiplus::Color(255, 0, 255, 100);    // Lime
    case 3: return Gdiplus::Color(255, 255, 100, 255);  // Magenta
    case 4: return Gdiplus::Color(255, 255, 128, 0);    // Orange
    case 5: return Gdiplus::Color(255, 128, 255, 128);  // Light green
    default: return Gdiplus::Color(255, 200, 200, 200); // Gray
    }
}

std::wstring ChartOverlay::FormatValue(double value) {
    wchar_t buf[64];
    double absVal = std::fabs(value);

    if (absVal < 1e-9)
        swprintf_s(buf, L"%.2f pA", value * 1e12);
    else if (absVal < 1e-6)
        swprintf_s(buf, L"%.2f nA", value * 1e9);
    else if (absVal < 1e-3)
        swprintf_s(buf, L"%.3f uA", value * 1e6);
    else if (absVal < 1.0)
        swprintf_s(buf, L"%.3f mA", value * 1e3);
    else
        swprintf_s(buf, L"%.4f", value);

    return buf;
}

// ============================================================================
// VisionResultOverlay Implementation
// ============================================================================

#ifdef HAS_OPENCV

void VisionResultOverlay::SetResult(const VisionResult& result, const std::string& profileName) {
    std::lock_guard<std::mutex> lock(m_resultMutex);
    m_lastResult = result;
    m_profileName = profileName;
    m_resultSequence.fetch_add(1, std::memory_order_relaxed);
}

void VisionResultOverlay::Clear() {
    std::lock_guard<std::mutex> lock(m_resultMutex);
    m_lastResult.clear();
    m_profileName.clear();
}

Gdiplus::PointF VisionResultOverlay::ImageToScreen(double imgX, double imgY,
                                                    int frameWidth, int frameHeight,
                                                    int destX, int destY,
                                                    int destWidth, int destHeight) {
    float scaleX = static_cast<float>(destWidth) / frameWidth;
    float scaleY = static_cast<float>(destHeight) / frameHeight;
    return Gdiplus::PointF(
        destX + static_cast<float>(imgX * scaleX),
        destY + static_cast<float>(imgY * scaleY)
    );
}

void VisionResultOverlay::Draw(HDC hDC, const RECT& /*windowRect*/, int frameWidth, int frameHeight,
                                int destX, int destY, int destWidth, int destHeight) {
    if (!m_enabled) return;

    std::lock_guard<std::mutex> lock(m_resultMutex);

    if (m_profileName.empty()) return;

    Gdiplus::Graphics gfx(hDC);
    gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    gfx.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    // Draw info panel on LEFT side
    Gdiplus::RectF panelRect(
        static_cast<Gdiplus::REAL>(OVERLAY_MARGIN),
        static_cast<Gdiplus::REAL>(OVERLAY_MARGIN),
        static_cast<Gdiplus::REAL>(OVERLAY_WIDTH),
        static_cast<Gdiplus::REAL>(300));  // Dynamic height based on content

    DrawInfoPanel(gfx, panelRect);

    // Draw geometric overlays on the camera image
    if (frameWidth > 0 && frameHeight > 0 && destWidth > 0 && destHeight > 0) {
        DrawGeometricOverlays(hDC, frameWidth, frameHeight, destX, destY, destWidth, destHeight);
    }
}

void VisionResultOverlay::DrawInfoPanel(Gdiplus::Graphics& gfx, const Gdiplus::RectF& panelRect) {
    // Semi-transparent dark background
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(200, 20, 20, 30));
    gfx.FillRectangle(&bgBrush, panelRect);

    // Border
    Gdiplus::Pen borderPen(Gdiplus::Color(200, 80, 80, 80), 1.0f);
    gfx.DrawRectangle(&borderPen, panelRect);

    // Fonts
    Gdiplus::Font titleFont(L"Segoe UI", 11.0f, Gdiplus::FontStyleBold);
    Gdiplus::Font labelFont(L"Segoe UI", 9.0f, Gdiplus::FontStyleRegular);
    Gdiplus::Font valueFont(L"Segoe UI", 9.0f, Gdiplus::FontStyleBold);

    // Colors
    Gdiplus::SolidBrush titleBrush(Gdiplus::Color(255, 255, 255, 255));
    Gdiplus::SolidBrush dimBrush(Gdiplus::Color(255, 180, 180, 180));
    Gdiplus::SolidBrush successBrush(Gdiplus::Color(255, 100, 255, 100));
    Gdiplus::SolidBrush errorBrush(Gdiplus::Color(255, 255, 100, 100));
    Gdiplus::SolidBrush valueBrush(Gdiplus::Color(255, 0, 200, 255));

    float y = panelRect.Y + 8.0f;
    float x = panelRect.X + 10.0f;
    float lineHeight = 18.0f;

    // Title
    gfx.DrawString(L"Vision Result", -1, &titleFont, Gdiplus::PointF(x, y), &titleBrush);
    y += lineHeight + 4.0f;

    // Profile name
    std::wstring wProfile(m_profileName.begin(), m_profileName.end());
    if (wProfile.length() > 28) {
        wProfile = wProfile.substr(0, 25) + L"...";
    }
    gfx.DrawString(wProfile.c_str(), -1, &labelFont, Gdiplus::PointF(x, y), &dimBrush);
    y += lineHeight + 8.0f;

    // Status
    std::wstring statusStr = m_lastResult.success ? L"SUCCESS" : L"FAILED";
    Gdiplus::SolidBrush* statusBrush = m_lastResult.success ? &successBrush : &errorBrush;
    gfx.DrawString(L"Status: ", -1, &labelFont, Gdiplus::PointF(x, y), &dimBrush);
    gfx.DrawString(statusStr.c_str(), -1, &valueFont, Gdiplus::PointF(x + 50.0f, y), statusBrush);
    y += lineHeight;

    // Processing time
    wchar_t timeBuf[32];
    swprintf_s(timeBuf, L"%.1f ms", m_lastResult.processingTimeMs);
    gfx.DrawString(L"Time: ", -1, &labelFont, Gdiplus::PointF(x, y), &dimBrush);
    gfx.DrawString(timeBuf, -1, &valueFont, Gdiplus::PointF(x + 50.0f, y), &valueBrush);
    y += lineHeight + 8.0f;

    // Detection counts
    if (!m_lastResult.circles.empty()) {
        wchar_t buf[32];
        swprintf_s(buf, L"Circles: %zu", m_lastResult.circles.size());
        gfx.DrawString(buf, -1, &valueFont, Gdiplus::PointF(x, y), &valueBrush);
        y += lineHeight;
    }

    if (!m_lastResult.points.empty()) {
        wchar_t buf[32];
        swprintf_s(buf, L"Points: %zu", m_lastResult.points.size());
        gfx.DrawString(buf, -1, &valueFont, Gdiplus::PointF(x, y), &valueBrush);
        y += lineHeight;
    }

    if (!m_lastResult.lines.empty()) {
        wchar_t buf[32];
        swprintf_s(buf, L"Lines: %zu", m_lastResult.lines.size());
        gfx.DrawString(buf, -1, &valueFont, Gdiplus::PointF(x, y), &valueBrush);
        y += lineHeight;
    }

    if (!m_lastResult.beams.empty()) {
        wchar_t buf[32];
        swprintf_s(buf, L"Beams: %zu", m_lastResult.beams.size());
        gfx.DrawString(buf, -1, &valueFont, Gdiplus::PointF(x, y), &valueBrush);
        y += lineHeight;
    }

    // Measurements
    if (!m_lastResult.measurements.empty()) {
        y += 4.0f;
        for (const auto& [key, val] : m_lastResult.measurements) {
            std::wstring wKey(key.begin(), key.end());
            if (wKey.length() > 15) {
                wKey = wKey.substr(0, 12) + L"...";
            }
            wchar_t valBuf[32];
            swprintf_s(valBuf, L"%.4f", val);
            gfx.DrawString(wKey.c_str(), -1, &labelFont, Gdiplus::PointF(x, y), &dimBrush);
            gfx.DrawString(valBuf, -1, &valueFont, Gdiplus::PointF(x + 100.0f, y), &valueBrush);
            y += lineHeight;

            // Limit display
            if (y > panelRect.Y + panelRect.Height - 20.0f) break;
        }
    }
}

void VisionResultOverlay::DrawGeometricOverlays(HDC hDC, int frameWidth, int frameHeight,
                                                 int destX, int destY, int destWidth, int destHeight) {
    Gdiplus::Graphics gfx(hDC);
    gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    float scaleX = static_cast<float>(destWidth) / frameWidth;
    float scaleY = static_cast<float>(destHeight) / frameHeight;

    // Draw circles (green outline + center crosshair)
    Gdiplus::Pen circlePen(Gdiplus::Color(255, 0, 255, 0), 2.0f);
    for (const auto& c : m_lastResult.circles) {
        Gdiplus::PointF center = ImageToScreen(c.centerPx.x, c.centerPx.y,
                                                frameWidth, frameHeight,
                                                destX, destY, destWidth, destHeight);
        float radius = static_cast<float>(c.radiusPx * scaleX);

        // Draw circle
        gfx.DrawEllipse(&circlePen,
                        center.X - radius, center.Y - radius,
                        radius * 2.0f, radius * 2.0f);

        // Draw crosshair at center
        float crossSize = 8.0f;
        gfx.DrawLine(&circlePen, center.X - crossSize, center.Y, center.X + crossSize, center.Y);
        gfx.DrawLine(&circlePen, center.X, center.Y - crossSize, center.X, center.Y + crossSize);
    }

    // Draw points (yellow filled circles)
    Gdiplus::SolidBrush pointBrush(Gdiplus::Color(255, 255, 255, 0));
    Gdiplus::Pen pointPen(Gdiplus::Color(255, 255, 200, 0), 1.0f);
    for (const auto& pt : m_lastResult.points) {
        Gdiplus::PointF screenPt = ImageToScreen(pt.pixelCoord.x, pt.pixelCoord.y,
                                                  frameWidth, frameHeight,
                                                  destX, destY, destWidth, destHeight);
        float radius = 5.0f;
        gfx.FillEllipse(&pointBrush,
                        screenPt.X - radius, screenPt.Y - radius,
                        radius * 2.0f, radius * 2.0f);
        gfx.DrawEllipse(&pointPen,
                        screenPt.X - radius, screenPt.Y - radius,
                        radius * 2.0f, radius * 2.0f);
    }

    // Draw lines (cyan)
    Gdiplus::Pen linePen(Gdiplus::Color(255, 0, 255, 255), 2.0f);
    for (const auto& line : m_lastResult.lines) {
        Gdiplus::PointF start = ImageToScreen(line.startPx.x, line.startPx.y,
                                               frameWidth, frameHeight,
                                               destX, destY, destWidth, destHeight);
        Gdiplus::PointF end = ImageToScreen(line.endPx.x, line.endPx.y,
                                             frameWidth, frameHeight,
                                             destX, destY, destWidth, destHeight);
        gfx.DrawLine(&linePen, start, end);

        // Draw endpoints
        float epRadius = 4.0f;
        gfx.DrawEllipse(&linePen, start.X - epRadius, start.Y - epRadius, epRadius * 2.0f, epRadius * 2.0f);
        gfx.DrawEllipse(&linePen, end.X - epRadius, end.Y - epRadius, epRadius * 2.0f, epRadius * 2.0f);
    }

    // Draw beams (magenta ellipse)
    Gdiplus::Pen beamPen(Gdiplus::Color(255, 255, 0, 255), 2.0f);
    for (const auto& beam : m_lastResult.beams) {
        Gdiplus::PointF center = ImageToScreen(beam.centerPx.x, beam.centerPx.y,
                                                frameWidth, frameHeight,
                                                destX, destY, destWidth, destHeight);
        float majorAxis = static_cast<float>(beam.fwhmMajorPx * scaleX);
        float minorAxis = static_cast<float>(beam.fwhmMinorPx * scaleY);

        // Draw ellipse (approximate - no rotation in GDI+)
        gfx.DrawEllipse(&beamPen,
                        center.X - majorAxis / 2.0f, center.Y - minorAxis / 2.0f,
                        majorAxis, minorAxis);

        // Draw center crosshair
        float crossSize = 10.0f;
        gfx.DrawLine(&beamPen, center.X - crossSize, center.Y, center.X + crossSize, center.Y);
        gfx.DrawLine(&beamPen, center.X, center.Y - crossSize, center.X, center.Y + crossSize);
    }
}

#endif // HAS_OPENCV

// ============================================================================
// BeamStreamOverlay Implementation
// ============================================================================

void BeamStreamOverlay::SetResult(const nlohmann::json& result) {
    std::lock_guard<std::mutex> lock(m_resultMutex);
    m_lastResult = result;
    ++m_resultSequence;
}

void BeamStreamOverlay::Clear() {
    std::lock_guard<std::mutex> lock(m_resultMutex);
    m_lastResult = nlohmann::json();
    m_fps = 0.0;
}

Gdiplus::PointF BeamStreamOverlay::ImageToScreen(double imgX, double imgY,
                                                  int frameWidth, int frameHeight,
                                                  int destX, int destY,
                                                  int destWidth, int destHeight) {
    float scaleX = static_cast<float>(destWidth) / frameWidth;
    float scaleY = static_cast<float>(destHeight) / frameHeight;
    return Gdiplus::PointF(
        destX + static_cast<float>(imgX * scaleX),
        destY + static_cast<float>(imgY * scaleY)
    );
}

void BeamStreamOverlay::Draw(HDC hDC, const RECT& windowRect, int frameWidth, int frameHeight,
                              int destX, int destY, int destWidth, int destHeight,
                              double umPerPxX, double umPerPxY,
                              int displayRotation, int origFrameWidth, int origFrameHeight) {
    if (!m_enabled) return;

    std::lock_guard<std::mutex> lock(m_resultMutex);

    Gdiplus::Graphics gfx(hDC);
    gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    gfx.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    // Info panel on TOP-RIGHT
    int panelWidth = OVERLAY_WIDTH;
    int panelX = (windowRect.right - windowRect.left) - panelWidth - OVERLAY_MARGIN;
    Gdiplus::RectF panelRect(
        static_cast<Gdiplus::REAL>(panelX),
        static_cast<Gdiplus::REAL>(OVERLAY_MARGIN),
        static_cast<Gdiplus::REAL>(panelWidth),
        static_cast<Gdiplus::REAL>(200));

    // Semi-transparent dark background
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(200, 20, 20, 30));
    gfx.FillRectangle(&bgBrush, panelRect);
    Gdiplus::Pen borderPen(Gdiplus::Color(200, 80, 80, 80), 1.0f);
    gfx.DrawRectangle(&borderPen, panelRect);

    // Fonts
    Gdiplus::Font titleFont(L"Segoe UI", 11.0f, Gdiplus::FontStyleBold);
    Gdiplus::Font labelFont(L"Segoe UI", 9.0f, Gdiplus::FontStyleRegular);
    Gdiplus::Font valueFont(L"Segoe UI", 9.0f, Gdiplus::FontStyleBold);

    Gdiplus::SolidBrush titleBrush(Gdiplus::Color(255, 255, 255, 255));
    Gdiplus::SolidBrush dimBrush(Gdiplus::Color(255, 180, 180, 180));
    Gdiplus::SolidBrush valueBrush(Gdiplus::Color(255, 100, 255, 100));

    float y = panelRect.Y + 8.0f;
    float x = panelRect.X + 10.0f;
    float lineHeight = 18.0f;

    // Title
    gfx.DrawString(L"Beam Stream", -1, &titleFont, Gdiplus::PointF(x, y), &titleBrush);
    y += lineHeight + 4.0f;

    // FPS
    wchar_t fpsBuf[64];
    swprintf_s(fpsBuf, L"Streaming @ %.1f Hz", m_fps);
    gfx.DrawString(fpsBuf, -1, &labelFont, Gdiplus::PointF(x, y), &dimBrush);
    y += lineHeight;

    // Process time
    if (m_lastResult.contains("process_ms")) {
        wchar_t timeBuf[64];
        swprintf_s(timeBuf, L"Process: %.1f ms", m_lastResult["process_ms"].get<double>());
        gfx.DrawString(timeBuf, -1, &labelFont, Gdiplus::PointF(x, y), &dimBrush);
        y += lineHeight + 4.0f;
    }

    // Per-beam info
    if (m_lastResult.contains("beams") && m_lastResult["beams"].is_array()) {
        for (const auto& beam : m_lastResult["beams"]) {
            std::string side = beam.value("side", "?");
            double cx = beam.value("centroid_x_px", 0.0);
            double cy = beam.value("centroid_y_px", 0.0);

            // Try um fields first, fall back to px fields converted to um
            double d4x = beam.value("d4sigma_x_um", 0.0);
            double d4y = beam.value("d4sigma_y_um", 0.0);
            if (d4x == 0.0 && beam.contains("d4sigma_x_px")) {
                d4x = beam["d4sigma_x_px"].get<double>() * umPerPxX;
            }
            if (d4y == 0.0 && beam.contains("d4sigma_y_px")) {
                d4y = beam["d4sigma_y_px"].get<double>() * umPerPxY;
            }

            wchar_t line1[128];
            swprintf_s(line1, L"%hs: cx=%.1f cy=%.1f px", side.c_str(), cx, cy);
            gfx.DrawString(line1, -1, &valueFont, Gdiplus::PointF(x, y), &valueBrush);
            y += lineHeight;

            wchar_t line2[128];
            swprintf_s(line2, L"    d4sx=%.1f d4sy=%.1f um", d4x, d4y);
            gfx.DrawString(line2, -1, &labelFont, Gdiplus::PointF(x, y), &dimBrush);
            y += lineHeight + 2.0f;
        }
    }

    // Geometric overlay on image
    if (frameWidth > 0 && frameHeight > 0 && destWidth > 0 && destHeight > 0 &&
        m_lastResult.contains("beams") && m_lastResult["beams"].is_array()) {

        float scaleX = static_cast<float>(destWidth) / frameWidth;
        float scaleY = static_cast<float>(destHeight) / frameHeight;

        Gdiplus::Pen beamPen(Gdiplus::Color(255, 0, 255, 0), 2.0f);

        for (const auto& beam : m_lastResult["beams"]) {
            double cx = beam.value("centroid_x_px", 0.0);
            double cy = beam.value("centroid_y_px", 0.0);

            // Get d4sigma in pixels (prefer px fields, convert um if needed)
            double d4x_px = beam.value("d4sigma_x_px", 0.0);
            double d4y_px = beam.value("d4sigma_y_px", 0.0);
            if (d4x_px == 0.0 && umPerPxX > 0) {
                d4x_px = beam.value("d4sigma_x_um", 0.0) / umPerPxX;
            }
            if (d4y_px == 0.0 && umPerPxY > 0) {
                d4y_px = beam.value("d4sigma_y_um", 0.0) / umPerPxY;
            }

            // Transform beam coordinates from original image space to rotated display space
            if (displayRotation != 0 && origFrameWidth > 0) {
                if (displayRotation == -90) {
                    // -90° CCW: (x, y) -> (y, W_orig - 1 - x)
                    double newCx = cy;
                    double newCy = static_cast<double>(origFrameWidth - 1) - cx;
                    cx = newCx;
                    cy = newCy;
                }
                else if (displayRotation == 90) {
                    // +90° CW: (x, y) -> (H_orig - 1 - y, x)
                    double newCx = static_cast<double>(origFrameHeight - 1) - cy;
                    double newCy = cx;
                    cx = newCx;
                    cy = newCy;
                }
                // Swap d4sigma axes (rotation swaps X and Y)
                std::swap(d4x_px, d4y_px);
            }

            Gdiplus::PointF center = ImageToScreen(cx, cy,
                frameWidth, frameHeight, destX, destY, destWidth, destHeight);

            // Crosshair at centroid
            float crossSize = 12.0f;
            gfx.DrawLine(&beamPen, center.X - crossSize, center.Y, center.X + crossSize, center.Y);
            gfx.DrawLine(&beamPen, center.X, center.Y - crossSize, center.X, center.Y + crossSize);

            // Ellipse sized by d4sigma in pixels, scaled to screen
            {
                float ellipseW = static_cast<float>(d4x_px * scaleX);
                float ellipseH = static_cast<float>(d4y_px * scaleY);
                if (ellipseW > 2.0f && ellipseH > 2.0f) {
                    gfx.DrawEllipse(&beamPen,
                        center.X - ellipseW / 2.0f, center.Y - ellipseH / 2.0f,
                        ellipseW, ellipseH);
                }
            }
        }
    }
}

// ============================================================================
// GdiCameraWindow Implementation
// ============================================================================

GdiCameraWindow::GdiCameraWindow() {
    m_bitmapInfoBuffer.resize(sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD));
    m_bitmapInfo = reinterpret_cast<BITMAPINFO*>(m_bitmapInfoBuffer.data());
    InitializeGdiPlus();
    LoadCalibrationFactors();
}

GdiCameraWindow::~GdiCameraWindow() {
    StopBeamStreaming();
#ifdef HAS_OPENCV
    StopVisionProcessing();
#endif
    Destroy();
}

bool GdiCameraWindow::InitializeGdiPlus() {
    if (s_gdiplusInitialized) return true;

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    if (Gdiplus::GdiplusStartup(&s_gdiplusToken, &gdiplusStartupInput, nullptr) == Gdiplus::Ok) {
        s_gdiplusInitialized = true;
        return true;
    }
    return false;
}

void GdiCameraWindow::ShutdownGdiPlus() {
    if (s_gdiplusInitialized) {
        Gdiplus::GdiplusShutdown(s_gdiplusToken);
        s_gdiplusInitialized = false;
    }
}

bool GdiCameraWindow::RegisterWindowClass() {
    if (s_classRegistered) return true;

    std::cout << "[GdiCameraWindow] Registering window class..." << std::endl;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = s_className;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);

    if (RegisterClassExW(&wc)) {
        s_classRegistered = true;
        std::cout << "[GdiCameraWindow] Window class registered successfully" << std::endl;
        return true;
    }
    DWORD error = GetLastError();
    std::cout << "[GdiCameraWindow] ERROR: Failed to register window class, error code: " << error << std::endl;
    return false;
}

bool GdiCameraWindow::Create(const std::string& title, int x, int y, int width, int height) {
    if (m_hwnd) return true;

    std::cout << "[GdiCameraWindow] Creating window: " << title << " at (" << x << ", " << y << ") size " << width << "x" << height << std::endl;

    if (!RegisterWindowClass()) {
        std::cout << "[GdiCameraWindow] ERROR: Window class registration failed" << std::endl;
        return false;
    }

    int titleLen = MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, nullptr, 0);
    std::vector<wchar_t> wideTitle(titleLen);
    MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, wideTitle.data(), titleLen);

    int actualX = (x == CW_USEDEFAULT) ? 100 : x;
    int actualY = (y == CW_USEDEFAULT) ? 100 : y;

    m_hwnd = CreateWindowExW(
        WS_EX_OVERLAPPEDWINDOW,
        s_className,
        wideTitle.data(),
        WS_OVERLAPPEDWINDOW,
        actualX, actualY, width, height,
        nullptr,
        nullptr,
        GetModuleHandle(nullptr),
        this
    );

    if (!m_hwnd) {
        DWORD error = GetLastError();
        std::cout << "[GdiCameraWindow] ERROR: CreateWindowExW failed, error code: " << error << std::endl;
        return false;
    }

    // Create menu bar
    CreateMenuBar();

    std::cout << "[GdiCameraWindow] Window created successfully, HWND: " << m_hwnd << std::endl;
    return true;
}

void GdiCameraWindow::CreateMenuBar() {
    m_menuBar = CreateMenu();
    m_showMenu = CreatePopupMenu();
    m_visionMenu = CreatePopupMenu();
    m_toolsMenu = CreatePopupMenu();
    m_cameraMenu = CreatePopupMenu();

    if (m_menuBar && m_showMenu) {
        // Camera menu (first, most important)
        if (m_cameraMenu) {
            AppendMenuW(m_cameraMenu, MF_STRING, IDM_CAMERA_DUAL_MODE, L"Dual Camera Mode (Split View)");
            AppendMenuW(m_cameraMenu, MF_SEPARATOR, 0, nullptr);
            // Camera list will be populated dynamically
            AppendMenuW(m_menuBar, MF_POPUP, (UINT_PTR)m_cameraMenu, L"Camera");
        }

        // Add "Enable Chart" item at top
        AppendMenuW(m_showMenu, MF_STRING, IDM_SHOW_CHART_ENABLED, L"Enable Chart Overlay");
        AppendMenuW(m_showMenu, MF_SEPARATOR, 0, nullptr);

        // "Data Channels" will be populated dynamically
        AppendMenuW(m_menuBar, MF_POPUP, (UINT_PTR)m_showMenu, L"Show");

#ifdef HAS_OPENCV
        // Vision menu
        if (m_visionMenu) {
            AppendMenuW(m_visionMenu, MF_STRING, IDM_VISION_ENABLE, L"Enable Continuous Vision");
            AppendMenuW(m_visionMenu, MF_STRING, IDM_VISION_REFRESH, L"Refresh Profile List");
            AppendMenuW(m_visionMenu, MF_SEPARATOR, 0, nullptr);
            // Profiles will be populated dynamically
            AppendMenuW(m_menuBar, MF_POPUP, (UINT_PTR)m_visionMenu, L"Vision");

            // Discover available profiles
            DiscoverVisionProfiles();
        }
#endif

        // Tools menu
        if (m_toolsMenu) {
            AppendMenuW(m_toolsMenu, MF_STRING, IDM_TOOLS_CROSSHAIR, L"Show Crosshair");
            AppendMenuW(m_toolsMenu, MF_STRING, IDM_TOOLS_CLICK_TO_MOVE, L"Enable Click-to-Move");
            AppendMenuW(m_toolsMenu, MF_STRING, IDM_TOOLS_SHOW_DISTANCE, L"Show Real World Distance");
            AppendMenuW(m_toolsMenu, MF_STRING, IDM_TOOLS_MAGNIFICATION, L"Magnification...");
            AppendMenuW(m_toolsMenu, MF_STRING, IDM_TOOLS_DISPLAY_ROTATION, L"Display Rotation (-90\u00B0)");
            AppendMenuW(m_toolsMenu, MF_STRING, IDM_TOOLS_DISPLAY_ROTATION_CW, L"Display Rotation (+90\u00B0)");
            AppendMenuW(m_toolsMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(m_toolsMenu, MF_STRING, IDM_OVERLAY_SIZE_INCREASE, L"Increase Overlay Size (+)");
            AppendMenuW(m_toolsMenu, MF_STRING, IDM_OVERLAY_SIZE_DECREASE, L"Decrease Overlay Size (-)");
            AppendMenuW(m_menuBar, MF_POPUP, (UINT_PTR)m_toolsMenu, L"Tools");
        }

        // Beam Stream menu
        m_beamStreamMenu = CreatePopupMenu();
        if (m_beamStreamMenu) {
            AppendMenuA(m_beamStreamMenu, MF_STRING, IDM_BEAM_STREAM_ENABLE, "Enable Beam Stream");
            AppendMenuA(m_beamStreamMenu, MF_SEPARATOR, 0, nullptr);
            // Camera selection items populated dynamically in UpdateBeamStreamMenu()
            AppendMenuW(m_menuBar, MF_POPUP, (UINT_PTR)m_beamStreamMenu, L"Beam Stream");
        }

        SetMenu(m_hwnd, m_menuBar);
    }
}

void GdiCameraWindow::UpdateShowMenu() {
    if (!m_showMenu) return;

    // Remove old channel items (keep first 2: "Enable Chart" and separator)
    int count = GetMenuItemCount(m_showMenu);
    while (count > 2) {
        DeleteMenu(m_showMenu, count - 1, MF_BYPOSITION);
        count--;
    }

    // Get available channels
    std::vector<std::string> channels;
    {
        std::lock_guard<std::mutex> lock(m_channelMutex);
        channels = m_availableChannels;
    }

    if (channels.empty()) {
        AppendMenuW(m_showMenu, MF_STRING | MF_GRAYED, 0, L"(No channels available)");
    } else {
        for (size_t i = 0; i < channels.size() && i < 50; ++i) {
            std::wstring wChannel(channels[i].begin(), channels[i].end());
            UINT flags = MF_STRING;
            if (m_chartOverlay.IsChannelEnabled(channels[i])) {
                flags |= MF_CHECKED;
            }
            AppendMenuW(m_showMenu, flags, IDM_CHANNEL_BASE + static_cast<UINT>(i), wChannel.c_str());
        }
    }

    // Update chart enabled state
    CheckMenuItem(m_showMenu, IDM_SHOW_CHART_ENABLED,
        m_chartOverlay.IsEnabled() ? MF_CHECKED : MF_UNCHECKED);

    DrawMenuBar(m_hwnd);
}

void GdiCameraWindow::UpdateToolsMenu() {
    if (!m_toolsMenu) return;

    CheckMenuItem(m_toolsMenu, IDM_TOOLS_CROSSHAIR,
                  m_crosshairEnabled ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m_toolsMenu, IDM_TOOLS_CLICK_TO_MOVE,
                  m_clickToMoveEnabled ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m_toolsMenu, IDM_TOOLS_SHOW_DISTANCE,
                  m_showDistanceEnabled ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m_toolsMenu, IDM_TOOLS_DISPLAY_ROTATION,
                  (m_displayRotation == -90) ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m_toolsMenu, IDM_TOOLS_DISPLAY_ROTATION_CW,
                  (m_displayRotation == 90) ? MF_CHECKED : MF_UNCHECKED);

    // Update magnification menu text to show current value
    std::wstring magText = L"Magnification... (" + std::to_wstring(static_cast<int>(m_magnification)) + L"x)";
    ModifyMenuW(m_toolsMenu, IDM_TOOLS_MAGNIFICATION, MF_BYCOMMAND | MF_STRING,
                IDM_TOOLS_MAGNIFICATION, magText.c_str());

    DrawMenuBar(m_hwnd);
}

void GdiCameraWindow::UpdateCameraMenu() {
    if (!m_cameraMenu) return;

    // Remove old camera items (keep first 2: "Dual Camera Mode" and separator)
    int count = GetMenuItemCount(m_cameraMenu);
    while (count > 2) {
        DeleteMenu(m_cameraMenu, count - 1, MF_BYPOSITION);
        count--;
    }

    // Update dual mode checkbox
    CheckMenuItem(m_cameraMenu, IDM_CAMERA_DUAL_MODE,
                  m_dualCameraMode ? MF_CHECKED : MF_UNCHECKED);

    // Get available cameras
    std::vector<std::string> cameras;
    {
        std::lock_guard<std::mutex> lock(m_cameraListMutex);
        cameras = m_availableCameraIds;
    }

    // Get current selections
    std::string primaryCamera;
    std::string secondaryCamera;
    {
        std::lock_guard<std::mutex> lock(m_cameraMutex);
        primaryCamera = m_targetCameraId;
        secondaryCamera = m_secondaryCameraId;
    }

    if (cameras.empty()) {
        AppendMenuW(m_cameraMenu, MF_STRING | MF_GRAYED, 0, L"(No cameras available)");
    } else {
        // Primary camera submenu
        HMENU primarySubmenu = CreatePopupMenu();
        if (primarySubmenu) {
            for (size_t i = 0; i < cameras.size() && i < 50; ++i) {
                std::wstring wCameraId(cameras[i].begin(), cameras[i].end());
                UINT flags = MF_STRING;
                if (cameras[i] == primaryCamera || (primaryCamera.empty() && i == 0)) {
                    flags |= MF_CHECKED;
                }
                AppendMenuW(primarySubmenu, flags,
                            IDM_CAMERA_PRIMARY_BASE + static_cast<UINT>(i),
                            wCameraId.c_str());
            }
            AppendMenuW(m_cameraMenu, MF_POPUP, (UINT_PTR)primarySubmenu,
                        m_dualCameraMode ? L"Left Camera (Primary)" : L"Select Camera");
        }

        // Secondary camera submenu (only in dual mode)
        if (m_dualCameraMode) {
            HMENU secondarySubmenu = CreatePopupMenu();
            if (secondarySubmenu) {
                // Add "None" option
                UINT noneFlags = MF_STRING;
                if (secondaryCamera.empty()) {
                    noneFlags |= MF_CHECKED;
                }
                AppendMenuW(secondarySubmenu, noneFlags, IDM_CAMERA_SECONDARY_BASE, L"(None)");
                AppendMenuW(secondarySubmenu, MF_SEPARATOR, 0, nullptr);

                for (size_t i = 0; i < cameras.size() && i < 50; ++i) {
                    std::wstring wCameraId(cameras[i].begin(), cameras[i].end());
                    UINT flags = MF_STRING;
                    if (cameras[i] == secondaryCamera) {
                        flags |= MF_CHECKED;
                    }
                    // Grey out if same as primary
                    if (cameras[i] == primaryCamera) {
                        flags |= MF_GRAYED;
                    }
                    AppendMenuW(secondarySubmenu, flags,
                                IDM_CAMERA_SECONDARY_BASE + 1 + static_cast<UINT>(i),
                                wCameraId.c_str());
                }
                AppendMenuW(m_cameraMenu, MF_POPUP, (UINT_PTR)secondarySubmenu, L"Right Camera (Secondary)");
            }
        }
    }

    DrawMenuBar(m_hwnd);
}

void GdiCameraWindow::UpdateBeamStreamMenu() {
    if (!m_beamStreamMenu) return;

    // Remove old camera items (keep first 2: "Enable Beam Stream" and separator)
    int count = GetMenuItemCount(m_beamStreamMenu);
    while (count > 2) {
        DeleteMenu(m_beamStreamMenu, count - 1, MF_BYPOSITION);
        count--;
    }

    // Update enable checkbox
    CheckMenuItem(m_beamStreamMenu, IDM_BEAM_STREAM_ENABLE,
                  m_beamStreamEnabled ? MF_CHECKED : MF_UNCHECKED);

    // Get available cameras
    std::vector<std::string> cameras;
    {
        std::lock_guard<std::mutex> lock(m_cameraListMutex);
        cameras = m_availableCameraIds;
    }

    // Get current beam stream camera
    std::string streamCamera;
    {
        std::lock_guard<std::mutex> lock(m_beamStreamCameraMutex);
        streamCamera = m_beamStreamCameraId;
    }

    if (cameras.empty()) {
        AppendMenuW(m_beamStreamMenu, MF_STRING | MF_GRAYED, 0, L"(No cameras available)");
    } else {
        for (size_t i = 0; i < cameras.size() && i < 50; ++i) {
            std::wstring wCameraId(cameras[i].begin(), cameras[i].end());
            UINT flags = MF_STRING;
            if (cameras[i] == streamCamera) {
                flags |= MF_CHECKED;
            }
            AppendMenuW(m_beamStreamMenu, flags,
                        IDM_BEAM_STREAM_CAMERA_BASE + static_cast<UINT>(i),
                        wCameraId.c_str());
        }
    }

    DrawMenuBar(m_hwnd);
}

#ifdef HAS_OPENCV
void GdiCameraWindow::UpdateVisionMenu() {
    if (!m_visionMenu) return;

    // Remove old profile items (keep first 3: "Enable Continuous Vision", "Refresh Profile List", and separator)
    int count = GetMenuItemCount(m_visionMenu);
    while (count > 3) {
        DeleteMenu(m_visionMenu, count - 1, MF_BYPOSITION);
        count--;
    }

    // Update enable checkbox
    CheckMenuItem(m_visionMenu, IDM_VISION_ENABLE,
                  m_visionEnabled ? MF_CHECKED : MF_UNCHECKED);

    // Get current active profile
    std::string activeProfile;
    {
        std::lock_guard<std::mutex> lock(m_visionProfileMutex);
        activeProfile = m_activeVisionProfile;
    }

    // Add profile items
    if (m_availableProfiles.empty()) {
        AppendMenuW(m_visionMenu, MF_STRING | MF_GRAYED, 0, L"(No profiles found)");
    } else {
        for (size_t i = 0; i < m_availableProfiles.size() && i < 100; ++i) {
            std::wstring wName(m_availableProfiles[i].displayName.begin(),
                               m_availableProfiles[i].displayName.end());
            UINT flags = MF_STRING;
            if (m_availableProfiles[i].fullPath == activeProfile) {
                flags |= MF_CHECKED;
            }
            AppendMenuW(m_visionMenu, flags, IDM_VISION_PROFILE_BASE + static_cast<UINT>(i), wName.c_str());
        }
    }

    DrawMenuBar(m_hwnd);
}

void GdiCameraWindow::DiscoverVisionProfiles() {
    m_availableProfiles.clear();

    // Look for vision profiles in config/vision directory
    // Try multiple possible paths
    std::vector<std::string> searchPaths = {
        "config/vision",
        "./config/vision",
        "../config/vision"
    };

    // Also try machine-specific paths
    WIN32_FIND_DATAW findData;
    HANDLE hFind;

    for (const auto& basePath : searchPaths) {
        std::wstring searchPath(basePath.begin(), basePath.end());
        searchPath += L"/*.json";

        hFind = FindFirstFileW(searchPath.c_str(), &findData);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                std::wstring fileName = findData.cFileName;
                // Skip recipe files
                if (fileName.find(L".recipe.json") != std::wstring::npos) {
                    continue;
                }

                // Convert to narrow string
                std::string narrowFileName(fileName.begin(), fileName.end());

                VisionProfileInfo info;
                info.fullPath = basePath + "/" + narrowFileName;
                // Display name is filename without .json extension
                info.displayName = narrowFileName;
                if (info.displayName.length() > 5 &&
                    info.displayName.substr(info.displayName.length() - 5) == ".json") {
                    info.displayName = info.displayName.substr(0, info.displayName.length() - 5);
                }

                m_availableProfiles.push_back(info);
            } while (FindNextFileW(hFind, &findData));

            FindClose(hFind);

            // If we found profiles in this path, stop searching
            if (!m_availableProfiles.empty()) {
                std::cout << "[GdiCameraWindow] Found " << m_availableProfiles.size()
                          << " vision profiles in " << basePath << std::endl;
                break;
            }
        }
    }

    // Also scan machine-specific folders (config/MACHINE/vision)
    for (const auto& basePath : {"config", "./config", "../config"}) {
        std::wstring configPath(basePath, basePath + strlen(basePath));
        configPath += L"/*";

        WIN32_FIND_DATAW dirFindData;
        hFind = FindFirstFileW(configPath.c_str(), &dirFindData);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (dirFindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    std::wstring dirName = dirFindData.cFileName;
                    if (dirName != L"." && dirName != L"..") {
                        // Check if this directory has a vision subfolder
                        std::wstring visionPath = configPath.substr(0, configPath.length() - 1);
                        visionPath += dirName + L"/vision/*.json";

                        WIN32_FIND_DATAW fileFindData;
                        HANDLE hFind2 = FindFirstFileW(visionPath.c_str(), &fileFindData);
                        if (hFind2 != INVALID_HANDLE_VALUE) {
                            do {
                                std::wstring fileName = fileFindData.cFileName;
                                if (fileName.find(L".recipe.json") != std::wstring::npos) {
                                    continue;
                                }

                                std::string narrowFileName(fileName.begin(), fileName.end());
                                std::string narrowDirName(dirName.begin(), dirName.end());

                                VisionProfileInfo info;
                                info.fullPath = std::string(basePath) + "/" + narrowDirName + "/vision/" + narrowFileName;
                                info.displayName = narrowFileName;
                                if (info.displayName.length() > 5 &&
                                    info.displayName.substr(info.displayName.length() - 5) == ".json") {
                                    info.displayName = info.displayName.substr(0, info.displayName.length() - 5);
                                }

                                // Avoid duplicates
                                bool exists = false;
                                for (const auto& existing : m_availableProfiles) {
                                    if (existing.displayName == info.displayName) {
                                        exists = true;
                                        break;
                                    }
                                }
                                if (!exists) {
                                    m_availableProfiles.push_back(info);
                                }
                            } while (FindNextFileW(hFind2, &fileFindData));
                            FindClose(hFind2);
                        }
                    }
                }
            } while (FindNextFileW(hFind, &dirFindData));
            FindClose(hFind);
        }
    }

    // Sort profiles alphabetically by display name
    std::sort(m_availableProfiles.begin(), m_availableProfiles.end(),
              [](const VisionProfileInfo& a, const VisionProfileInfo& b) {
                  return a.displayName < b.displayName;
              });

    if (!m_availableProfiles.empty()) {
        std::cout << "[GdiCameraWindow] Total vision profiles discovered: "
                  << m_availableProfiles.size() << std::endl;
    }
}
#endif

void GdiCameraWindow::OnMenuCommand(WORD commandId) {
    if (commandId == IDM_SHOW_CHART_ENABLED) {
        m_chartOverlay.SetEnabled(!m_chartOverlay.IsEnabled());
        UpdateShowMenu();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    else if (commandId >= IDM_CHANNEL_BASE && commandId < IDM_CHANNEL_BASE + 100) {
        size_t index = commandId - IDM_CHANNEL_BASE;
        std::vector<std::string> channels;
        {
            std::lock_guard<std::mutex> lock(m_channelMutex);
            channels = m_availableChannels;
        }

        if (index < channels.size()) {
            const std::string& channelId = channels[index];
            bool isEnabled = m_chartOverlay.IsChannelEnabled(channelId);
            m_chartOverlay.SetChannelEnabled(channelId, !isEnabled);
            UpdateShowMenu();
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }
#ifdef HAS_OPENCV
    else if (commandId == IDM_VISION_REFRESH) {
        std::cout << "[GdiCameraWindow] Refreshing vision profile list..." << std::endl;
        DiscoverVisionProfiles();
        UpdateVisionMenu();
        std::cout << "[GdiCameraWindow] Found " << m_availableProfiles.size() << " profiles after refresh" << std::endl;
    }
    else if (commandId == IDM_VISION_ENABLE) {
        if (m_visionEnabled) {
            // Disable vision processing
            StopVisionProcessing();
            m_visionEnabled = false;
            m_visionOverlay.SetEnabled(false);
            m_visionOverlay.Clear();
        } else {
            // Check if a profile is selected
            std::lock_guard<std::mutex> lock(m_visionProfileMutex);
            if (!m_activeVisionProfile.empty()) {
                m_visionEnabled = true;
                m_visionOverlay.SetEnabled(true);
                StartVisionProcessing();
            } else {
                // No profile selected - show message or auto-select first
                if (!m_availableProfiles.empty()) {
                    m_activeVisionProfile = m_availableProfiles[0].fullPath;
                    m_visionEnabled = true;
                    m_visionOverlay.SetEnabled(true);
                    StartVisionProcessing();
                }
            }
        }
        UpdateVisionMenu();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    else if (commandId >= IDM_VISION_PROFILE_BASE && commandId < IDM_VISION_PROFILE_BASE + 100) {
        size_t index = commandId - IDM_VISION_PROFILE_BASE;
        if (index < m_availableProfiles.size()) {
            {
                std::lock_guard<std::mutex> lock(m_visionProfileMutex);
                m_activeVisionProfile = m_availableProfiles[index].fullPath;
            }
            std::cout << "[GdiCameraWindow] Selected vision profile: "
                      << m_availableProfiles[index].displayName << std::endl;

            // If vision is enabled, the new profile will be used on next frame
            UpdateVisionMenu();
        }
    }
#endif
    // Tools menu commands
    else if (commandId == IDM_TOOLS_CROSSHAIR) {
        m_crosshairEnabled = !m_crosshairEnabled;
        std::cout << "[GdiCameraWindow] Crosshair " << (m_crosshairEnabled ? "enabled" : "disabled") << std::endl;
        UpdateToolsMenu();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    else if (commandId == IDM_TOOLS_CLICK_TO_MOVE) {
        m_clickToMoveEnabled = !m_clickToMoveEnabled;
        std::cout << "[GdiCameraWindow] Click-to-move " << (m_clickToMoveEnabled ? "enabled" : "disabled") << std::endl;
        UpdateToolsMenu();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    else if (commandId == IDM_TOOLS_SHOW_DISTANCE) {
        m_showDistanceEnabled = !m_showDistanceEnabled;
        std::cout << "[GdiCameraWindow] Show distance " << (m_showDistanceEnabled ? "enabled" : "disabled") << std::endl;
        UpdateToolsMenu();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    else if (commandId == IDM_TOOLS_MAGNIFICATION) {
        // Show magnification selection dialog
        struct MagOption { const wchar_t* label; double value; };
        static const MagOption options[] = {
            {L"1x",   1.0},
            {L"10x",  10.0},
            {L"20x",  20.0},
            {L"50x",  50.0},
            {L"100x", 100.0},
            {L"200x", 200.0}
        };
        static const int numOptions = sizeof(options) / sizeof(options[0]);

        // Build a popup menu as a simple magnification picker
        HMENU magPopup = CreatePopupMenu();
        if (magPopup) {
            for (int i = 0; i < numOptions; ++i) {
                UINT flags = MF_STRING;
                // Use a tolerance check for floating point comparison
                if (std::abs(m_magnification - options[i].value) < 0.01) {
                    flags |= MF_CHECKED;
                }
                AppendMenuW(magPopup, flags, 50000 + i, options[i].label);
            }

            // Show popup at cursor position
            POINT pt;
            GetCursorPos(&pt);
            int selected = TrackPopupMenu(magPopup, TPM_RETURNCMD | TPM_NONOTIFY,
                                          pt.x, pt.y, 0, m_hwnd, nullptr);
            DestroyMenu(magPopup);

            if (selected >= 50000 && selected < 50000 + numOptions) {
                m_magnification = options[selected - 50000].value;
                std::cout << "[GdiCameraWindow] Magnification set to " << m_magnification << "x" << std::endl;
                UpdateToolsMenu();
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
        }
    }
    else if (commandId == IDM_TOOLS_DISPLAY_ROTATION) {
        m_displayRotation = (m_displayRotation == -90) ? 0 : -90;
        std::cout << "[GdiCameraWindow] Display rotation: " << m_displayRotation << " degrees" << std::endl;
        if (m_onDisplayRotationChanged) {
            m_onDisplayRotationChanged(m_displayRotation);
        }
        UpdateToolsMenu();
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }
    else if (commandId == IDM_TOOLS_DISPLAY_ROTATION_CW) {
        m_displayRotation = (m_displayRotation == 90) ? 0 : 90;
        std::cout << "[GdiCameraWindow] Display rotation: " << m_displayRotation << " degrees" << std::endl;
        if (m_onDisplayRotationChanged) {
            m_onDisplayRotationChanged(m_displayRotation);
        }
        UpdateToolsMenu();
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }
    // Overlay size commands
    else if (commandId == IDM_OVERLAY_SIZE_INCREASE) {
        m_chartOverlay.IncreaseScale();
        std::cout << "[GdiCameraWindow] Overlay scale increased to " << m_chartOverlay.GetScale() << std::endl;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        // Emit signal for settings save (manager will handle this)
        if (m_onOverlayScaleChanged) {
            m_onOverlayScaleChanged(m_chartOverlay.GetScale());
        }
    }
    else if (commandId == IDM_OVERLAY_SIZE_DECREASE) {
        m_chartOverlay.DecreaseScale();
        std::cout << "[GdiCameraWindow] Overlay scale decreased to " << m_chartOverlay.GetScale() << std::endl;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        // Emit signal for settings save (manager will handle this)
        if (m_onOverlayScaleChanged) {
            m_onOverlayScaleChanged(m_chartOverlay.GetScale());
        }
    }
    // Camera menu commands
    else if (commandId == IDM_CAMERA_DUAL_MODE) {
        SetDualCameraMode(!m_dualCameraMode);
        UpdateCameraMenu();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    else if (commandId >= IDM_CAMERA_PRIMARY_BASE && commandId < IDM_CAMERA_PRIMARY_BASE + 50) {
        // Primary camera selection
        size_t index = commandId - IDM_CAMERA_PRIMARY_BASE;
        std::vector<std::string> cameras;
        {
            std::lock_guard<std::mutex> lock(m_cameraListMutex);
            cameras = m_availableCameraIds;
        }
        if (index < cameras.size()) {
            std::string newCameraId = cameras[index];
            {
                std::lock_guard<std::mutex> lock(m_cameraMutex);
                m_targetCameraId = newCameraId;
                // If secondary is same as new primary, clear secondary
                if (m_secondaryCameraId == newCameraId) {
                    m_secondaryCameraId.clear();
                }
            }
            std::cout << "[GdiCameraWindow] Primary camera set to: " << newCameraId << std::endl;
            // Clear primary frame buffer to show "Waiting for camera..."
            {
                std::lock_guard<std::mutex> lock(m_frameMutex);
                m_frameWidth = 0;
                m_frameHeight = 0;
            }
            UpdateCameraMenu();
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }
    else if (commandId >= IDM_CAMERA_SECONDARY_BASE && commandId < IDM_CAMERA_SECONDARY_BASE + 51) {
        // Secondary camera selection
        if (commandId == IDM_CAMERA_SECONDARY_BASE) {
            // "None" selected
            {
                std::lock_guard<std::mutex> lock(m_cameraMutex);
                m_secondaryCameraId.clear();
            }
            std::cout << "[GdiCameraWindow] Secondary camera cleared" << std::endl;
        } else {
            size_t index = commandId - IDM_CAMERA_SECONDARY_BASE - 1;
            std::vector<std::string> cameras;
            {
                std::lock_guard<std::mutex> lock(m_cameraListMutex);
                cameras = m_availableCameraIds;
            }
            if (index < cameras.size()) {
                std::string newCameraId = cameras[index];
                {
                    std::lock_guard<std::mutex> lock(m_cameraMutex);
                    // Don't set if same as primary
                    if (newCameraId != m_targetCameraId) {
                        m_secondaryCameraId = newCameraId;
                    }
                }
                std::cout << "[GdiCameraWindow] Secondary camera set to: " << newCameraId << std::endl;
            }
        }
        // Clear secondary frame buffer
        {
            std::lock_guard<std::mutex> lock(m_secondaryFrameMutex);
            m_secondaryFrameWidth = 0;
            m_secondaryFrameHeight = 0;
        }
        UpdateCameraMenu();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    // Beam Stream menu commands
    else if (commandId == IDM_BEAM_STREAM_ENABLE) {
        if (m_beamStreamEnabled) {
            StopBeamStreaming();
        } else {
            // Auto-select first camera if none selected
            {
                std::lock_guard<std::mutex> lock(m_beamStreamCameraMutex);
                if (m_beamStreamCameraId.empty()) {
                    std::lock_guard<std::mutex> camLock(m_cameraListMutex);
                    if (!m_availableCameraIds.empty()) {
                        m_beamStreamCameraId = m_availableCameraIds.front();
                    }
                }
            }
            StartBeamStreaming();
        }
        UpdateBeamStreamMenu();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    else if (commandId >= IDM_BEAM_STREAM_CAMERA_BASE && commandId < IDM_BEAM_STREAM_CAMERA_BASE + 50) {
        size_t index = commandId - IDM_BEAM_STREAM_CAMERA_BASE;
        std::vector<std::string> cameras;
        {
            std::lock_guard<std::mutex> lock(m_cameraListMutex);
            cameras = m_availableCameraIds;
        }
        if (index < cameras.size()) {
            {
                std::lock_guard<std::mutex> lock(m_beamStreamCameraMutex);
                m_beamStreamCameraId = cameras[index];
            }
            std::cout << "[GdiCameraWindow] Beam stream camera set to: " << cameras[index] << std::endl;
            UpdateBeamStreamMenu();
        }
    }
}

void GdiCameraWindow::Destroy() {
    StopBeamStreaming();
#ifdef HAS_OPENCV
    StopVisionProcessing();
#endif

    if (m_memDC) {
        if (m_oldBitmap) {
            SelectObject(m_memDC, m_oldBitmap);
            m_oldBitmap = nullptr;
        }
        if (m_memBitmap) {
            DeleteObject(m_memBitmap);
            m_memBitmap = nullptr;
        }
        DeleteDC(m_memDC);
        m_memDC = nullptr;
    }

    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

void GdiCameraWindow::Show() {
    if (m_hwnd) {
        std::cout << "[GdiCameraWindow] Showing window..." << std::endl;
        ShowWindow(m_hwnd, SW_SHOWNORMAL);
        UpdateWindow(m_hwnd);
        SetForegroundWindow(m_hwnd);
        std::cout << "[GdiCameraWindow] Window shown and brought to foreground" << std::endl;
    } else {
        std::cout << "[GdiCameraWindow] ERROR: Cannot show - HWND is null" << std::endl;
    }
}

void GdiCameraWindow::Hide() {
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_HIDE);
    }
}

bool GdiCameraWindow::IsVisible() const {
    return m_hwnd && IsWindowVisible(m_hwnd);
}

void GdiCameraWindow::SetTitle(const std::string& title) {
    if (m_hwnd) {
        int titleLen = MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, nullptr, 0);
        std::vector<wchar_t> wideTitle(titleLen);
        MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, wideTitle.data(), titleLen);
        SetWindowTextW(m_hwnd, wideTitle.data());
    }
}

void GdiCameraWindow::SetTargetCamera(const std::string& cameraId) {
    {
        std::lock_guard<std::mutex> lock(m_cameraMutex);
        m_targetCameraId = cameraId;
    }
    // Reload calibration for the new camera
    LoadCalibrationFactors();
}

std::string GdiCameraWindow::GetTargetCamera() const {
    std::lock_guard<std::mutex> lock(m_cameraMutex);
    return m_targetCameraId;
}

void GdiCameraWindow::SetDisplayRotation(int degrees) {
    m_displayRotation = degrees;
    std::cout << "[GdiCameraWindow] Display rotation: " << degrees << " degrees" << std::endl;
    if (m_hwnd) {
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }
}

void GdiCameraWindow::SetDataStore(DataStore* dataStore) {
    m_dataStore = dataStore;
    if (m_dataStore) {
        // Subscribe to all data changes
        m_dataStore->SubscribeToAll([this](const std::string& channelId, float value) {
            OnDataStoreValueChanged(channelId, value);
        });
        RefreshChannelMenu();
    }
}

void GdiCameraWindow::RefreshChannelMenu() {
    if (!m_dataStore) return;

    {
        std::lock_guard<std::mutex> lock(m_channelMutex);
        m_availableChannels = m_dataStore->GetAvailableChannels();
    }

    // Post message to update menu on main thread
    if (m_hwnd) {
        PostMessage(m_hwnd, WM_USER + 100, 0, 0);  // Custom message to refresh menu
    }
}

void GdiCameraWindow::OnDataStoreValueChanged(const std::string& channelId, float value) {
    // Add data point if channel is enabled
    if (m_chartOverlay.IsChannelEnabled(channelId)) {
        m_chartOverlay.AddDataPoint(channelId, static_cast<double>(value));

        // Throttled repaint to prevent UI freezing from high-frequency updates
        // Instead of calling InvalidateRect for every data point, we rate-limit repaints
        if (m_hwnd && m_chartOverlay.IsEnabled()) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - m_lastChartRepaint).count();

            // Only request repaint if enough time has passed since last one
            if (elapsed >= CHART_REPAINT_INTERVAL_MS) {
                m_lastChartRepaint = now;
                // Use PostMessage for thread-safe UI update (safer than InvalidateRect from callback)
                PostMessage(m_hwnd, WM_CHART_REPAINT_REQUEST, 0, 0);  // Throttled chart repaint
            }
        }
    }

    // Check if this is a new channel
    bool isNew = false;
    {
        std::lock_guard<std::mutex> lock(m_channelMutex);
        auto it = std::find(m_availableChannels.begin(), m_availableChannels.end(), channelId);
        if (it == m_availableChannels.end()) {
            m_availableChannels.push_back(channelId);
            isNew = true;
        }
    }

    // Refresh menu if new channel discovered
    if (isNew && m_hwnd) {
        PostMessage(m_hwnd, WM_USER + 100, 0, 0);
    }
}

bool GdiCameraWindow::WantsFramesFromCamera(const std::string& cameraId) const {
    // Also accept frames from beam stream camera
    if (m_beamStreamEnabled) {
        std::lock_guard<std::mutex> lock(m_beamStreamCameraMutex);
        if (!m_beamStreamCameraId.empty() && m_beamStreamCameraId == cameraId) return true;
    }

    std::lock_guard<std::mutex> lock(m_cameraMutex);
    // In dual mode, accept frames from both primary and secondary cameras
    if (m_dualCameraMode) {
        if (m_targetCameraId.empty()) return true;
        if (m_targetCameraId == cameraId) return true;
        if (!m_secondaryCameraId.empty() && m_secondaryCameraId == cameraId) return true;
        return false;
    }
    // Single mode: accept all if no target, or only target camera
    if (m_targetCameraId.empty()) return true;
    return m_targetCameraId == cameraId;
}

void GdiCameraWindow::OnNewFrame(const CameraFrameData& frameData) {
    if (!frameData.IsValid()) return;

    // Determine if this is primary or secondary camera
    bool isPrimaryCamera = false;
    bool isSecondaryCamera = false;
    {
        std::lock_guard<std::mutex> lock(m_cameraMutex);
        if (m_dualCameraMode) {
            // In dual mode, check if this matches primary or secondary
            if (m_targetCameraId.empty() || frameData.cameraId == m_targetCameraId) {
                isPrimaryCamera = true;
            } else if (!m_secondaryCameraId.empty() && frameData.cameraId == m_secondaryCameraId) {
                isSecondaryCamera = true;
            }
        } else {
            // Single mode: all frames go to primary
            if (m_targetCameraId.empty() || frameData.cameraId == m_targetCameraId) {
                isPrimaryCamera = true;
            }
        }
    }

    static std::set<std::string> loggedCameras;
    if (loggedCameras.find(frameData.cameraId) == loggedCameras.end()) {
        std::cout << "[GdiCameraWindow] First frame received from camera: " << frameData.cameraId
                  << " (" << frameData.width << "x" << frameData.height << ")"
                  << (isPrimaryCamera ? " [Primary]" : "")
                  << (isSecondaryCamera ? " [Secondary]" : "") << std::endl;
        loggedCameras.insert(frameData.cameraId);
    }

    // Store frame in appropriate buffer
    if (isPrimaryCamera) {
        std::lock_guard<std::mutex> lock(m_frameMutex);

        size_t requiredSize = frameData.width * frameData.height * 3;
        if (m_frameBuffer.size() < requiredSize) {
            m_frameBuffer.resize(requiredSize);
        }

        m_frameWidth = frameData.width;
        m_frameHeight = frameData.height;

        const uint8_t* src = frameData.imageData.data();
        uint8_t* dst = m_frameBuffer.data();

        // RGB->BGR conversion + vertical flip for bottom-up BMP
        for (uint32_t y = 0; y < frameData.height; ++y) {
            uint32_t srcRow = y;
            uint32_t dstRow = frameData.height - 1 - y;

            const uint8_t* srcLine = src + srcRow * frameData.width * 3;
            uint8_t* dstLine = dst + dstRow * frameData.width * 3;

            for (uint32_t x = 0; x < frameData.width; ++x) {
                dstLine[x * 3 + 0] = srcLine[x * 3 + 2];  // B
                dstLine[x * 3 + 1] = srcLine[x * 3 + 1];  // G
                dstLine[x * 3 + 2] = srcLine[x * 3 + 0];  // R
            }
        }

        // Apply rotation if enabled
        if (m_displayRotation != 0) {
            m_originalFrameWidth = m_frameWidth;
            m_originalFrameHeight = m_frameHeight;

            uint32_t srcW = m_frameWidth;
            uint32_t srcH = m_frameHeight;
            // Rotated dimensions: W becomes H, H becomes W
            uint32_t rotW = srcH;
            uint32_t rotH = srcW;

            std::vector<uint8_t> rotated(rotW * rotH * 3);
            for (uint32_t dy = 0; dy < rotH; ++dy) {
                for (uint32_t dx = 0; dx < rotW; ++dx) {
                    uint32_t sx, sy;
                    if (m_displayRotation == -90) {
                        // -90° CCW: dest(dx, dy) = src(srcW - 1 - dy, dx)
                        sx = srcW - 1 - dy;
                        sy = dx;
                    }
                    else {
                        // +90° CW: dest(dx, dy) = src(dy, srcH - 1 - dx)
                        sx = dy;
                        sy = srcH - 1 - dx;
                    }
                    size_t srcIdx = (sy * srcW + sx) * 3;
                    size_t dstIdx = (dy * rotW + dx) * 3;
                    rotated[dstIdx + 0] = m_frameBuffer[srcIdx + 0];
                    rotated[dstIdx + 1] = m_frameBuffer[srcIdx + 1];
                    rotated[dstIdx + 2] = m_frameBuffer[srcIdx + 2];
                }
            }

            m_frameBuffer = std::move(rotated);
            m_frameWidth = rotW;
            m_frameHeight = rotH;
        }

        m_hasNewFrame = true;

        // Track acquisition FPS
        ++m_acquisitionFrameCount;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastAcquisitionFpsTime).count();
        if (elapsed >= 1000) {
            m_acquisitionFps.store(m_acquisitionFrameCount * 1000.0 / elapsed);
            m_acquisitionFrameCount = 0;
            m_lastAcquisitionFpsTime = now;
        }
    }
    else if (isSecondaryCamera) {
        std::lock_guard<std::mutex> lock(m_secondaryFrameMutex);

        size_t requiredSize = frameData.width * frameData.height * 3;
        if (m_secondaryFrameBuffer.size() < requiredSize) {
            m_secondaryFrameBuffer.resize(requiredSize);
        }

        m_secondaryFrameWidth = frameData.width;
        m_secondaryFrameHeight = frameData.height;

        const uint8_t* src = frameData.imageData.data();
        uint8_t* dst = m_secondaryFrameBuffer.data();

        // RGB->BGR conversion + vertical flip for bottom-up BMP
        for (uint32_t y = 0; y < frameData.height; ++y) {
            uint32_t srcRow = y;
            uint32_t dstRow = frameData.height - 1 - y;

            const uint8_t* srcLine = src + srcRow * frameData.width * 3;
            uint8_t* dstLine = dst + dstRow * frameData.width * 3;

            for (uint32_t x = 0; x < frameData.width; ++x) {
                dstLine[x * 3 + 0] = srcLine[x * 3 + 2];  // B
                dstLine[x * 3 + 1] = srcLine[x * 3 + 1];  // G
                dstLine[x * 3 + 2] = srcLine[x * 3 + 0];  // R
            }
        }

        m_hasSecondaryNewFrame = true;
    } else {
        return; // Frame not for us
    }

#ifdef HAS_OPENCV
    // Queue frame for vision processing if enabled (only for primary camera)
    // Vision processes what you see: rotated image when rotation is on
    if (isPrimaryCamera && m_visionEnabled && m_visionThreadRunning) {
        std::lock_guard<std::mutex> lock(m_frameQueueMutex);
        // Keep max 2 frames in queue to avoid lag
        while (m_frameQueue.size() >= 2) {
            m_frameQueue.pop();
        }
        if (m_displayRotation != 0) {
            // Build rotated RGB from original frame data
            uint32_t srcW = frameData.width;
            uint32_t srcH = frameData.height;
            uint32_t rotW = srcH;
            uint32_t rotH = srcW;
            std::vector<uint8_t> rotatedRgb(rotW * rotH * 3);
            const uint8_t* src = frameData.imageData.data();
            for (uint32_t dy = 0; dy < rotH; ++dy) {
                for (uint32_t dx = 0; dx < rotW; ++dx) {
                    uint32_t sx, sy;
                    if (m_displayRotation == -90) {
                        sx = srcW - 1 - dy;
                        sy = dx;
                    }
                    else {
                        sx = dy;
                        sy = srcH - 1 - dx;
                    }
                    size_t srcIdx = (sy * srcW + sx) * 3;
                    size_t dstIdx = (dy * rotW + dx) * 3;
                    rotatedRgb[dstIdx + 0] = src[srcIdx + 0];
                    rotatedRgb[dstIdx + 1] = src[srcIdx + 1];
                    rotatedRgb[dstIdx + 2] = src[srcIdx + 2];
                }
            }
            m_frameQueue.push(std::move(rotatedRgb));
            m_queuedFrameWidth = rotW;
            m_queuedFrameHeight = rotH;
        } else {
            m_frameQueue.push(frameData.imageData);
            m_queuedFrameWidth = frameData.width;
            m_queuedFrameHeight = frameData.height;
        }
        m_frameQueueCV.notify_one();
    }
#endif

    // Queue frame for beam streaming (check against beam stream camera ID)
    if (m_beamStreamEnabled && m_beamStreamThreadRunning) {
        std::string streamCamId;
        {
            std::lock_guard<std::mutex> lock(m_beamStreamCameraMutex);
            streamCamId = m_beamStreamCameraId;
        }
        if (frameData.cameraId == streamCamId) {
            std::lock_guard<std::mutex> lock(m_beamStreamQueueMutex);
            while (m_beamStreamQueue.size() >= 2) {
                m_beamStreamQueue.pop();
            }
            m_beamStreamQueue.push(frameData.imageData);
            m_beamStreamQueuedWidth = frameData.width;
            m_beamStreamQueuedHeight = frameData.height;
            m_beamStreamQueueCV.notify_one();
        }
    }

    if (m_hwnd && IsWindowVisible(m_hwnd)) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void GdiCameraWindow::OnCameraStatusChanged(const std::string& cameraId, bool connected, bool grabbing) {
    bool isPrimaryCamera = false;
    bool isSecondaryCamera = false;
    {
        std::lock_guard<std::mutex> lock(m_cameraMutex);
        if (m_dualCameraMode) {
            if (m_targetCameraId.empty() || m_targetCameraId == cameraId) {
                isPrimaryCamera = true;
            } else if (!m_secondaryCameraId.empty() && m_secondaryCameraId == cameraId) {
                isSecondaryCamera = true;
            }
        } else {
            if (m_targetCameraId.empty() || m_targetCameraId == cameraId) {
                isPrimaryCamera = true;
            }
        }
    }

    if (!isPrimaryCamera && !isSecondaryCamera) {
        return;
    }

    // Only update main status for primary camera
    if (isPrimaryCamera) {
        m_cameraConnected = connected;
        m_cameraGrabbing = grabbing;
    }

    if (m_hwnd) {
        std::string title = "Camera Monitor (GDI)";
        if (m_dualCameraMode) {
            title += " [Dual View]";
        }
        if (!m_cameraConnected) {
            title += " [Disconnected]";
        } else if (!m_cameraGrabbing) {
            title += " [Stopped]";
        } else {
            title += " [Live]";
        }
        SetTitle(title);
    }
}

bool GdiCameraWindow::ProcessMessages() {
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return true;
}

LRESULT CALLBACK GdiCameraWindow::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    GdiCameraWindow* pThis = nullptr;

    if (uMsg == WM_NCCREATE) {
        CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        pThis = reinterpret_cast<GdiCameraWindow*>(pCreate->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    } else {
        pThis = reinterpret_cast<GdiCameraWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (pThis) {
        return pThis->HandleMessage(hwnd, uMsg, wParam, lParam);
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

LRESULT GdiCameraWindow::HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_PAINT:
        OnPaint();
        return 0;

    case WM_SIZE:
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_COMMAND:
        OnMenuCommand(LOWORD(wParam));
        return 0;

    case WM_INITMENUPOPUP:
        if ((HMENU)wParam == m_showMenu) {
            UpdateShowMenu();
        }
#ifdef HAS_OPENCV
        else if ((HMENU)wParam == m_visionMenu) {
            // Refresh profile list on menu open (allows runtime updates)
            DiscoverVisionProfiles();
            UpdateVisionMenu();
        }
#endif
        else if ((HMENU)wParam == m_toolsMenu) {
            UpdateToolsMenu();
        }
        else if ((HMENU)wParam == m_cameraMenu) {
            RefreshCameraMenu();
            UpdateCameraMenu();
        }
        else if ((HMENU)wParam == m_beamStreamMenu) {
            RefreshCameraMenu();
            UpdateBeamStreamMenu();
        }
        return 0;

    case WM_USER + 100:  // Custom message to refresh menu
        UpdateShowMenu();
        return 0;

    case WM_CHART_REPAINT_REQUEST:  // Throttled chart repaint request
        if (IsWindowVisible(m_hwnd)) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_VISION_RESULT_READY:  // Vision result ready from background thread
        if (IsWindowVisible(m_hwnd)) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_BEAM_STREAM_RESULT_READY:  // Beam stream result ready
        if (IsWindowVisible(m_hwnd)) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_MOUSEMOVE:
        if (m_clickToMoveEnabled || m_crosshairEnabled) {
            m_mousePosition.x = LOWORD(lParam);
            m_mousePosition.y = HIWORD(lParam);

            // Request WM_MOUSELEAVE notification
            if (!m_mouseOverImage) {
                TRACKMOUSEEVENT tme = {};
                tme.cbSize = sizeof(TRACKMOUSEEVENT);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = m_hwnd;
                TrackMouseEvent(&tme);
            }

            m_mouseOverImage = true;
            SetCursor(LoadCursor(nullptr, m_clickToMoveEnabled ? IDC_CROSS : IDC_ARROW));
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN:
        if (m_clickToMoveEnabled) {
            int mouseX = LOWORD(lParam);
            int mouseY = HIWORD(lParam);
            HandleClickToMove(mouseX, mouseY);
        }
        return 0;

    case WM_MOUSELEAVE:
        m_mouseOverImage = false;
        if (m_clickToMoveEnabled) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_CLOSE:
        Hide();
        return 0;

    case WM_DESTROY:
        if (m_memDC) {
            if (m_oldBitmap) {
                SelectObject(m_memDC, m_oldBitmap);
                m_oldBitmap = nullptr;
            }
            if (m_memBitmap) {
                DeleteObject(m_memBitmap);
                m_memBitmap = nullptr;
            }
            DeleteDC(m_memDC);
            m_memDC = nullptr;
        }
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            Hide();
            return 0;
        }
        break;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void GdiCameraWindow::PrepareBackBuffer() {
    RECT rect;
    GetClientRect(m_hwnd, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    if (width <= 0 || height <= 0) return;

    if (width != m_bufferWidth || height != m_bufferHeight || !m_memDC) {
        HDC hdc = GetDC(m_hwnd);

        if (m_memDC) {
            if (m_oldBitmap) {
                SelectObject(m_memDC, m_oldBitmap);
                m_oldBitmap = nullptr;
            }
            if (m_memBitmap) {
                DeleteObject(m_memBitmap);
                m_memBitmap = nullptr;
            }
            DeleteDC(m_memDC);
        }

        m_memDC = CreateCompatibleDC(hdc);
        m_memBitmap = CreateCompatibleBitmap(hdc, width, height);
        m_oldBitmap = (HBITMAP)SelectObject(m_memDC, m_memBitmap);

        m_bufferWidth = width;
        m_bufferHeight = height;

        ReleaseDC(m_hwnd, hdc);
    }
}

void GdiCameraWindow::OnPaint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(m_hwnd, &ps);

    PrepareBackBuffer();

    if (m_memDC) {
        RECT rect = {0, 0, m_bufferWidth, m_bufferHeight};
        HBRUSH blackBrush = (HBRUSH)GetStockObject(BLACK_BRUSH);
        FillRect(m_memDC, &rect, blackBrush);

        RenderFrame(m_memDC);

        // Draw crosshairs (center and tracking)
        DrawCrosshairs(m_memDC, rect);

#ifdef HAS_OPENCV
        // Draw vision overlay (on LEFT side of camera)
        m_visionOverlay.Draw(m_memDC, rect, m_frameWidth, m_frameHeight,
                             m_lastDestX, m_lastDestY, m_lastDestWidth, m_lastDestHeight);
#endif

        // Draw beam stream overlay (top-right)
        m_beamStreamOverlay.Draw(m_memDC, rect, m_frameWidth, m_frameHeight,
                                  m_lastDestX, m_lastDestY, m_lastDestWidth, m_lastDestHeight,
                                  m_beamStreamConfig.umPerPxX, m_beamStreamConfig.umPerPxY,
                                  m_displayRotation,
                                  static_cast<int>(m_originalFrameWidth),
                                  static_cast<int>(m_originalFrameHeight));

        // Draw chart overlay (on RIGHT side of camera)
        m_chartOverlay.Draw(m_memDC, rect);

        // Draw FPS overlay (bottom-left)
        DrawFpsOverlay(m_memDC, rect);

        // Track display FPS
        ++m_displayFrameCount;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastDisplayFpsTime).count();
        if (elapsed >= 1000) {
            m_displayFps.store(m_displayFrameCount * 1000.0 / elapsed);
            m_displayFrameCount = 0;
            m_lastDisplayFpsTime = now;
        }

        BitBlt(hdc, 0, 0, m_bufferWidth, m_bufferHeight, m_memDC, 0, 0, SRCCOPY);
    }

    EndPaint(m_hwnd, &ps);
}

void GdiCameraWindow::DrawFpsOverlay(HDC hDC, const RECT& windowRect) {
    Gdiplus::Graphics gfx(hDC);
    gfx.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    Gdiplus::Font font(L"Consolas", 9.0f, Gdiplus::FontStyleRegular);
    Gdiplus::SolidBrush textBrush(Gdiplus::Color(220, 255, 255, 255));
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(160, 0, 0, 0));

    double dispFps = m_displayFps.load();
    double acqFps = m_acquisitionFps.load();

    wchar_t line1[64];
    wchar_t line2[64];
    swprintf_s(line1, L"Display:     %.1f fps", dispFps);
    swprintf_s(line2, L"Acquisition: %.1f fps", acqFps);

    float x = 5.0f;
    float y = static_cast<float>(windowRect.bottom - windowRect.top) - 40.0f;

    // Background
    Gdiplus::RectF bgRect(x, y, 190.0f, 36.0f);
    gfx.FillRectangle(&bgBrush, bgRect);

    // Text
    gfx.DrawString(line1, -1, &font, Gdiplus::PointF(x + 4, y + 2), &textBrush);
    gfx.DrawString(line2, -1, &font, Gdiplus::PointF(x + 4, y + 18), &textBrush);
}

void GdiCameraWindow::RenderFrame(HDC hdc) {
    if (m_dualCameraMode) {
        // Dual camera mode: split view 50/50
        int halfWidth = m_bufferWidth / 2;

        // Render primary (left side)
        {
            std::lock_guard<std::mutex> lock(m_frameMutex);

            if (m_frameWidth == 0 || m_frameHeight == 0 || m_frameBuffer.empty()) {
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(128, 128, 128));

                RECT rect = {0, 0, halfWidth, m_bufferHeight};
                DrawTextW(hdc, L"Left: Waiting for camera...", -1, &rect,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                m_lastDestX = 0;
                m_lastDestY = 0;
                m_lastDestWidth = 0;
                m_lastDestHeight = 0;
            } else {
                BITMAPINFOHEADER& bih = m_bitmapInfo->bmiHeader;
                bih.biSize = sizeof(BITMAPINFOHEADER);
                bih.biWidth = m_frameWidth;
                bih.biHeight = m_frameHeight;
                bih.biPlanes = 1;
                bih.biBitCount = 24;
                bih.biCompression = BI_RGB;
                bih.biSizeImage = 0;
                bih.biXPelsPerMeter = 0;
                bih.biYPelsPerMeter = 0;
                bih.biClrUsed = 0;
                bih.biClrImportant = 0;

                float frameAspect = static_cast<float>(m_frameWidth) / m_frameHeight;
                float windowAspect = static_cast<float>(halfWidth) / m_bufferHeight;

                int destWidth, destHeight, destX, destY;

                if (frameAspect > windowAspect) {
                    destWidth = halfWidth;
                    destHeight = static_cast<int>(halfWidth / frameAspect);
                    destX = 0;
                    destY = (m_bufferHeight - destHeight) / 2;
                } else {
                    destHeight = m_bufferHeight;
                    destWidth = static_cast<int>(m_bufferHeight * frameAspect);
                    destX = (halfWidth - destWidth) / 2;
                    destY = 0;
                }

                SetStretchBltMode(hdc, COLORONCOLOR);
                StretchDIBits(
                    hdc,
                    destX, destY, destWidth, destHeight,
                    0, 0, m_frameWidth, m_frameHeight,
                    m_frameBuffer.data(),
                    m_bitmapInfo,
                    DIB_RGB_COLORS,
                    SRCCOPY
                );

                m_lastDestX = destX;
                m_lastDestY = destY;
                m_lastDestWidth = destWidth;
                m_lastDestHeight = destHeight;
            }
            m_hasNewFrame = false;
        }

        // Draw divider line
        Gdiplus::Graphics gfx(hdc);
        Gdiplus::Pen dividerPen(Gdiplus::Color(255, 80, 80, 80), 2.0f);
        gfx.DrawLine(&dividerPen, halfWidth, 0, halfWidth, m_bufferHeight);

        // Render secondary (right side)
        {
            std::lock_guard<std::mutex> lock(m_secondaryFrameMutex);

            if (m_secondaryFrameWidth == 0 || m_secondaryFrameHeight == 0 || m_secondaryFrameBuffer.empty()) {
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(128, 128, 128));

                RECT rect = {halfWidth, 0, m_bufferWidth, m_bufferHeight};

                // Check if secondary camera is selected
                std::string secondaryCam;
                {
                    std::lock_guard<std::mutex> camLock(m_cameraMutex);
                    secondaryCam = m_secondaryCameraId;
                }
                if (secondaryCam.empty()) {
                    DrawTextW(hdc, L"Right: Select secondary camera", -1, &rect,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                } else {
                    DrawTextW(hdc, L"Right: Waiting for camera...", -1, &rect,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
                m_lastSecondaryDestX = 0;
                m_lastSecondaryDestY = 0;
                m_lastSecondaryDestWidth = 0;
                m_lastSecondaryDestHeight = 0;
            } else {
                // Use a separate BITMAPINFO for secondary to avoid conflicts
                BITMAPINFO bitmapInfo2 = {};
                BITMAPINFOHEADER& bih2 = bitmapInfo2.bmiHeader;
                bih2.biSize = sizeof(BITMAPINFOHEADER);
                bih2.biWidth = m_secondaryFrameWidth;
                bih2.biHeight = m_secondaryFrameHeight;
                bih2.biPlanes = 1;
                bih2.biBitCount = 24;
                bih2.biCompression = BI_RGB;

                float frameAspect = static_cast<float>(m_secondaryFrameWidth) / m_secondaryFrameHeight;
                float windowAspect = static_cast<float>(halfWidth) / m_bufferHeight;

                int destWidth, destHeight, destX, destY;

                if (frameAspect > windowAspect) {
                    destWidth = halfWidth;
                    destHeight = static_cast<int>(halfWidth / frameAspect);
                    destX = halfWidth;  // Start at right half
                    destY = (m_bufferHeight - destHeight) / 2;
                } else {
                    destHeight = m_bufferHeight;
                    destWidth = static_cast<int>(m_bufferHeight * frameAspect);
                    destX = halfWidth + (halfWidth - destWidth) / 2;
                    destY = 0;
                }

                SetStretchBltMode(hdc, COLORONCOLOR);
                StretchDIBits(
                    hdc,
                    destX, destY, destWidth, destHeight,
                    0, 0, m_secondaryFrameWidth, m_secondaryFrameHeight,
                    m_secondaryFrameBuffer.data(),
                    &bitmapInfo2,
                    DIB_RGB_COLORS,
                    SRCCOPY
                );

                m_lastSecondaryDestX = destX;
                m_lastSecondaryDestY = destY;
                m_lastSecondaryDestWidth = destWidth;
                m_lastSecondaryDestHeight = destHeight;
            }
            m_hasSecondaryNewFrame = false;
        }

        // Draw camera labels
        {
            Gdiplus::Graphics gfx2(hdc);
            gfx2.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
            Gdiplus::Font labelFont(L"Segoe UI", 10.0f, Gdiplus::FontStyleBold);
            Gdiplus::SolidBrush labelBrush(Gdiplus::Color(220, 255, 255, 255));
            Gdiplus::SolidBrush bgBrush(Gdiplus::Color(160, 0, 0, 0));

            // Get camera IDs for labels
            std::string primaryId, secondaryId;
            {
                std::lock_guard<std::mutex> camLock(m_cameraMutex);
                primaryId = m_targetCameraId.empty() ? "Primary" : m_targetCameraId;
                secondaryId = m_secondaryCameraId.empty() ? "(None)" : m_secondaryCameraId;
            }

            // Left label
            std::wstring leftLabel = L"L: " + std::wstring(primaryId.begin(), primaryId.end());
            Gdiplus::RectF leftLabelRect(5, 5, 200, 20);
            gfx2.FillRectangle(&bgBrush, leftLabelRect);
            gfx2.DrawString(leftLabel.c_str(), -1, &labelFont, Gdiplus::PointF(8, 5), &labelBrush);

            // Right label
            std::wstring rightLabel = L"R: " + std::wstring(secondaryId.begin(), secondaryId.end());
            Gdiplus::RectF rightLabelRect(static_cast<float>(halfWidth + 5), 5, 200, 20);
            gfx2.FillRectangle(&bgBrush, rightLabelRect);
            gfx2.DrawString(rightLabel.c_str(), -1, &labelFont, Gdiplus::PointF(static_cast<float>(halfWidth + 8), 5), &labelBrush);
        }
    } else {
        // Single camera mode (original behavior)
        std::lock_guard<std::mutex> lock(m_frameMutex);

        if (m_frameWidth == 0 || m_frameHeight == 0 || m_frameBuffer.empty()) {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(128, 128, 128));

            RECT rect = {0, 0, m_bufferWidth, m_bufferHeight};
            DrawTextW(hdc, L"Waiting for camera...", -1, &rect,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return;
        }

        BITMAPINFOHEADER& bih = m_bitmapInfo->bmiHeader;
        bih.biSize = sizeof(BITMAPINFOHEADER);
        bih.biWidth = m_frameWidth;
        bih.biHeight = m_frameHeight;
        bih.biPlanes = 1;
        bih.biBitCount = 24;
        bih.biCompression = BI_RGB;
        bih.biSizeImage = 0;
        bih.biXPelsPerMeter = 0;
        bih.biYPelsPerMeter = 0;
        bih.biClrUsed = 0;
        bih.biClrImportant = 0;

        float frameAspect = static_cast<float>(m_frameWidth) / m_frameHeight;
        float windowAspect = static_cast<float>(m_bufferWidth) / m_bufferHeight;

        int destWidth, destHeight, destX, destY;

        if (frameAspect > windowAspect) {
            destWidth = m_bufferWidth;
            destHeight = static_cast<int>(m_bufferWidth / frameAspect);
            destX = 0;
            destY = (m_bufferHeight - destHeight) / 2;
        } else {
            destHeight = m_bufferHeight;
            destWidth = static_cast<int>(m_bufferHeight * frameAspect);
            destX = (m_bufferWidth - destWidth) / 2;
            destY = 0;
        }

        SetStretchBltMode(hdc, COLORONCOLOR);
        StretchDIBits(
            hdc,
            destX, destY, destWidth, destHeight,
            0, 0, m_frameWidth, m_frameHeight,
            m_frameBuffer.data(),
            m_bitmapInfo,
            DIB_RGB_COLORS,
            SRCCOPY
        );

        // Store destination info for overlays (vision overlay and crosshair)
        m_lastDestX = destX;
        m_lastDestY = destY;
        m_lastDestWidth = destWidth;
        m_lastDestHeight = destHeight;

        m_hasNewFrame = false;
    }
}

// ============================================================================
// Click-to-Move and Crosshair Implementation
// ============================================================================

void GdiCameraWindow::SetCameraConfigManager(CameraConfigManager* configMgr) {
    m_cameraConfigManager = configMgr;
    // Reload calibration with per-camera support now available
    LoadCalibrationFactors();
}

void GdiCameraWindow::LoadCalibrationFactors() {
    // Try per-camera calibration from CameraConfigManager first
    if (m_cameraConfigManager && !m_targetCameraId.empty()) {
        CameraConfigData::PixelCalibration cal;
        if (m_cameraConfigManager->GetCalibration(m_targetCameraId, cal)) {
            m_pixelToMmFactorX = cal.pixelToMmX;
            m_pixelToMmFactorY = cal.pixelToMmY;
            m_calibrationRefWidth = cal.referenceWidth;
            m_calibrationRefHeight = cal.referenceHeight;
            std::cout << "[GdiCameraWindow] Per-camera calibration for '" << m_targetCameraId
                      << "': PixelToMm X=" << m_pixelToMmFactorX
                      << ", Y=" << m_pixelToMmFactorY;
            if (m_calibrationRefWidth > 0 && m_calibrationRefHeight > 0) {
                std::cout << ", RefRes=" << m_calibrationRefWidth << "x" << m_calibrationRefHeight;
            }
            std::cout << std::endl;
            return;
        }
        std::cout << "[GdiCameraWindow] No per-camera calibration for '" << m_targetCameraId
                  << "', falling back to val_pixelmm.json" << std::endl;
    }

    // Fallback: load from global JSON config file (shared with CameraClickToMoveWidget)
    std::vector<std::string> configPaths = {
        CALIBRATION_CONFIG_FILE,
        std::string("./") + CALIBRATION_CONFIG_FILE,
        std::string("../") + CALIBRATION_CONFIG_FILE
    };

    for (const auto& configPath : configPaths) {
        try {
            std::ifstream file(configPath);
            if (file.is_open()) {
                nlohmann::json j;
                file >> j;

                if (j.contains("calibration")) {
                    auto& cal = j["calibration"];
                    m_pixelToMmFactorX = cal.value("pixel_to_mm_x", 0.00248);
                    m_pixelToMmFactorY = cal.value("pixel_to_mm_y", 0.00248);
                    m_calibrationRefWidth = cal.value("reference_width", 0);
                    m_calibrationRefHeight = cal.value("reference_height", 0);
                    std::cout << "[GdiCameraWindow] Loaded calibration from fallback: PixelToMm X=" << m_pixelToMmFactorX
                              << ", Y=" << m_pixelToMmFactorY;
                    if (m_calibrationRefWidth > 0 && m_calibrationRefHeight > 0) {
                        std::cout << ", RefRes=" << m_calibrationRefWidth << "x" << m_calibrationRefHeight;
                    }
                    std::cout << std::endl;
                    return;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[GdiCameraWindow] Error loading calibration from " << configPath
                      << ": " << e.what() << std::endl;
        }
    }

    std::cout << "[GdiCameraWindow] Using default calibration factors: X=" << m_pixelToMmFactorX
              << ", Y=" << m_pixelToMmFactorY << std::endl;
}

void GdiCameraWindow::SetMotionManager(UnifiedMotionManager* motionManager) {
    m_motionManager = motionManager;
    std::cout << "[GdiCameraWindow] Motion manager set" << std::endl;
}

void GdiCameraWindow::SetCameraManager(CameraManager* cameraManager) {
    m_cameraManager = cameraManager;
    if (m_cameraManager) {
        RefreshCameraMenu();
        std::cout << "[GdiCameraWindow] Camera manager set" << std::endl;
    }
}

void GdiCameraWindow::RefreshCameraMenu() {
    if (!m_cameraManager) return;

    std::vector<std::string> cameras = m_cameraManager->GetCameraIds();
    {
        std::lock_guard<std::mutex> lock(m_cameraListMutex);
        m_availableCameraIds = cameras;
    }

    std::cout << "[GdiCameraWindow] Refreshed camera list: " << cameras.size() << " cameras available" << std::endl;
}

void GdiCameraWindow::SetDualCameraMode(bool enabled) {
    m_dualCameraMode = enabled;
    std::cout << "[GdiCameraWindow] Dual camera mode " << (enabled ? "enabled" : "disabled") << std::endl;

    if (!enabled) {
        // Clear secondary camera data
        {
            std::lock_guard<std::mutex> lock(m_cameraMutex);
            m_secondaryCameraId.clear();
        }
        {
            std::lock_guard<std::mutex> lock(m_secondaryFrameMutex);
            m_secondaryFrameWidth = 0;
            m_secondaryFrameHeight = 0;
        }
    }

    // Update window title
    if (m_hwnd) {
        std::string title = "Camera Monitor (GDI)";
        if (enabled) {
            title += " [Dual View]";
        }
        if (m_cameraConnected && m_cameraGrabbing) {
            title += " [Live]";
        }
        SetTitle(title);
    }

    if (m_hwnd) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void GdiCameraWindow::SetSecondaryCameraId(const std::string& cameraId) {
    std::lock_guard<std::mutex> lock(m_cameraMutex);
    m_secondaryCameraId = cameraId;
    std::cout << "[GdiCameraWindow] Secondary camera set to: " << (cameraId.empty() ? "(none)" : cameraId) << std::endl;
}

std::string GdiCameraWindow::GetSecondaryCameraId() const {
    std::lock_guard<std::mutex> lock(m_cameraMutex);
    return m_secondaryCameraId;
}

void GdiCameraWindow::SetClickToMoveEnabled(bool enabled) {
    m_clickToMoveEnabled = enabled;
    UpdateToolsMenu();
    std::cout << "[GdiCameraWindow] Click-to-move " << (enabled ? "enabled" : "disabled") << std::endl;
}

void GdiCameraWindow::SetCrosshairEnabled(bool enabled) {
    m_crosshairEnabled = enabled;
    UpdateToolsMenu();
    std::cout << "[GdiCameraWindow] Crosshair " << (enabled ? "enabled" : "disabled") << std::endl;
}

void GdiCameraWindow::SetShowDistanceEnabled(bool enabled) {
    m_showDistanceEnabled = enabled;
    UpdateToolsMenu();
    std::cout << "[GdiCameraWindow] Show distance " << (enabled ? "enabled" : "disabled") << std::endl;
}

void GdiCameraWindow::SetMagnification(double mag) {
    if (mag > 0.0) {
        m_magnification = mag;
        UpdateToolsMenu();
        std::cout << "[GdiCameraWindow] Magnification set to " << m_magnification << "x" << std::endl;
    }
}

void GdiCameraWindow::DrawCrosshairs(HDC hDC, const RECT& /*windowRect*/) {
    if (!m_crosshairEnabled && !m_clickToMoveEnabled && !m_showDistanceEnabled) return;
    if (m_lastDestWidth <= 0 || m_lastDestHeight <= 0) return;

    Gdiplus::Graphics gfx(hDC);
    gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    // Calculate center of the rendered image area
    int centerX = m_lastDestX + m_lastDestWidth / 2;
    int centerY = m_lastDestY + m_lastDestHeight / 2;

    // Draw center crosshair (green, full width/height) if crosshair enabled
    if (m_crosshairEnabled) {
        Gdiplus::Pen centerPen(Gdiplus::Color(255, 0, 255, 0), 1.0f);  // Green

        // Horizontal line across entire image area
        gfx.DrawLine(&centerPen,
                     m_lastDestX, centerY,
                     m_lastDestX + m_lastDestWidth, centerY);

        // Vertical line across entire image area
        gfx.DrawLine(&centerPen,
                     centerX, m_lastDestY,
                     centerX, m_lastDestY + m_lastDestHeight);
    }

    // Draw tracking crosshair (cyan) if click-to-move enabled and mouse over image
    bool mouseInImage = m_mouseOverImage &&
        m_mousePosition.x >= m_lastDestX &&
        m_mousePosition.x < m_lastDestX + m_lastDestWidth &&
        m_mousePosition.y >= m_lastDestY &&
        m_mousePosition.y < m_lastDestY + m_lastDestHeight;

    if (m_clickToMoveEnabled && mouseInImage) {
        Gdiplus::Pen trackPen(Gdiplus::Color(255, 0, 255, 255), 1.0f);  // Cyan

        int mouseX = static_cast<int>(m_mousePosition.x);
        int mouseY = static_cast<int>(m_mousePosition.y);

        // Full crosshair lines across entire image area
        gfx.DrawLine(&trackPen,
                     m_lastDestX, mouseY,
                     m_lastDestX + m_lastDestWidth, mouseY);
        gfx.DrawLine(&trackPen,
                     mouseX, m_lastDestY,
                     mouseX, m_lastDestY + m_lastDestHeight);

        // Small cross at mouse position
        int crossSize = 15;
        gfx.DrawLine(&trackPen,
                     mouseX - crossSize, mouseY,
                     mouseX + crossSize, mouseY);
        gfx.DrawLine(&trackPen,
                     mouseX, mouseY - crossSize,
                     mouseX, mouseY + crossSize);
    }

    // Draw real-world distance text near mouse cursor
    if (m_showDistanceEnabled && mouseInImage && m_frameWidth > 0 && m_frameHeight > 0) {
        int mouseX = static_cast<int>(m_mousePosition.x);
        int mouseY = static_cast<int>(m_mousePosition.y);

        // Calculate pixel offset from image center to mouse position
        int pixelOffsetX = mouseX - centerX;
        int pixelOffsetY = mouseY - centerY;

        // Scale to original image pixels
        double scaleFactorX = static_cast<double>(m_frameWidth) / m_lastDestWidth;
        double scaleFactorY = static_cast<double>(m_frameHeight) / m_lastDestHeight;
        double scaledOffsetX = pixelOffsetX * scaleFactorX;
        double scaledOffsetY = pixelOffsetY * scaleFactorY;

        // Apply effective pixel-to-mm factor (with calibration ref scaling)
        double effectivePixelToMmX = m_pixelToMmFactorX;
        double effectivePixelToMmY = m_pixelToMmFactorY;
        if (m_calibrationRefWidth > 0 && m_calibrationRefHeight > 0) {
            effectivePixelToMmX = m_pixelToMmFactorX * static_cast<double>(m_calibrationRefWidth) / m_frameWidth;
            effectivePixelToMmY = m_pixelToMmFactorY * static_cast<double>(m_calibrationRefHeight) / m_frameHeight;
        }

        // Divide by magnification to get physical distance at the sample
        double distX_mm = (scaledOffsetX * effectivePixelToMmX) / m_magnification;
        double distY_mm = (scaledOffsetY * effectivePixelToMmY) / m_magnification;

        // Format text
        wchar_t dxBuf[64];
        wchar_t dyBuf[64];
        swprintf_s(dxBuf, L"dX: %+.3f mm", distX_mm);
        swprintf_s(dyBuf, L"dY: %+.3f mm", distY_mm);

        // Position text offset from mouse cursor (bottom-right, or adjust if near edge)
        int textOffsetX = 18;
        int textOffsetY = 18;
        float textX = static_cast<float>(mouseX + textOffsetX);
        float textY = static_cast<float>(mouseY + textOffsetY);

        // Flip to left side if too close to right edge
        if (mouseX + textOffsetX + 140 > m_lastDestX + m_lastDestWidth) {
            textX = static_cast<float>(mouseX - textOffsetX - 130);
        }
        // Flip above if too close to bottom
        if (mouseY + textOffsetY + 36 > m_lastDestY + m_lastDestHeight) {
            textY = static_cast<float>(mouseY - textOffsetY - 36);
        }

        Gdiplus::Font font(L"Consolas", 10.0f, Gdiplus::FontStyleBold);

        // Draw dark background rectangle for readability
        Gdiplus::RectF bgRect(textX - 3.0f, textY - 2.0f, 136.0f, 36.0f);
        Gdiplus::SolidBrush bgBrush(Gdiplus::Color(180, 0, 0, 0));
        gfx.FillRectangle(&bgBrush, bgRect);

        // Draw outline border
        Gdiplus::Pen borderPen(Gdiplus::Color(200, 255, 255, 255), 1.0f);
        gfx.DrawRectangle(&borderPen, bgRect);

        // Draw text in white
        Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));
        Gdiplus::PointF dxPos(textX, textY);
        Gdiplus::PointF dyPos(textX, textY + 16.0f);
        gfx.DrawString(dxBuf, -1, &font, dxPos, &textBrush);
        gfx.DrawString(dyBuf, -1, &font, dyPos, &textBrush);
    }
}

void GdiCameraWindow::HandleClickToMove(int mouseX, int mouseY) {
    if (!m_motionManager) {
        std::cerr << "[GdiCameraWindow] Cannot move - no motion manager!" << std::endl;
        return;
    }

    if (m_lastDestWidth <= 0 || m_lastDestHeight <= 0) {
        std::cerr << "[GdiCameraWindow] Cannot move - no valid image displayed!" << std::endl;
        return;
    }

    // Check if click is within image area
    if (mouseX < m_lastDestX || mouseX >= m_lastDestX + m_lastDestWidth ||
        mouseY < m_lastDestY || mouseY >= m_lastDestY + m_lastDestHeight) {
        std::cout << "[GdiCameraWindow] Click outside image area" << std::endl;
        return;
    }

    // Calculate pixel offset from center of displayed image
    int centerX = m_lastDestX + m_lastDestWidth / 2;
    int centerY = m_lastDestY + m_lastDestHeight / 2;

    int pixelOffsetX = mouseX - centerX;
    int pixelOffsetY = mouseY - centerY;

    // Scale to original image size (account for scaling when displaying)
    double scaleFactorX = static_cast<double>(m_frameWidth) / m_lastDestWidth;
    double scaleFactorY = static_cast<double>(m_frameHeight) / m_lastDestHeight;

    double scaledOffsetX = pixelOffsetX * scaleFactorX;
    double scaledOffsetY = pixelOffsetY * scaleFactorY;

    // Convert to mm (auto-scale if actual resolution differs from calibration reference)
    double effectivePixelToMmX = m_pixelToMmFactorX;
    double effectivePixelToMmY = m_pixelToMmFactorY;
    if (m_calibrationRefWidth > 0 && m_calibrationRefHeight > 0 &&
        m_frameWidth > 0 && m_frameHeight > 0) {
        effectivePixelToMmX = m_pixelToMmFactorX * static_cast<double>(m_calibrationRefWidth) / m_frameWidth;
        effectivePixelToMmY = m_pixelToMmFactorY * static_cast<double>(m_calibrationRefHeight) / m_frameHeight;
    }
    double offsetX_mm = (scaledOffsetX * effectivePixelToMmX) / m_magnification;
    double offsetY_mm = (scaledOffsetY * effectivePixelToMmY) / m_magnification;

    std::cout << "[GdiCameraWindow] Click detected: Pixel offset (" << pixelOffsetX << ", " << pixelOffsetY << ")"
              << " scaled (" << scaledOffsetX << ", " << scaledOffsetY << ")"
              << " = (" << offsetX_mm << " mm, " << offsetY_mm << " mm)" << std::endl;

    // Get current position
    PositionStruct currentPos = m_motionManager->GetCurrentPosition("gantry-main");

    // Calculate new absolute position
    double newX = currentPos.x + offsetX_mm;
    double newY = currentPos.y + offsetY_mm;
    double newZ = currentPos.z;  // Keep Z unchanged

    std::cout << "[GdiCameraWindow] Current position: X=" << currentPos.x
              << ", Y=" << currentPos.y << ", Z=" << currentPos.z << std::endl;
    std::cout << "[GdiCameraWindow] Target position: X=" << newX
              << ", Y=" << newY << ", Z=" << newZ << std::endl;

    // Move to absolute position (coordinated XY move, Z stays same, slow speed for precision)
    bool success = m_motionManager->MoveToAbs("gantry-main", newX, newY, newZ, 10.0, false);

    if (success) {
        std::cout << "[GdiCameraWindow] Gantry move command sent successfully" << std::endl;
    } else {
        std::cerr << "[GdiCameraWindow] Failed to send gantry move command!" << std::endl;
    }
}

// ============================================================================
// Vision Processing Thread
// ============================================================================

#ifdef HAS_OPENCV

void GdiCameraWindow::StartVisionProcessing() {
    if (m_visionThreadRunning) return;

    // Suppress verbose logging during continuous vision
    VisionManager::instance().setVerboseLogging(false);

    m_visionThreadRunning = true;
    m_visionThread = std::thread(&GdiCameraWindow::VisionThreadLoop, this);
    std::cout << "[GdiCameraWindow] Vision processing thread started" << std::endl;
}

void GdiCameraWindow::StopVisionProcessing() {
    if (!m_visionThreadRunning) return;

    m_visionThreadRunning = false;
    m_frameQueueCV.notify_all();

    if (m_visionThread.joinable()) {
        m_visionThread.join();
    }

    // Clear the frame queue
    {
        std::lock_guard<std::mutex> lock(m_frameQueueMutex);
        while (!m_frameQueue.empty()) {
            m_frameQueue.pop();
        }
    }

    // Restore verbose logging
    VisionManager::instance().setVerboseLogging(true);

    std::cout << "[GdiCameraWindow] Vision processing thread stopped" << std::endl;
}

void GdiCameraWindow::SetVisionEnabled(bool enabled) {
    if (enabled == m_visionEnabled.load()) return;

    if (enabled) {
        std::lock_guard<std::mutex> lock(m_visionProfileMutex);
        if (m_activeVisionProfile.empty()) {
            // Auto-select first profile if none selected
            if (!m_availableProfiles.empty()) {
                m_activeVisionProfile = m_availableProfiles[0].fullPath;
            } else {
                std::cout << "[GdiCameraWindow] SetVisionEnabled: No profile available" << std::endl;
                return;
            }
        }
        m_visionEnabled = true;
        m_visionOverlay.SetEnabled(true);
        StartVisionProcessing();
    } else {
        StopVisionProcessing();
        m_visionEnabled = false;
        m_visionOverlay.SetEnabled(false);
        m_visionOverlay.Clear();
    }

    UpdateVisionMenu();
    InvalidateRect(m_hwnd, nullptr, FALSE);
    std::cout << "[GdiCameraWindow] SetVisionEnabled(" << (enabled ? "true" : "false") << ")" << std::endl;
}

void GdiCameraWindow::SelectVisionProfile(const std::string& profilePath) {
    std::lock_guard<std::mutex> lock(m_visionProfileMutex);
    m_activeVisionProfile = profilePath;
    std::cout << "[GdiCameraWindow] SelectVisionProfile: " << profilePath << std::endl;
}

void GdiCameraWindow::VisionThreadLoop() {
    std::cout << "[GdiCameraWindow] Vision background thread started (ID: "
              << std::this_thread::get_id() << ")" << std::endl;

    while (m_visionThreadRunning) {
        std::vector<uint8_t> frameData;
        uint32_t frameWidth = 0;
        uint32_t frameHeight = 0;
        std::string profilePath;

        // Wait for a frame
        {
            std::unique_lock<std::mutex> lock(m_frameQueueMutex);
            m_frameQueueCV.wait_for(lock, std::chrono::milliseconds(100), [this] {
                return !m_frameQueue.empty() || !m_visionThreadRunning;
            });

            if (!m_visionThreadRunning) break;

            if (m_frameQueue.empty()) continue;

            frameData = std::move(m_frameQueue.front());
            m_frameQueue.pop();
            frameWidth = m_queuedFrameWidth;
            frameHeight = m_queuedFrameHeight;
        }

        // Get active profile
        {
            std::lock_guard<std::mutex> lock(m_visionProfileMutex);
            profilePath = m_activeVisionProfile;
        }

        if (profilePath.empty() || frameData.empty() || frameWidth == 0 || frameHeight == 0) {
            continue;
        }

        try {
            // Convert RGB frame to BGR cv::Mat for OpenCV
            cv::Mat bgrImage(frameHeight, frameWidth, CV_8UC3);
            const uint8_t* src = frameData.data();
            uint8_t* dst = bgrImage.data;

            for (uint32_t y = 0; y < frameHeight; ++y) {
                for (uint32_t x = 0; x < frameWidth; ++x) {
                    size_t srcIdx = (y * frameWidth + x) * 3;
                    size_t dstIdx = (y * frameWidth + x) * 3;
                    // RGB to BGR
                    dst[dstIdx + 0] = src[srcIdx + 2];  // B
                    dst[dstIdx + 1] = src[srcIdx + 1];  // G
                    dst[dstIdx + 2] = src[srcIdx + 0];  // R
                }
            }

            // Load vision profile JSON directly (not as a recipe)
            std::ifstream profileFile(profilePath);
            if (!profileFile.is_open()) {
                std::cerr << "[GdiCameraWindow] Cannot open profile: " << profilePath << std::endl;
                continue;
            }

            nlohmann::json profileJson;
            profileFile >> profileJson;
            profileFile.close();

            // Extract plugin name and parameters from profile
            std::string pluginName;
            nlohmann::json pluginParams;

            if (profileJson.contains("plugin") && profileJson["plugin"].is_object()) {
                pluginName = profileJson["plugin"].value("name", "");
                pluginParams = profileJson["plugin"].value("parameters", nlohmann::json::object());
            }

            if (pluginName.empty()) {
                std::cerr << "[GdiCameraWindow] Profile missing plugin name: " << profilePath << std::endl;
                continue;
            }

            // Set up ROI if specified in profile
            if (profileJson.contains("roi") && profileJson["roi"].is_object()) {
                const auto& roiJson = profileJson["roi"];
                bool roiEnabled = roiJson.value("enabled", false);
                if (roiEnabled) {
                    int startX = roiJson.value("startX", 0);
                    int startY = roiJson.value("startY", 0);
                    int endX = roiJson.value("endX", 0);
                    int endY = roiJson.value("endY", 0);
                    VisionROI roi(startX, startY, endX, endY);
                    roi.setEnabled(true);
                    VisionManager::instance().setROI(roi);
                } else {
                    VisionManager::instance().setROIEnabled(false);
                }
            } else {
                VisionManager::instance().setROIEnabled(false);
            }

            // Set input image
            VisionManager::instance().setInputImage(bgrImage);

            // Execute the plugin directly
            QString qPluginName = QString::fromStdString(pluginName);
            VisionResult result = VisionManager::instance().executePlugin(qPluginName, pluginParams);

            // Extract display name from path
            std::string displayName = profilePath;
            size_t lastSlash = displayName.find_last_of("/\\");
            if (lastSlash != std::string::npos) {
                displayName = displayName.substr(lastSlash + 1);
            }
            if (displayName.length() > 5 &&
                displayName.substr(displayName.length() - 5) == ".json") {
                displayName = displayName.substr(0, displayName.length() - 5);
            }

            // Update overlay (thread-safe via mutex in SetResult)
            m_visionOverlay.SetResult(result, displayName);

            // Post message to UI thread for repaint (safer than direct InvalidateRect)
            if (m_hwnd) {
                PostMessage(m_hwnd, WM_VISION_RESULT_READY, 0, 0);
            }
        }
        catch (const std::exception& e) {
            std::cerr << "[GdiCameraWindow] Vision processing error: " << e.what() << std::endl;
        }
    }

    std::cout << "[GdiCameraWindow] Vision thread loop exiting" << std::endl;
}

#endif // HAS_OPENCV

// ============================================================================
// Beam Stream Implementation
// ============================================================================

void GdiCameraWindow::SetBeamServerClient(BeamServerClient* client) {
    m_beamServerClient = client;
}

void GdiCameraWindow::LoadBeamStreamConfig() {
    // Primary: read from QSettings (Application Settings page)
    QSettings settings("Fabrinet", "BobcatEngine");
    if (settings.contains("BeamServer/Host")) {
        m_beamStreamConfig.host = settings.value("BeamServer/Host", "127.0.0.1").toString().toStdString();
        m_beamStreamConfig.port = settings.value("BeamServer/StreamPort", 5567).toInt();
        m_beamStreamConfig.umPerPxX = settings.value("BeamServer/UmPerPxX", "0.25").toString().toDouble();
        m_beamStreamConfig.umPerPxY = settings.value("BeamServer/UmPerPxY", "0.25").toString().toDouble();
        m_beamStreamConfig.rotate180 = settings.value("BeamServer/Rotate180", "false").toString() == "true";
        m_beamStreamConfig.intervalMs = settings.value("BeamServer/StreamIntervalMs", 100).toInt();
        std::cout << "[GdiCameraWindow] Loaded beam config from Application Settings"
                  << " (host=" << m_beamStreamConfig.host
                  << " port=" << m_beamStreamConfig.port
                  << " interval=" << m_beamStreamConfig.intervalMs << "ms)" << std::endl;
        return;
    }

    // Fallback: read from beam_stream.json
    std::vector<std::string> configPaths = {
        "config/beam_stream.json",
        "./config/beam_stream.json",
        "../config/beam_stream.json"
    };

    for (const auto& path : configPaths) {
        std::ifstream file(path);
        if (file.is_open()) {
            try {
                nlohmann::json cfg;
                file >> cfg;
                m_beamStreamConfig.host = cfg.value("host", "127.0.0.1");
                m_beamStreamConfig.port = cfg.value("port", 5567);
                m_beamStreamConfig.umPerPxX = cfg.value("um_per_px_x", 0.25);
                m_beamStreamConfig.umPerPxY = cfg.value("um_per_px_y", 0.25);
                m_beamStreamConfig.rotate180 = cfg.value("rotate_180", false);
                m_beamStreamConfig.intervalMs = cfg.value("interval_ms", 100);
                std::cout << "[GdiCameraWindow] Loaded beam_stream.json from " << path
                          << " (host=" << m_beamStreamConfig.host
                          << " port=" << m_beamStreamConfig.port
                          << " interval=" << m_beamStreamConfig.intervalMs << "ms)" << std::endl;
                return;
            } catch (const nlohmann::json::exception& e) {
                std::cout << "[GdiCameraWindow] Error parsing beam_stream.json: " << e.what() << std::endl;
            }
        }
    }
    std::cout << "[GdiCameraWindow] beam_stream.json not found, using defaults" << std::endl;
}

void GdiCameraWindow::StartBeamStreaming() {
    if (m_beamStreamThreadRunning) return;

    // Join previous thread if it ended on its own (e.g. connection lost)
    if (m_beamStreamThread.joinable()) {
        m_beamStreamThread.join();
    }

    // Load config
    LoadBeamStreamConfig();

    // Create dedicated stream client (separate from command client on port 5555)
    m_beamStreamClient = std::make_unique<BeamServerClient>();

    std::cout << "[GdiCameraWindow] Connecting to stream server at "
              << m_beamStreamConfig.host << ":" << m_beamStreamConfig.port << std::endl;
    if (!m_beamStreamClient->Connect(m_beamStreamConfig.host, m_beamStreamConfig.port)) {
        std::cout << "[GdiCameraWindow] Failed to connect to stream server: "
                  << m_beamStreamClient->GetLastError() << std::endl;
        m_beamStreamClient.reset();
        return;
    }

    // Get frame dimensions from current frame
    uint32_t width = 0;
    uint32_t height = 0;
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        width = m_frameWidth;
        height = m_frameHeight;
    }

    if (width == 0 || height == 0) {
        std::cout << "[GdiCameraWindow] Cannot start beam streaming: no frame data yet" << std::endl;
        m_beamStreamClient.reset();
        return;
    }

    // Send STREAM_START
    auto response = m_beamStreamClient->SendStreamStart(
        static_cast<int>(width), static_cast<int>(height), "mono8",
        m_beamStreamConfig.rotate180,
        m_beamStreamConfig.umPerPxX, m_beamStreamConfig.umPerPxY,
        m_beamStreamConfig.intervalMs);

    if (response.empty() || response.contains("error")) {
        std::string err = response.value("error", m_beamStreamClient->GetLastError());
        std::cout << "[GdiCameraWindow] STREAM_START failed: " << err << std::endl;
        m_beamStreamClient.reset();
        return;
    }

    m_beamStreamThreadRunning = true;
    m_beamStreamEnabled = true;
    m_beamStreamOverlay.SetEnabled(true);

    m_beamStreamThread = std::thread(&GdiCameraWindow::BeamStreamThreadLoop, this);
    std::cout << "[GdiCameraWindow] Beam streaming started" << std::endl;
}

void GdiCameraWindow::StopBeamStreaming() {
    m_beamStreamThreadRunning = false;
    m_beamStreamQueueCV.notify_all();

    if (m_beamStreamThread.joinable()) {
        m_beamStreamThread.join();
    }

    // Send STREAM_STOP and disconnect dedicated stream client
    if (m_beamStreamClient && m_beamStreamClient->IsConnected()) {
        auto response = m_beamStreamClient->SendStreamStop();
        if (response.contains("total_frames")) {
            std::cout << "[GdiCameraWindow] Stream stopped. Total frames: "
                      << response["total_frames"] << " avg_process_ms: "
                      << response.value("avg_process_ms", 0.0) << std::endl;
        }
    }
    m_beamStreamClient.reset();

    // Clear queue
    {
        std::lock_guard<std::mutex> lock(m_beamStreamQueueMutex);
        while (!m_beamStreamQueue.empty()) {
            m_beamStreamQueue.pop();
        }
    }

    m_beamStreamEnabled = false;
    m_beamStreamOverlay.SetEnabled(false);
    m_beamStreamOverlay.Clear();

    std::cout << "[GdiCameraWindow] Beam streaming stopped" << std::endl;
}

void GdiCameraWindow::BeamStreamThreadLoop() {
    std::cout << "[GdiCameraWindow] Beam stream thread started (ID: "
              << std::this_thread::get_id() << ")" << std::endl;

    static constexpr int MAX_STREAM_ITERATIONS = 10000000;
    uint32_t seqNum = 0;
    auto lastFrameTime = std::chrono::steady_clock::now();
    int frameCount = 0;

    for (int i = 0; i < MAX_STREAM_ITERATIONS && m_beamStreamThreadRunning; ++i) {
        std::vector<uint8_t> rgbData;
        uint32_t frameWidth = 0;
        uint32_t frameHeight = 0;

        // Wait for a frame
        {
            std::unique_lock<std::mutex> lock(m_beamStreamQueueMutex);
            m_beamStreamQueueCV.wait_for(lock,
                std::chrono::milliseconds(m_beamStreamConfig.intervalMs),
                [this] { return !m_beamStreamQueue.empty() || !m_beamStreamThreadRunning; });

            if (!m_beamStreamThreadRunning) break;
            if (m_beamStreamQueue.empty()) continue;

            rgbData = std::move(m_beamStreamQueue.front());
            m_beamStreamQueue.pop();
            frameWidth = m_beamStreamQueuedWidth;
            frameHeight = m_beamStreamQueuedHeight;
        }

        if (rgbData.empty() || frameWidth == 0 || frameHeight == 0) continue;

        // Convert RGB to grayscale mono8
        size_t pixelCount = static_cast<size_t>(frameWidth) * frameHeight;
        std::vector<uint8_t> grayData(pixelCount);
        const uint8_t* src = rgbData.data();
        for (size_t j = 0; j < pixelCount; ++j) {
            size_t idx = j * 3;
            grayData[j] = static_cast<uint8_t>(
                (static_cast<int>(src[idx]) + static_cast<int>(src[idx + 1]) + static_cast<int>(src[idx + 2])) / 3);
        }

        // Send frame
        auto response = m_beamStreamClient->SendStreamFrame(grayData, seqNum++);

        if (response.empty()) {
            std::cout << "[GdiCameraWindow] Stream frame send failed: "
                      << m_beamStreamClient->GetLastError() << std::endl;
            // Connection lost - stop streaming
            m_beamStreamThreadRunning = false;
            break;
        }

        // Calculate FPS
        frameCount++;
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - lastFrameTime).count();
        if (elapsed >= 1.0) {
            double fps = frameCount / elapsed;
            m_beamStreamOverlay.SetFps(fps);
            frameCount = 0;
            lastFrameTime = now;
        }

        // Update overlay with result
        m_beamStreamOverlay.SetResult(response);

        // Trigger repaint
        if (m_hwnd) {
            PostMessage(m_hwnd, WM_BEAM_STREAM_RESULT_READY, 0, 0);
        }
    }

    std::cout << "[GdiCameraWindow] Beam stream thread loop exiting" << std::endl;
}
