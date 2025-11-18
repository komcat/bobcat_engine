// TECControlPage.cpp
#include "TECControlPage.h"
#include "ApplicationContext.h"
#include "TECManager.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
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

  // Start polling timer (500ms)
  m_pollTimer = new QTimer(this);
  connect(m_pollTimer, &QTimer::timeout, this, &TECControlPage::onPollTimer);
  m_pollTimer->start(500);
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

  // === CONNECTION GROUP ===
  QGroupBox* connectionGroup = new QGroupBox("Connection");
  connectionGroup->setStyleSheet(
    "QGroupBox { color: #ffffff; font-weight: bold; border: 2px solid #3f3f46; "
    "background-color: #1e1e1e; border-radius: 5px; padding: 10px; margin-top: 10px; } "
    "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; color: #ffffff; }");
  QHBoxLayout* connectionLayout = new QHBoxLayout(connectionGroup);

  m_connectButton = new QPushButton("Connect");
  m_connectButton->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5cbf60, stop:1 #4CAF50); "
    "  color: white; "
    "  padding: 8px 16px; "
    "  border-radius: 4px; "
    "  font-weight: bold; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #6ccf70, stop:1 #5cbf60); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #3d9041; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 10px; "
    "  padding-left: 18px; "
    "}");
  connect(m_connectButton, &QPushButton::clicked, this, &TECControlPage::onConnectClicked);
  connectionLayout->addWidget(m_connectButton);

  m_disconnectButton = new QPushButton("Disconnect");
  m_disconnectButton->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #e85d5d, stop:1 #e74c3c); "
    "  color: white; "
    "  padding: 8px 16px; "
    "  border-radius: 4px; "
    "  font-weight: bold; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #f86d6d, stop:1 #e85d5d); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #c0392b; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 10px; "
    "  padding-left: 18px; "
    "} "
    "QPushButton:disabled { "
    "  background-color: #7f8c8d; "
    "  color: #bdc3c7; "
    "  border-top: 1px solid rgba(255, 255, 255, 0.2); "
    "  border-left: 1px solid rgba(255, 255, 255, 0.2); "
    "  border-bottom: 1px solid rgba(0, 0, 0, 0.3); "
    "  border-right: 1px solid rgba(0, 0, 0, 0.3); "
    "}");
  m_disconnectButton->setEnabled(false);
  connect(m_disconnectButton, &QPushButton::clicked, this, &TECControlPage::onDisconnectClicked);
  connectionLayout->addWidget(m_disconnectButton);

  m_connectionStatus = new QLabel("Not Connected");
  m_connectionStatus->setStyleSheet("QLabel { color: #e74c3c; font-weight: bold; }");
  connectionLayout->addWidget(m_connectionStatus);
  connectionLayout->addStretch();

  rightLayout->addWidget(connectionGroup);

  // === TEC CONTROL GROUP ===
  QGroupBox* tecGroup = new QGroupBox("TEC Control");
  tecGroup->setStyleSheet(
    "QGroupBox { color: #ffffff; font-weight: bold; border: 2px solid #3f3f46; "
    "background-color: #1e1e1e; border-radius: 5px; padding: 10px; margin-top: 10px; } "
    "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; color: #ffffff; }");
  QFormLayout* tecLayout = new QFormLayout(tecGroup);

  // Temperature slider (20-35C)
  QHBoxLayout* tempLayout = new QHBoxLayout();
  m_temperatureSlider = new QSlider(Qt::Horizontal);
  m_temperatureSlider->setRange(20, 35);
  m_temperatureSlider->setSingleStep(1);
  m_temperatureSlider->setValue(25);
  m_temperatureSlider->setStyleSheet(
    "QSlider::groove:horizontal { background: #3c3c3c; height: 8px; border-radius: 4px; } "
    "QSlider::handle:horizontal { background: #0e639c; width: 18px; margin: -5px 0; border-radius: 9px; }");
  connect(m_temperatureSlider, &QSlider::valueChanged, this, &TECControlPage::onTemperatureChanged);
  tempLayout->addWidget(m_temperatureSlider);

  m_temperatureValue = new QLabel("25 C");
  m_temperatureValue->setStyleSheet("QLabel { color: #d4d4d4; font-weight: bold; min-width: 60px; }");
  tempLayout->addWidget(m_temperatureValue);

  QLabel* setpointLabel = new QLabel("Setpoint:");
  setpointLabel->setStyleSheet("QLabel { color: #d4d4d4; }");
  tecLayout->addRow(setpointLabel, tempLayout);

  m_currentTemperature = new QLabel("-- C");
  m_currentTemperature->setStyleSheet("QLabel { color: #0e639c; font-weight: bold; font-size: 24px; }");
  QLabel* currentTempLabel = new QLabel("Current Temp:");
  currentTempLabel->setStyleSheet("QLabel { color: #d4d4d4; }");
  tecLayout->addRow(currentTempLabel, m_currentTemperature);

  m_currentTECCurrent = new QLabel("-- A");
  m_currentTECCurrent->setStyleSheet("QLabel { color: #f39c12; font-weight: bold; font-size: 18px; }");
  QLabel* currentTECCurrentLabel = new QLabel("TEC Current:");
  currentTECCurrentLabel->setStyleSheet("QLabel { color: #d4d4d4; }");
  tecLayout->addRow(currentTECCurrentLabel, m_currentTECCurrent);

  m_tecToggle = new QPushButton("TEC OFF");
  m_tecToggle->setCheckable(true);
  m_tecToggle->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #7a7a7a, stop:1 #666666); "
    "  color: white; "
    "  padding: 10px; "
    "  font-weight: bold; "
    "  border-radius: 4px; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.4); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.4); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #8a8a8a, stop:1 #7a7a7a); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "} "
    "QPushButton:checked { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5cbf60, stop:1 #4CAF50); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:checked:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #6ccf70, stop:1 #5cbf60); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "}");
  connect(m_tecToggle, &QPushButton::clicked, this, &TECControlPage::onTECToggled);
  QLabel* tecLabel = new QLabel("TEC:");
  tecLabel->setStyleSheet("QLabel { color: #d4d4d4; }");
  tecLayout->addRow(tecLabel, m_tecToggle);

  m_tecStatus = new QLabel("OFF");
  m_tecStatus->setStyleSheet("QLabel { color: #e74c3c; font-weight: bold; }");
  QLabel* statusLabel = new QLabel("Status:");
  statusLabel->setStyleSheet("QLabel { color: #d4d4d4; }");
  tecLayout->addRow(statusLabel, m_tecStatus);

  rightLayout->addWidget(tecGroup);

  // === LASER CONTROL GROUP ===
  QGroupBox* laserGroup = new QGroupBox("Laser Control");
  laserGroup->setStyleSheet(
    "QGroupBox { color: #ffffff; font-weight: bold; border: 2px solid #3f3f46; "
    "background-color: #1e1e1e; border-radius: 5px; padding: 10px; margin-top: 10px; } "
    "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; color: #ffffff; }");
  QFormLayout* laserLayout = new QFormLayout(laserGroup);

  // Laser current slider (0-250mA)
  QHBoxLayout* currentLayout = new QHBoxLayout();
  m_laserCurrentSlider = new QSlider(Qt::Horizontal);
  m_laserCurrentSlider->setRange(0, 250);
  m_laserCurrentSlider->setSingleStep(1);
  m_laserCurrentSlider->setValue(150);
  m_laserCurrentSlider->setStyleSheet(
    "QSlider::groove:horizontal { background: #3c3c3c; height: 8px; border-radius: 4px; } "
    "QSlider::handle:horizontal { background: #f39c12; width: 18px; margin: -5px 0; border-radius: 9px; }");
  connect(m_laserCurrentSlider, &QSlider::valueChanged, this, &TECControlPage::onLaserCurrentChanged);
  currentLayout->addWidget(m_laserCurrentSlider);

  m_laserCurrentValue = new QLabel("150 mA");
  m_laserCurrentValue->setStyleSheet("QLabel { color: #d4d4d4; font-weight: bold; min-width: 70px; }");
  currentLayout->addWidget(m_laserCurrentValue);

  QLabel* currentSetpointLabel = new QLabel("Setpoint:");
  currentSetpointLabel->setStyleSheet("QLabel { color: #d4d4d4; }");
  laserLayout->addRow(currentSetpointLabel, currentLayout);

  m_actualLaserCurrent = new QLabel("-- mA");
  m_actualLaserCurrent->setStyleSheet("QLabel { color: #0e639c; font-weight: bold; font-size: 24px; }");
  QLabel* actualCurrentLabel = new QLabel("Actual Current:");
  actualCurrentLabel->setStyleSheet("QLabel { color: #d4d4d4; }");
  laserLayout->addRow(actualCurrentLabel, m_actualLaserCurrent);

  m_laserToggle = new QPushButton("Laser OFF");
  m_laserToggle->setCheckable(true);
  m_laserToggle->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #7a7a7a, stop:1 #666666); "
    "  color: white; "
    "  padding: 10px; "
    "  font-weight: bold; "
    "  border-radius: 4px; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.4); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.4); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #8a8a8a, stop:1 #7a7a7a); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "} "
    "QPushButton:checked { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #f39c50, stop:1 #f39c12); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:checked:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ffac60, stop:1 #f39c50); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "}");
  connect(m_laserToggle, &QPushButton::clicked, this, &TECControlPage::onLaserToggled);
  QLabel* laserLabel = new QLabel("Laser:");
  laserLabel->setStyleSheet("QLabel { color: #d4d4d4; }");
  laserLayout->addRow(laserLabel, m_laserToggle);

  m_laserStatus = new QLabel("OFF");
  m_laserStatus->setStyleSheet("QLabel { color: #e74c3c; font-weight: bold; }");
  QLabel* laserStatusLabel = new QLabel("Status:");
  laserStatusLabel->setStyleSheet("QLabel { color: #d4d4d4; }");
  laserLayout->addRow(laserStatusLabel, m_laserStatus);

  rightLayout->addWidget(laserGroup);

  // === PRESETS GROUP ===
  QGroupBox* presetsGroup = new QGroupBox("Quick Presets");
  presetsGroup->setStyleSheet(
    "QGroupBox { color: #ffffff; font-weight: bold; border: 2px solid #3f3f46; "
    "background-color: #1e1e1e; border-radius: 5px; padding: 10px; margin-top: 10px; } "
    "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; color: #ffffff; }");
  QHBoxLayout* presetsLayout = new QHBoxLayout(presetsGroup);

  m_preset1Button = new QPushButton("Preset 1\n25C, 150mA\nAll ON");
  m_preset1Button->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #1e73ac, stop:1 #0e639c); "
    "  color: white; "
    "  padding: 15px; "
    "  font-weight: bold; "
    "  border-radius: 4px; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #2e83bc, stop:1 #1e73ac); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #0c5280; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 17px; "
    "  padding-left: 17px; "
    "}");
  connect(m_preset1Button, &QPushButton::clicked, this, &TECControlPage::onPreset1Clicked);
  presetsLayout->addWidget(m_preset1Button);

  m_preset2Button = new QPushButton("Preset 2\n25C, 250mA\nAll ON");
  m_preset2Button->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #9e54bd, stop:1 #8e44ad); "
    "  color: white; "
    "  padding: 15px; "
    "  font-weight: bold; "
    "  border-radius: 4px; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ae64cd, stop:1 #9e54bd); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #7d3493; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 17px; "
    "  padding-left: 17px; "
    "}");
  connect(m_preset2Button, &QPushButton::clicked, this, &TECControlPage::onPreset2Clicked);
  presetsLayout->addWidget(m_preset2Button);

  rightLayout->addWidget(presetsGroup);

  rightLayout->addStretch();

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
    m_connectionStatus->setStyleSheet("QLabel { color: #27ae60; font-weight: bold; }");
    enableControls(true);
    std::cout << "[TECControlPage] Connected to " << device.toStdString() << std::endl;
  }
  else {
    m_connectionStatus->setText("Connection Failed");
    m_connectionStatus->setStyleSheet("QLabel { color: #e74c3c; font-weight: bold; }");
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
  m_connectionStatus->setStyleSheet("QLabel { color: #e74c3c; font-weight: bold; }");
  enableControls(false);
  std::cout << "[TECControlPage] Disconnected from " << device.toStdString() << std::endl;
}

void TECControlPage::onTemperatureChanged(int value)
{
  m_temperatureValue->setText(QString::number(value) + " C");

  if (m_isUpdatingFromDevice) return;

  QString device = getCurrentDevice();
  if (device.isEmpty() || !m_context || !m_context->tecManager || !m_isConnected) {
    return;
  }

  m_context->tecManager->SetTemperature(device.toStdString(), value);
}

void TECControlPage::onTECToggled()
{
  if (m_isUpdatingFromDevice) return;

  QString device = getCurrentDevice();
  if (device.isEmpty() || !m_context || !m_context->tecManager || !m_isConnected) {
    return;
  }

  if (m_tecToggle->isChecked()) {
    m_context->tecManager->TEC_On(device.toStdString());
    m_tecToggle->setText("TEC ON");
  }
  else {
    m_context->tecManager->TEC_Off(device.toStdString());
    m_tecToggle->setText("TEC OFF");
  }
}

void TECControlPage::onLaserCurrentChanged(int value)
{
  m_laserCurrentValue->setText(QString::number(value) + " mA");

  if (m_isUpdatingFromDevice) return;

  QString device = getCurrentDevice();
  if (device.isEmpty() || !m_context || !m_context->tecManager || !m_isConnected) {
    return;
  }

  m_context->tecManager->SetLaserCurrent(device.toStdString(), value);
}

void TECControlPage::onLaserToggled()
{
  if (m_isUpdatingFromDevice) return;

  QString device = getCurrentDevice();
  if (device.isEmpty() || !m_context || !m_context->tecManager || !m_isConnected) {
    return;
  }

  if (m_laserToggle->isChecked()) {
    m_context->tecManager->Laser_On(device.toStdString());
    m_laserToggle->setText("Laser ON");
  }
  else {
    m_context->tecManager->Laser_Off(device.toStdString());
    m_laserToggle->setText("Laser OFF");
  }
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

  // Set temperature
  m_temperatureSlider->setValue(static_cast<int>(temp));
  m_context->tecManager->SetTemperature(device.toStdString(), temp);

  // Set laser current
  m_laserCurrentSlider->setValue(static_cast<int>(current));
  m_context->tecManager->SetLaserCurrent(device.toStdString(), current);

  // Set TEC state
  if (tecOn) {
    m_context->tecManager->TEC_On(device.toStdString());
    m_tecToggle->setChecked(true);
    m_tecToggle->setText("TEC ON");
  }
  else {
    m_context->tecManager->TEC_Off(device.toStdString());
    m_tecToggle->setChecked(false);
    m_tecToggle->setText("TEC OFF");
  }

  // Set laser state
  if (laserOn) {
    m_context->tecManager->Laser_On(device.toStdString());
    m_laserToggle->setChecked(true);
    m_laserToggle->setText("Laser ON");
  }
  else {
    m_context->tecManager->Laser_Off(device.toStdString());
    m_laserToggle->setChecked(false);
    m_laserToggle->setText("Laser OFF");
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
    m_connectionStatus->setStyleSheet("QLabel { color: #27ae60; font-weight: bold; }");
  }
  else {
    m_connectionStatus->setText("Not Connected");
    m_connectionStatus->setStyleSheet("QLabel { color: #e74c3c; font-weight: bold; }");
  }

  // Update TEC toggle
  m_tecToggle->setChecked(status.TECOn);
  m_tecToggle->setText(status.TECOn ? "TEC ON" : "TEC OFF");

  // Update laser toggle
  m_laserToggle->setChecked(status.LaserOn);
  m_laserToggle->setText(status.LaserOn ? "Laser ON" : "Laser OFF");

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
    m_currentTemperature->setText(QString::number(tempResult.Value, 'f', 2) + " C");
  }

  // Read TEC current
  auto tecCurrentResult = m_context->tecManager->ReadTECCurrent(device.toStdString());
  if (tecCurrentResult.Success) {
    m_currentTECCurrent->setText(QString::number(tecCurrentResult.Value, 'f', 3) + " A");
  }

  // Read laser current
  auto currentResult = m_context->tecManager->ReadLaserCurrent(device.toStdString());
  if (currentResult.Success) {
    m_actualLaserCurrent->setText(QString::number(currentResult.Value, 'f', 2) + " mA");
  }

  // Get status
  TECStatus status = m_context->tecManager->GetDeviceStatus(device.toStdString());

  // Prevent triggering slot handlers during polling update
  m_isUpdatingFromDevice = true;

  // Update TEC button state to match device status
  m_tecToggle->setChecked(status.TECOn);
  m_tecToggle->setText(status.TECOn ? "TEC ON" : "TEC OFF");

  // Update TEC status label
  if (status.TECOn) {
    m_tecStatus->setText("ON");
    m_tecStatus->setStyleSheet("QLabel { color: #27ae60; font-weight: bold; }");
  }
  else {
    m_tecStatus->setText("OFF");
    m_tecStatus->setStyleSheet("QLabel { color: #e74c3c; font-weight: bold; }");
  }

  // Update laser button state to match device status
  m_laserToggle->setChecked(status.LaserOn);
  m_laserToggle->setText(status.LaserOn ? "Laser ON" : "Laser OFF");

  // Update laser status label
  if (status.LaserOn) {
    m_laserStatus->setText("ON");
    m_laserStatus->setStyleSheet("QLabel { color: #f39c12; font-weight: bold; }");
  }
  else {
    m_laserStatus->setText("OFF");
    m_laserStatus->setStyleSheet("QLabel { color: #e74c3c; font-weight: bold; }");
  }

  m_isUpdatingFromDevice = false;
}

void TECControlPage::enableControls(bool enabled)
{
  m_connectButton->setEnabled(!enabled);
  m_disconnectButton->setEnabled(enabled);
  m_temperatureSlider->setEnabled(enabled);
  m_tecToggle->setEnabled(enabled);

  // Check if device supports laser control
  bool laserSupported = deviceSupportsLaser();

  // Only enable laser controls if device supports laser AND is connected
  m_laserCurrentSlider->setEnabled(enabled && laserSupported);
  m_laserToggle->setEnabled(enabled && laserSupported);

  // Presets include laser settings, so disable for TEC-only devices
  m_preset1Button->setEnabled(enabled && laserSupported);
  m_preset2Button->setEnabled(enabled && laserSupported);
}