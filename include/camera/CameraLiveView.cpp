// CameraLiveView.cpp - Using CameraImageWidget
#include "CameraLiveView.h"
#include "CameraManager.h"
#include "CameraConfigManager.h"
#include "LiveVideoSubscriber.h"
#include "CameraImageWidget.h"  // ✅ ADD THIS
#include <QMessageBox>
#include <QGroupBox>
#include <iostream>

CameraLiveView::CameraLiveView(QWidget* parent)
  : QWidget(parent)
  , m_displayUpdateTimer(nullptr)
  , m_isConnected(false)
  , m_frameCounter(0)
  , m_avgProcessingTime(0.0)
  , m_lastTotalFrames(0)
{
  setupUI();

  m_cameraManager = std::make_unique<CameraManager>();
  m_configManager = std::make_unique<CameraConfigManager>("config/camera_config.json");

  loadCameraConfig();
}

CameraLiveView::~CameraLiveView()
{
  std::cout << "[CameraLiveView] Destructor called" << std::endl;

  if (m_displayUpdateTimer) {
    m_displayUpdateTimer->stop();
  }

  if (m_subscriber && m_cameraManager) {
    m_cameraManager->UnsubscribeFromFrames(m_subscriber->GetSubscriberId());
  }

  if (m_isConnected && m_cameraManager) {
    m_cameraManager->StopGrabbing(m_cameraId);
    m_cameraManager->DisconnectCamera(m_cameraId);
  }

  std::cout << "[CameraLiveView] Destructor completed" << std::endl;
}

void CameraLiveView::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  // Status label
  m_statusLabel = new QLabel("Status: Disconnected");
  m_statusLabel->setStyleSheet(
    "QLabel { background-color: #ff4444; color: white; padding: 5px; font-weight: bold; }");
  mainLayout->addWidget(m_statusLabel);

  // Performance info label
  m_performanceLabel = new QLabel("Performance: --");
  m_performanceLabel->setStyleSheet(
    "QLabel { background-color: #333333; color: #00ff00; padding: 5px; font-family: monospace; }");
  mainLayout->addWidget(m_performanceLabel);

  // ✅ CHANGED: Use CameraImageWidget instead of QLabel
  m_viewport = new CameraImageWidget(this);
  m_viewport->setMinimumDisplaySize(320, 240);  // ← CHANGED from 640x480
  m_viewport->setPlaceholderText("No camera feed");
  mainLayout->addWidget(m_viewport);

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
  connect(m_zoomSlider, &QSlider::valueChanged, this, &CameraLiveView::onZoomSliderChanged);
  zoomLayout->addWidget(m_zoomSlider);

  m_zoomSpinBox = new QSpinBox();
  m_zoomSpinBox->setMinimum(1);
  m_zoomSpinBox->setMaximum(100);
  m_zoomSpinBox->setValue(100);
  m_zoomSpinBox->setSuffix("%");
  m_zoomSpinBox->setEnabled(false);
  connect(m_zoomSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
    this, &CameraLiveView::onZoomSpinBoxChanged);
  zoomLayout->addWidget(m_zoomSpinBox);

  m_zoomResetButton = new QPushButton("Reset (100%)");
  m_zoomResetButton->setEnabled(false);
  connect(m_zoomResetButton, &QPushButton::clicked, this, &CameraLiveView::onZoomResetClicked);
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

  m_connectButton = new QPushButton("Connect Camera");
  m_connectButton->setEnabled(true);
  connect(m_connectButton, &QPushButton::clicked, this, &CameraLiveView::onConnectClicked);

  m_disconnectButton = new QPushButton("Disconnect Camera");
  m_disconnectButton->setEnabled(false);
  connect(m_disconnectButton, &QPushButton::clicked, this, &CameraLiveView::onDisconnectClicked);

  buttonLayout->addWidget(m_connectButton);
  buttonLayout->addWidget(m_disconnectButton);
  mainLayout->addLayout(buttonLayout);

  setLayout(mainLayout);
  setWindowTitle("Camera Live View - Optimized Pub-Sub");
  resize(800, 750);
}

void CameraLiveView::loadCameraConfig()
{
  if (!m_configManager->LoadConfig()) {
    QMessageBox::warning(this, "Config Error",
      "Failed to load camera_config.json. Using default settings.");
    return;
  }

  auto cameraIds = m_configManager->GetEnabledCameraIds();

  if (cameraIds.empty()) {
    QMessageBox::warning(this, "No Cameras",
      "No enabled cameras found in camera_config.json");
    return;
  }

  m_cameraId = cameraIds[0];
  m_configManager->InitializeCameraManager(*m_cameraManager);

  std::cout << "[CameraLiveView] Camera configuration loaded. Camera ID: "
    << m_cameraId << std::endl;
}

void CameraLiveView::onConnectClicked()
{
  if (m_cameraId.empty()) {
    QMessageBox::warning(this, "Error", "No camera configured.");
    return;
  }

  std::cout << "[CameraLiveView] Connecting to camera..." << std::endl;

  if (!m_cameraManager->ConnectCamera(m_cameraId)) {
    QMessageBox::critical(this, "Connection Failed",
      "Failed to connect to camera: " + QString::fromStdString(m_cameraId));
    return;
  }

  if (!m_cameraManager->StartGrabbing(m_cameraId)) {
    QMessageBox::critical(this, "Grabbing Failed",
      "Failed to start video grabbing.");
    m_cameraManager->DisconnectCamera(m_cameraId);
    return;
  }

  std::cout << "[CameraLiveView] Camera connected and grabbing started" << std::endl;

  // PUB-SUB SETUP
  m_subscriber = std::make_shared<LiveVideoSubscriber>(m_cameraId, "CameraLiveView");
  m_subscriber->InitializeStatusFromManager(m_cameraManager.get());
  m_cameraManager->SubscribeToFrames(m_subscriber);

  std::cout << "[CameraLiveView] Subscribed to camera broadcasts (ID: "
    << m_subscriber->GetSubscriberId() << ")" << std::endl;

  // START DISPLAY TIMER
  m_displayUpdateTimer = new QTimer(this);
  m_displayUpdateTimer->setTimerType(Qt::PreciseTimer);
  connect(m_displayUpdateTimer, &QTimer::timeout, this, &CameraLiveView::onUpdateDisplay);
  m_displayUpdateTimer->start(33); // 30 FPS

  m_lastUpdateTime = std::chrono::steady_clock::now();
  m_frameCounter = 0;
  m_lastTotalFrames = 0;

  std::cout << "[CameraLiveView] Display update timer started (33ms interval)" << std::endl;

  // Update UI
  updateConnectionStatus(true);
  m_zoomSlider->setEnabled(true);
  m_zoomSpinBox->setEnabled(true);
  m_zoomResetButton->setEnabled(true);
}

void CameraLiveView::onDisconnectClicked()
{
  std::cout << "[CameraLiveView] Disconnecting camera..." << std::endl;

  if (m_displayUpdateTimer) {
    m_displayUpdateTimer->stop();
    m_displayUpdateTimer->deleteLater();
    m_displayUpdateTimer = nullptr;
  }

  if (m_subscriber && m_cameraManager) {
    m_cameraManager->UnsubscribeFromFrames(m_subscriber->GetSubscriberId());
    m_subscriber.reset();
  }

  m_cameraManager->StopGrabbing(m_cameraId);
  m_cameraManager->DisconnectCamera(m_cameraId);

  m_zoomSlider->setEnabled(false);
  m_zoomSpinBox->setEnabled(false);
  m_zoomResetButton->setEnabled(false);

  // ✅ CHANGED: Clear viewport
  m_viewport->clearDisplay();

  updateConnectionStatus(false);
  m_fpsLabel->setText("FPS: --");
  m_performanceLabel->setText("Performance: --");
  updateZoomLabel(100);

  std::cout << "[CameraLiveView] Camera disconnected" << std::endl;
}

// ✅ SIMPLIFIED: onUpdateDisplay now just calls setFrame()
void CameraLiveView::onUpdateDisplay()
{
  if (!m_subscriber || !m_subscriber->HasNewFrame()) {
    return;
  }

  auto frameStart = std::chrono::steady_clock::now();

  // Get frame from subscriber
  CameraFrameData frameData = m_subscriber->GetLatestFrame();
  m_subscriber->MarkFrameConsumed();

  if (!frameData.IsValid() || frameData.imageData.empty()) {
    return;
  }

  // ✅ ONE LINE TO DISPLAY FRAME!
  m_viewport->setFrame(frameData);

  // Performance tracking
  auto frameEnd = std::chrono::steady_clock::now();
  double processingMs = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();

  m_avgProcessingTime = (m_avgProcessingTime * 0.9) + (processingMs * 0.1);
  m_frameCounter++;

  // Update FPS display every second
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastUpdateTime).count();

  if (elapsed >= 1) {
    uint64_t totalFrames = m_subscriber->GetTotalFramesReceived();

    double captureFPS = m_frameCounter / static_cast<double>(elapsed);

    double actualFPS = (totalFrames > m_lastTotalFrames)
      ? (totalFrames - m_lastTotalFrames) / static_cast<double>(elapsed)
      : 0.0;

    m_lastTotalFrames = totalFrames;

    m_fpsLabel->setText(QString("FPS: %1 (Actual: %2)")
      .arg(captureFPS, 0, 'f', 1)
      .arg(actualFPS, 0, 'f', 1));

    m_performanceLabel->setText(
      QString("Processing: %1 ms/frame | Total: %2 | Displayed: %3")
      .arg(m_avgProcessingTime, 0, 'f', 2)
      .arg(totalFrames)
      .arg(m_frameCounter));

    m_frameCounter = 0;
    m_lastUpdateTime = now;
  }
}

// ✅ SIMPLIFIED: Zoom control now just calls setZoomPercent()
void CameraLiveView::onZoomSliderChanged(int value)
{
  m_zoomSpinBox->blockSignals(true);
  m_zoomSpinBox->setValue(value);
  m_zoomSpinBox->blockSignals(false);

  m_viewport->setZoomPercent(value);  // ✅ ONE LINE!
  updateZoomLabel(value);
}

void CameraLiveView::onZoomSpinBoxChanged(int value)
{
  m_zoomSlider->blockSignals(true);
  m_zoomSlider->setValue(value);
  m_zoomSlider->blockSignals(false);

  m_viewport->setZoomPercent(value);  // ✅ ONE LINE!
  updateZoomLabel(value);
}

void CameraLiveView::onZoomResetClicked()
{
  m_zoomSlider->setValue(100);
  m_zoomSpinBox->setValue(100);
}

void CameraLiveView::updateZoomLabel(int zoomPercent)
{
  double zoomFactor = 100.0 / zoomPercent;
  m_zoomLabel->setText(QString("Zoom: %1% (%2x)")
    .arg(zoomPercent)
    .arg(zoomFactor, 0, 'f', 1));
}

void CameraLiveView::updateConnectionStatus(bool connected)
{
  m_isConnected = connected;

  if (connected) {
    m_statusLabel->setText("Status: Connected - Optimized Pub-Sub");
    m_statusLabel->setStyleSheet(
      "QLabel { background-color: #44ff44; color: black; padding: 5px; font-weight: bold; }");
    m_connectButton->setEnabled(false);
    m_disconnectButton->setEnabled(true);
  }
  else {
    m_statusLabel->setText("Status: Disconnected");
    m_statusLabel->setStyleSheet(
      "QLabel { background-color: #ff4444; color: white; padding: 5px; font-weight: bold; }");
    m_connectButton->setEnabled(true);
    m_disconnectButton->setEnabled(false);
  }
}