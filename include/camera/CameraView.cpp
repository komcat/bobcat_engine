// CameraView.cpp
#include "CameraView.h"
#include "ApplicationContext.h"
#include "CameraManager.h"
#include "LiveVideoSubscriber.h"
#include "CameraImageWidget.h"
#include "ICameraHardware.h"
#include <QGroupBox>
#include <QMessageBox>
#include <QDateTime>
#include <QDir>
#include <QHBoxLayout>
#include <QShowEvent>
#include <iostream>

CameraView::CameraView(ApplicationContext* context,
  const std::string& cameraId,
  const std::string& displayName,
  QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_cameraId(cameraId)
  , m_displayName(displayName)
  , m_isConnected(false)
  , m_displayUpdateTimer(nullptr)
  , m_frameCounter(0)
  , m_avgProcessingTime(0.0)
  , m_lastTotalFrames(0)
{
  setupUI();

  // ✅ NEW - Check if camera is already connected in CameraManager
  syncWithManagerState();

  std::cout << "[CameraView] Created for camera: " << m_cameraId << std::endl;
}

CameraView::~CameraView()
{
  std::cout << "[CameraView] Destructor for: " << m_cameraId << std::endl;
  disconnectCamera();
}

void CameraView::setupUI()
{
  // Set dark theme for the entire CameraView widget
  setStyleSheet(
    "CameraView { "
    "  background-color: #2d2d30; "
    "} "
    "QWidget { "
    "  background-color: #2d2d30; "
    "  color: #d4d4d4; "
    "} "
    "QGroupBox { "
    "  background-color: #1e1e1e; "
    "  color: #ffffff; "
    "  border: 1px solid #3f3f46; "
    "  border-radius: 4px; "
    "  margin-top: 8px; "
    "  padding: 10px; "
    "  font-weight: bold; "
    "} "
    "QGroupBox::title { "
    "  subcontrol-origin: margin; "
    "  subcontrol-position: top left; "
    "  padding: 2px 5px; "
    "  color: #ffffff; "
    "} "
    "QCheckBox { "
    "  color: #d4d4d4; "
    "  spacing: 8px; "
    "} "
    "QCheckBox::indicator { "
    "  width: 16px; "
    "  height: 16px; "
    "  border: 1px solid #555555; "
    "  border-radius: 3px; "
    "  background-color: #3c3c3c; "
    "} "
    "QCheckBox::indicator:checked { "
    "  background-color: #0e639c; "
    "  border-color: #0e639c; "
    "} "
    "QLabel { "
    "  color: #d4d4d4; "
    "} "
    "QSlider::groove:horizontal { "
    "  border: 1px solid #3f3f46; "
    "  height: 6px; "
    "  background-color: #3c3c3c; "
    "  border-radius: 3px; "
    "} "
    "QSlider::handle:horizontal { "
    "  background-color: #0e639c; "
    "  border: 1px solid #0c5280; "
    "  width: 16px; "
    "  margin: -5px 0; "
    "  border-radius: 8px; "
    "} "
    "QSlider::handle:horizontal:hover { "
    "  background-color: #1177bb; "
    "} "
    "QSpinBox { "
    "  background-color: #3c3c3c; "
    "  color: #d4d4d4; "
    "  border: 1px solid #555555; "
    "  border-radius: 3px; "
    "  padding: 4px; "
    "} "
    "QSpinBox::up-button, QSpinBox::down-button { "
    "  background-color: #2d2d30; "
    "  border: 1px solid #555555; "
    "} "
    "QSpinBox::up-button:hover, QSpinBox::down-button:hover { "
    "  background-color: #3c3c3c; "
    "} "
    "QPushButton { "
    "  background-color: #0e639c; "
    "  color: white; "
    "  border: 1px solid #0c5280; "
    "  border-radius: 4px; "
    "  padding: 6px 12px; "
    "  font-weight: 600; "
    "} "
    "QPushButton:hover { "
    "  background-color: #1177bb; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #0c5280; "
    "} "
    "QPushButton:disabled { "
    "  background-color: #3c3c3c; "
    "  color: #666666; "
    "  border-color: #555555; "
    "}"
  );

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

  // ✅ NEW - Main content: LEFT PANEL + VIDEO
  QHBoxLayout* contentLayout = new QHBoxLayout();

  // ================== LEFT PANEL ==================
  QWidget* leftPanel = new QWidget();
  leftPanel->setMaximumWidth(250);
  leftPanel->setMinimumWidth(200);
  QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
  leftLayout->setSpacing(10);

  // -------- Camera Settings Group --------
  m_settingsGroup = new QGroupBox("Camera Settings");
  QVBoxLayout* settingsLayout = new QVBoxLayout();

  // Auto Exposure checkbox
  m_autoExposureCheck = new QCheckBox("Auto Exposure");
  m_autoExposureCheck->setEnabled(false);
  connect(m_autoExposureCheck, &QCheckBox::checkStateChanged,
    this, &CameraView::onAutoExposureChanged);
  settingsLayout->addWidget(m_autoExposureCheck);

  // Auto Gain checkbox
  m_autoGainCheck = new QCheckBox("Auto Gain");
  m_autoGainCheck->setEnabled(false);
  connect(m_autoGainCheck, &QCheckBox::checkStateChanged,
    this, &CameraView::onAutoGainChanged);
  settingsLayout->addWidget(m_autoGainCheck);

  settingsLayout->addSpacing(10);

  // Exposure control
  QLabel* expLabel = new QLabel("Exposure:");
  expLabel->setStyleSheet("QLabel { font-weight: bold; }");
  settingsLayout->addWidget(expLabel);

  m_exposureSlider = new QSlider(Qt::Horizontal);
  m_exposureSlider->setMinimum(1);
  m_exposureSlider->setMaximum(10000);
  m_exposureSlider->setValue(1000);
  m_exposureSlider->setEnabled(false);
  connect(m_exposureSlider, &QSlider::valueChanged,
    this, &CameraView::onExposureSliderChanged);
  settingsLayout->addWidget(m_exposureSlider);

  m_exposureValueLabel = new QLabel("1000 μs");
  m_exposureValueLabel->setAlignment(Qt::AlignCenter);
  settingsLayout->addWidget(m_exposureValueLabel);

  settingsLayout->addSpacing(10);

  // Gain control
  QLabel* gainLabel = new QLabel("Gain:");
  gainLabel->setStyleSheet("QLabel { font-weight: bold; }");
  settingsLayout->addWidget(gainLabel);

  m_gainSlider = new QSlider(Qt::Horizontal);
  m_gainSlider->setMinimum(0);
  m_gainSlider->setMaximum(100); // 0-10 with 0.1 precision
  m_gainSlider->setValue(10);
  m_gainSlider->setEnabled(false);
  connect(m_gainSlider, &QSlider::valueChanged,
    this, &CameraView::onGainSliderChanged);
  settingsLayout->addWidget(m_gainSlider);

  m_gainValueLabel = new QLabel("1.0");
  m_gainValueLabel->setAlignment(Qt::AlignCenter);
  settingsLayout->addWidget(m_gainValueLabel);

  settingsLayout->addSpacing(15);

  // Save Image button
  m_saveImageButton = new QPushButton("Save Image");
  m_saveImageButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #44aa44; "
    "  color: white; "
    "  padding: 8px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { "
    "  background-color: #55bb55; "
    "} "
    "QPushButton:disabled { "
    "  background-color: #666666; "
    "}");
  m_saveImageButton->setEnabled(false);
  connect(m_saveImageButton, &QPushButton::clicked,
    this, &CameraView::onSaveImageClicked);
  settingsLayout->addWidget(m_saveImageButton);

  m_settingsGroup->setLayout(settingsLayout);
  leftLayout->addWidget(m_settingsGroup);

  leftLayout->addSpacing(20);

  // -------- Zoom Controls Group --------
  QGroupBox* zoomGroup = new QGroupBox("Digital Zoom");
  QVBoxLayout* zoomLayout = new QVBoxLayout();

  m_zoomLabel = new QLabel("Zoom: 100%");
  m_zoomLabel->setStyleSheet("QLabel { font-weight: bold; }");
  m_zoomLabel->setAlignment(Qt::AlignCenter);
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
  leftLayout->addWidget(zoomGroup);

  leftLayout->addStretch();

  contentLayout->addWidget(leftPanel);

  // ================== VIDEO DISPLAY ==================
  m_viewport = new CameraImageWidget(this);
  m_viewport->setMinimumDisplaySize(320, 240);
  m_viewport->setPlaceholderText("No camera feed");
  contentLayout->addWidget(m_viewport, 1); // stretch factor

  mainLayout->addLayout(contentLayout);

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

  // Use context to access camera manager
  if (!m_context->cameraManager->ConnectCamera(m_cameraId)) {
    QString error = QString("Failed to connect to camera: %1")
      .arg(QString::fromStdString(m_cameraId));
    emit cameraError(QString::fromStdString(m_cameraId), error);
    return false;
  }

  if (!m_context->cameraManager->StartGrabbing(m_cameraId)) {
    m_context->cameraManager->DisconnectCamera(m_cameraId);
    emit cameraError(QString::fromStdString(m_cameraId), "Failed to start grabbing");
    return false;
  }

  m_subscriber = std::make_shared<LiveVideoSubscriber>(m_cameraId);
  m_subscriber->InitializeStatusFromManager(m_context->cameraManager);
  m_context->cameraManager->SubscribeToFrames(m_subscriber);

  std::cout << "[CameraView] Subscribed to camera broadcasts: " << m_cameraId << std::endl;

  m_displayUpdateTimer = new QTimer(this);
  connect(m_displayUpdateTimer, &QTimer::timeout, this, &CameraView::onUpdateDisplay);
  m_displayUpdateTimer->start(33);

  m_lastUpdateTime = std::chrono::steady_clock::now();
  m_frameCounter = 0;
  m_lastTotalFrames = 0;

  updateConnectionStatus(true);

  // ✅ Enable controls
  m_zoomSlider->setEnabled(true);
  m_zoomSpinBox->setEnabled(true);
  m_zoomResetButton->setEnabled(true);
  m_autoExposureCheck->setEnabled(true);
  m_autoGainCheck->setEnabled(true);
  m_exposureSlider->setEnabled(true);
  m_gainSlider->setEnabled(true);
  m_saveImageButton->setEnabled(true);

  // ✅ Load current camera settings
  loadCurrentSettings();

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

  if (m_displayUpdateTimer) {
    m_displayUpdateTimer->stop();
    m_displayUpdateTimer->deleteLater();
    m_displayUpdateTimer = nullptr;
  }

  if (m_subscriber && m_context->cameraManager) {
    m_context->cameraManager->UnsubscribeFromFrames(m_subscriber->GetSubscriberId());
    m_subscriber.reset();
  }

  m_context->cameraManager->StopGrabbing(m_cameraId);
  m_context->cameraManager->DisconnectCamera(m_cameraId);

  // ✅ Disable ALL controls
  m_zoomSlider->setEnabled(false);
  m_zoomSpinBox->setEnabled(false);
  m_zoomResetButton->setEnabled(false);
  m_autoExposureCheck->setEnabled(false);
  m_autoGainCheck->setEnabled(false);
  m_exposureSlider->setEnabled(false);
  m_gainSlider->setEnabled(false);
  m_saveImageButton->setEnabled(false);

  m_viewport->clearDisplay();

  updateConnectionStatus(false);
  m_fpsLabel->setText("FPS: --");
  m_performanceLabel->setText("Performance: --");
  updateZoomLabel(100);

  emit cameraDisconnected(QString::fromStdString(m_cameraId));
  std::cout << "[CameraView] Camera disconnected: " << m_cameraId << std::endl;
}

// ============================================================================
// ✅ NEW - CAMERA SETTINGS SLOTS
// ============================================================================

void CameraView::onAutoExposureChanged(int state)
{
  bool autoEnabled = (state == Qt::Checked);

  // Enable/disable manual slider
  m_exposureSlider->setEnabled(!autoEnabled && m_isConnected);

  // Apply setting immediately
  applyCameraSettings();

  std::cout << "[CameraView] Auto Exposure: " << (autoEnabled ? "ON" : "OFF") << std::endl;
}

void CameraView::onAutoGainChanged(int state)
{
  bool autoEnabled = (state == Qt::Checked);

  // Enable/disable manual slider
  m_gainSlider->setEnabled(!autoEnabled && m_isConnected);

  // Apply setting immediately
  applyCameraSettings();

  std::cout << "[CameraView] Auto Gain: " << (autoEnabled ? "ON" : "OFF") << std::endl;
}

void CameraView::onExposureSliderChanged(int value)
{
  updateExposureLabel(value);
  applyCameraSettings(); // Auto-apply
}

void CameraView::onGainSliderChanged(int value)
{
  double gain = value / 10.0; // Convert to 0.0 - 10.0 range
  updateGainLabel(gain);
  applyCameraSettings(); // Auto-apply
}

void CameraView::onSaveImageClicked()
{
  if (!m_isConnected || !m_subscriber) {
    QMessageBox::warning(this, "Error", "Camera not connected");
    return;
  }

  // Get latest frame
  CameraFrameData frameData = m_subscriber->GetLatestFrame();

  if (!frameData.IsValid()) {
    QMessageBox::warning(this, "Error", "No valid frame available");
    return;
  }

  // Create captures directory if it doesn't exist
  QDir capturesDir("captures");
  if (!capturesDir.exists()) {
    capturesDir.mkpath(".");
    std::cout << "[CameraView] Created captures directory" << std::endl;
  }

  // Generate filename
  QString filename = generateSaveFilename();
  QString fullPath = "captures/" + filename;

  // Convert frame to QImage
  QImage::Format format = (frameData.channels == 3)
    ? QImage::Format_RGB888
    : QImage::Format_Grayscale8;

  QImage image(frameData.imageData.data(),
    frameData.width,
    frameData.height,
    frameData.width * frameData.channels,
    format);

  // Save as PNG
  if (image.save(fullPath, "PNG")) {
    std::cout << "[CameraView] Image saved: " << fullPath.toStdString() << std::endl;

    // Show success message briefly
    QString savedMsg = QString("Camera: %1 - ✓ Saved: %2")
      .arg(QString::fromStdString(m_displayName))
      .arg(filename);
    m_statusLabel->setText(savedMsg);
    m_statusLabel->setStyleSheet(
      "QLabel { background-color: #44ff44; color: black; padding: 5px; font-weight: bold; }");

    // Reset status after 2 seconds
    QTimer::singleShot(2000, [this]() {
      updateConnectionStatus(m_isConnected);
    });
  }
  else {
    QMessageBox::critical(this, "Error", "Failed to save image to: " + fullPath);
    std::cerr << "[CameraView] Failed to save image: " << fullPath.toStdString() << std::endl;
  }
}

// ============================================================================
// ✅ NEW - HELPER METHODS
// ============================================================================

void CameraView::applyCameraSettings()
{
  if (!m_isConnected || !m_context || !m_context->cameraManager) {
    return;
  }

  ICameraHardware::ExposureSettings settings;

  settings.auto_exposure = m_autoExposureCheck->isChecked();
  settings.auto_gain = m_autoGainCheck->isChecked();
  settings.exposure_time = m_exposureSlider->value();
  settings.gain = m_gainSlider->value() / 10.0;

  if (m_context->cameraManager->ApplyExposureSettings(m_cameraId, settings)) {
    // Success - settings applied
  }
  else {
    std::cerr << "[CameraView] Failed to apply camera settings" << std::endl;
  }
}

void CameraView::loadCurrentSettings()
{
  if (!m_isConnected || !m_context || !m_context->cameraManager) {
    return;
  }

  auto settings = m_context->cameraManager->GetExposureSettings(m_cameraId);

  // Block signals during load to prevent unnecessary updates
  m_autoExposureCheck->blockSignals(true);
  m_autoGainCheck->blockSignals(true);
  m_exposureSlider->blockSignals(true);
  m_gainSlider->blockSignals(true);

  m_autoExposureCheck->setChecked(settings.auto_exposure);
  m_autoGainCheck->setChecked(settings.auto_gain);

  int exposureValue = static_cast<int>(settings.exposure_time);
  if (exposureValue < 1) exposureValue = 1;
  if (exposureValue > 10000) exposureValue = 10000;
  m_exposureSlider->setValue(exposureValue);

  int gainValue = static_cast<int>(settings.gain * 10);
  if (gainValue < 0) gainValue = 0;
  if (gainValue > 100) gainValue = 100;
  m_gainSlider->setValue(gainValue);

  // Update labels
  updateExposureLabel(exposureValue);
  updateGainLabel(settings.gain);

  // Update slider enabled state
  m_exposureSlider->setEnabled(!settings.auto_exposure);
  m_gainSlider->setEnabled(!settings.auto_gain);

  // Unblock signals
  m_autoExposureCheck->blockSignals(false);
  m_autoGainCheck->blockSignals(false);
  m_exposureSlider->blockSignals(false);
  m_gainSlider->blockSignals(false);

  std::cout << "[CameraView] Loaded camera settings: "
    << "Exposure=" << settings.exposure_time << "μs, "
    << "Gain=" << settings.gain << ", "
    << "AutoExp=" << (settings.auto_exposure ? "ON" : "OFF") << ", "
    << "AutoGain=" << (settings.auto_gain ? "ON" : "OFF") << std::endl;
}

void CameraView::updateExposureLabel(int microseconds)
{
  m_exposureValueLabel->setText(QString("%1 μs").arg(microseconds));
}

void CameraView::updateGainLabel(double gain)
{
  m_gainValueLabel->setText(QString::number(gain, 'f', 1));
}

QString CameraView::generateSaveFilename()
{
  QDateTime now = QDateTime::currentDateTime();
  QString timestamp = now.toString("yyyyMMdd_HHmmss_zzz");
  QString cameraName = QString::fromStdString(m_cameraId);

  return QString("%1_%2.png").arg(cameraName).arg(timestamp);
}

// ============================================================================
// EXISTING METHODS (unchanged from your baseline)
// ============================================================================

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
  if (!m_subscriber || !m_subscriber->HasNewFrame()) {
    return;
  }

  auto frameStart = std::chrono::steady_clock::now();

  CameraFrameData frameData = m_subscriber->GetLatestFrame();
  m_subscriber->MarkFrameConsumed();

  if (!frameData.IsValid() || frameData.imageData.empty()) {
    return;
  }

  m_viewport->setFrame(frameData);

  auto frameEnd = std::chrono::steady_clock::now();
  double processingMs = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();

  m_avgProcessingTime = (m_avgProcessingTime * 0.9) + (processingMs * 0.1);
  m_frameCounter++;

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

void CameraView::onZoomSliderChanged(int value)
{
  m_zoomSpinBox->blockSignals(true);
  m_zoomSpinBox->setValue(value);
  m_zoomSpinBox->blockSignals(false);

  m_viewport->setZoomPercent(value);
  updateZoomLabel(value);
}

void CameraView::onZoomSpinBoxChanged(int value)
{
  m_zoomSlider->blockSignals(true);
  m_zoomSlider->setValue(value);
  m_zoomSlider->blockSignals(false);

  m_viewport->setZoomPercent(value);
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

// ============================================================================
// ✅ NEW - FORCE REFRESH ON SHOW
// ============================================================================

void CameraView::showEvent(QShowEvent* event)
{
  QWidget::showEvent(event);

  // Force repaint of the viewport when shown
  if (m_viewport) {
    m_viewport->forceRefresh();
    std::cout << "[CameraView] Forcing viewport refresh on show" << std::endl;
  }

  // Force update of entire widget
  update();
  repaint();
}

// ============================================================================
// ✅ NEW - SYNC WITH CAMERA MANAGER STATE
// ============================================================================

void CameraView::syncWithManagerState()
{
  if (!m_context || !m_context->cameraManager) {
    std::cout << "[CameraView] Cannot sync - no CameraManager" << std::endl;
    return;
  }

  // Get current camera status from CameraManager
  auto status = m_context->cameraManager->GetCameraStatus(m_cameraId);

  std::cout << "[CameraView] Syncing state for " << m_cameraId
    << " - Connected: " << status.connected
    << ", Grabbing: " << status.grabbing << std::endl;

  // If camera is already connected and grabbing, set up the view accordingly
  if (status.connected && status.grabbing) {
    std::cout << "[CameraView] Camera already connected - setting up subscriber" << std::endl;

    // Create subscriber to receive frames
    m_subscriber = std::make_shared<LiveVideoSubscriber>(m_cameraId);
    m_subscriber->InitializeStatusFromManager(m_context->cameraManager);
    m_context->cameraManager->SubscribeToFrames(m_subscriber);

    std::cout << "[CameraView] Subscribed to camera broadcasts: " << m_cameraId << std::endl;

    // Start display update timer
    m_displayUpdateTimer = new QTimer(this);
    connect(m_displayUpdateTimer, &QTimer::timeout, this, &CameraView::onUpdateDisplay);
    m_displayUpdateTimer->start(33);  // ~30 FPS

    m_lastUpdateTime = std::chrono::steady_clock::now();
    m_frameCounter = 0;
    m_lastTotalFrames = 0;

    // Update UI state
    updateConnectionStatus(true);

    // Enable controls
    m_zoomSlider->setEnabled(true);
    m_zoomSpinBox->setEnabled(true);
    m_zoomResetButton->setEnabled(true);
    m_autoExposureCheck->setEnabled(true);
    m_autoGainCheck->setEnabled(true);
    m_exposureSlider->setEnabled(true);
    m_gainSlider->setEnabled(true);
    m_saveImageButton->setEnabled(true);

    // Load current camera settings
    loadCurrentSettings();

    // ✅ Force refresh viewport after sync
    if (m_viewport) {
      m_viewport->forceRefresh();
      std::cout << "[CameraView] Forcing viewport refresh after sync (connected)" << std::endl;
    }

    emit cameraConnected(QString::fromStdString(m_cameraId));
    std::cout << "[CameraView] Sync complete - camera is active" << std::endl;
  }
  else {
    std::cout << "[CameraView] Camera not connected in manager - staying disconnected" << std::endl;

    // ✅ Force refresh even when disconnected to show placeholder
    if (m_viewport) {
      m_viewport->forceRefresh();
      std::cout << "[CameraView] Forcing viewport refresh after sync (disconnected)" << std::endl;
    }
  }
}