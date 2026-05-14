// QtCameraViewport.h
#pragma once

#include <QWidget>
#include <QImage>
#include <QPixmap>
#include <QRect>
#include <QPoint>
#include <chrono>
#include <deque>
#include <map>
#include <set>
#include <mutex>
#include <string>
#include <vector>
#include "nlohmann/json.hpp"
#ifdef HAS_OPENCV
#include "vision/VisionResult.h"
#endif

/**
 * @brief Custom QWidget that renders camera frames via QPainter.
 *
 * Supports single and dual camera display, crosshair overlay, real-world
 * distance display, display rotation, magnification, and FPS counters.
 * This is the Qt-based replacement for the Win32 GDI camera rendering surface.
 *
 * Thread safety: setFrame() / setSecondaryFrame() must be called from the
 * main (Qt) thread after the caller has already performed a mutex-protected
 * copy from the camera callback thread.
 */
class QtCameraViewport : public QWidget {
    Q_OBJECT
public:
    explicit QtCameraViewport(QWidget* parent = nullptr);

    // Frame update (called from main thread after mutex-protected copy)
    void setFrame(const uint8_t* rgb, int width, int height);
    void setSecondaryFrame(const uint8_t* rgb, int width, int height);
    void clearFrame();

    // Display modes
    void setDualMode(bool enabled);
    bool isDualMode() const { return m_dualMode; }
    void setDisplayRotation(int degrees);
    int displayRotation() const { return m_displayRotation; }

    // Tools
    void setCrosshairEnabled(bool enabled);
    bool isCrosshairEnabled() const { return m_crosshairEnabled; }
    void setShowDistanceEnabled(bool enabled);
    bool isShowDistanceEnabled() const { return m_showDistance; }
    void setClickToMoveEnabled(bool enabled);
    bool isClickToMoveEnabled() const { return m_clickToMoveEnabled; }
    void setMagnification(double mag);
    double magnification() const { return m_magnification; }
    void setCalibrationFactors(double pixToMmX, double pixToMmY,
                                int refWidth = 0, int refHeight = 0);

    // Dest rect access (for Phase 2 overlay coordinate transforms)
    QRect primaryDestRect() const { return m_primaryDest; }
    QRect secondaryDestRect() const { return m_secondaryDest; }
    int displayedFrameWidth() const;
    int displayedFrameHeight() const;

    // FPS
    void setAcquisitionFps(double fps);

    // Diagnostic label used in periodic paint-cost log lines. Set by
    // QtCameraWindow so the two camera windows are distinguishable in stdout.
    void setDiagName(const std::string& name) { m_diagName = name; }

    // --- Overlay data setters (called by QtCameraWindow) ---

    // Chart overlay
    struct ChartChannelSnapshot {
        std::string channelId;
        std::deque<double> values;
    };
    void setChartOverlayData(bool enabled, float scale,
                              const std::vector<ChartChannelSnapshot>& channels);

#ifdef HAS_OPENCV
    // Vision overlay
    void setVisionOverlayData(bool enabled, const VisionResult& result,
                               const std::string& profileName);
#endif

    // Beam overlay
    void setBeamOverlayData(bool enabled, const nlohmann::json& result, double fps,
                             double umPerPxX, double umPerPxY);

    // Device tracking overlay (bottom-right): live hex device positions in
    // gantry frame, expressed as delta from a registered zero point P0.
    struct DeviceTrackingOverlayData {
        bool enabled = false;
        bool p0Set = false;
        double p0X = 0.0, p0Y = 0.0;
        bool hexLeftActive = false;   // registered + matrix available + live data ok
        double hexLeftDX = 0.0, hexLeftDY = 0.0;
        bool hexLeftAlert = false;    // distance below threshold
        bool hexRightActive = false;
        double hexRightDX = 0.0, hexRightDY = 0.0;
        bool hexRightAlert = false;
    };
    void setDeviceTrackingOverlayData(const DeviceTrackingOverlayData& data);

signals:
    void imageClicked(int imageX, int imageY);
    void mouseMoved(int imageX, int imageY);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    // Primary frame
    QImage m_frameImage;
    bool m_hasFrame = false;
    int m_frameW = 0, m_frameH = 0;

    // Secondary frame (dual mode)
    QImage m_secondaryFrameImage;
    bool m_hasSecondaryFrame = false;
    int m_secondaryW = 0, m_secondaryH = 0;

    // Display state
    bool m_dualMode = false;
    int m_displayRotation = 0;
    bool m_crosshairEnabled = false;
    bool m_clickToMoveEnabled = false;
    bool m_showDistance = false;
    double m_magnification = 1.0;
    double m_pixToMmX = 0.00248;
    double m_pixToMmY = 0.00248;
    int m_calibRefWidth = 0;
    int m_calibRefHeight = 0;

    // Render state (updated each paint)
    QRect m_primaryDest;
    QRect m_secondaryDest;

    // FPS tracking
    double m_displayFps = 0.0;
    double m_acquisitionFps = 0.0;
    int m_paintCount = 0;
    std::chrono::steady_clock::time_point m_fpsTimestamp;

    // Per-paint cost diagnostics (UI thread only — no atomics needed).
    // Logged every 5 s alongside which overlays were active during the window.
    std::string m_diagName;
    std::chrono::steady_clock::time_point m_paintLogWindowStart;
    std::chrono::steady_clock::time_point m_paintLogStartupTime;
    int64_t m_paintDurUsTotal = 0;
    int64_t m_paintDurUsMax = 0;
    int64_t m_paintRotateUsTotal = 0;
    int64_t m_paintBlitUsTotal = 0;
    int64_t m_paintOverlayUsTotal = 0;
    int m_paintWindowCount = 0;
    int m_paintChartActiveCount = 0;
    int m_paintVisionActiveCount = 0;
    int m_paintBeamActiveCount = 0;
    int m_paintTrackingActiveCount = 0;
    int m_paintDualCount = 0;

    // Mouse state
    QPoint m_lastMousePos;
    bool m_mouseInWidget = false;

    // Chart overlay state
    bool m_chartOverlayEnabled = false;
    float m_chartOverlayScale = 1.0f;
    std::vector<ChartChannelSnapshot> m_chartSnapshots;

    // Chart overlay cache. The chart is expensive (~7-10 ms with antialiased
    // polylines on 200-pt deques), so re-rendering it on every camera frame
    // dominates paintEvent cost. Cache the rasterized chart into a pixmap and
    // only rebuild it when data, scale, or geometry change. Camera frames
    // then just drawPixmap the cached panel (sub-millisecond).
    QPixmap m_chartCachePixmap;
    bool    m_chartCacheDirty = true;

#ifdef HAS_OPENCV
    // Vision overlay state
    bool m_visionOverlayEnabled = false;
    VisionResult m_visionResult;
    std::string m_visionProfileName;
#endif

    // Device tracking overlay state
    DeviceTrackingOverlayData m_deviceTracking;

    // Beam overlay state
    bool m_beamOverlayEnabled = false;
    nlohmann::json m_beamResult;
    double m_beamFps = 0.0;
    double m_beamUmPerPxX = 0.25, m_beamUmPerPxY = 0.25;

    // Helpers
    QRect calcDestRect(const QRect& area, int imgW, int imgH) const;
    QImage applyRotation(const QImage& src, int degrees) const;
    void drawFpsOverlay(QPainter& p, const QRect& widgetRect);
    void drawCrosshair(QPainter& p, const QRect& destRect);
    void drawTrackingCrosshair(QPainter& p, const QRect& destRect);
    void drawDistanceText(QPainter& p);
    QPoint screenToImage(const QPoint& screenPt, const QRect& destRect,
                         int imgW, int imgH) const;

    // Overlay draw methods
    void drawChartOverlay(QPainter& p, const QRect& widgetRect);
    // Chart panel rect (top-right area inside the widget). Depends only on
    // widgetRect and m_chartOverlayScale — independent of data, so it's safe
    // to use to size the chart cache pixmap.
    QRect chartOverlayRect(const QRect& widgetRect) const;
    // Render the chart into the cache pixmap. Called eagerly from
    // setChartOverlayData (on the UI thread, but outside paintEvent) so that
    // when paintEvent runs, the cached pixmap is already up-to-date and the
    // paint cost stays at "blit only". Also called from drawChartOverlayCached
    // as a fallback when the pixmap size is stale (after resize).
    void rebuildChartCachePixmap();
    // Blit the cached chart pixmap onto `p`. Will rebuild if size is stale.
    void drawChartOverlayCached(QPainter& p, const QRect& widgetRect);
#ifdef HAS_OPENCV
    void drawVisionOverlay(QPainter& p);
#endif
    void drawBeamOverlay(QPainter& p, const QRect& widgetRect);
    void drawDeviceTrackingOverlay(QPainter& p, const QRect& widgetRect);

    // Chart helpers
    static QColor chartChannelColor(int index);
    static QString formatChartValue(double value);
};
