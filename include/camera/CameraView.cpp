// CameraView.cpp
#include "CameraView.h"
#include "ApplicationContext.h"
#include "CameraManager.h"
#include "LiveVideoSubscriber.h"
#include "CameraImageWidget.h"
#include "ICameraHardware.h"
#include "CameraParameterWorker.h"
#include <QGroupBox>
#include <QMessageBox>
#include <QDateTime>
#include <QDir>
#include <QHBoxLayout>
#include <QShowEvent>
#include <QFile>
#include <iostream>
#include <nlohmann/json.hpp>

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

  // Setup throttle timer for settings apply (500ms single-shot)
  m_applySettingsTimer = new QTimer(this);
  m_applySettingsTimer->setSingleShot(true);
  connect(m_applySettingsTimer, &QTimer::timeout, this, &CameraView::onApplySettingsTimer);

  // Setup background parameter worker for non-blocking camera updates
  setupParameterWorker();

  // ✅ NEW - Check if camera is already connected in CameraManager
  syncWithManagerState();

  std::cout << "[CameraView] Created for camera: " << m_cameraId << std::endl;
}

CameraView::~CameraView()
{
  std::cout << "[CameraView] Destructor for: " << m_cameraId << std::endl;

  // Stop parameter worker first
  if (m_parameterWorker) {
    m_parameterWorker->stop();
    delete m_parameterWorker;
    m_parameterWorker = nullptr;
  }

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
    "QSpinBox, QDoubleSpinBox { "
    "  background-color: #3c3c3c; "
    "  color: #d4d4d4; "
    "  border: 1px solid #555555; "
    "  border-radius: 3px; "
    "  padding: 4px; "
    "} "
    "QSpinBox::up-button, QSpinBox::down-button, "
    "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { "
    "  background-color: #2d2d30; "
    "  border: 1px solid #555555; "
    "} "
    "QSpinBox::up-button:hover, QSpinBox::down-button:hover, "
    "QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover { "
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

  // ✅ Main content: LEFT PANELS (two columns) + VIDEO
  QHBoxLayout* contentLayout = new QHBoxLayout();

  // ================== LEFT PANEL - CAMERA SETTINGS ==================
  QWidget* settingsPanel = new QWidget();
  settingsPanel->setMaximumWidth(220);
  settingsPanel->setMinimumWidth(180);
  QVBoxLayout* settingsPanelLayout = new QVBoxLayout(settingsPanel);
  settingsPanelLayout->setSpacing(8);
  settingsPanelLayout->setContentsMargins(0, 0, 5, 0);

  // -------- Camera Settings Group --------
  m_settingsGroup = new QGroupBox("Camera Settings");
  QVBoxLayout* settingsLayout = new QVBoxLayout();
  settingsLayout->setSpacing(12);  // More spacing between elements

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

  settingsLayout->addSpacing(15);

  // Exposure control
  QLabel* expLabel = new QLabel("Exposure:");
  expLabel->setStyleSheet("QLabel { font-weight: bold; }");
  settingsLayout->addWidget(expLabel);

  // Exposure spinbox + Update button row
  QHBoxLayout* exposureRow = new QHBoxLayout();
  exposureRow->setSpacing(5);

  m_exposureSpinBox = new QSpinBox();
  m_exposureSpinBox->setMinimum(1);
  m_exposureSpinBox->setMaximum(100000);
  m_exposureSpinBox->setSingleStep(50);
  m_exposureSpinBox->setValue(1000);
  m_exposureSpinBox->setSuffix(" μs");
  m_exposureSpinBox->setEnabled(false);
  exposureRow->addWidget(m_exposureSpinBox);

  m_exposureUpdateBtn = new QPushButton("⏎");
  m_exposureUpdateBtn->setFixedSize(28, 28);
  m_exposureUpdateBtn->setToolTip("Apply exposure");
  m_exposureUpdateBtn->setEnabled(false);
  connect(m_exposureUpdateBtn, &QPushButton::clicked, this, &CameraView::onExposureUpdateClicked);
  exposureRow->addWidget(m_exposureUpdateBtn);

  settingsLayout->addLayout(exposureRow);

  m_exposureValueLabel = new QLabel("1000 μs");
  m_exposureValueLabel->setAlignment(Qt::AlignCenter);
  m_exposureValueLabel->setVisible(false);  // Hidden - spinbox shows value now

  settingsLayout->addSpacing(15);

  // Gain control
  QLabel* gainLabel = new QLabel("Gain:");
  gainLabel->setStyleSheet("QLabel { font-weight: bold; }");
  settingsLayout->addWidget(gainLabel);

  // Gain spinbox + Update button row
  QHBoxLayout* gainRow = new QHBoxLayout();
  gainRow->setSpacing(5);

  m_gainSpinBox = new QDoubleSpinBox();
  m_gainSpinBox->setMinimum(0.0);
  m_gainSpinBox->setMaximum(24.0);
  m_gainSpinBox->setSingleStep(0.1);
  m_gainSpinBox->setDecimals(1);
  m_gainSpinBox->setValue(1.0);
  m_gainSpinBox->setEnabled(false);
  gainRow->addWidget(m_gainSpinBox);

  m_gainUpdateBtn = new QPushButton("⏎");
  m_gainUpdateBtn->setFixedSize(28, 28);
  m_gainUpdateBtn->setToolTip("Apply gain");
  m_gainUpdateBtn->setEnabled(false);
  connect(m_gainUpdateBtn, &QPushButton::clicked, this, &CameraView::onGainUpdateClicked);
  gainRow->addWidget(m_gainUpdateBtn);

  settingsLayout->addLayout(gainRow);

  m_gainValueLabel = new QLabel("1.0");
  m_gainValueLabel->setAlignment(Qt::AlignCenter);
  m_gainValueLabel->setVisible(false);  // Hidden - spinbox shows value now

  settingsLayout->addSpacing(15);

  // Frame Rate control
  QLabel* fpsLabel = new QLabel("Frame Rate:");
  fpsLabel->setStyleSheet("QLabel { font-weight: bold; }");
  settingsLayout->addWidget(fpsLabel);

  // Frame rate spinbox + Update button row
  QHBoxLayout* frameRateRow = new QHBoxLayout();
  frameRateRow->setSpacing(5);

  m_frameRateSpinBox = new QDoubleSpinBox();
  m_frameRateSpinBox->setMinimum(0.1);
  m_frameRateSpinBox->setMaximum(30.0);
  m_frameRateSpinBox->setSingleStep(0.5);
  m_frameRateSpinBox->setDecimals(1);
  m_frameRateSpinBox->setValue(8.0);
  m_frameRateSpinBox->setSuffix(" fps");
  m_frameRateSpinBox->setEnabled(false);
  frameRateRow->addWidget(m_frameRateSpinBox);

  m_frameRateUpdateBtn = new QPushButton("⏎");
  m_frameRateUpdateBtn->setFixedSize(28, 28);
  m_frameRateUpdateBtn->setToolTip("Apply frame rate");
  m_frameRateUpdateBtn->setEnabled(false);
  connect(m_frameRateUpdateBtn, &QPushButton::clicked, this, &CameraView::onFrameRateUpdateClicked);
  frameRateRow->addWidget(m_frameRateUpdateBtn);

  settingsLayout->addLayout(frameRateRow);

  m_frameRateValueLabel = new QLabel("8.0 fps");
  m_frameRateValueLabel->setAlignment(Qt::AlignCenter);
  m_frameRateValueLabel->setVisible(false);  // Hidden - spinbox shows value now

  settingsLayout->addSpacing(20);

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

  settingsLayout->addStretch();  // Push content to top

  m_settingsGroup->setLayout(settingsLayout);
  settingsPanelLayout->addWidget(m_settingsGroup);

  contentLayout->addWidget(settingsPanel);

  // ================== LEFT PANEL 2 - DIGITAL ZOOM ==================
  QWidget* zoomPanel = new QWidget();
  zoomPanel->setMaximumWidth(150);
  zoomPanel->setMinimumWidth(120);
  QVBoxLayout* zoomPanelLayout = new QVBoxLayout(zoomPanel);
  zoomPanelLayout->setSpacing(8);
  zoomPanelLayout->setContentsMargins(5, 0, 5, 0);

  // -------- Zoom Controls Group --------
  QGroupBox* zoomGroup = new QGroupBox("Digital Zoom");
  QVBoxLayout* zoomLayout = new QVBoxLayout();
  zoomLayout->setSpacing(12);

  m_zoomLabel = new QLabel("Zoom: 100%");
  m_zoomLabel->setStyleSheet("QLabel { font-weight: bold; }");
  m_zoomLabel->setAlignment(Qt::AlignCenter);
  zoomLayout->addWidget(m_zoomLabel);

  // Zoom spinbox + Update button row
  QHBoxLayout* zoomRow = new QHBoxLayout();
  zoomRow->setSpacing(5);

  m_zoomSpinBox = new QSpinBox();
  m_zoomSpinBox->setMinimum(100);
  m_zoomSpinBox->setMaximum(400);  // 100% to 400% zoom
  m_zoomSpinBox->setValue(100);
  m_zoomSpinBox->setSuffix("%");
  m_zoomSpinBox->setEnabled(false);
  zoomRow->addWidget(m_zoomSpinBox);

  m_zoomUpdateBtn = new QPushButton("⏎");
  m_zoomUpdateBtn->setFixedSize(28, 28);
  m_zoomUpdateBtn->setToolTip("Apply zoom");
  m_zoomUpdateBtn->setEnabled(false);
  connect(m_zoomUpdateBtn, &QPushButton::clicked, this, &CameraView::onZoomUpdateClicked);
  zoomRow->addWidget(m_zoomUpdateBtn);

  zoomLayout->addLayout(zoomRow);

  m_zoomResetButton = new QPushButton("Reset");
  m_zoomResetButton->setEnabled(false);
  connect(m_zoomResetButton, &QPushButton::clicked, this, &CameraView::onZoomResetClicked);
  zoomLayout->addWidget(m_zoomResetButton);

  zoomLayout->addStretch();  // Push content to top

  zoomGroup->setLayout(zoomLayout);
  zoomPanelLayout->addWidget(zoomGroup);

  contentLayout->addWidget(zoomPanel);

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

  m_subscriber = std::make_shared<LiveVideoSubscriber>(m_cameraId, "CameraView_" + m_cameraId);
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

  // Enable controls
  m_zoomSpinBox->setEnabled(true);
  if (m_zoomUpdateBtn) m_zoomUpdateBtn->setEnabled(true);
  m_zoomResetButton->setEnabled(true);
  m_autoExposureCheck->setEnabled(true);
  m_autoGainCheck->setEnabled(true);
  m_exposureSpinBox->setEnabled(true);
  if (m_exposureUpdateBtn) m_exposureUpdateBtn->setEnabled(true);
  m_gainSpinBox->setEnabled(true);
  if (m_gainUpdateBtn) m_gainUpdateBtn->setEnabled(true);
  if (m_frameRateSpinBox) m_frameRateSpinBox->setEnabled(true);
  if (m_frameRateUpdateBtn) m_frameRateUpdateBtn->setEnabled(true);
  m_saveImageButton->setEnabled(true);

  // ✅ Load current camera settings (use worker if available)
  if (m_parameterWorker && m_useParameterWorker) {
    m_parameterWorker->requestReadAll();  // Non-blocking read via worker
  } else {
    loadCurrentSettings();  // Fallback to direct read
  }

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

  // Disable ALL controls
  m_zoomSpinBox->setEnabled(false);
  if (m_zoomUpdateBtn) m_zoomUpdateBtn->setEnabled(false);
  m_zoomResetButton->setEnabled(false);
  m_autoExposureCheck->setEnabled(false);
  m_autoGainCheck->setEnabled(false);
  m_exposureSpinBox->setEnabled(false);
  if (m_exposureUpdateBtn) m_exposureUpdateBtn->setEnabled(false);
  m_gainSpinBox->setEnabled(false);
  if (m_gainUpdateBtn) m_gainUpdateBtn->setEnabled(false);
  if (m_frameRateSpinBox) m_frameRateSpinBox->setEnabled(false);
  if (m_frameRateUpdateBtn) m_frameRateUpdateBtn->setEnabled(false);
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

void CameraView::onAutoExposureChanged(Qt::CheckState state)
{
  bool autoEnabled = (state == Qt::Checked);

  // Enable/disable manual spinbox and update button
  m_exposureSpinBox->setEnabled(!autoEnabled && m_isConnected);
  if (m_exposureUpdateBtn) m_exposureUpdateBtn->setEnabled(!autoEnabled && m_isConnected);

  // Use worker for non-blocking camera update
  if (m_parameterWorker && m_useParameterWorker) {
    m_parameterWorker->requestAutoExposure(autoEnabled);
  } else {
    // Fallback to direct apply
    applyCameraSettings();
  }

  std::cout << "[CameraView] Auto Exposure: " << (autoEnabled ? "ON" : "OFF") << std::endl;
}

void CameraView::onAutoGainChanged(Qt::CheckState state)
{
  bool autoEnabled = (state == Qt::Checked);

  // Enable/disable manual spinbox and update button
  m_gainSpinBox->setEnabled(!autoEnabled && m_isConnected);
  if (m_gainUpdateBtn) m_gainUpdateBtn->setEnabled(!autoEnabled && m_isConnected);

  // Use worker for non-blocking camera update
  if (m_parameterWorker && m_useParameterWorker) {
    m_parameterWorker->requestAutoGain(autoEnabled);
  } else {
    // Fallback to direct apply
    applyCameraSettings();
  }

  std::cout << "[CameraView] Auto Gain: " << (autoEnabled ? "ON" : "OFF") << std::endl;
}

void CameraView::onExposureUpdateClicked()
{
  int value = m_exposureSpinBox->value();
  updateExposureLabel(value);

  // Use worker for non-blocking camera update
  if (m_parameterWorker && m_useParameterWorker) {
    m_parameterWorker->requestExposure(value);
  } else {
    applyCameraSettings();
  }
  std::cout << "[CameraView] Exposure updated: " << value << " μs" << std::endl;
}

void CameraView::onGainUpdateClicked()
{
  double value = m_gainSpinBox->value();
  updateGainLabel(value);

  // Use worker for non-blocking camera update
  if (m_parameterWorker && m_useParameterWorker) {
    m_parameterWorker->requestGain(value);
  } else {
    applyCameraSettings();
  }
  std::cout << "[CameraView] Gain updated: " << value << std::endl;
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
  settings.exposure_time = m_exposureSpinBox->value();
  settings.gain = m_gainSpinBox->value();
  settings.frame_rate = m_frameRateSpinBox ? m_frameRateSpinBox->value() : 8.0;

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
  m_exposureSpinBox->blockSignals(true);
  m_gainSpinBox->blockSignals(true);
  if (m_frameRateSpinBox) m_frameRateSpinBox->blockSignals(true);

  m_autoExposureCheck->setChecked(settings.auto_exposure);
  m_autoGainCheck->setChecked(settings.auto_gain);

  int exposureValue = static_cast<int>(settings.exposure_time);
  if (exposureValue < 1) exposureValue = 1;
  if (exposureValue > 100000) exposureValue = 100000;
  m_exposureSpinBox->setValue(exposureValue);

  double gainValue = settings.gain;
  if (gainValue < 0.0) gainValue = 0.0;
  if (gainValue > 24.0) gainValue = 24.0;
  m_gainSpinBox->setValue(gainValue);

  // Load frame rate
  if (m_frameRateSpinBox) m_frameRateSpinBox->setValue(settings.frame_rate);

  // Update labels
  updateExposureLabel(exposureValue);
  updateGainLabel(gainValue);
  updateFrameRateLabel(settings.frame_rate);

  // Update spinbox and button enabled state
  m_exposureSpinBox->setEnabled(!settings.auto_exposure);
  if (m_exposureUpdateBtn) m_exposureUpdateBtn->setEnabled(!settings.auto_exposure);
  m_gainSpinBox->setEnabled(!settings.auto_gain);
  if (m_gainUpdateBtn) m_gainUpdateBtn->setEnabled(!settings.auto_gain);
  // Frame rate is always manually controlled
  if (m_frameRateSpinBox) m_frameRateSpinBox->setEnabled(true);
  if (m_frameRateUpdateBtn) m_frameRateUpdateBtn->setEnabled(true);

  // Unblock signals
  m_autoExposureCheck->blockSignals(false);
  m_autoGainCheck->blockSignals(false);
  m_exposureSpinBox->blockSignals(false);
  m_gainSpinBox->blockSignals(false);
  if (m_frameRateSpinBox) m_frameRateSpinBox->blockSignals(false);

  std::cout << "[CameraView] Loaded camera settings: "
    << "Exposure=" << settings.exposure_time << "μs, "
    << "Gain=" << settings.gain << ", "
    << "FrameRate=" << settings.frame_rate << "fps, "
    << "AutoExp=" << (settings.auto_exposure ? "ON" : "OFF") << ", "
    << "AutoGain=" << (settings.auto_gain ? "ON" : "OFF") << std::endl;
}

void CameraView::updateExposureLabel(int microseconds)
{
  if (microseconds >= 1000) {
    double ms = microseconds / 1000.0;
    m_exposureValueLabel->setText(QString("%1 ms").arg(ms, 0, 'f', 2));
  } else {
    m_exposureValueLabel->setText(QString("%1 μs").arg(microseconds));
  }
}

void CameraView::updateGainLabel(double gain)
{
  m_gainValueLabel->setText(QString::number(gain, 'f', 1));
}

void CameraView::updateFrameRateLabel(double fps)
{
  if (m_frameRateValueLabel) {
    m_frameRateValueLabel->setText(QString("%1 fps").arg(fps, 0, 'f', 1));
  }
}

void CameraView::onFrameRateUpdateClicked()
{
  if (!m_frameRateSpinBox) return;

  double value = m_frameRateSpinBox->value();
  updateFrameRateLabel(value);

  // Use worker for non-blocking camera update
  if (m_parameterWorker && m_useParameterWorker) {
    m_parameterWorker->requestFrameRate(value);
  } else {
    applyCameraSettings();
  }
  std::cout << "[CameraView] Frame rate updated: " << value << " fps" << std::endl;
}

// ============================================================================
// CAMERA PARAMETER WORKER - Background processing for UI responsiveness
// ============================================================================

void CameraView::setupParameterWorker()
{
  if (!m_useParameterWorker) {
    std::cout << "[CameraView] Parameter worker disabled" << std::endl;
    return;
  }

  m_parameterWorker = new CameraParameterWorker(m_context, m_cameraId);
  connectWorkerSignals();
  m_parameterWorker->start();

  std::cout << "[CameraView] Parameter worker started for: " << m_cameraId << std::endl;
}

void CameraView::connectWorkerSignals()
{
  if (!m_parameterWorker) {
    return;
  }

  // Connect worker signals to UI update slots (Qt::QueuedConnection for thread safety)
  connect(m_parameterWorker, &CameraParameterWorker::exposureUpdated,
          this, &CameraView::onWorkerExposureUpdated, Qt::QueuedConnection);
  connect(m_parameterWorker, &CameraParameterWorker::gainUpdated,
          this, &CameraView::onWorkerGainUpdated, Qt::QueuedConnection);
  connect(m_parameterWorker, &CameraParameterWorker::frameRateUpdated,
          this, &CameraView::onWorkerFrameRateUpdated, Qt::QueuedConnection);
  connect(m_parameterWorker, &CameraParameterWorker::autoExposureChanged,
          this, &CameraView::onWorkerAutoExposureChanged, Qt::QueuedConnection);
  connect(m_parameterWorker, &CameraParameterWorker::autoGainChanged,
          this, &CameraView::onWorkerAutoGainChanged, Qt::QueuedConnection);
  connect(m_parameterWorker, &CameraParameterWorker::workerError,
          this, &CameraView::onWorkerError, Qt::QueuedConnection);
}

void CameraView::onWorkerExposureUpdated(int microseconds)
{
  // Update UI with actual value from camera (block signals to prevent loop)
  m_exposureSpinBox->blockSignals(true);
  m_exposureSpinBox->setValue(microseconds);
  m_exposureSpinBox->blockSignals(false);
  updateExposureLabel(microseconds);
}

void CameraView::onWorkerGainUpdated(double gain)
{
  // Update UI with actual value from camera
  m_gainSpinBox->blockSignals(true);
  m_gainSpinBox->setValue(gain);
  m_gainSpinBox->blockSignals(false);
  updateGainLabel(gain);
}

void CameraView::onWorkerFrameRateUpdated(double fps)
{
  // Update UI with actual value from camera
  if (m_frameRateSpinBox) {
    m_frameRateSpinBox->blockSignals(true);
    m_frameRateSpinBox->setValue(fps);
    m_frameRateSpinBox->blockSignals(false);
  }
  updateFrameRateLabel(fps);
}

void CameraView::onWorkerAutoExposureChanged(bool enabled)
{
  // Update checkbox state
  m_autoExposureCheck->blockSignals(true);
  m_autoExposureCheck->setChecked(enabled);
  m_autoExposureCheck->blockSignals(false);

  // Update control enabled state
  m_exposureSpinBox->setEnabled(!enabled && m_isConnected);
  if (m_exposureUpdateBtn) m_exposureUpdateBtn->setEnabled(!enabled && m_isConnected);
}

void CameraView::onWorkerAutoGainChanged(bool enabled)
{
  // Update checkbox state
  m_autoGainCheck->blockSignals(true);
  m_autoGainCheck->setChecked(enabled);
  m_autoGainCheck->blockSignals(false);

  // Update control enabled state
  m_gainSpinBox->setEnabled(!enabled && m_isConnected);
  if (m_gainUpdateBtn) m_gainUpdateBtn->setEnabled(!enabled && m_isConnected);
}

void CameraView::onWorkerError(QString error)
{
  std::cerr << "[CameraView] Worker error: " << error.toStdString() << std::endl;
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

void CameraView::onZoomUpdateClicked()
{
  int value = m_zoomSpinBox->value();
  m_viewport->setZoomPercent(value);
  updateZoomLabel(value);
  std::cout << "[CameraView] Zoom updated: " << value << "%" << std::endl;
}

void CameraView::onZoomResetClicked()
{
  m_zoomSpinBox->setValue(100);
  m_viewport->setZoomPercent(100);
  updateZoomLabel(100);

  // Also reset pan position
  if (m_viewport) {
    m_viewport->resetPan();
  }
}

void CameraView::updateZoomLabel(int zoomPercent)
{
  // 100% = 1.0x, 200% = 2.0x, etc.
  double zoomFactor = zoomPercent / 100.0;
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

  // Sync with CameraManager state in case camera was connected while page was hidden
  syncWithManagerState();

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
    << " - Manager(Connected: " << status.connected
    << ", Grabbing: " << status.grabbing
    << "), UI Connected: " << m_isConnected << std::endl;

  // Check if manager says camera is connected and grabbing
  if (status.connected && status.grabbing) {
    // Camera is connected in manager - ensure UI reflects this

    // If UI doesn't have a subscriber yet, or thinks it's disconnected, set up subscription
    if (!m_isConnected || !m_subscriber) {
      std::cout << "[CameraView] Setting up subscriber for connected camera" << std::endl;

      // Clean up old subscriber if exists
      if (m_subscriber) {
        m_context->cameraManager->UnsubscribeFromFrames(m_subscriber->GetSubscriberId());
        m_subscriber.reset();
      }

      // Clean up old timer if exists
      if (m_displayUpdateTimer) {
        m_displayUpdateTimer->stop();
        delete m_displayUpdateTimer;
        m_displayUpdateTimer = nullptr;
      }

      // Create subscriber to receive frames
      m_subscriber = std::make_shared<LiveVideoSubscriber>(m_cameraId, "CameraView_" + m_cameraId);
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

      // Load current camera settings (use worker if available)
      if (m_parameterWorker && m_useParameterWorker) {
        m_parameterWorker->requestReadAll();  // Non-blocking read via worker
      } else {
        loadCurrentSettings();  // Fallback to direct read
      }

      emit cameraConnected(QString::fromStdString(m_cameraId));
    }

    // Always update UI status to connected (fixes stale state)
    updateConnectionStatus(true);

    // Enable controls
    m_zoomSpinBox->setEnabled(true);
    if (m_zoomUpdateBtn) m_zoomUpdateBtn->setEnabled(true);
    m_zoomResetButton->setEnabled(true);
    m_autoExposureCheck->setEnabled(true);
    m_autoGainCheck->setEnabled(true);
    m_exposureSpinBox->setEnabled(true);
    if (m_exposureUpdateBtn) m_exposureUpdateBtn->setEnabled(true);
    m_gainSpinBox->setEnabled(true);
    if (m_gainUpdateBtn) m_gainUpdateBtn->setEnabled(true);
    if (m_frameRateSpinBox) m_frameRateSpinBox->setEnabled(true);
    if (m_frameRateUpdateBtn) m_frameRateUpdateBtn->setEnabled(true);
    m_saveImageButton->setEnabled(true);

    // Force refresh viewport after sync
    if (m_viewport) {
      m_viewport->forceRefresh();
    }

    std::cout << "[CameraView] Sync complete - camera is connected" << std::endl;
  }
  else {
    // Camera is not connected or not grabbing in manager
    std::cout << "[CameraView] Camera not connected/grabbing in manager" << std::endl;

    // Always update UI to disconnected state (fixes stale state)
    updateConnectionStatus(false);

    // Disable controls
    m_zoomSpinBox->setEnabled(false);
    if (m_zoomUpdateBtn) m_zoomUpdateBtn->setEnabled(false);
    m_zoomResetButton->setEnabled(false);
    m_autoExposureCheck->setEnabled(false);
    m_autoGainCheck->setEnabled(false);
    m_exposureSpinBox->setEnabled(false);
    if (m_exposureUpdateBtn) m_exposureUpdateBtn->setEnabled(false);
    m_gainSpinBox->setEnabled(false);
    if (m_gainUpdateBtn) m_gainUpdateBtn->setEnabled(false);
    if (m_frameRateSpinBox) m_frameRateSpinBox->setEnabled(false);
    if (m_frameRateUpdateBtn) m_frameRateUpdateBtn->setEnabled(false);
    m_saveImageButton->setEnabled(false);

    // Force refresh to show disconnected state
    if (m_viewport) {
      m_viewport->forceRefresh();
    }
  }
}

void CameraView::onApplySettingsTimer()
{
  // Throttled apply - only called 500ms after last slider change
  if (m_settingsPending) {
    m_settingsPending = false;
    applyCameraSettings();
    std::cout << "[CameraView] Applied throttled settings" << std::endl;
  }
}