#include "CameraImageWidget.h"
#include <cstring>
#include <iostream>

CameraImageWidget::CameraImageWidget(QWidget* parent)
  : QWidget(parent)
  , m_displayLabel(nullptr)
  , m_zoomPercent(100)
  , m_scalingMode(ScalingMode::Fast)
{
  // Create layout with no margins or spacing
  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  // Create display label
  m_displayLabel = new QLabel("No camera feed");
  m_displayLabel->setAlignment(Qt::AlignCenter);
  m_displayLabel->setScaledContents(false);

  // Make label expand to fill available space
  m_displayLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  m_displayLabel->setStyleSheet(
    "QLabel { "
    "  background-color: #2b2b2b; "
    "  color: #888888; "
    "  border: 1px solid #555555; "
    "}");

  layout->addWidget(m_displayLabel);
  setLayout(layout);

  std::cout << "[CameraImageWidget] Created" << std::endl;
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

  if (zoom < 100) {
    displayImage = applyZoom(m_reusableBuffer);
  }
  else {
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
  m_reusableBuffer = QImage();

  std::cout << "[CameraImageWidget] Display cleared" << std::endl;
}

void CameraImageWidget::setZoomPercent(int percent)
{
  // Clamp to valid range
  if (percent < 1) percent = 1;
  if (percent > 100) percent = 100;

  m_zoomPercent.store(percent);
}

void CameraImageWidget::setMinimumDisplaySize(int width, int height)
{
  m_displayLabel->setMinimumSize(width, height);
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

  if (zoom >= 100) {
    return source;
  }

  // Calculate centered ROI
  double zoomFactor = zoom / 100.0;
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

  // Get widget size for proper scaling
  QSize widgetSize = this->size();

  // Make sure we have a valid size
  if (widgetSize.width() > 0 && widgetSize.height() > 0) {
    // Scale to 90% of widget size to add uniform padding on all sides
    QSize targetSize(
      static_cast<int>(widgetSize.width() * 0.9),
      static_cast<int>(widgetSize.height() * 0.9)
    );

    pixmap = pixmap.scaled(targetSize, Qt::KeepAspectRatio, transformMode);
  }

  m_displayLabel->setPixmap(pixmap);
}

void CameraImageWidget::resizeEvent(QResizeEvent* event)
{
  QWidget::resizeEvent(event);

  // Re-render current frame at new size
  if (!m_reusableBuffer.isNull()) {
    renderFrame(m_reusableBuffer);
  }
}