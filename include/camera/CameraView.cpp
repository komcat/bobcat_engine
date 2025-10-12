// CameraView.cpp - Single camera widget implementation
#include "CameraView.h"
#include "CameraManager.h"
#include "CameraWorker.h"
#include <QGroupBox>
#include <QMessageBox>
#include <iostream>

CameraView::CameraView(CameraManager* cameraManager,
  const std::string& cameraId,
  const std::string& displayName,
  QWidget* parent)
  : QWidget(parent)
  , m_cameraManager(cameraManager)
  , m_cameraId(cameraId)
  , m_displayName(displayName)
  , m_isConnected(false)
  , m_workerThread(nullptr)
  , m_worker(nullptr)
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

  // Create worker thread
  m_workerThread = new QThread(this);
  m_workerThread->setObjectName(QString("Worker_%1").arg(QString::fromStdString(m_cameraId)));

  m_worker = new CameraWorker(m_cameraManager, m_cameraId);
  m_worker->moveToThread(m_workerThread);

  // Connect signals
  connect(m_workerThread, &QThread::started, m_worker, &CameraWorker::startCapture);
  connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);

  connect(m_worker, &CameraWorker::frameReady, this, &CameraView::onFrameReady);
  connect(m_worker, &CameraWorker::fpsUpdated, this, &CameraView::onFpsUpdated);
  connect(m_worker, &CameraWorker::processingTimeUpdated, this, &CameraView::onProcessingTimeUpdated);
  connect(m_worker, &CameraWorker::error, this, &CameraView::onWorkerError);
  connect(m_worker, &CameraWorker::statusMessage, this, &CameraView::onWorkerStatus);
  connect(m_worker, &CameraWorker::zoomChanged, this, &CameraView::onZoomChanged);

  // Start worker thread
  m_workerThread->start();

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

  // Disable zoom controls
  m_zoomSlider->setEnabled(false);
  m_zoomSpinBox->setEnabled(false);
  m_zoomResetButton->setEnabled(false);

  // Cleanup worker thread
  cleanupWorkerThread();

  // Disconnect camera
  m_cameraManager->StopGrabbing(m_cameraId);
  m_cameraManager->DisconnectCamera(m_cameraId);

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

void CameraView::onFrameReady(QImage image)
{
  if (image.isNull()) {
    return;
  }

  QPixmap pixmap = QPixmap::fromImage(image);
  pixmap = pixmap.scaled(m_videoLabel->size(),
    Qt::KeepAspectRatio,
    Qt::SmoothTransformation);

  m_videoLabel->setPixmap(pixmap);
}

void CameraView::onFpsUpdated(double fps)
{
  m_fpsLabel->setText(QString("FPS: %1").arg(fps, 0, 'f', 1));
}

void CameraView::onProcessingTimeUpdated(double ms)
{
  m_performanceLabel->setText(
    QString("Performance: %1 ms/frame").arg(ms, 0, 'f', 2));
}

void CameraView::onWorkerError(QString errorMsg)
{
  std::cerr << "[CameraView] Worker error [" << m_cameraId << "]: "
    << errorMsg.toStdString() << std::endl;

  m_statusLabel->setText("Error: " + errorMsg);
  m_statusLabel->setStyleSheet(
    "QLabel { background-color: #ff4444; color: white; padding: 5px; font-weight: bold; }");

  emit cameraError(QString::fromStdString(m_cameraId), errorMsg);
}

void CameraView::onWorkerStatus(QString msg)
{
  //std::cout << "[CameraView] Worker status [" << m_cameraId << "]: "
  //  << msg.toStdString() << std::endl;
}

void CameraView::onZoomSliderChanged(int value)
{
  m_zoomSpinBox->blockSignals(true);
  m_zoomSpinBox->setValue(value);
  m_zoomSpinBox->blockSignals(false);

  updateZoomLabel(value);

  if (m_worker && m_workerThread && m_workerThread->isRunning()) {
    QMetaObject::invokeMethod(m_worker, "setZoomPercent",
      Qt::QueuedConnection, Q_ARG(int, value));
  }
}

void CameraView::onZoomSpinBoxChanged(int value)
{
  m_zoomSlider->blockSignals(true);
  m_zoomSlider->setValue(value);
  m_zoomSlider->blockSignals(false);

  updateZoomLabel(value);

  if (m_worker && m_workerThread && m_workerThread->isRunning()) {
    QMetaObject::invokeMethod(m_worker, "setZoomPercent",
      Qt::QueuedConnection, Q_ARG(int, value));
  }
}

void CameraView::onZoomResetClicked()
{
  m_zoomSlider->setValue(100);
  m_zoomSpinBox->setValue(100);
}

void CameraView::onZoomChanged(int zoomPercent)
{
  updateZoomLabel(zoomPercent);
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
    m_statusLabel->setText(QString("Camera: %1 - Connected")
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

void CameraView::cleanupWorkerThread()
{
  if (!m_workerThread) {
    return;
  }

  std::cout << "[CameraView] Cleaning up worker thread for: " << m_cameraId << std::endl;

  if (m_worker) {
    QMetaObject::invokeMethod(m_worker, "stopCapture", Qt::QueuedConnection);
    QThread::msleep(100);
  }

  if (m_workerThread->isRunning()) {
    m_workerThread->quit();

    if (!m_workerThread->wait(3000)) {
      std::cerr << "[CameraView] Warning: Worker thread did not stop gracefully" << std::endl;
      m_workerThread->terminate();
      m_workerThread->wait();
    }
  }

  m_workerThread->deleteLater();
  m_workerThread = nullptr;
  m_worker = nullptr;
}