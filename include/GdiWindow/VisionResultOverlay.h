// VisionResultOverlay.h
#pragma once

#ifdef HAS_OPENCV

// winsock2.h must be included before Windows.h to avoid conflicts
#include <winsock2.h>
#include <Windows.h>
#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include <string>
#include <mutex>
#include <atomic>

#include "vision/VisionResult.h"

/**
 * @brief Vision result overlay for displaying detection results on camera view
 */
class VisionResultOverlay {
public:
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }

    void SetResult(const VisionResult& result, const std::string& profileName);
    void Clear();

    void Draw(HDC hDC, const RECT& windowRect, int frameWidth, int frameHeight,
              int destX, int destY, int destWidth, int destHeight);

    // Thread-safe copy of the latest result (for SequenceContext injection)
    VisionResult GetResult() const {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        return m_lastResult;
    }
    uint64_t GetResultSequence() const { return m_resultSequence.load(); }

private:
    bool m_enabled = false;
    mutable std::mutex m_resultMutex;
    VisionResult m_lastResult;
    std::string m_profileName;
    std::atomic<uint64_t> m_resultSequence{0};

    static constexpr int OVERLAY_WIDTH = 280;
    static constexpr int OVERLAY_MARGIN = 10;

    void DrawInfoPanel(Gdiplus::Graphics& gfx, const Gdiplus::RectF& panelRect);
    void DrawGeometricOverlays(HDC hDC, int frameWidth, int frameHeight,
                                int destX, int destY, int destWidth, int destHeight);

    // Convert image pixel coordinates to screen coordinates
    static Gdiplus::PointF ImageToScreen(double imgX, double imgY,
                                          int frameWidth, int frameHeight,
                                          int destX, int destY,
                                          int destWidth, int destHeight);
};

#endif // HAS_OPENCV
