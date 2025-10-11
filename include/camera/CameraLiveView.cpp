// CameraLiveView.cpp - With Zoom Controls
#include "CameraLiveView.h"
#include "CameraManager.h"
#include "CameraConfigManager.h"
#include "CameraWorker.h"
#include <QMessageBox>
#include <QThread>
#include <QGroupBox>
#include <iostream>

CameraLiveView::CameraLiveView(QWidget* parent)
  : QWidget(parent)
  , m_workerThread(nullptr)
  , m_worker(nullptr)
  , m_isConnected(false)
{
  setupUI();

  // Create camera manager and config manager
  m_cameraManager = std::make_unique<CameraManager>();
  m_configManager = std::make_unique<CameraConfigManager>("config/camera_config.json");

  // Load camera configuration
  loadCameraConfig();
}

CameraLiveView::~CameraLiveView()
{
  std::cout << "[CameraLiveView] Destructor called" << std::endl;

  cleanupWorkerThread();

  if (m_isConnected && m_cameraManager) {
    m_cameraManager->StopGrabbing(m_cameraId);
    m_cameraManager->DisconnectCamera(m_cameraId);
  }

  std::cout << "[CameraLiveView] Destructor completed" << std::endl;
}

void CameraLiveView::setupUI()
{
  // Create main layout
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

  // Video display area
  m_videoLabel = new QLabel("No camera feed");
  m_videoLabel->setMinimumSize(640, 480);
  m_videoLabel->setAlignment(Qt::AlignCenter);
  m_videoLabel->setStyleSheet(
    "QLabel { background-color: #2b2b2b; color: #888888; border: 2px solid #555555; }");
  m_videoLabel->setScaledContents(false);
  mainLayout->addWidget(m_videoLabel);

  // ============================================================================
  // ZOOM CONTROLS (NEW)
  // ============================================================================
  QGroupBox* zoomGroup = new QGroupBox("Digital Zoom");
  QHBoxLayout* zoomLayout = new QHBoxLayout();

  // Zoom label
  m_zoomLabel = new QLabel("Zoom: 100%");
  m_zoomLabel->setMinimumWidth(100);
  m_zoomLabel->setStyleSheet("QLabel { font-weight: bold; }");
  zoomLayout->addWidget(m_zoomLabel);

  // Zoom slider (1% to 100%)
  m_zoomSlider = new QSlider(Qt::Horizontal);
  m_zoomSlider->setMinimum(1);
  m_zoomSlider->setMaximum(100);
  m_zoomSlider->setValue(100); // Default: no zoom
  m_zoomSlider->setTickPosition(QSlider::TicksBelow);
  m_zoomSlider->setTickInterval(10);
  m_zoomSlider->setEnabled(false); // Disabled until camera connected
  connect(m_zoomSlider, &QSlider::valueChanged, this, &CameraLiveView::onZoomSliderChanged);
  zoomLayout->addWidget(m_zoomSlider);

  // Zoom spinbox (numeric input)
  m_zoomSpinBox = new QSpinBox();
  m_zoomSpinBox->setMinimum(1);
  m_zoomSpinBox->setMaximum(100);
  m_zoomSpinBox->setValue(100);
  m_zoomSpinBox->setSuffix("%");
  m_zoomSpinBox->setEnabled(false); // Disabled until camera connected
  connect(m_zoomSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
    this, &CameraLiveView::onZoomSpinBoxChanged);
  zoomLayout->addWidget(m_zoomSpinBox);

  // Reset zoom button
  m_zoomResetButton = new QPushButton("Reset (100%)");
  m_zoomResetButton->setEnabled(false); // Disabled until camera connected
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
  setWindowTitle("IDS Camera Live View - With Zoom");
  resize(800, 750); // Taller for zoom controls
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

  std::cout << "[CameraLiveView] Camera configuration loaded. Camera ID: " << m_cameraId << std::endl;
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

  // Create worker thread
  m_workerThread = new QThread(this);
  m_workerThread->setObjectName("CameraWorkerThread");

  m_worker = new CameraWorker(m_cameraManager.get(), m_cameraId);
  m_worker->moveToThread(m_workerThread);

  // Connect signals/slots
  connect(m_workerThread, &QThread::started, m_worker, &CameraWorker::startCapture);
  connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);

  connect(m_worker, &CameraWorker::frameReady, this, &CameraLiveView::onFrameReady);
  connect(m_worker, &CameraWorker::fpsUpdated, this, &CameraLiveView::onFpsUpdated);
  connect(m_worker, &CameraWorker::processingTimeUpdated, this, &CameraLiveView::onProcessingTimeUpdated);
  connect(m_worker, &CameraWorker::error, this, &CameraLiveView::onWorkerError);
  connect(m_worker, &CameraWorker::statusMessage, this, &CameraLiveView::onWorkerStatus);

  // NEW: Connect zoom signals
  connect(m_worker, &CameraWorker::zoomChanged, this, &CameraLiveView::onZoomChanged);

  m_workerThread->start();
  std::cout << "[CameraLiveView] Worker thread started" << std::endl;

  // Update UI
  updateConnectionStatus(true);

  // Enable zoom controls
  m_zoomSlider->setEnabled(true);
  m_zoomSpinBox->setEnabled(true);
  m_zoomResetButton->setEnabled(true);
}

void CameraLiveView::onDisconnectClicked()
{
  std::cout << "[CameraLiveView] Disconnecting camera..." << std::endl;

  // Disable zoom controls
  m_zoomSlider->setEnabled(false);
  m_zoomSpinBox->setEnabled(false);
  m_zoomResetButton->setEnabled(false);

  cleanupWorkerThread();

  m_cameraManager->StopGrabbing(m_cameraId);
  m_cameraManager->DisconnectCamera(m_cameraId);

  updateConnectionStatus(false);
  m_videoLabel->setText("No camera feed");
  m_fpsLabel->setText("FPS: --");
  m_performanceLabel->setText("Performance: --");
  updateZoomLabel(100);

  std::cout << "[CameraLiveView] Camera disconnected" << std::endl;
}

void CameraLiveView::onFrameReady(QImage image)
{
  if (image.isNull()) {
    return;
  }

  // Scale image to fit label while maintaining aspect ratio
  QPixmap pixmap = QPixmap::fromImage(image);
  pixmap = pixmap.scaled(m_videoLabel->size(),
    Qt::KeepAspectRatio,
    Qt::SmoothTransformation);

  m_videoLabel->setPixmap(pixmap);
}

void CameraLiveView::onFpsUpdated(double fps)
{
  m_fpsLabel->setText(QString("FPS: %1").arg(fps, 0, 'f', 1));
}

void CameraLiveView::onProcessingTimeUpdated(double ms)
{
  m_performanceLabel->setText(
    QString("Performance: %1 ms/frame").arg(ms, 0, 'f', 2));
}

void CameraLiveView::onWorkerError(QString errorMsg)
{
  std::cerr << "[CameraLiveView] Worker error: " << errorMsg.toStdString() << std::endl;

  m_statusLabel->setText("Error: " + errorMsg);
  m_statusLabel->setStyleSheet(
    "QLabel { background-color: #ff4444; color: white; padding: 5px; font-weight: bold; }");
}

void CameraLiveView::onWorkerStatus(QString msg)
{
  std::cout << "[CameraLiveView] Worker status: " << msg.toStdString() << std::endl;
}

// ============================================================================
// ZOOM CONTROL IMPLEMENTATIONS (NEW)
// ============================================================================

void CameraLiveView::onZoomSliderChanged(int value)
{
  // Update spinbox (without triggering its signal)
  m_zoomSpinBox->blockSignals(true);
  m_zoomSpinBox->setValue(value);
  m_zoomSpinBox->blockSignals(false);

  // Update label
  updateZoomLabel(value);

  // Send to worker (thread-safe via Qt signal)
  if (m_worker && m_workerThread && m_workerThread->isRunning()) {
    QMetaObject::invokeMethod(m_worker, "setZoomPercent",
      Qt::QueuedConnection, Q_ARG(int, value));
  }
}

void CameraLiveView::onZoomSpinBoxChanged(int value)
{
  // Update slider (without triggering its signal)
  m_zoomSlider->blockSignals(true);
  m_zoomSlider->setValue(value);
  m_zoomSlider->blockSignals(false);

  // Update label
  updateZoomLabel(value);

  // Send to worker
  if (m_worker && m_workerThread && m_workerThread->isRunning()) {
    QMetaObject::invokeMethod(m_worker, "setZoomPercent",
      Qt::QueuedConnection, Q_ARG(int, value));
  }
}

void CameraLiveView::onZoomResetClicked()
{
  // Reset to 100% (no zoom)
  m_zoomSlider->setValue(100);
  m_zoomSpinBox->setValue(100);

  std::cout << "[CameraLiveView] Zoom reset to 100%" << std::endl;
}

void CameraLiveView::onZoomChanged(int zoomPercent)
{
  // Worker confirmed zoom change
  updateZoomLabel(zoomPercent);
}

void CameraLiveView::updateZoomLabel(int zoomPercent)
{
  // Calculate actual zoom factor
  double zoomFactor = 100.0 / zoomPercent;

  m_zoomLabel->setText(QString("Zoom: %1% (%2x)")
    .arg(zoomPercent)
    .arg(zoomFactor, 0, 'f', 1));
}

// ============================================================================

void CameraLiveView::updateConnectionStatus(bool connected)
{
  m_isConnected = connected;

  if (connected) {
    m_statusLabel->setText("Status: Connected - Live Feed Active");
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

void CameraLiveView::cleanupWorkerThread()
{
  if (!m_workerThread) {
    return;
  }

  std::cout << "[CameraLiveView] Cleaning up worker thread..." << std::endl;

  if (m_worker) {
    QMetaObject::invokeMethod(m_worker, "stopCapture", Qt::QueuedConnection);
    QThread::msleep(100);
  }

  if (m_workerThread->isRunning()) {
    m_workerThread->quit();

    if (!m_workerThread->wait(3000)) {
      std::cerr << "[CameraLiveView] Warning: Worker thread did not stop gracefully, terminating..." << std::endl;
      m_workerThread->terminate();
      m_workerThread->wait();
    }
  }

  m_workerThread->deleteLater();
  m_workerThread = nullptr;
  m_worker = nullptr;

  std::cout << "[CameraLiveView] Worker thread cleaned up" << std::endl;
}