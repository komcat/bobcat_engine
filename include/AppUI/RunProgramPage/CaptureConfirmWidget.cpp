// CaptureConfirmWidget.cpp
#include "CaptureConfirmWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QPixmap>
#include <iostream>

CaptureConfirmWidget::CaptureConfirmWidget(QWidget* parent)
  : QWidget(parent)
{
  setupUI();
}

void CaptureConfirmWidget::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Create centered container
  QWidget* container = new QWidget();
  container->setMaximumWidth(900);
  container->setStyleSheet(
    "QWidget { "
    "  background-color: #1e1e1e; "
    "  border: 2px solid #0e639c; "
    "}");

  QVBoxLayout* containerLayout = new QVBoxLayout(container);
  containerLayout->setContentsMargins(20, 15, 20, 15);
  containerLayout->setSpacing(10);

  // Title
  QLabel* titleLabel = new QLabel("Image Capture Confirmation");
  titleLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 16px; "
    "  font-weight: bold; "
    "  color: #0e639c; "
    "  background: transparent; "
    "  border: none; "
    "}");
  containerLayout->addWidget(titleLabel);

  // Message
  m_messageLabel = new QLabel("Please review the captured image");
  m_messageLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 13px; "
    "  color: #d4d4d4; "
    "  background: transparent; "
    "  border: none; "
    "  padding: 5px 0px; "
    "}");
  m_messageLabel->setWordWrap(true);
  containerLayout->addWidget(m_messageLabel);

  // Image info label (camera name, dimensions)
  m_imageInfoLabel = new QLabel("Camera: N/A | Size: 0 x 0");
  m_imageInfoLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 11px; "
    "  color: #888888; "
    "  background: transparent; "
    "  border: none; "
    "  padding: 2px 0px; "
    "}");
  containerLayout->addWidget(m_imageInfoLabel);

  // Image display label
  m_imageLabel = new QLabel("No image");
  m_imageLabel->setFixedSize(IMAGE_DISPLAY_WIDTH, IMAGE_DISPLAY_HEIGHT);
  m_imageLabel->setAlignment(Qt::AlignCenter);
  m_imageLabel->setStyleSheet(
    "QLabel { "
    "  background-color: #2d2d30; "
    "  border: 1px solid #555555; "
    "  color: #808080; "
    "}");
  m_imageLabel->setScaledContents(false);  // Maintain aspect ratio
  containerLayout->addWidget(m_imageLabel, 0, Qt::AlignHCenter);

  // Action buttons
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(10);
  buttonLayout->addStretch();

  // Continue button (primary action)
  m_continueButton = new QPushButton("Continue");
  m_continueButton->setFixedSize(120, 35);
  m_continueButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #0e639c; "
    "  color: white; "
    "  font-size: 13px; "
    "  font-weight: bold; "
    "  border: none; "
    "  border-radius: 3px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #1177bb; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #0c5280; "
    "}");
  connect(m_continueButton, &QPushButton::clicked, this, &CaptureConfirmWidget::onContinueClicked);
  buttonLayout->addWidget(m_continueButton);

  // Cancel button
  m_cancelButton = new QPushButton("Cancel");
  m_cancelButton->setFixedSize(120, 35);
  m_cancelButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #c9302c; "
    "  color: white; "
    "  font-size: 13px; "
    "  font-weight: bold; "
    "  border: none; "
    "  border-radius: 3px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #d9534f; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #a02622; "
    "}");
  connect(m_cancelButton, &QPushButton::clicked, this, &CaptureConfirmWidget::onCancelClicked);
  buttonLayout->addWidget(m_cancelButton);

  buttonLayout->addStretch();
  containerLayout->addLayout(buttonLayout);

  // Add container to main layout (centered)
  mainLayout->addWidget(container, 0, Qt::AlignCenter);

  // Initially hidden
  hide();
}

void CaptureConfirmWidget::showConfirmation(const CameraFrameData& frameData, const QString& message)
{
  // Update message
  m_messageLabel->setText(message);

  // Update image info
  QString info = QString("Camera: %1 | Size: %2 x %3 | Channels: %4")
    .arg(QString::fromStdString(frameData.cameraId))
    .arg(frameData.width)
    .arg(frameData.height)
    .arg(frameData.channels);
  m_imageInfoLabel->setText(info);

  // Convert frame to QPixmap and display
  QPixmap pixmap = convertFrameToPixmap(frameData);
  if (!pixmap.isNull()) {
    // Scale to fit display area while maintaining aspect ratio
    QPixmap scaledPixmap = pixmap.scaled(
      IMAGE_DISPLAY_WIDTH,
      IMAGE_DISPLAY_HEIGHT,
      Qt::KeepAspectRatio,
      Qt::SmoothTransformation
    );
    m_imageLabel->setPixmap(scaledPixmap);
  } else {
    m_imageLabel->setText("Failed to load image");
    std::cerr << "[CaptureConfirmWidget] Failed to convert frame to pixmap" << std::endl;
  }

  // Show widget
  show();
  raise();

  std::cout << "[CaptureConfirmWidget] Showing confirmation for camera: "
    << frameData.cameraId << " (" << frameData.width << "x" << frameData.height << ")"
    << std::endl;
}

void CaptureConfirmWidget::hideConfirmation()
{
  hide();
  m_imageLabel->clear();
  m_imageLabel->setText("No image");
}

void CaptureConfirmWidget::onContinueClicked()
{
  std::cout << "[CaptureConfirmWidget] User clicked Continue" << std::endl;
  hideConfirmation();
  emit responseReceived(true);
}

void CaptureConfirmWidget::onCancelClicked()
{
  std::cout << "[CaptureConfirmWidget] User clicked Cancel" << std::endl;
  hideConfirmation();
  emit responseReceived(false);
}

QPixmap CaptureConfirmWidget::convertFrameToPixmap(const CameraFrameData& frameData)
{
  if (!frameData.IsValid()) {
    std::cerr << "[CaptureConfirmWidget] Invalid frame data" << std::endl;
    return QPixmap();
  }

  // Create QImage from raw RGB data
  QImage::Format format;
  if (frameData.channels == 3) {
    format = QImage::Format_RGB888;
  } else if (frameData.channels == 1) {
    format = QImage::Format_Grayscale8;
  } else if (frameData.channels == 4) {
    format = QImage::Format_RGBA8888;
  } else {
    std::cerr << "[CaptureConfirmWidget] Unsupported channel count: " << frameData.channels << std::endl;
    return QPixmap();
  }

  // Create QImage (deep copy of data)
  QImage image(
    frameData.imageData.data(),
    frameData.width,
    frameData.height,
    frameData.width * frameData.channels,
    format
  );

  // Convert to QPixmap
  QPixmap pixmap = QPixmap::fromImage(image.copy());

  std::cout << "[CaptureConfirmWidget] Converted frame to pixmap: "
    << frameData.width << "x" << frameData.height
    << " (" << frameData.channels << " channels)"
    << std::endl;

  return pixmap;
}
