// CameraView.cpp - Tab view using pub-sub pattern
#include "CameraView.h"
#include "CameraManager.h"
#include "LiveVideoSubscriber.h"
#include <QGroupBox>
#include <QMessageBox>
#include <iostream>
#include <cstring>

CameraView::CameraView(CameraManager* cameraManager,
  const std::string& cameraId,
  const std::string& displayName,
  QWidget* parent)
  : QWidget(parent)
  , m_cameraManager(cameraManager)
  , m_cameraId(cameraId)
  , m_displayName(displayName)
  , m_isConnected(false)
  , m_displayUpdateTimer(nullptr)
  , m_zoomPercent(100)
  , m_frameCounter(0)
  , m_avgProcessingTime(0.0)
{
  setupUI();
  std::cout << "[CameraView] Created for camera: " << m_cameraId << std::endl;
}

CameraView::~CameraView()
{
  std::cout << "[CameraView] Destructor for: " << m_cameraId << std::endl;
  disconnectCamera();
}

void CameraView::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  // Status label
  m_statusLabel = new QLabel(QString("Camera: %1 - Disconnected")
    .arg(QString::fromStdString(m_displayName)));
  m_statusLabel->setStyleSheet(
    "QLabel { background-color: #ff4444; color: white; padding: 5px; font-weight: bold; }");
  mainLayout->addWidget(m_statusLabel);

  // Performance info
  m_performanceLabel = new QLabel("Performance: --");
  m_performanceLabel->setStyleSheet(
    "QLabel { background-color: #333333; color: #00ff00; padding: 3px; font-family: monospace; }");
  mainLayout->addWidget(m_performanceLabel);

  // Video display area
  m_videoLabel = new QLabel("No camera feed");
  m_videoLabel->setMinimumSize(640, 480);
  m_videoLabel->setAlignment(Qt::AlignCenter);
  m_videoLabel->setStyleSheet(
    "QLabel { background-color: #2b2b2b; color: #888888; border: 2px solid #555555; }");
  m_videoLabel->setScaledContents(false);
  mainLayout->addWidget(m_videoLabel);

  // Zoom controls
  QGroupBox* zoomGroup = new QGroupBox("Digital Zoom");
  QHBoxLayout* zoomLayout = new QHBoxLayout();

  m_zoomLabel = new QLabel("Zoom: 100%");
  m_zoomLabel->setMinimumWidth(100);
  m_zoomLabel->setStyleSheet("QLabel { font-weight: bold; }");
  zoomLayout->addWidget(m_zoomLabel);

  m_zoomSlider = new QSlider(Qt::Horizontal);
  m_zoomSlider->setMinimum(1);
  m_zoomSlider->setMaximum(100);
  m_zoomSlider->setValue(100);
  m_zoomSlider->setTickPosition(QSlider::TicksBelow);
  m_zoomSlider->setTickInterval(10);
  m_zoomSlider->setEnabled(false);
  connect(m_zoomSlider, &QSlider::valueChanged, this, &CameraView::onZoomSliderChanged);
  zoomLayout->addWidget(m_zoomSlider);

  m_zoomSpinBox = new QSpinBox();
  m_zoomSpinBox->setMinimum(1);
  m_zoomSpinBox->setMaximum(100);
  m_zoomSpinBox->setValue(100);
  m_zoomSpinBox->setSuffix("%");
  m_zoomSpinBox->setEnabled(false);
  connect(m_zoomSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
    this, &CameraView::onZoomSpinBoxChanged);
  zoomLayout->addWidget(m_zoomSpinBox);

  m_zoomResetButton = new QPushButton("Reset");
  m_zoomResetButton->setEnabled(false);
  connect(m_zoomResetButton, &QPushButton::clicked, this, &CameraView::onZoomResetClicked);
  zoomLayout->addWidget(m_zoomResetButton);

  zoomGroup->setLayout(zoomLayout);
  mainLayout->addWidget(zoomGroup);

  // FPS label
  m_fpsLabel = new QLabel("FPS: --");
  m_fpsLabel->setStyleSheet(
    "QLabel { background-color: #444444; color: white; padding: 3px; }");
  mainLayout->addWidget(m_fpsLabel);

  // Control buttons
  QHBoxLayout* buttonLayout = new QHBoxLayout();

  m_connectButton = new QPushButton("Connect");
  m_connectButton->setEnabled(true);
  connect(m_connectButton, &QPushButton::clicked, this, &CameraView::onConnectClicked);

  m_disconnectButton = new QPushButton("Disconnect");
  m_disconnectButton->setEnabled(false);
  connect(m_disconnectButton, &QPushButton::clicked, this, &CameraView::onDisconnectClicked);

  buttonLayout->addWidget(m_connectButton);
  buttonLayout->addWidget(m_disconnectButton);
  mainLayout->addLayout(buttonLayout);

  setLayout(mainLayout);
}

bool CameraView::connectCamera()
{
  if (m_isConnected) {
    return true;
  }

  std::cout << "[CameraView] Connecting camera: " << m_cameraId << std::endl;

  // Connect to camera via manager
  if (!m_cameraManager->ConnectCamera(m_cameraId)) {
    QString error = QString("Failed to connect to camera: %1")
      .arg(QString::fromStdString(m_cameraId));
    emit cameraError(QString::fromStdString(m_cameraId), error);
    return false;
  }

  // Start grabbing
  if (!m_cameraManager->StartGrabbing(m_cameraId)) {
    m_cameraManager->DisconnectCamera(m_cameraId);
    emit cameraError(QString::fromStdString(m_cameraId), "Failed to start grabbing");
    return false;
  }

  // ========================================================================
  // PUB-SUB SETUP
  // ========================================================================

  // Create subscriber
  m_subscriber = std::make_shared<LiveVideoSubscriber>(m_cameraId);
  m_subscriber->InitializeStatusFromManager(m_cameraManager);

  // Subscribe to broadcasts
  m_cameraManager->SubscribeToFrames(m_subscriber);

  std::cout << "[CameraView] Subscribed to camera broadcasts: " << m_cameraId << std::endl;

  // Start display update timer (NOT capture timer!)
  m_displayUpdateTimer = new QTimer(this);
  connect(m_displayUpdateTimer, &QTimer::timeout, this, &CameraView::onUpdateDisplay);
  m_displayUpdateTimer->start(33); // 30 FPS UI polling

  m_lastUpdateTime = std::chrono::steady_clock::now();
  m_frameCounter = 0;

  // Update UI
  updateConnectionStatus(true);
  m_zoomSlider->setEnabled(true);
  m_zoomSpinBox->setEnabled(true);
  m_zoomResetButton->setEnabled(true);

  emit cameraConnected(QString::fromStdString(m_cameraId));
  std::cout << "[CameraView] Camera connected: " << m_cameraId << std::endl;

  return true;
}

void CameraView::disconnectCamera()
{
  if (!m_isConnected) {
    return;
  }

  std::cout << "[CameraView] Disconnecting camera: " << m_cameraId << std::endl;

  // Stop display timer
  if (m_displayUpdateTimer) {
    m_displayUpdateTimer->stop();
    m_displayUpdateTimer->deleteLater();
    m_displayUpdateTimer = nullptr;
  }

  // Unsubscribe
  if (m_subscriber && m_cameraManager) {
    m_cameraManager->UnsubscribeFromFrames(m_subscriber->GetSubscriberId());
    m_subscriber.reset();
  }

  // Disconnect camera
  m_cameraManager->StopGrabbing(m_cameraId);
  m_cameraManager->DisconnectCamera(m_cameraId);

  // Disable zoom controls
  m_zoomSlider->setEnabled(false);
  m_zoomSpinBox->setEnabled(false);
  m_zoomResetButton->setEnabled(false);

  // Update UI
  updateConnectionStatus(false);
  m_videoLabel->setText("No camera feed");
  m_fpsLabel->setText("FPS: --");
  m_performanceLabel->setText("Performance: --");
  updateZoomLabel(100);

  emit cameraDisconnected(QString::fromStdString(m_cameraId));
  std::cout << "[CameraView] Camera disconnected: " << m_cameraId << std::endl;
}

void CameraView::onConnectClicked()
{
  connectCamera();
}

void CameraView::onDisconnectClicked()
{
  disconnectCamera();
}

void CameraView::onUpdateDisplay()
{
  // Check if subscriber has new frame
  if (!m_subscriber || !m_subscriber->HasNewFrame()) {
    return;
  }

  auto frameStart = std::chrono::steady_clock::now();

  // Get frame from subscriber
  CameraFrameData frameData = m_subscriber->GetLatestFrame();
  m_subscriber->MarkFrameConsumed();

  // Convert CameraFrameData to QImage
  if (!frameData.IsValid() || frameData.imageData.empty()) {
    return;
  }

  QImage::Format format = (frameData.channels == 3)
    ? QImage::Format_RGB888
    : QImage::Format_Grayscale8;

  QImage image(frameData.width, frameData.height, format);

  if (image.isNull()) {
    return;
  }

  // Copy frame data
  size_t expectedSize = frameData.width * frameData.height * frameData.channels;
  if (frameData.imageData.size() != expectedSize) {
    return;
  }

  std::memcpy(image.bits(), frameData.imageData.data(), frameData.imageData.size());

  // Apply zoom if needed
  int zoom = m_zoomPercent.load();
  if (zoom < 100) {
    // Calculate ROI for zoom
    double zoomFactor = zoom / 100.0;
    int roiWidth = static_cast<int>(image.width() * zoomFactor);
    int roiHeight = static_cast<int>(image.height() * zoomFactor);

    if (roiWidth < 1) roiWidth = 1;
    if (roiHeight < 1) roiHeight = 1;

    int roiX = (image.width() - roiWidth) / 2;
    int roiY = (image.height() - roiHeight) / 2;

    image = image.copy(QRect(roiX, roiY, roiWidth, roiHeight));
  }

  // Display image
  QPixmap pixmap = QPixmap::fromImage(image);
  pixmap = pixmap.scaled(m_videoLabel->size(),
    Qt::KeepAspectRatio,
    Qt::SmoothTransformation);
  m_videoLabel->setPixmap(pixmap);

  // Performance tracking
  auto frameEnd = std::chrono::steady_clock::now();
  double processingMs = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();
  m_avgProcessingTime = (m_avgProcessingTime * 0.9) + (processingMs * 0.1);
  m_frameCounter++;

  // Update FPS every second
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastUpdateTime).count();

  if (elapsed >= 1) {
    double fps = m_frameCounter / static_cast<double>(elapsed);
    m_fpsLabel->setText(QString("FPS: %1").arg(fps, 0, 'f', 1));
    m_performanceLabel->setText(
      QString("Processing: %1 ms | Total: %2")
      .arg(m_avgProcessingTime, 0, 'f', 2)
      .arg(m_subscriber->GetTotalFramesReceived()));

    m_frameCounter = 0;
    m_lastUpdateTime = now;
  }
}

void CameraView::onZoomSliderChanged(int value)
{
  m_zoomSpinBox->blockSignals(true);
  m_zoomSpinBox->setValue(value);
  m_zoomSpinBox->blockSignals(false);

  m_zoomPercent.store(value);
  updateZoomLabel(value);
}

void CameraView::onZoomSpinBoxChanged(int value)
{
  m_zoomSlider->blockSignals(true);
  m_zoomSlider->setValue(value);
  m_zoomSlider->blockSignals(false);

  m_zoomPercent.store(value);
  updateZoomLabel(value);
}

void CameraView::onZoomResetClicked()
{
  m_zoomSlider->setValue(100);
  m_zoomSpinBox->setValue(100);
}

void CameraView::updateZoomLabel(int zoomPercent)
{
  double zoomFactor = 100.0 / zoomPercent;
  m_zoomLabel->setText(QString("Zoom: %1% (%2x)")
    .arg(zoomPercent)
    .arg(zoomFactor, 0, 'f', 1));
}

void CameraView::updateConnectionStatus(bool connected)
{
  m_isConnected = connected;

  if (connected) {
    m_statusLabel->setText(QString("Camera: %1 - Connected (Pub-Sub)")
      .arg(QString::fromStdString(m_displayName)));
    m_statusLabel->setStyleSheet(
      "QLabel { background-color: #44ff44; color: black; padding: 5px; font-weight: bold; }");
    m_connectButton->setEnabled(false);
    m_disconnectButton->setEnabled(true);
  }
  else {
    m_statusLabel->setText(QString("Camera: %1 - Disconnected")
      .arg(QString::fromStdString(m_displayName)));
    m_statusLabel->setStyleSheet(
      "QLabel { background-color: #ff4444; color: white; padding: 5px; font-weight: bold; }");
    m_connectButton->setEnabled(true);
    m_disconnectButton->setEnabled(false);
  }
}