// ChartOverlay.h
#pragma once

// winsock2.h must be included before Windows.h to avoid conflicts
#include <winsock2.h>
#include <Windows.h>
#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include <string>
#include <vector>
#include <deque>
#include <map>
#include <set>
#include <mutex>

/**
 * @brief Chart overlay for displaying live data on camera view
 */
class ChartOverlay {
public:
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }

    void AddDataPoint(const std::string& channelId, double value);
    void Clear();

    void SetChannelEnabled(const std::string& channelId, bool enabled);
    bool IsChannelEnabled(const std::string& channelId) const;
    std::vector<std::string> GetEnabledChannels() const;

    void Draw(HDC hDC, const RECT& windowRect);

    // Scale factor for overlay size (1.0 = default, 0.5 = half, 2.0 = double)
    void SetScale(float scale);
    float GetScale() const { return m_scale; }
    void IncreaseScale();
    void DecreaseScale();

private:
    bool m_enabled = false;
    float m_scale = 1.0f;  // Scale factor for overlay size
    mutable std::mutex m_dataMutex;

    struct ChannelData {
        std::deque<double> values;
        double runningMin = 1e30;
        double runningMax = -1e30;
    };
    std::map<std::string, ChannelData> m_channels;
    std::set<std::string> m_enabledChannels;

    static constexpr size_t MAX_POINTS = 200;
    static constexpr int BASE_CHART_WIDTH = 320;
    static constexpr int BASE_CHART_HEIGHT = 180;
    static constexpr int CHART_MARGIN = 10;
    static constexpr float MIN_SCALE = 0.5f;
    static constexpr float MAX_SCALE = 3.0f;
    static constexpr float SCALE_STEP = 0.25f;

    // Computed dimensions based on scale
    int GetChartWidth() const { return static_cast<int>(BASE_CHART_WIDTH * m_scale); }
    int GetChartHeight() const { return static_cast<int>(BASE_CHART_HEIGHT * m_scale); }

    static Gdiplus::Color GetChannelColor(int index);
    static std::wstring FormatValue(double value);
};
