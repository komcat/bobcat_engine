// CameraSettingsWidget.cpp
#include "CameraSettingsWidget.h"
#include "CameraManager.h"
#include "ICameraHardware.h"
#include "LiveVideoSubscriber.h"
#include "CameraImageWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QIntValidator>
#include <QFile>
#include <iostream>
#include <nlohmann/json.hpp>

// Helper function to load UI throttle delay from config
static int loadUIThrottleDelay() {
    const int DEFAULT_THROTTLE_MS = 350;
    try {
        QFile file("config/SYSTEM/systemdelay.json");
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray data = file.readAll();
            nlohmann::json config = nlohmann::json::parse(data.constData());
            if (config.contains("ui_throttle_ms")) {
                return config["ui_throttle_ms"].get<int>();
            }
        }
    } catch (...) {}
    return DEFAULT_THROTTLE_MS;
}

CameraSettingsWidget::CameraSettingsWidget(CameraManager* cameraManager, QWidget* parent)
  : QWidget(parent)
  , m_cameraManager(cameraManager)
{
  setupUI();

  // Setup display timer for live preview (30 fps)
  m_displayTimer = new QTimer(this);
  connect(m_displayTimer, &QTimer::timeout, this, &CameraSettingsWidget::onDisplayTimer);

  // Setup throttle timer for settings apply (500ms single-shot)
  // This prevents flooding the camera with commands when dragging sliders
  m_applySettingsTimer = new QTimer(this);
  m_applySettingsTimer->setSingleShot(true);
  connect(m_applySettingsTimer, &QTimer::timeout, this, &CameraSettingsWidget::onApplySettingsTimer);

  // Select first camera if available
  if (m_cameraCombo->count() > 0) {
    m_currentCameraId = m_cameraCombo->currentText().toStdString();
    loadCurrentSettings();
    startPreview();
  }
}

CameraSettingsWidget::~CameraSettingsWidget()
{
  stopPreview();
  std::cout << "[CameraSettingsWidget] Destroyed" << std::endl;
}

void CameraSettingsWidget::setupUI()
{
  // Dark theme stylesheet
  setStyleSheet(R"(
    QWidget {
      background-color: #1e1e1e;
      color: #d4d4d4;
    }
    QGroupBox {
      background-color: #252525;
      border: 1px solid #3f3f46;
      border-radius: 4px;
      margin-top: 10px;
      padding: 15px;
      font-weight: bold;
    }
    QGroupBox::title {
      subcontrol-origin: margin;
      subcontrol-position: top left;
      padding: 2px 8px;
      color: #ffffff;
    }
    QCheckBox {
      color: #d4d4d4;
      spacing: 8px;
    }
    QCheckBox::indicator {
      width: 18px;
      height: 18px;
      border: 2px solid #555555;
      border-radius: 4px;
      background-color: #3c3c3c;
    }
    QCheckBox::indicator:checked {
      background-color: #0e639c;
      border-color: #0e639c;
    }
    QComboBox {
      background-color: #3c3c3c;
      color: #d4d4d4;
      border: 1px solid #555555;
      border-radius: 4px;
      padding: 6px 10px;
      min-width: 150px;
    }
    QComboBox::drop-down {
      border: none;
    }
    QComboBox QAbstractItemView {
      background-color: #3c3c3c;
      color: #d4d4d4;
      selection-background-color: #0e639c;
    }
    QSlider::groove:horizontal {
      border: 1px solid #3f3f46;
      height: 6px;
      background-color: #3c3c3c;
      border-radius: 3px;
    }
    QSlider::handle:horizontal {
      background-color: #0e639c;
      border: 1px solid #0c5280;
      width: 18px;
      margin: -6px 0;
      border-radius: 9px;
    }
    QSlider::handle:horizontal:hover {
      background-color: #1177bb;
    }
    QSlider::sub-page:horizontal {
      background-color: #0e639c;
      border-radius: 3px;
    }
    QLabel {
      color: #d4d4d4;
    }
    QSplitter::handle {
      background-color: #3f3f46;
    }
  )");

  QHBoxLayout* mainLayout = new QHBoxLayout(this);
  mainLayout->setContentsMargins(5, 5, 5, 5);
  mainLayout->setSpacing(5);

  // Create splitter for preview and settings
  QSplitter* splitter = new QSplitter(Qt::Horizontal);
  splitter->setHandleWidth(3);

  // ========== LEFT PANEL: Live Preview ==========
  QWidget* previewPanel = new QWidget();
  previewPanel->setStyleSheet("QWidget { background-color: #252525; }");
  QVBoxLayout* previewLayout = new QVBoxLayout(previewPanel);
  previewLayout->setContentsMargins(5, 5, 5, 5);
  previewLayout->setSpacing(5);

  QLabel* previewTitle = new QLabel("Live Preview");
  previewTitle->setStyleSheet("QLabel { font-weight: bold; color: #ffffff; font-size: 12px; }");
  previewTitle->setAlignment(Qt::AlignCenter);
  previewLayout->addWidget(previewTitle);

  m_previewWidget = new CameraImageWidget();
  m_previewWidget->setMinimumSize(200, 150);
  m_previewWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  m_previewWidget->setBackgroundColor(QColor(30, 30, 30));
  m_previewWidget->setPlaceholderText("No camera feed");
  m_previewWidget->setCrosshairEnabled(true);
  previewLayout->addWidget(m_previewWidget, 1);

  splitter->addWidget(previewPanel);

  // ========== RIGHT PANEL: Settings ==========
  QWidget* settingsPanel = new QWidget();
  QVBoxLayout* settingsPanelLayout = new QVBoxLayout(settingsPanel);
  settingsPanelLayout->setContentsMargins(5, 5, 5, 5);
  settingsPanelLayout->setSpacing(10);

  // Camera selector
  QGroupBox* cameraGroup = new QGroupBox("Camera");
  QHBoxLayout* cameraLayout = new QHBoxLayout(cameraGroup);

  m_cameraCombo = new QComboBox();
  refreshCameraList();
  connect(m_cameraCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &CameraSettingsWidget::onCameraSelectionChanged);
  cameraLayout->addWidget(m_cameraCombo);
  cameraLayout->addStretch();

  settingsPanelLayout->addWidget(cameraGroup);

  // Settings group
  QGroupBox* settingsGroup = new QGroupBox("Exposure Settings");
  QVBoxLayout* settingsLayout = new QVBoxLayout(settingsGroup);
  settingsLayout->setSpacing(8);

  // Auto Exposure checkbox
  m_autoExposureCheck = new QCheckBox("Auto Exposure");
  connect(m_autoExposureCheck, &QCheckBox::checkStateChanged,
          this, &CameraSettingsWidget::onAutoExposureChanged);
  settingsLayout->addWidget(m_autoExposureCheck);

  // Auto Gain checkbox
  m_autoGainCheck = new QCheckBox("Auto Gain");
  connect(m_autoGainCheck, &QCheckBox::checkStateChanged,
          this, &CameraSettingsWidget::onAutoGainChanged);
  settingsLayout->addWidget(m_autoGainCheck);

  settingsLayout->addSpacing(5);

  // Exposure control
  QLabel* expLabel = new QLabel("Exposure:");
  expLabel->setStyleSheet("QLabel { font-weight: bold; }");
  settingsLayout->addWidget(expLabel);

  m_exposureSlider = new QSlider(Qt::Horizontal);
  m_exposureSlider->setMinimum(1);
  m_exposureSlider->setMaximum(100000);  // Up to 100ms
  m_exposureSlider->setSingleStep(50);   // 50µs steps to reduce command frequency
  m_exposureSlider->setPageStep(500);    // Page step 500µs
  m_exposureSlider->setValue(1000);
  connect(m_exposureSlider, &QSlider::valueChanged,
          this, &CameraSettingsWidget::onExposureSliderChanged);
  settingsLayout->addWidget(m_exposureSlider);

  // Exposure value input row (text input + unit label)
  QHBoxLayout* exposureValueLayout = new QHBoxLayout();
  exposureValueLayout->setContentsMargins(0, 0, 0, 0);
  exposureValueLayout->setSpacing(5);

  m_exposureInput = new QLineEdit();
  m_exposureInput->setFixedWidth(80);
  m_exposureInput->setValidator(new QIntValidator(1, 100000, this));  // Allow only integers 1-100000
  m_exposureInput->setText("1000");
  m_exposureInput->setStyleSheet(R"(
    QLineEdit {
      background-color: #3c3c3c;
      color: #00ff00;
      border: 1px solid #555555;
      border-radius: 3px;
      padding: 4px 6px;
      font-family: monospace;
    }
    QLineEdit:focus {
      border-color: #0e639c;
    }
  )");
  connect(m_exposureInput, &QLineEdit::editingFinished,
          this, &CameraSettingsWidget::onExposureInputChanged);
  exposureValueLayout->addWidget(m_exposureInput);

  m_exposureUnitLabel = new QLabel("us");
  m_exposureUnitLabel->setStyleSheet("QLabel { color: #00ff00; font-family: monospace; }");
  exposureValueLayout->addWidget(m_exposureUnitLabel);
  exposureValueLayout->addStretch();

  settingsLayout->addLayout(exposureValueLayout);

  settingsLayout->addSpacing(5);

  // Gain control
  QLabel* gainLabel = new QLabel("Gain:");
  gainLabel->setStyleSheet("QLabel { font-weight: bold; }");
  settingsLayout->addWidget(gainLabel);

  m_gainSlider = new QSlider(Qt::Horizontal);
  m_gainSlider->setMinimum(0);
  m_gainSlider->setMaximum(240);  // 0 to 24.0 (x10 for precision)
  m_gainSlider->setValue(10);     // Default 1.0
  connect(m_gainSlider, &QSlider::valueChanged,
          this, &CameraSettingsWidget::onGainSliderChanged);
  settingsLayout->addWidget(m_gainSlider);

  m_gainValueLabel = new QLabel("1.0");
  m_gainValueLabel->setStyleSheet("QLabel { color: #00ff00; font-family: monospace; }");
  settingsLayout->addWidget(m_gainValueLabel);

  settingsPanelLayout->addWidget(settingsGroup);
  settingsPanelLayout->addStretch();

  splitter->addWidget(settingsPanel);

  // Set initial splitter sizes (60% preview, 40% settings)
  splitter->setSizes({300, 200});

  mainLayout->addWidget(splitter);
}

void CameraSettingsWidget::refreshCameraList()
{
  m_cameraCombo->blockSignals(true);
  m_cameraCombo->clear();

  if (m_cameraManager) {
    auto cameraStatuses = m_cameraManager->GetAllCameraStatus();
    for (const auto& status : cameraStatuses) {
      m_cameraCombo->addItem(QString::fromStdString(status.id));
    }
  }

  m_cameraCombo->blockSignals(false);
}

void CameraSettingsWidget::setCurrentCamera(const std::string& cameraId)
{
  int index = m_cameraCombo->findText(QString::fromStdString(cameraId));
  if (index >= 0) {
    m_cameraCombo->setCurrentIndex(index);
  }
}

void CameraSettingsWidget::reloadSettings()
{
  loadCurrentSettings();
}

void CameraSettingsWidget::onCameraSelectionChanged(int index)
{
  if (index < 0) return;

  // Stop current preview before switching
  stopPreview();

  m_currentCameraId = m_cameraCombo->currentText().toStdString();
  loadCurrentSettings();

  // Start preview with new camera
  startPreview();

  emit cameraChanged(QString::fromStdString(m_currentCameraId));
  std::cout << "[CameraSettingsWidget] Camera changed to: " << m_currentCameraId << std::endl;
}

void CameraSettingsWidget::onAutoExposureChanged(Qt::CheckState state)
{
  bool autoEnabled = (state == Qt::Checked);
  m_exposureSlider->setEnabled(!autoEnabled);
  m_exposureInput->setEnabled(!autoEnabled);
  applySettings();

  std::cout << "[CameraSettingsWidget] Auto Exposure: " << (autoEnabled ? "ON" : "OFF") << std::endl;
}

void CameraSettingsWidget::onAutoGainChanged(Qt::CheckState state)
{
  bool autoEnabled = (state == Qt::Checked);
  m_gainSlider->setEnabled(!autoEnabled);
  applySettings();

  std::cout << "[CameraSettingsWidget] Auto Gain: " << (autoEnabled ? "ON" : "OFF") << std::endl;
}

void CameraSettingsWidget::onExposureSliderChanged(int value)
{
  updateExposureLabel(value);

  // Throttle: restart timer (delay loaded from config/SYSTEM/systemdelay.json)
  // This prevents flooding the camera with commands while dragging
  m_settingsPending = true;
  m_applySettingsTimer->start(loadUIThrottleDelay());
}

void CameraSettingsWidget::onExposureInputChanged()
{
  bool ok;
  int value = m_exposureInput->text().toInt(&ok);
  if (ok) {
    // Clamp to valid range
    if (value < 1) value = 1;
    if (value > 100000) value = 100000;

    // Update text if clamped
    if (value != m_exposureInput->text().toInt()) {
      m_exposureInput->setText(QString::number(value));
    }

    // Update slider (block signals to avoid circular updates)
    m_exposureSlider->blockSignals(true);
    m_exposureSlider->setValue(value);
    m_exposureSlider->blockSignals(false);

    // Update unit label
    if (value >= 1000) {
      double ms = value / 1000.0;
      m_exposureUnitLabel->setText(QString("us (%1 ms)").arg(ms, 0, 'f', 2));
    } else {
      m_exposureUnitLabel->setText("us");
    }

    applySettings();
  }
}

void CameraSettingsWidget::onGainSliderChanged(int value)
{
  double gain = value / 10.0;
  updateGainLabel(gain);

  // Throttle: restart timer (delay loaded from config/SYSTEM/systemdelay.json)
  m_settingsPending = true;
  m_applySettingsTimer->start(loadUIThrottleDelay());
}

void CameraSettingsWidget::applySettings()
{
  if (!m_cameraManager || m_currentCameraId.empty()) {
    return;
  }

  ICameraHardware::ExposureSettings settings;
  settings.auto_exposure = m_autoExposureCheck->isChecked();
  settings.auto_gain = m_autoGainCheck->isChecked();
  settings.exposure_time = m_exposureSlider->value();
  settings.gain = m_gainSlider->value() / 10.0;

  if (m_cameraManager->ApplyExposureSettings(m_currentCameraId, settings)) {
    emit settingsChanged();
  } else {
    std::cerr << "[CameraSettingsWidget] Failed to apply camera settings" << std::endl;
  }
}

void CameraSettingsWidget::loadCurrentSettings()
{
  if (!m_cameraManager || m_currentCameraId.empty()) {
    return;
  }

  auto settings = m_cameraManager->GetExposureSettings(m_currentCameraId);

  // Block signals during load
  m_autoExposureCheck->blockSignals(true);
  m_autoGainCheck->blockSignals(true);
  m_exposureSlider->blockSignals(true);
  m_exposureInput->blockSignals(true);
  m_gainSlider->blockSignals(true);

  m_autoExposureCheck->setChecked(settings.auto_exposure);
  m_autoGainCheck->setChecked(settings.auto_gain);

  int exposureValue = static_cast<int>(settings.exposure_time);
  if (exposureValue < 1) exposureValue = 1;
  if (exposureValue > 100000) exposureValue = 100000;
  m_exposureSlider->setValue(exposureValue);

  int gainValue = static_cast<int>(settings.gain * 10);
  if (gainValue < 0) gainValue = 0;
  if (gainValue > 240) gainValue = 240;
  m_gainSlider->setValue(gainValue);

  // Update labels
  updateExposureLabel(exposureValue);
  updateGainLabel(settings.gain);

  // Update slider/input enabled state
  m_exposureSlider->setEnabled(!settings.auto_exposure);
  m_exposureInput->setEnabled(!settings.auto_exposure);
  m_gainSlider->setEnabled(!settings.auto_gain);

  // Unblock signals
  m_autoExposureCheck->blockSignals(false);
  m_autoGainCheck->blockSignals(false);
  m_exposureSlider->blockSignals(false);
  m_exposureInput->blockSignals(false);
  m_gainSlider->blockSignals(false);

  std::cout << "[CameraSettingsWidget] Loaded settings for " << m_currentCameraId << ": "
            << "Exp=" << settings.exposure_time << "us, "
            << "Gain=" << settings.gain << ", "
            << "AutoExp=" << (settings.auto_exposure ? "ON" : "OFF") << ", "
            << "AutoGain=" << (settings.auto_gain ? "ON" : "OFF") << std::endl;
}

void CameraSettingsWidget::updateExposureLabel(int microseconds)
{
  // Update the text input field (always shows microseconds)
  m_exposureInput->blockSignals(true);
  m_exposureInput->setText(QString::number(microseconds));
  m_exposureInput->blockSignals(false);

  // Update unit label to show helpful conversion for large values
  if (microseconds >= 1000) {
    double ms = microseconds / 1000.0;
    m_exposureUnitLabel->setText(QString("us (%1 ms)").arg(ms, 0, 'f', 2));
  } else {
    m_exposureUnitLabel->setText("us");
  }
}

void CameraSettingsWidget::updateGainLabel(double gain)
{
  m_gainValueLabel->setText(QString::number(gain, 'f', 1));
}

// =============================================================================
// LIVE PREVIEW METHODS
// =============================================================================

void CameraSettingsWidget::setPreviewEnabled(bool enabled)
{
  if (m_previewEnabled == enabled) return;

  m_previewEnabled = enabled;
  if (enabled) {
    startPreview();
  } else {
    stopPreview();
  }
}

void CameraSettingsWidget::startPreview()
{
  if (!m_previewEnabled || !m_cameraManager || m_currentCameraId.empty()) {
    return;
  }

  // Stop any existing preview
  stopPreview();

  // Create subscriber for the current camera
  m_subscriber = std::make_shared<LiveVideoSubscriber>(m_currentCameraId, "CameraSettingsWidget");
  m_cameraManager->SubscribeToFrames(m_subscriber);

  // Initialize status from camera manager
  m_subscriber->InitializeStatusFromManager(m_cameraManager);

  // Start display timer (30 fps)
  if (m_displayTimer) {
    m_displayTimer->start(33);  // ~30 fps
  }

  std::cout << "[CameraSettingsWidget] Started live preview for camera: " << m_currentCameraId << std::endl;
}

void CameraSettingsWidget::stopPreview()
{
  // Stop timer
  if (m_displayTimer && m_displayTimer->isActive()) {
    m_displayTimer->stop();
  }

  // Clear subscriber (CameraManager will clean up)
  if (m_subscriber) {
    m_subscriber.reset();
  }

  // Clear display
  if (m_previewWidget) {
    m_previewWidget->clearDisplay();
  }

  std::cout << "[CameraSettingsWidget] Stopped live preview" << std::endl;
}

void CameraSettingsWidget::onDisplayTimer()
{
  if (!m_subscriber || !m_previewWidget) {
    return;
  }

  // Check if we have a new frame
  if (m_subscriber->HasNewFrame()) {
    CameraFrameData frame = m_subscriber->GetLatestFrame();
    m_subscriber->MarkFrameConsumed();

    // Display the frame
    m_previewWidget->setFrame(frame);
  }
}

void CameraSettingsWidget::onApplySettingsTimer()
{
  // Throttled apply - only called 500ms after last slider change
  if (m_settingsPending) {
    m_settingsPending = false;
    applySettings();
    std::cout << "[CameraSettingsWidget] Applied throttled settings" << std::endl;
  }
}
