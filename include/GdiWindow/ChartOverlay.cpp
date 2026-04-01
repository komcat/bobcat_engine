// ChartOverlay.cpp
#include "ChartOverlay.h"
#include <algorithm>
#include <cmath>
#include <iostream>

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
