#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLTexture>
#include <QImage>
#include <QColor>
#include <QPoint>
#include <QMutex>
#include <QPainter>
#include "CameraFrameData.h"
#include <atomic>
#include <memory>

/**
 * @brief GPU-accelerated camera image viewport widget using OpenGL
 *
 * Drop-in replacement for CameraImageWidget with GPU rendering:
 * - OpenGL texture upload (fast GPU memory transfer)
 * - GPU-accelerated scaling and display
 * - Same API as CameraImageWidget for easy swapping
 *
 * Usage:
 *   // Replace CameraImageWidget with CameraImageWidgetGL
 *   CameraImageWidgetGL* viewport = new CameraImageWidgetGL();
 *   viewport->setFrame(frameData);  // Update with new frame
 */
class CameraImageWidgetGL : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit CameraImageWidgetGL(QWidget* parent = nullptr);
    ~CameraImageWidgetGL() override;

    // Main API - set frame to display (same as CameraImageWidget)
    void setFrame(const CameraFrameData& frameData);

    // Clear display
    void clearDisplay();

    // Zoom control (optional)
    void setZoomPercent(int percent);  // 100 = no zoom, 200 = 2x zoom, 400 = 4x zoom
    int getZoomPercent() const { return m_zoomPercent.load(); }

    // Crosshair overlay
    void setCrosshairEnabled(bool enabled);
    bool isCrosshairEnabled() const { return m_crosshairEnabled.load(); }
    void setCrosshairColor(const QColor& color);

    // Display properties
    void setMinimumDisplaySize(int width, int height);
    void setBackgroundColor(const QColor& color);
    void setPlaceholderText(const QString& text);

    // Scaling mode (kept for API compatibility, but GPU always uses linear filtering)
    enum class ScalingMode {
        Fast,       // GL_NEAREST (pixelated, fastest)
        Smooth      // GL_LINEAR (smooth, default)
    };
    void setScalingMode(ScalingMode mode);

    // Force refresh - useful when page is shown
    void forceRefresh();

    // Reset pan position to center
    void resetPan();

signals:
    void frameDisplayed();  // Emitted after frame is rendered
    void renderError(QString error);

protected:
    // OpenGL overrides
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

    // Event overrides
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    // Texture management
    std::unique_ptr<QOpenGLTexture> m_texture;
    QMutex m_textureMutex;

    // Frame buffer (CPU side before upload)
    QImage m_frameBuffer;
    bool m_frameReady = false;
    int m_frameWidth = 0;
    int m_frameHeight = 0;
    int m_frameChannels = 1;

    // Display state
    std::atomic<int> m_zoomPercent{100};
    std::atomic<bool> m_crosshairEnabled{false};
    QColor m_crosshairColor{Qt::green};
    QColor m_backgroundColor{45, 45, 48};  // #2d2d30
    QString m_placeholderText{"No camera feed"};
    ScalingMode m_scalingMode{ScalingMode::Smooth};

    // Pan/zoom state
    float m_panX = 0.0f;
    float m_panY = 0.0f;
    bool m_isPanning = false;
    QPoint m_lastPanPos;

    // OpenGL state
    bool m_glInitialized = false;

    // Helper methods (use QPainter for GPU-accelerated rendering)
    void drawFrame(QPainter& painter);
    void drawCrosshair(QPainter& painter);
    void drawPlaceholder(QPainter& painter);
};
