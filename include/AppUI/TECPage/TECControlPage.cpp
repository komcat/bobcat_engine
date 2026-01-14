// TECControlPage.cpp
#include "TECControlPage.h"
#include "ApplicationContext.h"
#include "TECManager.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QShowEvent>
#include <QHideEvent>
#include <iostream>
#include <algorithm>

TECControlPage::TECControlPage(ApplicationContext* context, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_isConnected(false)
  , m_isUpdatingFromDevice(false)
{
  setupUI();
  updateDeviceList();

  // Create polling timer (500ms) but don't start it yet
  // Timer will start in showEvent() when page becomes visible
  m_pollTimer = new QTimer(this);
  connect(m_pollTimer, &QTimer::timeout, this, &TECControlPage::onPollTimer);
}

TECControlPage::~TECControlPage()
{
  if (m_pollTimer) {
    m_pollTimer->stop();
  }
}

void TECControlPage::setupUI()
{
  QHBoxLayout* mainLayout = new QHBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  createLeftPanel();
  createRightPanel();

  enableControls(false);
}

void TECControlPage::createLeftPanel()
{
  // Left panel (15%)
  QWidget* leftPanel = new QWidget();
  leftPanel->setStyleSheet("QWidget { background-color: #1e1e1e; }");
  leftPanel->setMaximumWidth(200);

  QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
  leftLayout->setContentsMargins(10, 10, 10, 10);

  QLabel* titleLabel = new QLabel("TEC Devices");
  titleLabel->setStyleSheet("QLabel { color: #ffffff; font-size: 14px; font-weight: bold; }");
  leftLayout->addWidget(titleLabel);

  m_deviceList = new QListWidget();
  m_deviceList->setStyleSheet(
    "QListWidget { "
    "  background-color: #2d2d30; "
    "  color: #d4d4d4; "
    "  border: 1px solid #3f3f46; "
    "} "
    "QListWidget::item:selected { "
    "  background-color: #0e639c; "
    "  color: white; "
    "}");
  connect(m_deviceList, &QListWidget::itemSelectionChanged, this, &TECControlPage::onDeviceSelected);
  leftLayout->addWidget(m_deviceList);

  QHBoxLayout* mainLayout = qobject_cast<QHBoxLayout*>(layout());
  if (!mainLayout) {
    mainLayout = new QHBoxLayout(this);
  }
  mainLayout->addWidget(leftPanel, 15);
}

void TECControlPage::createRightPanel()
{
  // Right panel (85%)
  QWidget* rightPanel = new QWidget();
  rightPanel->setStyleSheet("QWidget { background-color: #2d2d30; }");

  QVBoxLayout* rightLayout = new QVBoxLayout(rightPanel);
  rightLayout->setContentsMargins(20, 20, 20, 20);
  rightLayout->setSpacing(15);

  // Common styles - matching DarkTheme.cpp
  const QString groupBoxStyle =
    "QGroupBox { color: #ffffff; font-weight: bold; border: 1px solid #555555; "
    "background-color: #2d2d30; border-radius: 4px; padding-top: 12px; margin-top: 12px; } "
    "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; color: #ffffff; }";

  const QString labelStyle = "QLabel { color: #ffffff; background-color: transparent; }";

  // Slider style matching DarkTheme
  const QString sliderStyle =
    "QSlider::groove:horizontal { background: #3c3c3c; height: 6px; border-radius: 3px; } "
    "QSlider::handle:horizontal { background: #0e639c; width: 16px; margin: -5px 0; border-radius: 8px; } "
    "QSlider::handle:horizontal:hover { background: #1177bb; }";

  // SpinBox style matching DarkTheme
  const QString spinBoxStyle =
    "QSpinBox { "
    "  background-color: #3c3c3c; "
    "  color: #ffffff; "
    "  border: 1px solid #555555; "
    "  border-radius: 3px; "
    "  padding: 4px; "
    "  min-height: 24px; "
    "  min-width: 80px; "
    "} "
    "QSpinBox:focus { border: 1px solid #0e639c; } "
    "QSpinBox::up-button, QSpinBox::down-button { "
    "  background-color: #555555; "
    "  border: none; "
    "  width: 16px; "
    "} "
    "QSpinBox::up-button:hover, QSpinBox::down-button:hover { "
    "  background-color: #0e639c; "
    "}";

  // ON button - green for visual feedback (semantic color for "active/on" state)
  const QString onButtonStyle =
    "QPushButton { "
    "  background-color: #27ae60; "
    "  color: white; "
    "  border: 1px solid #1e8449; "
    "  border-radius: 3px; "
    "  padding: 6px 12px; "
    "  font-weight: 600; "
    "  min-height: 24px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #2ecc71; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #1e8449; "
    "} "
    "QPushButton:disabled { "
    "  background-color: #555555; "
    "  color: #888888; "
    "  border-color: #3f3f46; "
    "}";

  // OFF button - red for visual feedback (semantic color for "inactive/off" state)
  const QString offButtonStyle =
    "QPushButton { "
    "  background-color: #c0392b; "
    "  color: white; "
    "  border: 1px solid #922b21; "
    "  border-radius: 3px; "
    "  padding: 6px 12px; "
    "  font-weight: 600; "
    "  min-height: 24px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #e74c3c; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #922b21; "
    "} "
    "QPushButton:disabled { "
    "  background-color: #555555; "
    "  color: #888888; "
    "  border-color: #3f3f46; "
    "}";

  // === CONNECTION GROUP ===
  QGroupBox* connectionGroup = new QGroupBox("Connection");
  connectionGroup->setStyleSheet(groupBoxStyle);
  QHBoxLayout* connectionLayout = new QHBoxLayout(connectionGroup);

  // Connect button - uses theme accent color
  m_connectButton = new QPushButton("Connect");
  m_connectButton->setStyleSheet(
    "QPushButton { background-color: #0e639c; color: white; border: 1px solid #0c5280; "
    "border-radius: 3px; padding: 6px 12px; font-weight: 600; min-height: 24px; } "
    "QPushButton:hover { background-color: #1177bb; } "
    "QPushButton:pressed { background-color: #0c5280; } "
    "QPushButton:disabled { background-color: #555555; color: #888888; border-color: #3f3f46; }");
  connect(m_connectButton, &QPushButton::clicked, this, &TECControlPage::onConnectClicked);
  connectionLayout->addWidget(m_connectButton);

  // Disconnect button - uses red for "stop" action
  m_disconnectButton = new QPushButton("Disconnect");
  m_disconnectButton->setStyleSheet(offButtonStyle);
  m_disconnectButton->setEnabled(false);
  connect(m_disconnectButton, &QPushButton::clicked, this, &TECControlPage::onDisconnectClicked);
  connectionLayout->addWidget(m_disconnectButton);

  m_connectionStatus = new QLabel("Not Connected");
  m_connectionStatus->setStyleSheet("QLabel { color: #e74c3c; font-weight: bold; background-color: transparent; }");
  connectionLayout->addWidget(m_connectionStatus);
  connectionLayout->addStretch();

  rightLayout->addWidget(connectionGroup);

  // === TWO-COLUMN CONTROL AREA ===
  QHBoxLayout* columnsLayout = new QHBoxLayout();
  columnsLayout->setSpacing(15);

  // --- LEFT COLUMN: TEC Control ---
  QGroupBox* tecGroup = new QGroupBox("TEC Control");
  tecGroup->setStyleSheet(groupBoxStyle);
  QVBoxLayout* tecLayout = new QVBoxLayout(tecGroup);
  tecLayout->setSpacing(12);

  // Setpoint row: Label + Slider + SpinBox
  QLabel* tecSetpointLabel = new QLabel("Setpoint:");
  tecSetpointLabel->setStyleSheet(labelStyle);
  tecLayout->addWidget(tecSetpointLabel);

  QHBoxLayout* tempInputLayout = new QHBoxLayout();
  m_temperatureSlider = new QSlider(Qt::Horizontal);
  m_temperatureSlider->setRange(20, 35);
  m_temperatureSlider->setSingleStep(1);
  m_temperatureSlider->setValue(25);
  m_temperatureSlider->setStyleSheet(sliderStyle);
  connect(m_temperatureSlider, &QSlider::valueChanged, this, &TECControlPage::onTemperatureSliderChanged);
  tempInputLayout->addWidget(m_temperatureSlider, 1);

  m_temperatureSpinBox = new QSpinBox();
  m_temperatureSpinBox->setRange(20, 35);
  m_temperatureSpinBox->setValue(25);
  m_temperatureSpinBox->setSuffix(" °C");
  m_temperatureSpinBox->setStyleSheet(spinBoxStyle);
  connect(m_temperatureSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &TECControlPage::onTemperatureSpinBoxChanged);
  tempInputLayout->addWidget(m_temperatureSpinBox);
  tecLayout->addLayout(tempInputLayout);

  // Current Temperature display
  QLabel* currentTempLabel = new QLabel("Current Temp:");
  currentTempLabel->setStyleSheet(labelStyle);
  tecLayout->addWidget(currentTempLabel);

  m_currentTemperature = new QLabel("-- °C");
  m_currentTemperature->setStyleSheet("QLabel { color: #1177bb; font-weight: bold; font-size: 24px; background-color: transparent; }");
  m_currentTemperature->setAlignment(Qt::AlignCenter);
  tecLayout->addWidget(m_currentTemperature);

  // TEC Current display
  QLabel* tecCurrentLabel = new QLabel("TEC Current:");
  tecCurrentLabel->setStyleSheet(labelStyle);
  tecLayout->addWidget(tecCurrentLabel);

  m_currentTECCurrent = new QLabel("-- A");
  m_currentTECCurrent->setStyleSheet("QLabel { color: #d4d4d4; font-weight: bold; font-size: 18px; background-color: transparent; }");
  m_currentTECCurrent->setAlignment(Qt::AlignCenter);
  tecLayout->addWidget(m_currentTECCurrent);

  // ON/OFF buttons
  QHBoxLayout* tecButtonLayout = new QHBoxLayout();
  tecButtonLayout->setSpacing(10);

  m_tecOnButton = new QPushButton("ON");
  m_tecOnButton->setStyleSheet(onButtonStyle);
  m_tecOnButton->setMinimumHeight(40);
  connect(m_tecOnButton, &QPushButton::clicked, this, &TECControlPage::onTECOnClicked);
  tecButtonLayout->addWidget(m_tecOnButton);

  m_tecOffButton = new QPushButton("OFF");
  m_tecOffButton->setStyleSheet(offButtonStyle);
  m_tecOffButton->setMinimumHeight(40);
  connect(m_tecOffButton, &QPushButton::clicked, this, &TECControlPage::onTECOffClicked);
  tecButtonLayout->addWidget(m_tecOffButton);

  tecLayout->addLayout(tecButtonLayout);

  // Status indicator frame
  m_tecStatusFrame = new QFrame();
  m_tecStatusFrame->setMinimumHeight(40);
  m_tecStatusFrame->setStyleSheet(
    "QFrame { background-color: #3c3c3c; border-radius: 3px; border: 1px solid #555555; }");

  QHBoxLayout* tecStatusLayout = new QHBoxLayout(m_tecStatusFrame);
  m_tecStatusLabel = new QLabel("OFF");
  m_tecStatusLabel->setStyleSheet("QLabel { color: #888888; font-weight: bold; font-size: 14px; background: transparent; }");
  m_tecStatusLabel->setAlignment(Qt::AlignCenter);
  tecStatusLayout->addWidget(m_tecStatusLabel);

  tecLayout->addWidget(m_tecStatusFrame);
  tecLayout->addStretch();

  columnsLayout->addWidget(tecGroup, 1);

  // --- RIGHT COLUMN: Laser Control ---
  QGroupBox* laserGroup = new QGroupBox("Laser Control");
  laserGroup->setStyleSheet(groupBoxStyle);
  QVBoxLayout* laserLayout = new QVBoxLayout(laserGroup);
  laserLayout->setSpacing(12);

  // Setpoint row: Label + Slider + SpinBox
  QLabel* laserSetpointLabel = new QLabel("Setpoint:");
  laserSetpointLabel->setStyleSheet(labelStyle);
  laserLayout->addWidget(laserSetpointLabel);

  QHBoxLayout* laserInputLayout = new QHBoxLayout();
  m_laserCurrentSlider = new QSlider(Qt::Horizontal);
  m_laserCurrentSlider->setRange(0, 250);
  m_laserCurrentSlider->setSingleStep(1);
  m_laserCurrentSlider->setValue(150);
  m_laserCurrentSlider->setStyleSheet(sliderStyle);  // Use same theme style as TEC slider
  connect(m_laserCurrentSlider, &QSlider::valueChanged, this, &TECControlPage::onLaserCurrentSliderChanged);
  laserInputLayout->addWidget(m_laserCurrentSlider, 1);

  m_laserCurrentSpinBox = new QSpinBox();
  m_laserCurrentSpinBox->setRange(0, 250);
  m_laserCurrentSpinBox->setValue(150);
  m_laserCurrentSpinBox->setSuffix(" mA");
  m_laserCurrentSpinBox->setStyleSheet(spinBoxStyle);
  connect(m_laserCurrentSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &TECControlPage::onLaserCurrentSpinBoxChanged);
  laserInputLayout->addWidget(m_laserCurrentSpinBox);
  laserLayout->addLayout(laserInputLayout);

  // Actual Current display
  QLabel* actualCurrentLabel = new QLabel("Actual Current:");
  actualCurrentLabel->setStyleSheet(labelStyle);
  laserLayout->addWidget(actualCurrentLabel);

  m_actualLaserCurrent = new QLabel("-- mA");
  m_actualLaserCurrent->setStyleSheet("QLabel { color: #1177bb; font-weight: bold; font-size: 24px; background-color: transparent; }");
  m_actualLaserCurrent->setAlignment(Qt::AlignCenter);
  laserLayout->addWidget(m_actualLaserCurrent);

  // Spacer to align with TEC column (TEC has extra "TEC Current" row)
  QLabel* spacerLabel = new QLabel("");
  spacerLabel->setStyleSheet(labelStyle);
  laserLayout->addWidget(spacerLabel);

  QLabel* spacerValue = new QLabel("");
  spacerValue->setStyleSheet("QLabel { font-size: 20px; }");
  laserLayout->addWidget(spacerValue);

  // ON/OFF buttons
  QHBoxLayout* laserButtonLayout = new QHBoxLayout();
  laserButtonLayout->setSpacing(10);

  m_laserOnButton = new QPushButton("ON");
  m_laserOnButton->setStyleSheet(onButtonStyle);
  m_laserOnButton->setMinimumHeight(40);
  connect(m_laserOnButton, &QPushButton::clicked, this, &TECControlPage::onLaserOnClicked);
  laserButtonLayout->addWidget(m_laserOnButton);

  m_laserOffButton = new QPushButton("OFF");
  m_laserOffButton->setStyleSheet(offButtonStyle);
  m_laserOffButton->setMinimumHeight(40);
  connect(m_laserOffButton, &QPushButton::clicked, this, &TECControlPage::onLaserOffClicked);
  laserButtonLayout->addWidget(m_laserOffButton);

  laserLayout->addLayout(laserButtonLayout);

  // Status indicator frame
  m_laserStatusFrame = new QFrame();
  m_laserStatusFrame->setMinimumHeight(40);
  m_laserStatusFrame->setStyleSheet(
    "QFrame { background-color: #3c3c3c; border-radius: 3px; border: 1px solid #555555; }");

  QHBoxLayout* laserStatusLayout = new QHBoxLayout(m_laserStatusFrame);
  m_laserStatusLabel = new QLabel("OFF");
  m_laserStatusLabel->setStyleSheet("QLabel { color: #888888; font-weight: bold; font-size: 14px; background: transparent; }");
  m_laserStatusLabel->setAlignment(Qt::AlignCenter);
  laserStatusLayout->addWidget(m_laserStatusLabel);

  laserLayout->addWidget(m_laserStatusFrame);
  laserLayout->addStretch();

  columnsLayout->addWidget(laserGroup, 1);

  rightLayout->addLayout(columnsLayout, 1);

  // === PRESETS GROUP ===
  QGroupBox* presetsGroup = new QGroupBox("Quick Presets");
  presetsGroup->setStyleSheet(groupBoxStyle);
  QHBoxLayout* presetsLayout = new QHBoxLayout(presetsGroup);

  // Preset buttons using theme accent color
  const QString presetButtonStyle =
    "QPushButton { "
    "  background-color: #0e639c; "
    "  color: white; "
    "  border: 1px solid #0c5280; "
    "  border-radius: 3px; "
    "  padding: 12px 16px; "
    "  font-weight: 600; "
    "  min-height: 60px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #1177bb; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #0c5280; "
    "} "
    "QPushButton:disabled { "
    "  background-color: #555555; "
    "  color: #888888; "
    "  border-color: #3f3f46; "
    "}";

  m_preset1Button = new QPushButton("Preset 1\n25°C, 150mA\nAll ON");
  m_preset1Button->setStyleSheet(presetButtonStyle);
  connect(m_preset1Button, &QPushButton::clicked, this, &TECControlPage::onPreset1Clicked);
  presetsLayout->addWidget(m_preset1Button);

  m_preset2Button = new QPushButton("Preset 2\n25°C, 250mA\nAll ON");
  m_preset2Button->setStyleSheet(presetButtonStyle);
  connect(m_preset2Button, &QPushButton::clicked, this, &TECControlPage::onPreset2Clicked);
  presetsLayout->addWidget(m_preset2Button);

  rightLayout->addWidget(presetsGroup);

  QHBoxLayout* mainLayout = qobject_cast<QHBoxLayout*>(layout());
  if (mainLayout) {
    mainLayout->addWidget(rightPanel, 85);
  }
}

void TECControlPage::updateDeviceList()
{
  m_deviceList->clear();

  if (!m_context || !m_context->tecManager) {
    return;
  }

  auto devices = m_context->tecManager->GetAllDeviceNames();
  for (const auto& name : devices) {
    m_deviceList->addItem(QString::fromStdString(name));
  }

  if (!devices.empty()) {
    m_deviceList->setCurrentRow(0);
  }
}

QString TECControlPage::getCurrentDevice() const
{
  QListWidgetItem* item = m_deviceList->currentItem();
  if (item) {
    return item->text();
  }
  return QString();
}

bool TECControlPage::deviceSupportsLaser() const
{
  QString device = getCurrentDevice();
  if (device.isEmpty() || !m_context || !m_context->tecManager) {
    return false;
  }

  // Get device configuration
  TECDevice config;
  if (!m_context->tecManager->GetDeviceConfig(device.toStdString(), config)) {
    return false;
  }

  // Check if device type is TEC-only (TED4015 doesn't support laser)
  std::string typeUpper = config.TypeController;
  std::transform(typeUpper.begin(), typeUpper.end(), typeUpper.begin(), ::toupper);

  // TED4015 is TEC-only, no laser support
  if (typeUpper.find("TED4015") != std::string::npos ||
      typeUpper.find("TED4000") != std::string::npos) {
    return false;
  }

  // CLD101x supports laser
  return true;
}

void TECControlPage::onDeviceSelected()
{
  QString device = getCurrentDevice();
  if (device.isEmpty()) {
    enableControls(false);
    return;
  }

  if (!m_context || !m_context->tecManager) {
    return;
  }

  // Check if device is connected
  TECStatus status = m_context->tecManager->GetDeviceStatus(device.toStdString());
  m_isConnected = status.Connected;

  enableControls(m_isConnected);
  updateControls();
}

void TECControlPage::onConnectClicked()
{
  QString device = getCurrentDevice();
  if (device.isEmpty() || !m_context || !m_context->tecManager) {
    return;
  }

  bool success = m_context->tecManager->ConnectDevice(device.toStdString());

  if (success) {
    m_isConnected = true;
    m_connectionStatus->setText("Connected");
    m_connectionStatus->setStyleSheet("QLabel { color: #27ae60; font-weight: bold; background-color: transparent; }");
    enableControls(true);
    std::cout << "[TECControlPage] Connected to " << device.toStdString() << std::endl;
  }
  else {
    m_connectionStatus->setText("Connection Failed");
    m_connectionStatus->setStyleSheet("QLabel { color: #e74c3c; font-weight: bold; background-color: transparent; }");
  }
}

void TECControlPage::onDisconnectClicked()
{
  QString device = getCurrentDevice();
  if (device.isEmpty() || !m_context || !m_context->tecManager) {
    return;
  }

  m_context->tecManager->DisconnectDevice(device.toStdString());
  m_isConnected = false;
  m_connectionStatus->setText("Disconnected");
  m_connectionStatus->setStyleSheet("QLabel { color: #888888; font-weight: bold; background-color: transparent; }");
  enableControls(false);
  std::cout << "[TECControlPage] Disconnected from " << device.toStdString() << std::endl;
}

void TECControlPage::onTemperatureSliderChanged(int value)
{
  if (m_isUpdatingFromDevice) return;

  // Sync spinbox with slider
  m_temperatureSpinBox->blockSignals(true);
  m_temperatureSpinBox->setValue(value);
  m_temperatureSpinBox->blockSignals(false);

  QString device = getCurrentDevice();
  if (device.isEmpty() || !m_context || !m_context->tecManager || !m_isConnected) {
    return;
  }

  m_context->tecManager->SetTemperature(device.toStdString(), value);
}

void TECControlPage::onTemperatureSpinBoxChanged(int value)
{
  if (m_isUpdatingFromDevice) return;

  // Sync slider with spinbox
  m_temperatureSlider->blockSignals(true);
  m_temperatureSlider->setValue(value);
  m_temperatureSlider->blockSignals(false);

  QString device = getCurrentDevice();
  if (device.isEmpty() || !m_context || !m_context->tecManager || !m_isConnected) {
    return;
  }

  m_context->tecManager->SetTemperature(device.toStdString(), value);
}

void TECControlPage::onTECOnClicked()
{
  if (m_isUpdatingFromDevice) return;

  QString device = getCurrentDevice();
  if (device.isEmpty() || !m_context || !m_context->tecManager || !m_isConnected) {
    return;
  }

  m_context->tecManager->TEC_On(device.toStdString());
  std::cout << "[TECControlPage] TEC ON clicked for " << device.toStdString() << std::endl;
}

void TECControlPage::onTECOffClicked()
{
  if (m_isUpdatingFromDevice) return;

  QString device = getCurrentDevice();
  if (device.isEmpty() || !m_context || !m_context->tecManager || !m_isConnected) {
    return;
  }

  m_context->tecManager->TEC_Off(device.toStdString());
  std::cout << "[TECControlPage] TEC OFF clicked for " << device.toStdString() << std::endl;
}

void TECControlPage::onLaserCurrentSliderChanged(int value)
{
  if (m_isUpdatingFromDevice) return;

  // Sync spinbox with slider
  m_laserCurrentSpinBox->blockSignals(true);
  m_laserCurrentSpinBox->setValue(value);
  m_laserCurrentSpinBox->blockSignals(false);

  QString device = getCurrentDevice();
  if (device.isEmpty() || !m_context || !m_context->tecManager || !m_isConnected) {
    return;
  }

  m_context->tecManager->SetLaserCurrent(device.toStdString(), value);
}

void TECControlPage::onLaserCurrentSpinBoxChanged(int value)
{
  if (m_isUpdatingFromDevice) return;

  // Sync slider with spinbox
  m_laserCurrentSlider->blockSignals(true);
  m_laserCurrentSlider->setValue(value);
  m_laserCurrentSlider->blockSignals(false);

  QString device = getCurrentDevice();
  if (device.isEmpty() || !m_context || !m_context->tecManager || !m_isConnected) {
    return;
  }

  m_context->tecManager->SetLaserCurrent(device.toStdString(), value);
}

void TECControlPage::onLaserOnClicked()
{
  if (m_isUpdatingFromDevice) return;

  QString device = getCurrentDevice();
  if (device.isEmpty() || !m_context || !m_context->tecManager || !m_isConnected) {
    return;
  }

  m_context->tecManager->Laser_On(device.toStdString());
  std::cout << "[TECControlPage] Laser ON clicked for " << device.toStdString() << std::endl;
}

void TECControlPage::onLaserOffClicked()
{
  if (m_isUpdatingFromDevice) return;

  QString device = getCurrentDevice();
  if (device.isEmpty() || !m_context || !m_context->tecManager || !m_isConnected) {
    return;
  }

  m_context->tecManager->Laser_Off(device.toStdString());
  std::cout << "[TECControlPage] Laser OFF clicked for " << device.toStdString() << std::endl;
}

void TECControlPage::onPreset1Clicked()
{
  applyPreset(25.0, 150.0, true, true);
}

void TECControlPage::onPreset2Clicked()
{
  applyPreset(25.0, 250.0, true, true);
}

void TECControlPage::applyPreset(double temp, double current, bool tecOn, bool laserOn)
{
  QString device = getCurrentDevice();
  if (device.isEmpty() || !m_context || !m_context->tecManager || !m_isConnected) {
    return;
  }

  std::cout << "[TECControlPage] Applying preset: " << temp << "C, "
    << current << "mA, TEC:" << (tecOn ? "ON" : "OFF")
    << ", Laser:" << (laserOn ? "ON" : "OFF") << std::endl;

  // Set temperature (update both slider and spinbox)
  m_temperatureSlider->setValue(static_cast<int>(temp));
  m_temperatureSpinBox->setValue(static_cast<int>(temp));
  m_context->tecManager->SetTemperature(device.toStdString(), temp);

  // Set laser current (update both slider and spinbox)
  m_laserCurrentSlider->setValue(static_cast<int>(current));
  m_laserCurrentSpinBox->setValue(static_cast<int>(current));
  m_context->tecManager->SetLaserCurrent(device.toStdString(), current);

  // Set TEC state
  if (tecOn) {
    m_context->tecManager->TEC_On(device.toStdString());
  }
  else {
    m_context->tecManager->TEC_Off(device.toStdString());
  }

  // Set laser state
  if (laserOn) {
    m_context->tecManager->Laser_On(device.toStdString());
  }
  else {
    m_context->tecManager->Laser_Off(device.toStdString());
  }
}

void TECControlPage::onPollTimer()
{
  QString device = getCurrentDevice();
  if (device.isEmpty() || !m_context || !m_context->tecManager || !m_isConnected) {
    return;
  }

  updateStatusDisplay();
}

void TECControlPage::updateControls()
{
  QString device = getCurrentDevice();
  if (device.isEmpty() || !m_context || !m_context->tecManager) {
    return;
  }

  TECStatus status = m_context->tecManager->GetDeviceStatus(device.toStdString());

  m_isUpdatingFromDevice = true;

  // Update connection status
  if (status.Connected) {
    m_connectionStatus->setText("Connected");
    m_connectionStatus->setStyleSheet("QLabel { color: #27ae60; font-weight: bold; background-color: transparent; }");
  }
  else {
    m_connectionStatus->setText("Not Connected");
    m_connectionStatus->setStyleSheet("QLabel { color: #888888; font-weight: bold; background-color: transparent; }");
  }

  // Update TEC status indicator
  if (status.TECOn) {
    m_tecStatusFrame->setStyleSheet(
      "QFrame { background-color: #27ae60; border-radius: 3px; border: 1px solid #1e8449; }");
    m_tecStatusLabel->setText("ON");
    m_tecStatusLabel->setStyleSheet("QLabel { color: white; font-weight: bold; font-size: 14px; background: transparent; }");
  }
  else {
    m_tecStatusFrame->setStyleSheet(
      "QFrame { background-color: #3c3c3c; border-radius: 3px; border: 1px solid #555555; }");
    m_tecStatusLabel->setText("OFF");
    m_tecStatusLabel->setStyleSheet("QLabel { color: #888888; font-weight: bold; font-size: 14px; background: transparent; }");
  }

  // Update laser status indicator
  if (status.LaserOn) {
    m_laserStatusFrame->setStyleSheet(
      "QFrame { background-color: #27ae60; border-radius: 3px; border: 1px solid #1e8449; }");
    m_laserStatusLabel->setText("ON");
    m_laserStatusLabel->setStyleSheet("QLabel { color: white; font-weight: bold; font-size: 14px; background: transparent; }");
  }
  else {
    m_laserStatusFrame->setStyleSheet(
      "QFrame { background-color: #3c3c3c; border-radius: 3px; border: 1px solid #555555; }");
    m_laserStatusLabel->setText("OFF");
    m_laserStatusLabel->setStyleSheet("QLabel { color: #888888; font-weight: bold; font-size: 14px; background: transparent; }");
  }

  m_isUpdatingFromDevice = false;
}

void TECControlPage::updateStatusDisplay()
{
  QString device = getCurrentDevice();
  if (device.isEmpty() || !m_context || !m_context->tecManager) {
    return;
  }

  // Read current temperature
  auto tempResult = m_context->tecManager->ReadTemperature(device.toStdString());
  if (tempResult.Success) {
    // Validate temperature is within reasonable range (-50°C to +100°C)
    if (tempResult.Value >= -50.0 && tempResult.Value <= 100.0) {
      m_currentTemperature->setText(QString::number(tempResult.Value, 'f', 2) + " °C");
      m_currentTemperature->setStyleSheet("QLabel { color: #1177bb; font-weight: bold; font-size: 24px; background-color: transparent; }");
    }
    else {
      m_currentTemperature->setText("-- °C");
      m_currentTemperature->setStyleSheet("QLabel { color: #888888; font-weight: bold; font-size: 24px; background-color: transparent; }");
    }
  }
  else {
    m_currentTemperature->setText("-- °C");
    m_currentTemperature->setStyleSheet("QLabel { color: #888888; font-weight: bold; font-size: 24px; background-color: transparent; }");
  }

  // Read TEC current
  auto tecCurrentResult = m_context->tecManager->ReadTECCurrent(device.toStdString());
  if (tecCurrentResult.Success) {
    // Validate TEC current is within reasonable range (-5A to +5A)
    if (tecCurrentResult.Value >= -5.0 && tecCurrentResult.Value <= 5.0) {
      m_currentTECCurrent->setText(QString::number(tecCurrentResult.Value, 'f', 3) + " A");
    }
    else {
      m_currentTECCurrent->setText("-- A");
    }
  }
  else {
    m_currentTECCurrent->setText("-- A");
  }

  // Read laser current
  auto currentResult = m_context->tecManager->ReadLaserCurrent(device.toStdString());
  if (currentResult.Success) {
    // Validate laser current is within reasonable range (0 to 500mA)
    if (currentResult.Value >= 0.0 && currentResult.Value <= 500.0) {
      m_actualLaserCurrent->setText(QString::number(currentResult.Value, 'f', 2) + " mA");
      m_actualLaserCurrent->setStyleSheet("QLabel { color: #1177bb; font-weight: bold; font-size: 24px; background-color: transparent; }");
    }
    else {
      m_actualLaserCurrent->setText("-- mA");
      m_actualLaserCurrent->setStyleSheet("QLabel { color: #888888; font-weight: bold; font-size: 24px; background-color: transparent; }");
    }
  }
  else {
    m_actualLaserCurrent->setText("-- mA");
    m_actualLaserCurrent->setStyleSheet("QLabel { color: #888888; font-weight: bold; font-size: 24px; background-color: transparent; }");
  }

  // Get status
  TECStatus status = m_context->tecManager->GetDeviceStatus(device.toStdString());

  // Prevent triggering slot handlers during polling update
  m_isUpdatingFromDevice = true;

  // Update TEC status indicator
  if (status.TECOn) {
    m_tecStatusFrame->setStyleSheet(
      "QFrame { background-color: #27ae60; border-radius: 3px; border: 1px solid #1e8449; }");
    m_tecStatusLabel->setText("ON");
    m_tecStatusLabel->setStyleSheet("QLabel { color: white; font-weight: bold; font-size: 14px; background: transparent; }");
  }
  else {
    m_tecStatusFrame->setStyleSheet(
      "QFrame { background-color: #3c3c3c; border-radius: 3px; border: 1px solid #555555; }");
    m_tecStatusLabel->setText("OFF");
    m_tecStatusLabel->setStyleSheet("QLabel { color: #888888; font-weight: bold; font-size: 14px; background: transparent; }");
  }

  // Update laser status indicator
  if (status.LaserOn) {
    m_laserStatusFrame->setStyleSheet(
      "QFrame { background-color: #27ae60; border-radius: 3px; border: 1px solid #1e8449; }");
    m_laserStatusLabel->setText("ON");
    m_laserStatusLabel->setStyleSheet("QLabel { color: white; font-weight: bold; font-size: 14px; background: transparent; }");
  }
  else {
    m_laserStatusFrame->setStyleSheet(
      "QFrame { background-color: #3c3c3c; border-radius: 3px; border: 1px solid #555555; }");
    m_laserStatusLabel->setText("OFF");
    m_laserStatusLabel->setStyleSheet("QLabel { color: #888888; font-weight: bold; font-size: 14px; background: transparent; }");
  }

  m_isUpdatingFromDevice = false;
}

void TECControlPage::showEvent(QShowEvent* event)
{
  QWidget::showEvent(event);

  std::cout << "[TECControlPage] Page shown - starting polling" << std::endl;

  // Refresh device list
  updateDeviceList();

  // Start polling timer (500ms)
  m_pollTimer->start(500);
}

void TECControlPage::hideEvent(QHideEvent* event)
{
  QWidget::hideEvent(event);

  std::cout << "[TECControlPage] Page hidden - stopping polling" << std::endl;

  // Stop polling to prevent timeouts when page is not visible
  m_pollTimer->stop();
}

void TECControlPage::enableControls(bool enabled)
{
  m_connectButton->setEnabled(!enabled);
  m_disconnectButton->setEnabled(enabled);

  // TEC controls
  m_temperatureSlider->setEnabled(enabled);
  m_temperatureSpinBox->setEnabled(enabled);
  m_tecOnButton->setEnabled(enabled);
  m_tecOffButton->setEnabled(enabled);

  // Check if device supports laser control
  bool laserSupported = deviceSupportsLaser();

  // Only enable laser controls if device supports laser AND is connected
  m_laserCurrentSlider->setEnabled(enabled && laserSupported);
  m_laserCurrentSpinBox->setEnabled(enabled && laserSupported);
  m_laserOnButton->setEnabled(enabled && laserSupported);
  m_laserOffButton->setEnabled(enabled && laserSupported);

  // Presets include laser settings, so disable for TEC-only devices
  m_preset1Button->setEnabled(enabled && laserSupported);
  m_preset2Button->setEnabled(enabled && laserSupported);
}