// BeamStreamOverlay.h
#pragma once

// winsock2.h must be included before Windows.h to avoid conflicts
#include <winsock2.h>
#include <Windows.h>
#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include <string>
#include <mutex>
#include <atomic>

#include "nlohmann/json.hpp"

/**
 * @brief Beam stream overlay for displaying live beam detection results on camera view
 */
class BeamStreamOverlay {
public:
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }

    void SetResult(const nlohmann::json& result);
    void Clear();

    void Draw(HDC hDC, const RECT& windowRect, int frameWidth, int frameHeight,
              int destX, int destY, int destWidth, int destHeight,
              double umPerPxX, double umPerPxY,
              int displayRotation = 0, int origFrameWidth = 0, int origFrameHeight = 0);

    double GetFps() const { return m_fps; }
    void SetFps(double fps) { m_fps = fps; }

    // Thread-safe accessors for SequenceContext injection
    nlohmann::json GetResult() const {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        return m_lastResult;
    }
    uint64_t GetResultSequence() const { return m_resultSequence.load(); }

private:
    bool m_enabled = false;
    mutable std::mutex m_resultMutex;
    nlohmann::json m_lastResult;
    double m_fps = 0.0;
    std::atomic<uint64_t> m_resultSequence{0};

    static constexpr int OVERLAY_WIDTH = 280;
    static constexpr int OVERLAY_MARGIN = 10;

    // Convert image pixel coordinates to screen coordinates
    static Gdiplus::PointF ImageToScreen(double imgX, double imgY,
                                          int frameWidth, int frameHeight,
                                          int destX, int destY,
                                          int destWidth, int destHeight);
};
