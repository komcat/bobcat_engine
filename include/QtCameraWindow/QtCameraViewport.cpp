// QtCameraViewport.cpp
#include "QtCameraViewport.h"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QTransform>
#include <QFont>
#include <QString>
#include <QStringList>
#include <QList>

#include <cstring>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <limits>

// ============================================================================
// Constructor
// ============================================================================

QtCameraViewport::QtCameraViewport(QWidget* parent)
    : QWidget(parent)
    , m_fpsTimestamp(std::chrono::steady_clock::now())
    , m_paintLogWindowStart(std::chrono::steady_clock::now())
    , m_paintLogStartupTime(std::chrono::steady_clock::now())
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
    setMinimumSize(320, 240);

    // Dark background palette so areas outside the image look correct even
    // before the first paintEvent fills the rect.
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);
    setAutoFillBackground(false);  // we fill manually in paintEvent
}

// ============================================================================
// Frame updates
// ============================================================================

void QtCameraViewport::setFrame(const uint8_t* rgb, int width, int height)
{
    if (width <= 0 || height <= 0 || rgb == nullptr) {
        return;
    }

    // Reallocate only when dimensions change to avoid repeated heap allocation.
    if (m_frameW != width || m_frameH != height) {
        m_frameImage = QImage(width, height, QImage::Format_RGB888);
        m_frameW = width;
        m_frameH = height;
    }

    std::memcpy(m_frameImage.bits(), rgb, static_cast<std::size_t>(width * height * 3));
    m_hasFrame = true;
    update();
}

void QtCameraViewport::setSecondaryFrame(const uint8_t* rgb, int width, int height)
{
    if (width <= 0 || height <= 0 || rgb == nullptr) {
        return;
    }

    if (m_secondaryW != width || m_secondaryH != height) {
        m_secondaryFrameImage = QImage(width, height, QImage::Format_RGB888);
        m_secondaryW = width;
        m_secondaryH = height;
    }

    std::memcpy(m_secondaryFrameImage.bits(), rgb, static_cast<std::size_t>(width * height * 3));
    m_hasSecondaryFrame = true;
    update();
}

void QtCameraViewport::clearFrame()
{
    m_hasFrame = false;
    m_hasSecondaryFrame = false;
    m_frameImage = QImage{};
    m_secondaryFrameImage = QImage{};
    m_frameW = 0;
    m_frameH = 0;
    m_secondaryW = 0;
    m_secondaryH = 0;
    m_primaryDest = QRect{};
    m_secondaryDest = QRect{};
    update();
}

// ============================================================================
// Display mode setters
// ============================================================================

void QtCameraViewport::setDualMode(bool enabled)
{
    m_dualMode = enabled;
    update();
}

void QtCameraViewport::setDisplayRotation(int degrees)
{
    // Only 0, -90 and +90 are supported.
    if (degrees == 0 || degrees == 90 || degrees == -90) {
        m_displayRotation = degrees;
        update();
    }
}

void QtCameraViewport::setCrosshairEnabled(bool enabled)
{
    m_crosshairEnabled = enabled;
    update();
}

void QtCameraViewport::setShowDistanceEnabled(bool enabled)
{
    m_showDistance = enabled;
    update();
}

void QtCameraViewport::setClickToMoveEnabled(bool enabled)
{
    m_clickToMoveEnabled = enabled;
    setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void QtCameraViewport::setMagnification(double mag)
{
    if (mag > 0.0) {
        m_magnification = mag;
        update();
    }
}

void QtCameraViewport::setCalibrationFactors(double pixToMmX, double pixToMmY,
                                              int refWidth, int refHeight)
{
    m_pixToMmX = pixToMmX;
    m_pixToMmY = pixToMmY;
    m_calibRefWidth  = refWidth;
    m_calibRefHeight = refHeight;
}

void QtCameraViewport::setAcquisitionFps(double fps)
{
    m_acquisitionFps = fps;
    // No update() here — the next frame arrival will trigger one anyway.
}

// ============================================================================
// Overlay data setters
// ============================================================================

void QtCameraViewport::setChartOverlayData(bool enabled, float scale,
                                            const std::vector<ChartChannelSnapshot>& channels)
{
    m_chartOverlayEnabled = enabled;
    m_chartOverlayScale   = scale;
    m_chartSnapshots      = channels;
    m_chartCacheDirty     = true;
    // Rebuild eagerly so paintEvent stays at "blit only" cost. The rebuild
    // runs on the UI thread (via the queued chart-overlay lambda), but
    // outside paintEvent, so camera-frame paints don't pay chart-render cost.
    if (m_chartOverlayEnabled) {
        rebuildChartCachePixmap();
    }
}

#ifdef HAS_OPENCV
void QtCameraViewport::setVisionOverlayData(bool enabled, const VisionResult& result,
                                              const std::string& profileName)
{
    m_visionOverlayEnabled = enabled;
    m_visionResult         = result;
    m_visionProfileName    = profileName;
}
#endif

void QtCameraViewport::setBeamOverlayData(bool enabled, const nlohmann::json& result, double fps,
                                           double umPerPxX, double umPerPxY)
{
    m_beamOverlayEnabled = enabled;
    m_beamResult         = result;
    m_beamFps            = fps;
    m_beamUmPerPxX       = umPerPxX;
    m_beamUmPerPxY       = umPerPxY;
}

void QtCameraViewport::setDeviceTrackingOverlayData(const DeviceTrackingOverlayData& data)
{
    m_deviceTracking = data;
}

// ============================================================================
// Dimension accessors (post-rotation)
// ============================================================================

int QtCameraViewport::displayedFrameWidth() const
{
    if (!m_hasFrame) return 0;
    // After a 90-degree rotation, width and height swap.
    if (m_displayRotation == 90 || m_displayRotation == -90) {
        return m_frameH;
    }
    return m_frameW;
}

int QtCameraViewport::displayedFrameHeight() const
{
    if (!m_hasFrame) return 0;
    if (m_displayRotation == 90 || m_displayRotation == -90) {
        return m_frameW;
    }
    return m_frameH;
}

// ============================================================================
// Helper: aspect-ratio preserving fit
// ============================================================================

QRect QtCameraViewport::calcDestRect(const QRect& area, int imgW, int imgH) const
{
    if (imgW <= 0 || imgH <= 0 || area.width() <= 0 || area.height() <= 0) {
        return area;
    }

    const double frameAspect  = static_cast<double>(imgW) / imgH;
    const double windowAspect = static_cast<double>(area.width()) / area.height();

    int destW, destH;
    if (frameAspect > windowAspect) {
        // Image is wider than the area — fit to width.
        destW = area.width();
        destH = static_cast<int>(area.width() / frameAspect);
    } else {
        // Image is taller than (or same as) the area — fit to height.
        destH = area.height();
        destW = static_cast<int>(area.height() * frameAspect);
    }

    const int destX = area.x() + (area.width()  - destW) / 2;
    const int destY = area.y() + (area.height() - destH) / 2;

    return QRect(destX, destY, destW, destH);
}

// ============================================================================
// Helper: rotation via QTransform
// ============================================================================

QImage QtCameraViewport::applyRotation(const QImage& src, int degrees) const
{
    if (degrees == 0) return src;

    QTransform transform;
    transform.rotate(static_cast<double>(degrees));
    return src.transformed(transform, Qt::SmoothTransformation);
}

// ============================================================================
// paintEvent
// ============================================================================

void QtCameraViewport::paintEvent(QPaintEvent* /*event*/)
{
    const auto paintStart = std::chrono::steady_clock::now();
    QPainter p(this);
    const QRect widgetRect = rect();

    // Fill background black.
    p.fillRect(widgetRect, Qt::black);

    if (!m_hasFrame) {
        // No camera feed — show placeholder text.
        p.setPen(QColor(128, 128, 128));
        p.setFont(QFont("Consolas", 11));
        p.drawText(widgetRect, Qt::AlignCenter, "No camera feed");
        drawFpsOverlay(p, widgetRect);
        return;
    }

    if (m_dualMode) {
        // --- Dual mode: split widget horizontally ---
        const int dividerWidth = 2;
        const int halfW = (widgetRect.width() - dividerWidth) / 2;

        const QRect leftArea(0, 0, halfW, widgetRect.height());
        const QRect rightArea(halfW + dividerWidth, 0, halfW, widgetRect.height());

        // Primary (left)
        {
            const int dispW = displayedFrameWidth();
            const int dispH = displayedFrameHeight();
            m_primaryDest = calcDestRect(leftArea, dispW, dispH);

            const QImage displayed = applyRotation(m_frameImage, m_displayRotation);
            p.drawImage(m_primaryDest, displayed);
        }

        // Divider line
        p.setPen(QPen(Qt::green, dividerWidth));
        p.drawLine(halfW + dividerWidth / 2, 0,
                   halfW + dividerWidth / 2, widgetRect.height());

        // Secondary (right)
        if (m_hasSecondaryFrame) {
            const QRect secDest = calcDestRect(rightArea, m_secondaryW, m_secondaryH);
            m_secondaryDest = secDest;
            p.drawImage(secDest, m_secondaryFrameImage);
        } else {
            m_secondaryDest = QRect{};
            p.setPen(QColor(128, 128, 128));
            p.setFont(QFont("Consolas", 10));
            p.drawText(rightArea, Qt::AlignCenter, "No secondary feed");
        }
    } else {
        // --- Single mode ---
        const int dispW = displayedFrameWidth();
        const int dispH = displayedFrameHeight();
        m_primaryDest = calcDestRect(widgetRect, dispW, dispH);
        m_secondaryDest = QRect{};

        const auto rotStart = std::chrono::steady_clock::now();
        const QImage displayed = applyRotation(m_frameImage, m_displayRotation);
        const auto rotEnd = std::chrono::steady_clock::now();
        p.drawImage(m_primaryDest, displayed);
        const auto blitEnd = std::chrono::steady_clock::now();
        m_paintRotateUsTotal += std::chrono::duration_cast<std::chrono::microseconds>(
                                    rotEnd - rotStart).count();
        m_paintBlitUsTotal   += std::chrono::duration_cast<std::chrono::microseconds>(
                                    blitEnd - rotEnd).count();
    }

    const auto overlayStart = std::chrono::steady_clock::now();

    // --- Overlays ---
    if (m_crosshairEnabled && m_primaryDest.isValid()) {
        drawCrosshair(p, m_primaryDest);
    }

    // Blue tracking crosshair at mouse position when click-to-move is enabled
    if (m_clickToMoveEnabled && m_mouseInWidget && m_primaryDest.isValid()) {
        drawTrackingCrosshair(p, m_primaryDest);
    }

    // Distance overlay: show when showDistance OR clickToMove is active
    if ((m_showDistance || m_clickToMoveEnabled) && m_mouseInWidget && m_primaryDest.isValid()) {
        drawDistanceText(p);
    }

#ifdef HAS_OPENCV
    if (m_visionOverlayEnabled) drawVisionOverlay(p);
#endif
    if (m_beamOverlayEnabled)  drawBeamOverlay(p, widgetRect);
    if (m_chartOverlayEnabled) drawChartOverlayCached(p, widgetRect);

    if (m_deviceTracking.enabled) drawDeviceTrackingOverlay(p, widgetRect);

    drawFpsOverlay(p, widgetRect);

    m_paintOverlayUsTotal += std::chrono::duration_cast<std::chrono::microseconds>(
                                  std::chrono::steady_clock::now() - overlayStart).count();

    // --- Update display FPS counter ---
    ++m_paintCount;
    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - m_fpsTimestamp).count();
    if (elapsed >= 1.0) {
        m_displayFps  = m_paintCount / elapsed;
        m_paintCount  = 0;
        m_fpsTimestamp = now;
    }

    // --- Per-paint cost diagnostics ---
    // Measured AFTER QPainter `p` is still in scope (so we include its work
    // but exclude its destructor flush — close enough for relative comparison).
    const int64_t paintUs = std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - paintStart).count();
    m_paintDurUsTotal += paintUs;
    if (paintUs > m_paintDurUsMax) m_paintDurUsMax = paintUs;
    ++m_paintWindowCount;
    if (m_dualMode) ++m_paintDualCount;
    if (m_chartOverlayEnabled) ++m_paintChartActiveCount;
#ifdef HAS_OPENCV
    if (m_visionOverlayEnabled) ++m_paintVisionActiveCount;
#endif
    if (m_beamOverlayEnabled) ++m_paintBeamActiveCount;
    if (m_deviceTracking.enabled) ++m_paintTrackingActiveCount;

    const int64_t windowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 now - m_paintLogWindowStart).count();
    if (windowMs >= 5000 && m_paintWindowCount > 0) {
        const double seconds  = windowMs / 1000.0;
        const double paintsHz = m_paintWindowCount / seconds;
        const double avgMs    = (static_cast<double>(m_paintDurUsTotal) / m_paintWindowCount) / 1000.0;
        const double maxMs    = m_paintDurUsMax / 1000.0;
        const double avgRotMs     = (static_cast<double>(m_paintRotateUsTotal)  / m_paintWindowCount) / 1000.0;
        const double avgBlitMs    = (static_cast<double>(m_paintBlitUsTotal)    / m_paintWindowCount) / 1000.0;
        const double avgOverlayMs = (static_cast<double>(m_paintOverlayUsTotal) / m_paintWindowCount) / 1000.0;
        const auto upSec = std::chrono::duration_cast<std::chrono::seconds>(
                               now - m_paintLogStartupTime).count();
        std::cout << "[Viewport/" << m_diagName << "] t=" << upSec << "s"
                  << " paints=" << m_paintWindowCount << " (" << paintsHz << "/s)"
                  << " src=" << m_frameW << "x" << m_frameH
                  << " dst=" << m_primaryDest.width() << "x" << m_primaryDest.height()
                  << " rot=" << m_displayRotation
                  << " cost: avg=" << avgMs << "ms max=" << maxMs << "ms"
                  << " phase: rot=" << avgRotMs << "ms blit=" << avgBlitMs
                  << "ms overlays=" << avgOverlayMs << "ms"
                  << " overlays: chart=" << m_paintChartActiveCount
                  << " vision=" << m_paintVisionActiveCount
                  << " beam=" << m_paintBeamActiveCount
                  << " tracking=" << m_paintTrackingActiveCount
                  << " dual=" << m_paintDualCount
                  << std::endl;
        m_paintDurUsTotal = 0;
        m_paintDurUsMax = 0;
        m_paintRotateUsTotal = 0;
        m_paintBlitUsTotal = 0;
        m_paintOverlayUsTotal = 0;
        m_paintWindowCount = 0;
        m_paintChartActiveCount = 0;
        m_paintVisionActiveCount = 0;
        m_paintBeamActiveCount = 0;
        m_paintTrackingActiveCount = 0;
        m_paintDualCount = 0;
        m_paintLogWindowStart = now;
    }
}

// ============================================================================
// drawFpsOverlay
// ============================================================================

void QtCameraViewport::drawFpsOverlay(QPainter& p, const QRect& widgetRect)
{
    // Semi-transparent black background rectangle at bottom-left.
    const int overlayW = 195;
    const int overlayH = 38;
    const int margin   = 5;
    const QRect bgRect(margin,
                       widgetRect.height() - overlayH - margin,
                       overlayW, overlayH);

    p.save();
    p.fillRect(bgRect, QColor(0, 0, 0, 160));

    p.setPen(Qt::white);
    p.setFont(QFont("Consolas", 9));

    const QString line1 = QString("Display:     %1 fps")
                          .arg(m_displayFps, 5, 'f', 1);
    const QString line2 = QString("Acquisition: %1 fps")
                          .arg(m_acquisitionFps, 5, 'f', 1);

    const int textX = bgRect.x() + 5;
    p.drawText(textX, bgRect.y() + 13, line1);
    p.drawText(textX, bgRect.y() + 29, line2);

    p.restore();
}

// ============================================================================
// drawCrosshair
// ============================================================================

void QtCameraViewport::drawCrosshair(QPainter& p, const QRect& destRect)
{
    if (destRect.width() <= 0 || destRect.height() <= 0) return;

    p.save();
    p.setPen(QPen(Qt::green, 1));

    const int cx = destRect.x() + destRect.width()  / 2;
    const int cy = destRect.y() + destRect.height() / 2;

    // Horizontal line spanning full dest width.
    p.drawLine(destRect.left(), cy, destRect.right(), cy);
    // Vertical line spanning full dest height.
    p.drawLine(cx, destRect.top(), cx, destRect.bottom());

    p.restore();
}

// ============================================================================
// drawTrackingCrosshair — blue crosshair following the mouse pointer
// ============================================================================

void QtCameraViewport::drawTrackingCrosshair(QPainter& p, const QRect& destRect)
{
    if (destRect.width() <= 0 || destRect.height() <= 0) return;
    if (!destRect.contains(m_lastMousePos)) return;

    p.save();
    p.setPen(QPen(QColor(0, 150, 255), 1, Qt::DashLine));

    const int mx = m_lastMousePos.x();
    const int my = m_lastMousePos.y();

    // Horizontal line spanning full dest width at mouse Y
    p.drawLine(destRect.left(), my, destRect.right(), my);
    // Vertical line spanning full dest height at mouse X
    p.drawLine(mx, destRect.top(), mx, destRect.bottom());

    // Small solid crosshair at mouse position for visibility
    p.setPen(QPen(QColor(0, 150, 255), 2));
    const int cs = 12;
    p.drawLine(mx - cs, my, mx + cs, my);
    p.drawLine(mx, my - cs, mx, my + cs);

    p.restore();
}

// ============================================================================
// drawDistanceText
// ============================================================================

void QtCameraViewport::drawDistanceText(QPainter& p)
{
    if (!m_hasFrame || m_frameW <= 0 || m_frameH <= 0) return;
    if (!m_primaryDest.isValid()) return;

    const int destW = m_primaryDest.width();
    const int destH = m_primaryDest.height();
    if (destW <= 0 || destH <= 0) return;

    // Check mouse is within the rendered image area.
    if (!m_primaryDest.contains(m_lastMousePos)) return;

    const int cx = m_primaryDest.x() + destW / 2;
    const int cy = m_primaryDest.y() + destH / 2;

    // Pixel offset on screen from image centre to mouse position.
    const int screenOffsetX = m_lastMousePos.x() - cx;
    const int screenOffsetY = m_lastMousePos.y() - cy;

    // Scale screen pixels back to original image pixels.
    const double scaleX = static_cast<double>(m_frameW) / destW;
    const double scaleY = static_cast<double>(m_frameH) / destH;
    const double scaledOffsetX = screenOffsetX * scaleX;
    const double scaledOffsetY = screenOffsetY * scaleY;

    // Effective pixel-to-mm factor, adjusted for calibration reference resolution.
    double effectivePixToMmX = m_pixToMmX;
    double effectivePixToMmY = m_pixToMmY;
    if (m_calibRefWidth > 0 && m_calibRefHeight > 0) {
        effectivePixToMmX = m_pixToMmX * static_cast<double>(m_calibRefWidth)  / m_frameW;
        effectivePixToMmY = m_pixToMmY * static_cast<double>(m_calibRefHeight) / m_frameH;
    }

    // Divide by magnification to get physical distance at the sample plane.
    const double distX_mm = (scaledOffsetX * effectivePixToMmX) / m_magnification;
    const double distY_mm = (scaledOffsetY * effectivePixToMmY) / m_magnification;

    const QString dxText = QString("dX: %1 mm").arg(distX_mm,  +8, 'f', 4);
    const QString dyText = QString("dY: %1 mm").arg(distY_mm,  +8, 'f', 4);

    // Choose text position: offset to bottom-right of cursor, flip if near edges.
    const int textOffsetX = 18;
    const int textOffsetY = 18;
    const int textBoxW = 140;
    const int textBoxH = 38;

    int textX = m_lastMousePos.x() + textOffsetX;
    int textY = m_lastMousePos.y() + textOffsetY;

    // QRect::right() / bottom() are inclusive (last pixel), so compare against
    // right()+1 / bottom()+1 to test whether the box would extend past the edge.
    if (textX + textBoxW > m_primaryDest.right() + 1) {
        textX = m_lastMousePos.x() - textOffsetX - textBoxW;
    }
    if (textY + textBoxH > m_primaryDest.bottom() + 1) {
        textY = m_lastMousePos.y() - textOffsetY - textBoxH;
    }

    p.save();
    p.fillRect(QRect(textX, textY, textBoxW, textBoxH), QColor(0, 0, 0, 160));

    p.setPen(Qt::white);
    p.setFont(QFont("Consolas", 10, QFont::Bold));
    p.drawText(textX + 4, textY + 14, dxText);
    p.drawText(textX + 4, textY + 30, dyText);
    p.restore();
}

// ============================================================================
// screenToImage
// ============================================================================

QPoint QtCameraViewport::screenToImage(const QPoint& screenPt,
                                        const QRect& destRect,
                                        int imgW, int imgH) const
{
    if (destRect.width() <= 0 || destRect.height() <= 0) return {0, 0};

    const double scaleX = static_cast<double>(imgW) / destRect.width();
    const double scaleY = static_cast<double>(imgH) / destRect.height();

    const int imgX = static_cast<int>((screenPt.x() - destRect.x()) * scaleX);
    const int imgY = static_cast<int>((screenPt.y() - destRect.y()) * scaleY);

    return {
        std::clamp(imgX, 0, imgW - 1),
        std::clamp(imgY, 0, imgH - 1)
    };
}

// ============================================================================
// Mouse events
// ============================================================================

void QtCameraViewport::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_hasFrame && m_primaryDest.isValid()) {
        if (m_primaryDest.contains(event->pos())) {
            const QPoint imgPt = screenToImage(event->pos(), m_primaryDest,
                                               m_frameW, m_frameH);
            emit imageClicked(imgPt.x(), imgPt.y());
        }
    }
    QWidget::mousePressEvent(event);
}

void QtCameraViewport::mouseMoveEvent(QMouseEvent* event)
{
    m_mouseInWidget = true;
    m_lastMousePos  = event->pos();

    if (m_hasFrame && m_primaryDest.isValid() && m_primaryDest.contains(event->pos())) {
        const QPoint imgPt = screenToImage(event->pos(), m_primaryDest,
                                           m_frameW, m_frameH);
        emit mouseMoved(imgPt.x(), imgPt.y());
    }

    // Force repaint when tracking crosshair or distance readout is active.
    if (m_showDistance || m_clickToMoveEnabled) {
        update();
    }

    QWidget::mouseMoveEvent(event);
}

void QtCameraViewport::leaveEvent(QEvent* event)
{
    m_mouseInWidget = false;
    update();
    QWidget::leaveEvent(event);
}

void QtCameraViewport::resizeEvent(QResizeEvent* event)
{
    // Dest rects will be recalculated on the next paintEvent; just trigger one.
    // The chart overlay rect depends on widget width, so its cache must be
    // rebuilt at the new geometry.
    m_chartCacheDirty = true;
    update();
    QWidget::resizeEvent(event);
}

// ============================================================================
// Chart helpers
// ============================================================================

QColor QtCameraViewport::chartChannelColor(int index)
{
    static const QColor colors[] = {
        QColor(0, 200, 255),   // cyan
        QColor(255, 220, 0),   // yellow
        QColor(0, 255, 100),   // lime
        QColor(255, 100, 255), // magenta
        QColor(255, 128, 0),   // orange
        QColor(128, 255, 128)  // light green
    };
    return colors[index % 6];
}

QString QtCameraViewport::formatChartValue(double value)
{
    const double absVal = std::fabs(value);
    if (absVal < 1e-9) return QString::asprintf("%.2f pA", value * 1e12);
    if (absVal < 1e-6) return QString::asprintf("%.2f nA", value * 1e9);
    if (absVal < 1e-3) return QString::asprintf("%.3f uA", value * 1e6);
    if (absVal < 1.0)  return QString::asprintf("%.3f mA", value * 1e3);
    return QString::asprintf("%.4f", value);
}

// ============================================================================
// drawChartOverlay — top-right semi-transparent chart panel
// ============================================================================

void QtCameraViewport::drawChartOverlay(QPainter& p, const QRect& widgetRect)
{
    // Filter to channels that actually have data
    std::vector<const ChartChannelSnapshot*> active;
    active.reserve(m_chartSnapshots.size());
    for (const auto& snap : m_chartSnapshots) {
        if (!snap.values.empty()) {
            active.push_back(&snap);
        }
    }

    // If chart is enabled but no channels selected, show a hint
    if (active.empty()) {
        p.save();
        const int hintW = 200, hintH = 30, margin = 6;
        const int hx = widgetRect.right() - hintW - margin;
        const int hy = widgetRect.top() + margin;
        p.fillRect(QRect(hx, hy, hintW, hintH), QColor(20, 20, 30, 180));
        p.setPen(QColor(80, 80, 80, 200));
        p.drawRect(QRect(hx, hy, hintW, hintH));
        p.setPen(QColor(180, 180, 180));
        p.setFont(QFont("Segoe UI", 9));
        p.drawText(QRect(hx, hy, hintW, hintH), Qt::AlignCenter, "Check a channel to plot");
        p.restore();
        return;
    }

    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);

    const float scale = std::max(0.5f, std::min(m_chartOverlayScale, 3.0f));
    const int BASE_W  = 320;
    const int BASE_H  = 180;
    const int chartW  = static_cast<int>(BASE_W * scale);
    const int chartH  = static_cast<int>(BASE_H * scale);
    const int margin  = 6;

    // Position: top-right corner
    const int chartX = widgetRect.right() - chartW - margin;
    const int chartY = widgetRect.top()   + margin;
    const QRect chartRect(chartX, chartY, chartW, chartH);

    // Background and border
    p.fillRect(chartRect, QColor(20, 20, 30, 180));
    p.setPen(QColor(80, 80, 80, 200));
    p.drawRect(chartRect);

    // --- Compute global Y range across all active channels ---
    double yMin =  std::numeric_limits<double>::max();
    double yMax = -std::numeric_limits<double>::max();
    for (const auto* snap : active) {
        for (double v : snap->values) {
            if (v < yMin) yMin = v;
            if (v > yMax) yMax = v;
        }
    }
    if (yMin == yMax) { yMax = yMin + 1.0; }

    // Layout constants (scaled)
    const int legendLineH = static_cast<int>(14 * scale);
    const int legendAreaH = legendLineH * static_cast<int>(active.size());
    const int valueAreaH  = static_cast<int>(22 * scale);
    const int plotPadL    = static_cast<int>(4 * scale);
    const int plotPadR    = static_cast<int>(4 * scale);
    const int plotPadT    = static_cast<int>(4 * scale);

    // Plot area sits between legend (top) and value row (bottom)
    const int plotY    = chartY + plotPadT + legendAreaH;
    const int plotH    = chartH - plotPadT - legendAreaH - valueAreaH;
    const int plotX    = chartX + plotPadL;
    const int plotW    = chartW - plotPadL - plotPadR;

    // Legend font
    QFont legendFont("Segoe UI", static_cast<int>(8 * scale));
    p.setFont(legendFont);

    // --- Draw legend ---
    for (int i = 0; i < static_cast<int>(active.size()); ++i) {
        const QColor color = chartChannelColor(i);
        const int ly = chartY + plotPadT + i * legendLineH;
        const int squareSz = static_cast<int>(9 * scale);

        // Colored square
        p.fillRect(QRect(chartX + plotPadL, ly + 2, squareSz, squareSz), color);

        // Channel name (truncated to 20 chars)
        QString name = QString::fromStdString(active[i]->channelId);
        if (name.length() > 20) name = name.left(17) + "...";
        p.setPen(color);
        p.drawText(chartX + plotPadL + squareSz + static_cast<int>(3 * scale),
                   ly + squareSz, name);
    }

    // --- Draw plot lines (one per channel) ---
    if (plotH > 4 && plotW > 4) {
        p.setRenderHint(QPainter::Antialiasing, true);
        const double yRange = yMax - yMin;

        for (int i = 0; i < static_cast<int>(active.size()); ++i) {
            const auto& vals = active[i]->values;
            if (vals.size() < 2) continue;

            const QColor color = chartChannelColor(i);
            p.setPen(QPen(color, static_cast<int>(1 * scale) + 1));

            const int nPts = static_cast<int>(vals.size());

            QPainterPath path;
            bool firstPt = true;
            for (int j = 0; j < nPts; ++j) {
                const double t = static_cast<double>(j) / (nPts - 1);
                const double x = plotX + t * plotW;
                const double norm = (vals[j] - yMin) / yRange;
                // Flip Y: high values at top
                const double y = plotY + plotH - norm * plotH;

                if (firstPt) {
                    path.moveTo(x, y);
                    firstPt = false;
                } else {
                    path.lineTo(x, y);
                }
            }
            p.drawPath(path);
        }
        p.setRenderHint(QPainter::Antialiasing, false);
    }

    // --- Draw current values at bottom ---
    QFont valueFont("Segoe UI", static_cast<int>(std::max(7.0f, 9.0f * scale)));
    p.setFont(valueFont);
    const int valueY = chartY + chartH - static_cast<int>(4 * scale);
    const int colW   = (plotW > 0 && !active.empty())
                       ? plotW / static_cast<int>(active.size())
                       : plotW;

    for (int i = 0; i < static_cast<int>(active.size()); ++i) {
        if (active[i]->values.empty()) continue;
        const double lastVal = active[i]->values.back();
        const QString valStr = formatChartValue(lastVal);
        const QColor color = chartChannelColor(i);
        p.setPen(color);
        const int vx = plotX + i * colW;
        p.drawText(vx, valueY, valStr);
    }

    p.restore();
}

QRect QtCameraViewport::chartOverlayRect(const QRect& widgetRect) const
{
    const float scale = std::max(0.5f, std::min(m_chartOverlayScale, 3.0f));
    constexpr int BASE_W = 320;
    constexpr int BASE_H = 180;
    constexpr int margin = 6;
    const int chartW = static_cast<int>(BASE_W * scale);
    const int chartH = static_cast<int>(BASE_H * scale);
    return QRect(widgetRect.right() - chartW - margin,
                 widgetRect.top()  + margin,
                 chartW, chartH);
}

void QtCameraViewport::rebuildChartCachePixmap()
{
    const QRect chartRect = chartOverlayRect(rect());
    if (chartRect.width() <= 0 || chartRect.height() <= 0) return;

    if (m_chartCachePixmap.size() != chartRect.size()) {
        m_chartCachePixmap = QPixmap(chartRect.size());
    }
    m_chartCachePixmap.fill(Qt::transparent);
    QPainter cacheP(&m_chartCachePixmap);
    // drawChartOverlay positions itself from widgetRect.right(); translate
    // so widget-coords land at the pixmap origin.
    cacheP.translate(-chartRect.topLeft());
    drawChartOverlay(cacheP, rect());
    m_chartCacheDirty = false;
}

void QtCameraViewport::drawChartOverlayCached(QPainter& p, const QRect& widgetRect)
{
    const QRect chartRect = chartOverlayRect(widgetRect);
    if (chartRect.width() <= 0 || chartRect.height() <= 0) return;

    // Fallback rebuild only when geometry changed (resize) and the eager
    // rebuild in setChartOverlayData hasn't picked up the new size yet, or
    // before the first data push.
    if (m_chartCacheDirty || m_chartCachePixmap.size() != chartRect.size()) {
        rebuildChartCachePixmap();
    }

    p.drawPixmap(chartRect.topLeft(), m_chartCachePixmap);
}

// ============================================================================
// drawBeamOverlay — top-right info panel + geometric overlays
// ============================================================================

void QtCameraViewport::drawBeamOverlay(QPainter& p, const QRect& widgetRect)
{
    if (m_beamResult.empty()) return;
    if (!m_beamResult.contains("beams")) return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    const int margin   = 8;
    const int panelW   = 220;
    const int lineH    = 16;
    const int padX     = 8;
    const int padY     = 6;

    // --- Gather beams array ---
    const auto& beamsArr = m_beamResult["beams"];
    const int nBeams = static_cast<int>(beamsArr.size());

    // Panel height: title + fps + proc time + per-beam rows (4 lines each) + padding
    const int linesPerBeam = 4;
    const int panelH = padY * 2 + lineH + lineH + lineH
                       + nBeams * linesPerBeam * lineH + lineH;

    // Position: top-right (same X as chart would be, but placed at top)
    const int panelX = widgetRect.right() - panelW - margin;
    const int panelY = widgetRect.top() + margin;

    const QRect panelRect(panelX, panelY, panelW, panelH);
    p.fillRect(panelRect, QColor(20, 20, 30, 200));
    p.setPen(QColor(80, 80, 80, 200));
    p.drawRect(panelRect);

    QFont titleFont("Segoe UI", 10, QFont::Bold);
    QFont infoFont("Segoe UI", 8);
    p.setFont(titleFont);

    int ty = panelY + padY + lineH - 2;

    // Title
    p.setPen(QColor(220, 220, 220));
    p.drawText(panelX + padX, ty, "Beam Stream");
    ty += lineH;

    p.setFont(infoFont);

    // FPS
    p.setPen(QColor(180, 180, 180));
    p.drawText(panelX + padX, ty,
               QString("FPS: %1").arg(m_beamFps, 0, 'f', 1));
    ty += lineH;

    // Process time from JSON
    if (m_beamResult.contains("process_time_ms")) {
        double procMs = m_beamResult["process_time_ms"].get<double>();
        p.drawText(panelX + padX, ty,
                   QString("Proc: %1 ms").arg(procMs, 0, 'f', 1));
        ty += lineH;
    } else {
        ty += lineH;
    }

    // --- Per-beam data ---
    const QColor beamColors[] = {
        QColor(0, 255, 120),   // green
        QColor(0, 180, 255),   // sky blue
        QColor(255, 220, 0),   // yellow
        QColor(255, 100, 200)  // pink
    };

    for (int bi = 0; bi < nBeams; ++bi) {
        const auto& bj = beamsArr[bi];
        const QColor bc = beamColors[bi % 4];
        p.setPen(bc);

        // Beam label
        QString label = QString("Beam %1").arg(bi + 1);
        if (bj.contains("name") && bj["name"].is_string()) {
            label = QString::fromStdString(bj["name"].get<std::string>());
        }
        QFont boldInfo("Segoe UI", 8, QFont::Bold);
        p.setFont(boldInfo);
        p.drawText(panelX + padX, ty, label);
        ty += lineH;

        p.setFont(infoFont);
        p.setPen(QColor(200, 200, 200));

        // Centroid position
        if (bj.contains("cx") && bj.contains("cy")) {
            const double cx = bj["cx"].get<double>();
            const double cy = bj["cy"].get<double>();
            p.drawText(panelX + padX + 8, ty,
                       QString("Cen: (%1, %2) px")
                           .arg(cx, 0, 'f', 1)
                           .arg(cy, 0, 'f', 1));
            ty += lineH;
        } else {
            ty += lineH;
        }

        // d4sigma in um (converted from pixels using calibration)
        if (bj.contains("d4sigma_x") && bj.contains("d4sigma_y")) {
            const double d4x = bj["d4sigma_x"].get<double>() * m_beamUmPerPxX;
            const double d4y = bj["d4sigma_y"].get<double>() * m_beamUmPerPxY;
            p.drawText(panelX + padX + 8, ty,
                       QString("d4\xCF\x83: %1 x %2 um")
                           .arg(d4x, 0, 'f', 1)
                           .arg(d4y, 0, 'f', 1));
            ty += lineH;
        } else {
            ty += lineH;
        }

        // Mean intensity
        if (bj.contains("mean_intensity")) {
            const double mi = bj["mean_intensity"].get<double>();
            p.drawText(panelX + padX + 8, ty,
                       QString("Int: %1").arg(mi, 0, 'f', 1));
            ty += lineH;
        } else {
            ty += lineH;
        }
    }

    // --- Geometric overlays on image ---
    if (m_primaryDest.isValid() && m_frameW > 0 && m_frameH > 0) {
        const float scaleX = static_cast<float>(m_primaryDest.width())  / m_frameW;
        const float scaleY = static_cast<float>(m_primaryDest.height()) / m_frameH;

        for (int bi = 0; bi < nBeams; ++bi) {
            const auto& bj = beamsArr[bi];
            if (!bj.contains("cx") || !bj.contains("cy")) continue;

            const QColor bc = beamColors[bi % 4];

            double rawCx = bj["cx"].get<double>();
            double rawCy = bj["cy"].get<double>();

            // Apply rotate180 if flagged in result metadata
            bool rotate180 = false;
            if (m_beamResult.contains("rotate180")) {
                rotate180 = m_beamResult["rotate180"].get<bool>();
            }
            if (rotate180) {
                rawCx = m_frameW - 1 - rawCx;
                rawCy = m_frameH - 1 - rawCy;
            }

            const float sx = static_cast<float>(m_primaryDest.x()) + static_cast<float>(rawCx) * scaleX;
            const float sy = static_cast<float>(m_primaryDest.y()) + static_cast<float>(rawCy) * scaleY;

            // Crosshair at centroid
            const int armLen = 14;
            p.setPen(QPen(bc, 1));
            p.drawLine(QPointF(sx - armLen, sy), QPointF(sx + armLen, sy));
            p.drawLine(QPointF(sx, sy - armLen), QPointF(sx, sy + armLen));

            // Intensity label near centroid (drawn before ellipse so the
            // ellipse stroke doesn't cross it). Placed just below-right of
            // the crosshair with a translucent box for legibility against
            // bright beams.
            if (bj.contains("mean_intensity")) {
                const double mi = bj["mean_intensity"].get<double>();
                QString intText = QString("Int: %1").arg(mi, 0, 'f', 1);

                QFont intFont("Segoe UI", 8, QFont::Bold);
                p.setFont(intFont);

                QFontMetrics fm(intFont);
                const int textW = fm.horizontalAdvance(intText);
                const int textH = fm.height();
                const int boxPad = 3;
                const QRectF box(sx + armLen + 2,
                                 sy + armLen + 2,
                                 textW + boxPad * 2,
                                 textH + boxPad);
                p.fillRect(box, QColor(0, 0, 0, 160));
                p.setPen(bc);
                p.drawText(box.x() + boxPad,
                           box.y() + boxPad + fm.ascent(),
                           intText);
            }

            // Ellipse from d4sigma axes
            if (bj.contains("d4sigma_x") && bj.contains("d4sigma_y")) {
                const double d4sx = bj["d4sigma_x"].get<double>() * scaleX;
                const double d4sy = bj["d4sigma_y"].get<double>() * scaleY;
                const double angle = bj.contains("angle_deg")
                                     ? bj["angle_deg"].get<double>()
                                     : 0.0;

                p.save();
                p.translate(sx, sy);
                p.rotate(angle);
                p.setPen(QPen(bc, 1));
                p.setBrush(Qt::NoBrush);
                // d4sigma is the full width — draw as radii (half-widths)
                p.drawEllipse(QRectF(-d4sx * 0.5, -d4sy * 0.5, d4sx, d4sy));
                p.restore();
            }
        }
    }

    p.restore();
}

// ============================================================================
// drawVisionOverlay — left-side info panel + geometric overlays on image
// ============================================================================

#ifdef HAS_OPENCV
void QtCameraViewport::drawVisionOverlay(QPainter& p)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    // --- Info panel (left side) ---
    const int margin  = 8;
    const int panelW  = 210;
    const int lineH   = 16;
    const int padX    = 8;
    const int padY    = 6;

    // Count detections
    const int nCircles = static_cast<int>(m_visionResult.circles.size());
    const int nPoints  = static_cast<int>(m_visionResult.points.size());
    const int nLines   = static_cast<int>(m_visionResult.lines.size());
    const int nBeams   = static_cast<int>(m_visionResult.beams.size());
    const int nMeas    = static_cast<int>(m_visionResult.measurements.size());

    // Panel height: title + profile + status + time + counts + measurements + padding
    const int panelH = padY * 2
                       + lineH        // title
                       + lineH        // profile
                       + lineH        // status
                       + lineH        // processing time
                       + lineH        // counts line
                       + nMeas * lineH
                       + lineH;       // bottom margin

    const int panelX = margin;
    const int panelY = margin;

    const QRect panelRect(panelX, panelY, panelW, panelH);
    p.fillRect(panelRect, QColor(20, 20, 30, 200));
    p.setPen(QColor(80, 80, 80, 200));
    p.drawRect(panelRect);

    QFont titleFont("Segoe UI", 11, QFont::Bold);
    QFont infoFont("Segoe UI", 8);

    int ty = panelY + padY + lineH - 2;

    // Title
    p.setFont(titleFont);
    p.setPen(QColor(220, 220, 220));
    p.drawText(panelX + padX, ty, "Vision Result");
    ty += lineH;

    p.setFont(infoFont);

    // Profile name (truncate to 28 chars)
    QString profileStr = QString::fromStdString(m_visionProfileName);
    if (profileStr.length() > 28) profileStr = profileStr.left(25) + "...";
    p.setPen(QColor(160, 160, 200));
    p.drawText(panelX + padX, ty, profileStr);
    ty += lineH;

    // Status
    if (m_visionResult.success) {
        p.setPen(QColor(60, 220, 60));
        p.drawText(panelX + padX, ty, "SUCCESS");
    } else {
        p.setPen(QColor(240, 60, 60));
        QString errStr = "FAILED";
        if (!m_visionResult.errorMessage.empty()) {
            errStr += ": " + QString::fromStdString(m_visionResult.errorMessage).left(20);
        }
        p.drawText(panelX + padX, ty, errStr);
    }
    ty += lineH;

    // Processing time
    p.setPen(QColor(180, 180, 180));
    p.drawText(panelX + padX, ty,
               QString("Time: %1 ms").arg(m_visionResult.processingTimeMs, 0, 'f', 1));
    ty += lineH;

    // Detection counts
    QString countsStr;
    if (nCircles > 0) countsStr += QString("%1 cir ").arg(nCircles);
    if (nPoints  > 0) countsStr += QString("%1 pts ").arg(nPoints);
    if (nLines   > 0) countsStr += QString("%1 ln ").arg(nLines);
    if (nBeams   > 0) countsStr += QString("%1 bm ").arg(nBeams);
    if (!countsStr.isEmpty()) {
        p.setPen(QColor(180, 220, 180));
        p.drawText(panelX + padX, ty, countsStr.trimmed());
    }
    ty += lineH;

    // Measurements table
    if (!m_visionResult.measurements.empty()) {
        p.setPen(QColor(180, 180, 180));
        for (const auto& [key, val] : m_visionResult.measurements) {
            const QString kvStr = QString("%1: %2")
                                  .arg(QString::fromStdString(key).left(14))
                                  .arg(val, 0, 'f', 4);
            p.drawText(panelX + padX, ty, kvStr);
            ty += lineH;
        }
    }

    // --- Geometric overlays on image ---
    if (!m_primaryDest.isValid() || m_frameW <= 0 || m_frameH <= 0) {
        p.restore();
        return;
    }

    const float scaleX = static_cast<float>(m_primaryDest.width())  / m_frameW;
    const float scaleY = static_cast<float>(m_primaryDest.height()) / m_frameH;

    // Lambda: image coord -> screen coord
    auto imgToScreen = [&](double ix, double iy) -> QPointF {
        return QPointF(
            m_primaryDest.x() + ix * scaleX,
            m_primaryDest.y() + iy * scaleY
        );
    };

    // --- Green circles with center crosshair ---
    p.setPen(QPen(QColor(0, 230, 0), 2));
    for (const auto& c : m_visionResult.circles) {
        const QPointF center = imgToScreen(c.centerPx.x, c.centerPx.y);
        const double  rx     = c.radiusPx * scaleX;
        const double  ry     = c.radiusPx * scaleY;
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QRectF(center.x() - rx, center.y() - ry, rx * 2, ry * 2));
        // Crosshair
        const int arm = 6;
        p.drawLine(QPointF(center.x() - arm, center.y()),
                   QPointF(center.x() + arm, center.y()));
        p.drawLine(QPointF(center.x(), center.y() - arm),
                   QPointF(center.x(), center.y() + arm));
    }

    // --- Yellow filled points ---
    p.setBrush(QColor(255, 230, 0, 200));
    p.setPen(QPen(QColor(255, 230, 0), 1));
    for (const auto& pt : m_visionResult.points) {
        const QPointF sp = imgToScreen(pt.pixelCoord.x, pt.pixelCoord.y);
        p.drawEllipse(sp, 4.0, 4.0);
    }

    // --- Cyan lines with endpoint markers ---
    p.setPen(QPen(QColor(0, 220, 220), 2));
    p.setBrush(Qt::NoBrush);
    for (const auto& ln : m_visionResult.lines) {
        const QPointF s = imgToScreen(ln.startPx.x, ln.startPx.y);
        const QPointF e = imgToScreen(ln.endPx.x,   ln.endPx.y);
        p.drawLine(s, e);
        // Small markers at endpoints
        p.drawEllipse(s, 3.0, 3.0);
        p.drawEllipse(e, 3.0, 3.0);
    }

    // --- Magenta beam ellipses with center crosshair ---
    for (const auto& b : m_visionResult.beams) {
        const QPointF center = imgToScreen(b.centerPx.x, b.centerPx.y);
        const double  rx     = (b.fwhmMajorPx * 0.5) * scaleX;
        const double  ry     = (b.fwhmMinorPx * 0.5) * scaleY;

        p.save();
        p.translate(center);
        p.rotate(b.angleDeg);
        p.setPen(QPen(QColor(220, 0, 220), 2));
        p.setBrush(Qt::NoBrush);
        if (rx > 0.5 && ry > 0.5) {
            p.drawEllipse(QRectF(-rx, -ry, rx * 2, ry * 2));
        }
        p.restore();

        // Crosshair (not rotated)
        p.setPen(QPen(QColor(220, 0, 220), 1));
        const int arm = 8;
        p.drawLine(QPointF(center.x() - arm, center.y()),
                   QPointF(center.x() + arm, center.y()));
        p.drawLine(QPointF(center.x(), center.y() - arm),
                   QPointF(center.x(), center.y() + arm));
    }

    p.restore();
}
#endif // HAS_OPENCV

// ============================================================================
// drawDeviceTrackingOverlay (bottom-right): live hex device positions in
// gantry frame, expressed as deltas from registered zero point P0.
// ============================================================================

void QtCameraViewport::drawDeviceTrackingOverlay(QPainter& p, const QRect& widgetRect)
{
    const auto& d = m_deviceTracking;

    auto fmt = [](double v) {
        return QString("%1%2")
            .arg(v >= 0 ? "+" : "")
            .arg(QString::number(v, 'f', 4));
    };

    QStringList lines;
    if (d.p0Set) {
        lines << QString("P0: %1, %2").arg(fmt(d.p0X), fmt(d.p0Y));
    } else {
        lines << "P0: not set";
    }
    QList<bool> alertFlags;
    alertFlags << false;  // P0 line never alerts
    if (d.hexLeftActive) {
        lines << QString("hex-L: %1, %2").arg(fmt(d.hexLeftDX), fmt(d.hexLeftDY));
        alertFlags << d.hexLeftAlert;
    } else {
        lines << "hex-L: --";
        alertFlags << false;
    }
    if (d.hexRightActive) {
        lines << QString("hex-R: %1, %2").arg(fmt(d.hexRightDX), fmt(d.hexRightDY));
        alertFlags << d.hexRightAlert;
    } else {
        lines << "hex-R: --";
        alertFlags << false;
    }

    p.save();
    p.setFont(QFont("Consolas", 10));
    const QFontMetrics fm(p.font());
    int textW = 0;
    for (const auto& l : lines) textW = std::max(textW, fm.horizontalAdvance(l));

    const int padding   = 6;
    const int lineH     = fm.height();
    const int overlayW  = textW + padding * 2;
    const int overlayH  = lineH * lines.size() + padding * 2;
    const int margin    = 5;
    const QRect bgRect(widgetRect.right() - overlayW - margin,
                       widgetRect.bottom() - overlayH - margin,
                       overlayW, overlayH);

    p.fillRect(bgRect, QColor(0, 0, 0, 160));
    int y = bgRect.top() + padding + fm.ascent();
    for (int i = 0; i < lines.size(); ++i) {
        p.setPen(alertFlags[i] ? QColor(255, 60, 60) : QColor(255, 220, 100));
        p.drawText(bgRect.left() + padding, y, lines[i]);
        y += lineH;
    }
    p.restore();
}
