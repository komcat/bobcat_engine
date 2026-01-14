#include "CameraImageWidgetGL.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <cstring>
#include <iostream>

CameraImageWidgetGL::CameraImageWidgetGL(QWidget* parent)
    : QOpenGLWidget(parent)
{
    // Set format for better performance
    QSurfaceFormat format;
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    format.setSwapInterval(1);  // VSync
    setFormat(format);

    // Enable mouse tracking for panning
    setMouseTracking(true);
    setCursor(Qt::OpenHandCursor);

    // Set minimum size
    setMinimumSize(320, 240);

    // Set background auto-fill
    setAutoFillBackground(false);

    std::cout << "[CameraImageWidgetGL] Created with OpenGL acceleration" << std::endl;
}

CameraImageWidgetGL::~CameraImageWidgetGL()
{
    makeCurrent();
    m_texture.reset();
    doneCurrent();
}

void CameraImageWidgetGL::initializeGL()
{
    initializeOpenGLFunctions();

    glClearColor(
        m_backgroundColor.redF(),
        m_backgroundColor.greenF(),
        m_backgroundColor.blueF(),
        1.0f
    );

    m_glInitialized = true;

    std::cout << "[CameraImageWidgetGL] OpenGL initialized - Version: "
              << reinterpret_cast<const char*>(glGetString(GL_VERSION)) << std::endl;
}

void CameraImageWidgetGL::resizeGL(int w, int h)
{
    // QPainter handles viewport setup
    glViewport(0, 0, w, h);
}

void CameraImageWidgetGL::paintGL()
{
    // Use QPainter for all rendering - it's GPU accelerated on QOpenGLWidget
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform,
                          m_scalingMode == ScalingMode::Smooth);

    // Fill background
    painter.fillRect(rect(), m_backgroundColor);

    if (!m_frameReady || m_frameBuffer.isNull()) {
        drawPlaceholder(painter);
        painter.end();
        return;
    }

    // Draw the camera frame
    drawFrame(painter);

    // Draw crosshair overlay if enabled
    if (m_crosshairEnabled.load()) {
        drawCrosshair(painter);
    }

    painter.end();

    emit frameDisplayed();
}

void CameraImageWidgetGL::setFrame(const CameraFrameData& frameData)
{
    if (!frameData.IsValid() || frameData.imageData.empty()) {
        emit renderError("Invalid frame data");
        return;
    }

    QMutexLocker locker(&m_textureMutex);

    // Store frame dimensions
    m_frameWidth = static_cast<int>(frameData.width);
    m_frameHeight = static_cast<int>(frameData.height);
    m_frameChannels = static_cast<int>(frameData.channels);

    // Convert to QImage format
    QImage::Format format = (frameData.channels == 3)
        ? QImage::Format_RGB888
        : QImage::Format_Grayscale8;

    // Reallocate buffer only if dimensions changed
    if (m_frameBuffer.width() != m_frameWidth ||
        m_frameBuffer.height() != m_frameHeight ||
        m_frameBuffer.format() != format)
    {
        m_frameBuffer = QImage(m_frameWidth, m_frameHeight, format);
        std::cout << "[CameraImageWidgetGL] Allocated frame buffer: "
                  << m_frameWidth << "x" << m_frameHeight << std::endl;
    }

    // Copy frame data
    std::memcpy(m_frameBuffer.bits(),
                frameData.imageData.data(),
                frameData.imageData.size());

    m_frameReady = true;

    // Request repaint (schedules paintGL call)
    update();
}

void CameraImageWidgetGL::drawFrame(QPainter& painter)
{
    QMutexLocker locker(&m_textureMutex);

    if (m_frameBuffer.isNull()) {
        return;
    }

    // Calculate display rectangle with aspect ratio preservation
    float widgetW = static_cast<float>(width());
    float widgetH = static_cast<float>(height());
    float imageW = static_cast<float>(m_frameWidth);
    float imageH = static_cast<float>(m_frameHeight);

    // Apply zoom
    int zoom = m_zoomPercent.load();
    float zoomFactor = zoom / 100.0f;

    // Calculate scaled image size (fill width, preserve aspect)
    float aspectRatio = imageH / imageW;
    float displayW = widgetW * zoomFactor;
    float displayH = displayW * aspectRatio;

    // Center the image with pan offset
    float x = (widgetW - displayW) / 2.0f + m_panX;
    float y = (widgetH - displayH) / 2.0f + m_panY;

    QRectF targetRect(x, y, displayW, displayH);

    // Draw the image - QPainter on QOpenGLWidget uses GPU
    painter.drawImage(targetRect, m_frameBuffer);
}

void CameraImageWidgetGL::drawCrosshair(QPainter& painter)
{
    float centerX = width() / 2.0f;
    float centerY = height() / 2.0f;

    QPen pen(m_crosshairColor);
    pen.setWidth(2);
    painter.setPen(pen);

    // Vertical line
    painter.drawLine(QPointF(centerX, 0), QPointF(centerX, height()));

    // Horizontal line
    painter.drawLine(QPointF(0, centerY), QPointF(width(), centerY));
}

void CameraImageWidgetGL::drawPlaceholder(QPainter& painter)
{
    painter.setPen(QColor(136, 136, 136));  // #888888
    painter.setFont(QFont("Arial", 12));
    painter.drawText(rect(), Qt::AlignCenter, m_placeholderText);
}

void CameraImageWidgetGL::clearDisplay()
{
    QMutexLocker locker(&m_textureMutex);

    m_frameBuffer = QImage();
    m_frameReady = false;
    m_frameWidth = 0;
    m_frameHeight = 0;

    makeCurrent();
    m_texture.reset();
    doneCurrent();

    resetPan();
    update();

    std::cout << "[CameraImageWidgetGL] Display cleared" << std::endl;
}

void CameraImageWidgetGL::setZoomPercent(int percent)
{
    if (percent < 100) percent = 100;
    if (percent > 400) percent = 400;

    m_zoomPercent.store(percent);
    update();
}

void CameraImageWidgetGL::setCrosshairEnabled(bool enabled)
{
    m_crosshairEnabled.store(enabled);
    update();
}

void CameraImageWidgetGL::setCrosshairColor(const QColor& color)
{
    m_crosshairColor = color;
    if (m_crosshairEnabled.load()) {
        update();
    }
}

void CameraImageWidgetGL::setMinimumDisplaySize(int w, int h)
{
    setMinimumSize(w, h);
}

void CameraImageWidgetGL::setBackgroundColor(const QColor& color)
{
    m_backgroundColor = color;

    if (m_glInitialized) {
        makeCurrent();
        glClearColor(color.redF(), color.greenF(), color.blueF(), 1.0f);
        doneCurrent();
    }

    update();
}

void CameraImageWidgetGL::setPlaceholderText(const QString& text)
{
    m_placeholderText = text;
    if (!m_frameReady) {
        update();
    }
}

void CameraImageWidgetGL::setScalingMode(ScalingMode mode)
{
    m_scalingMode = mode;
    update();
}

void CameraImageWidgetGL::forceRefresh()
{
    update();
    repaint();
    std::cout << "[CameraImageWidgetGL] Forced refresh" << std::endl;
}

void CameraImageWidgetGL::resetPan()
{
    m_panX = 0.0f;
    m_panY = 0.0f;
    update();
}

// ============================================================================
// MOUSE EVENT HANDLERS FOR PANNING
// ============================================================================

void CameraImageWidgetGL::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_isPanning = true;
        m_lastPanPos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    } else {
        QOpenGLWidget::mousePressEvent(event);
    }
}

void CameraImageWidgetGL::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isPanning) {
        QPoint delta = event->pos() - m_lastPanPos;
        m_lastPanPos = event->pos();

        m_panX += delta.x();
        m_panY += delta.y();

        update();
        event->accept();
    } else {
        QOpenGLWidget::mouseMoveEvent(event);
    }
}

void CameraImageWidgetGL::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_isPanning) {
        m_isPanning = false;
        setCursor(Qt::OpenHandCursor);
        event->accept();
    } else {
        QOpenGLWidget::mouseReleaseEvent(event);
    }
}

void CameraImageWidgetGL::wheelEvent(QWheelEvent* event)
{
    // Scroll wheel zoom
    int delta = event->angleDelta().y();
    int currentZoom = m_zoomPercent.load();

    if (delta > 0) {
        // Zoom in
        setZoomPercent(currentZoom + 25);
    } else if (delta < 0) {
        // Zoom out
        setZoomPercent(currentZoom - 25);
    }

    event->accept();
}
