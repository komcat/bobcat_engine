// CameraLiveView.cpp - Updated for worker thread
#include "CameraLiveView.h"
#include "CameraManager.h"
#include "CameraConfigManager.h"
#include "CameraWorker.h"
#include <QMessageBox>
#include <QThread>
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

  // Clean up worker thread first
  cleanupWorkerThread();

  // Then disconnect camera
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

  // Performance info label (NEW)
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

  // FPS label (NEW)
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
  setWindowTitle("IDS Camera Live View");
  resize(800, 650); // Slightly taller for performance labels
}

void CameraLiveView::loadCameraConfig()
{
  if (!m_configManager->LoadConfig()) {
    QMessageBox::warning(this, "Config Error",
      "Failed to load camera_config.json. Using default settings.");
    return;
  }

  // Get enabled cameras
  auto cameraIds = m_configManager->GetEnabledCameraIds();

  if (cameraIds.empty()) {
    QMessageBox::warning(this, "No Cameras",
      "No enabled cameras found in camera_config.json");
    return;
  }

  // Use first enabled camera
  m_cameraId = cameraIds[0];

  // Initialize camera manager with config
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

  // Connect to camera
  if (!m_cameraManager->ConnectCamera(m_cameraId)) {
    QMessageBox::critical(this, "Connection Failed",
      "Failed to connect to camera: " + QString::fromStdString(m_cameraId));
    return;
  }

  // Start grabbing
  if (!m_cameraManager->StartGrabbing(m_cameraId)) {
    QMessageBox::critical(this, "Grabbing Failed",
      "Failed to start video grabbing.");
    m_cameraManager->DisconnectCamera(m_cameraId);
    return;
  }

  std::cout << "[CameraLiveView] Camera connected and grabbing started" << std::endl;

  // ============================================================================
  // CREATE WORKER THREAD
  // ============================================================================

  // Create worker thread
  m_workerThread = new QThread(this);
  m_workerThread->setObjectName("CameraWorkerThread");

  // Create worker object
  m_worker = new CameraWorker(m_cameraManager.get(), m_cameraId);

  // Move worker to thread (IMPORTANT!)
  m_worker->moveToThread(m_workerThread);

  // ============================================================================
  // CONNECT SIGNALS/SLOTS
  // ============================================================================

  // Thread lifecycle
  connect(m_workerThread, &QThread::started, m_worker, &CameraWorker::startCapture);
  connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);

  // Frame updates (cross-thread, queued connection automatically)
  connect(m_worker, &CameraWorker::frameReady, this, &CameraLiveView::onFrameReady);

  // Performance monitoring
  connect(m_worker, &CameraWorker::fpsUpdated, this, &CameraLiveView::onFpsUpdated);
  connect(m_worker, &CameraWorker::processingTimeUpdated, this, &CameraLiveView::onProcessingTimeUpdated);

  // Error handling
  connect(m_worker, &CameraWorker::error, this, &CameraLiveView::onWorkerError);
  connect(m_worker, &CameraWorker::statusMessage, this, &CameraLiveView::onWorkerStatus);

  // ============================================================================
  // START WORKER THREAD
  // ============================================================================

  m_workerThread->start();
  std::cout << "[CameraLiveView] Worker thread started" << std::endl;

  // Update UI
  updateConnectionStatus(true);
}

void CameraLiveView::onDisconnectClicked()
{
  std::cout << "[CameraLiveView] Disconnecting camera..." << std::endl;

  // Clean up worker thread first
  cleanupWorkerThread();

  // Stop grabbing and disconnect camera
  m_cameraManager->StopGrabbing(m_cameraId);
  m_cameraManager->DisconnectCamera(m_cameraId);

  // Update UI
  updateConnectionStatus(false);
  m_videoLabel->setText("No camera feed");
  m_fpsLabel->setText("FPS: --");
  m_performanceLabel->setText("Performance: --");

  std::cout << "[CameraLiveView] Camera disconnected" << std::endl;
}

void CameraLiveView::onFrameReady(QImage image)
{
  // ✅ This runs on UI thread via Qt's event system
  // ✅ QImage is implicitly shared (efficient)
  // ✅ Only UI operations here (no blocking)

  if (image.isNull()) {
    return;
  }

  // Scale image to fit label while maintaining aspect ratio
  QPixmap pixmap = QPixmap::fromImage(image);
  pixmap = pixmap.scaled(m_videoLabel->size(),
    Qt::KeepAspectRatio,
    Qt::SmoothTransformation);

  // Display in label
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

  // Update status
  m_statusLabel->setText("Error: " + errorMsg);
  m_statusLabel->setStyleSheet(
    "QLabel { background-color: #ff4444; color: white; padding: 5px; font-weight: bold; }");
}

void CameraLiveView::onWorkerStatus(QString msg)
{
  std::cout << "[CameraLiveView] Worker status: " << msg.toStdString() << std::endl;
}

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
    return; // Nothing to clean up
  }

  std::cout << "[CameraLiveView] Cleaning up worker thread..." << std::endl;

  // Stop worker
  if (m_worker) {
    // Call stop on worker thread
    QMetaObject::invokeMethod(m_worker, "stopCapture", Qt::QueuedConnection);

    // Give it a moment to stop gracefully
    QThread::msleep(100);
  }

  // Stop thread
  if (m_workerThread->isRunning()) {
    m_workerThread->quit();

    // Wait for thread to finish (with timeout)
    if (!m_workerThread->wait(3000)) {
      std::cerr << "[CameraLiveView] Warning: Worker thread did not stop gracefully, terminating..." << std::endl;
      m_workerThread->terminate();
      m_workerThread->wait();
    }
  }

  // Delete thread (will also delete worker due to deleteLater connection)
  m_workerThread->deleteLater();
  m_workerThread = nullptr;
  m_worker = nullptr;

  std::cout << "[CameraLiveView] Worker thread cleaned up" << std::endl;
}