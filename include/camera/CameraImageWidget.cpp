#include "CameraImageWidget.h"
#include <cstring>
#include <iostream>
#include <QPainter>
#include <QPaintEvent>
#include <QShowEvent>
#include <QMouseEvent>

CameraImageWidget::CameraImageWidget(QWidget* parent)
  : QWidget(parent)
  , m_scrollArea(nullptr)
  , m_displayLabel(nullptr)
  , m_zoomPercent(100)
  , m_scalingMode(ScalingMode::Fast)
  , m_crosshairEnabled(false)
  , m_crosshairColor(Qt::green)
  , m_isPanning(false)
{
  // Performance optimizations - reduce repaints and improve rendering
  setAttribute(Qt::WA_OpaquePaintEvent, true);  // No need to erase background
  setAttribute(Qt::WA_NoSystemBackground, true); // Skip system background paint

  // Create layout with no margins or spacing
  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  // Create scroll area for panning and scrollbars
  m_scrollArea = new QScrollArea(this);
  m_scrollArea->setWidgetResizable(false);  // We control the size
  m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  m_scrollArea->setFrameShape(QFrame::NoFrame);
  m_scrollArea->setStyleSheet(
    "QScrollArea { "
    "  background-color: #1e1e1e; "
    "  border: none; "
    "} "
    "QScrollBar:horizontal { "
    "  background-color: #2d2d30; "
    "  height: 12px; "
    "  margin: 0; "
    "} "
    "QScrollBar::handle:horizontal { "
    "  background-color: #5a5a5a; "
    "  min-width: 30px; "
    "  border-radius: 4px; "
    "  margin: 2px; "
    "} "
    "QScrollBar::handle:horizontal:hover { "
    "  background-color: #6a6a6a; "
    "} "
    "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { "
    "  width: 0; "
    "} "
    "QScrollBar:vertical { "
    "  background-color: #2d2d30; "
    "  width: 12px; "
    "  margin: 0; "
    "} "
    "QScrollBar::handle:vertical { "
    "  background-color: #5a5a5a; "
    "  min-height: 30px; "
    "  border-radius: 4px; "
    "  margin: 2px; "
    "} "
    "QScrollBar::handle:vertical:hover { "
    "  background-color: #6a6a6a; "
    "} "
    "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { "
    "  height: 0; "
    "}"
  );

  // Create display label inside scroll area
  m_displayLabel = new QLabel();
  m_displayLabel->setText("No camera feed");
  m_displayLabel->setAlignment(Qt::AlignCenter);
  m_displayLabel->setScaledContents(false);
  m_displayLabel->setStyleSheet(
    "QLabel { "
    "  background-color: #1e1e1e; "
    "  color: #888888; "
    "}");

  m_scrollArea->setWidget(m_displayLabel);
  layout->addWidget(m_scrollArea);
  setLayout(layout);

  // Set minimum size for the widget itself
  setMinimumSize(320, 240);

  // Set cursor to indicate panning is available
  setCursor(Qt::OpenHandCursor);

  std::cout << "[CameraImageWidget] Created with scrolling and panning support" << std::endl;
}

void CameraImageWidget::setFrame(const CameraFrameData& frameData)
{
  // Validate frame
  if (!frameData.IsValid() || frameData.imageData.empty()) {
    emit renderError("Invalid frame data");
    return;
  }

  // Update reusable buffer with new frame data
  if (!updateReusableBuffer(frameData)) {
    emit renderError("Failed to update image buffer");
    return;
  }

  // Apply zoom if needed
  QImage displayImage;
  int zoom = m_zoomPercent.load();

  if (zoom > 100) {
    // Zoom in - crop and magnify
    displayImage = applyZoom(m_reusableBuffer);
  }
  else {
    // 100% - show full image
    displayImage = m_reusableBuffer;
  }

  // Render to display
  renderFrame(displayImage);

  emit frameDisplayed();
}

void CameraImageWidget::clearDisplay()
{
  m_displayLabel->clear();
  m_displayLabel->setText("No camera feed");
  m_displayLabel->resize(320, 240);  // Reset to minimum size
  m_reusableBuffer = QImage();
  resetPan();

  std::cout << "[CameraImageWidget] Display cleared" << std::endl;
}

void CameraImageWidget::setZoomPercent(int percent)
{
  // Clamp to valid range (100% to 400%)
  if (percent < 100) percent = 100;
  if (percent > 400) percent = 400;

  m_zoomPercent.store(percent);

  // Re-render current frame with new zoom level
  if (!m_reusableBuffer.isNull()) {
    QImage displayImage;
    int zoom = m_zoomPercent.load();

    if (zoom > 100) {
      // Zoom in - crop and magnify
      displayImage = applyZoom(m_reusableBuffer);
    }
    else {
      // 100% - show full image
      displayImage = m_reusableBuffer;
    }

    renderFrame(displayImage);
  }
}

void CameraImageWidget::setMinimumDisplaySize(int width, int height)
{
  setMinimumSize(width, height);
}

void CameraImageWidget::setBackgroundColor(const QColor& color)
{
  QString style = QString(
    "QLabel { "
    "  background-color: %1; "
    "  color: #888888; "
    "  border: 1px solid #555555; "
    "}").arg(color.name());

  m_displayLabel->setStyleSheet(style);
}

void CameraImageWidget::setPlaceholderText(const QString& text)
{
  if (m_displayLabel->pixmap().isNull()) {
    m_displayLabel->setText(text);
  }
}

void CameraImageWidget::setScalingMode(ScalingMode mode)
{
  m_scalingMode = mode;
}

void CameraImageWidget::forceRefresh()
{
  // Force update of display label
  if (m_displayLabel) {
    m_displayLabel->update();
    m_displayLabel->repaint();
  }

  // Force update of scroll area
  if (m_scrollArea) {
    m_scrollArea->update();
    m_scrollArea->repaint();
  }

  // Force update of entire widget
  update();
  repaint();

  std::cout << "[CameraImageWidget] Forced refresh" << std::endl;
}

void CameraImageWidget::resetPan()
{
  if (m_scrollArea) {
    // Center the view
    QScrollBar* hBar = m_scrollArea->horizontalScrollBar();
    QScrollBar* vBar = m_scrollArea->verticalScrollBar();

    if (hBar) {
      hBar->setValue((hBar->maximum() + hBar->minimum()) / 2);
    }
    if (vBar) {
      vBar->setValue((vBar->maximum() + vBar->minimum()) / 2);
    }
  }
}

void CameraImageWidget::setCrosshairEnabled(bool enabled)
{
  m_crosshairEnabled.store(enabled);
  // Re-render current frame to show/hide crosshair
  if (!m_reusableBuffer.isNull()) {
    QImage displayImage;
    int zoom = m_zoomPercent.load();

    if (zoom > 100) {
      displayImage = applyZoom(m_reusableBuffer);
    }
    else {
      displayImage = m_reusableBuffer;
    }

    renderFrame(displayImage);
  }
}

void CameraImageWidget::setCrosshairColor(const QColor& color)
{
  m_crosshairColor = color;
  // Re-render current frame if crosshair is visible
  if (m_crosshairEnabled.load() && !m_reusableBuffer.isNull()) {
    QImage displayImage;
    int zoom = m_zoomPercent.load();

    if (zoom > 100) {
      displayImage = applyZoom(m_reusableBuffer);
    }
    else {
      displayImage = m_reusableBuffer;
    }

    renderFrame(displayImage);
  }
}

// ============================================================================
// PRIVATE HELPER METHODS
// ============================================================================

bool CameraImageWidget::updateReusableBuffer(const CameraFrameData& frameData)
{
  // Determine format
  QImage::Format format = (frameData.channels == 3)
    ? QImage::Format_RGB888
    : QImage::Format_Grayscale8;

  // Reallocate buffer only if dimensions/format changed
  if (m_reusableBuffer.width() != static_cast<int>(frameData.width) ||
    m_reusableBuffer.height() != static_cast<int>(frameData.height) ||
    m_reusableBuffer.format() != format)
  {
    m_reusableBuffer = QImage(frameData.width, frameData.height, format);

    if (m_reusableBuffer.isNull()) {
      std::cerr << "[CameraImageWidget] Failed to allocate buffer" << std::endl;
      return false;
    }

    std::cout << "[CameraImageWidget] Allocated buffer: "
      << frameData.width << "x" << frameData.height << std::endl;
  }

  // Fast memory copy to reusable buffer
  size_t expectedSize = frameData.width * frameData.height * frameData.channels;
  if (frameData.imageData.size() != expectedSize) {
    std::cerr << "[CameraImageWidget] Frame size mismatch!" << std::endl;
    return false;
  }

  std::memcpy(m_reusableBuffer.bits(),
    frameData.imageData.data(),
    frameData.imageData.size());

  return true;
}

QImage CameraImageWidget::applyZoom(const QImage& source)
{
  int zoom = m_zoomPercent.load();

  if (zoom <= 100) {
    // No zoom - return full image
    return source;
  }

  // Calculate centered ROI for zoom
  // zoom 200% = show 50% of image (1/2)
  // zoom 400% = show 25% of image (1/4)
  double zoomFactor = 100.0 / zoom;  // Inverted: higher zoom = smaller ROI
  int roiWidth = static_cast<int>(source.width() * zoomFactor);
  int roiHeight = static_cast<int>(source.height() * zoomFactor);

  if (roiWidth < 1) roiWidth = 1;
  if (roiHeight < 1) roiHeight = 1;

  int roiX = (source.width() - roiWidth) / 2;
  int roiY = (source.height() - roiHeight) / 2;

  return source.copy(QRect(roiX, roiY, roiWidth, roiHeight));
}

void CameraImageWidget::renderFrame(const QImage& image)
{
  if (image.isNull()) {
    return;
  }

  // Convert to pixmap
  QPixmap pixmap = QPixmap::fromImage(image);

  Qt::TransformationMode transformMode =
    (m_scalingMode == ScalingMode::Fast)
    ? Qt::FastTransformation
    : Qt::SmoothTransformation;

  // Get scroll area viewport size for proper scaling
  QSize viewportSize = m_scrollArea ? m_scrollArea->viewport()->size() : this->size();

  // Make sure we have a valid size
  if (viewportSize.width() > 0 && viewportSize.height() > 0) {
    // Scale to fill container width while maintaining aspect ratio
    // Calculate target width (full viewport width)
    int targetWidth = viewportSize.width();

    // Calculate height based on original aspect ratio
    double aspectRatio = static_cast<double>(image.height()) / static_cast<double>(image.width());
    int targetHeight = static_cast<int>(targetWidth * aspectRatio);

    // Note: We always fill width and let height be proportional
    // Scrollbars will appear if content exceeds viewport

    QSize targetSize(targetWidth, targetHeight);
    pixmap = pixmap.scaled(targetSize, Qt::IgnoreAspectRatio, transformMode);
  }

  // Draw crosshair overlay on pixmap if enabled
  if (m_crosshairEnabled.load() && !pixmap.isNull()) {
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, false);  // Sharp lines

    // Set crosshair pen
    QPen pen(m_crosshairColor);
    pen.setWidth(2);  // Make it visible
    pen.setStyle(Qt::SolidLine);
    painter.setPen(pen);

    // Get pixmap center
    int centerX = pixmap.width() / 2;
    int centerY = pixmap.height() / 2;

    // Draw vertical line (full height)
    painter.drawLine(centerX, 0, centerX, pixmap.height());

    // Draw horizontal line (full width)
    painter.drawLine(0, centerY, pixmap.width(), centerY);

    painter.end();
  }

  // Set pixmap and resize label to match pixmap size for scrolling
  m_displayLabel->setPixmap(pixmap);
  m_displayLabel->resize(pixmap.size());
}

void CameraImageWidget::resizeEvent(QResizeEvent* event)
{
  QWidget::resizeEvent(event);

  // Re-render current frame at new size
  if (!m_reusableBuffer.isNull()) {
    renderFrame(m_reusableBuffer);
  }
}

void CameraImageWidget::paintEvent(QPaintEvent* event)
{
  // Since we have WA_OpaquePaintEvent set, we must paint the entire widget
  // Fill background with dark color to prevent leftover pixels
  QPainter painter(this);
  painter.fillRect(rect(), QColor(45, 45, 48));  // #2d2d30

  // Child widgets (m_displayLabel) will paint themselves on top
  // No need to call QWidget::paintEvent when WA_OpaquePaintEvent is set
}

void CameraImageWidget::showEvent(QShowEvent* event)
{
  QWidget::showEvent(event);

  // Force complete repaint when widget becomes visible
  // This fixes overlay artifacts when switching pages
  if (m_displayLabel) {
    m_displayLabel->update();
    m_displayLabel->repaint();
  }

  // Re-render current frame if we have one (same effect as resize)
  if (!m_reusableBuffer.isNull()) {
    QImage displayImage;
    int zoom = m_zoomPercent.load();

    if (zoom > 100) {
      displayImage = applyZoom(m_reusableBuffer);
    }
    else {
      displayImage = m_reusableBuffer;
    }

    renderFrame(displayImage);
  }

  update();
  repaint();

  std::cout << "[CameraImageWidget] Widget shown - forcing complete repaint" << std::endl;
}

// ============================================================================
// MOUSE EVENT HANDLERS FOR PANNING
// ============================================================================

void CameraImageWidget::mousePressEvent(QMouseEvent* event)
{
  if (event->button() == Qt::LeftButton) {
    m_isPanning = true;
    m_lastPanPos = event->pos();
    setCursor(Qt::ClosedHandCursor);
    event->accept();
  }
  else {
    QWidget::mousePressEvent(event);
  }
}

void CameraImageWidget::mouseMoveEvent(QMouseEvent* event)
{
  if (m_isPanning && m_scrollArea) {
    QPoint delta = event->pos() - m_lastPanPos;
    m_lastPanPos = event->pos();

    // Update scrollbar positions (inverted for natural panning)
    QScrollBar* hBar = m_scrollArea->horizontalScrollBar();
    QScrollBar* vBar = m_scrollArea->verticalScrollBar();

    if (hBar) {
      hBar->setValue(hBar->value() - delta.x());
    }
    if (vBar) {
      vBar->setValue(vBar->value() - delta.y());
    }

    event->accept();
  }
  else {
    QWidget::mouseMoveEvent(event);
  }
}

void CameraImageWidget::mouseReleaseEvent(QMouseEvent* event)
{
  if (event->button() == Qt::LeftButton && m_isPanning) {
    m_isPanning = false;
    setCursor(Qt::OpenHandCursor);
    event->accept();
  }
  else {
    QWidget::mouseReleaseEvent(event);
  }
}