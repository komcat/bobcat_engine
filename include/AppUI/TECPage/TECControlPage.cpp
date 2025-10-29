// TECControlPage.cpp
#include "TECControlPage.h"
#include "ApplicationContext.h"
#include "TECManager.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <iostream>

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
    "QPushButton { background-color: #27ae60; color: white; padding: 8px 16px; "
    "border: none; border-radius: 4px; font-weight: bold; } "
    "QPushButton:hover { background-color: #2ecc71; }");
  connect(m_connectButton, &QPushButton::clicked, this, &TECControlPage::onConnectClicked);
  connectionLayout->addWidget(m_connectButton);

  m_disconnectButton = new QPushButton("Disconnect");
  m_disconnectButton->setStyleSheet(
    "QPushButton { background-color: #e74c3c; color: white; padding: 8px 16px; "
    "border: none; border-radius: 4px; font-weight: bold; } "
    "QPushButton:hover { background-color: #c0392b; }");
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
  m_currentTemperature->setStyleSheet("QLabel { color: #0e639c; font-weight: bold; }");
  QLabel* currentTempLabel = new QLabel("Current Temp:");
  currentTempLabel->setStyleSheet("QLabel { color: #d4d4d4; }");
  tecLayout->addRow(currentTempLabel, m_currentTemperature);

  m_tecToggle = new QPushButton("TEC OFF");
  m_tecToggle->setCheckable(true);
  m_tecToggle->setStyleSheet(
    "QPushButton { background-color: #666666; color: white; padding: 10px; "
    "font-weight: bold; border: none; border-radius: 4px; } "
    "QPushButton:checked { background-color: #27ae60; }");
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
  m_actualLaserCurrent->setStyleSheet("QLabel { color: #0e639c; font-weight: bold; }");
  QLabel* actualCurrentLabel = new QLabel("Actual Current:");
  actualCurrentLabel->setStyleSheet("QLabel { color: #d4d4d4; }");
  laserLayout->addRow(actualCurrentLabel, m_actualLaserCurrent);

  m_laserToggle = new QPushButton("Laser OFF");
  m_laserToggle->setCheckable(true);
  m_laserToggle->setStyleSheet(
    "QPushButton { background-color: #666666; color: white; padding: 10px; "
    "font-weight: bold; border: none; border-radius: 4px; } "
    "QPushButton:checked { background-color: #f39c12; }");
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
    "QPushButton { background-color: #0e639c; color: white; padding: 15px; "
    "font-weight: bold; border: none; border-radius: 4px; } "
    "QPushButton:hover { background-color: #1177bb; }");
  connect(m_preset1Button, &QPushButton::clicked, this, &TECControlPage::onPreset1Clicked);
  presetsLayout->addWidget(m_preset1Button);

  m_preset2Button = new QPushButton("Preset 2\n25C, 250mA\nAll ON");
  m_preset2Button->setStyleSheet(
    "QPushButton { background-color: #8e44ad; color: white; padding: 15px; "
    "font-weight: bold; border: none; border-radius: 4px; } "
    "QPushButton:hover { background-color: #9b59b6; }");
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

  // Read laser current
  auto currentResult = m_context->tecManager->ReadLaserCurrent(device.toStdString());
  if (currentResult.Success) {
    m_actualLaserCurrent->setText(QString::number(currentResult.Value, 'f', 2) + " mA");
  }

  // Get status
  TECStatus status = m_context->tecManager->GetDeviceStatus(device.toStdString());

  // Update TEC status
  if (status.TECOn) {
    m_tecStatus->setText("ON");
    m_tecStatus->setStyleSheet("QLabel { color: #27ae60; font-weight: bold; }");
  }
  else {
    m_tecStatus->setText("OFF");
    m_tecStatus->setStyleSheet("QLabel { color: #e74c3c; font-weight: bold; }");
  }

  // Update laser status
  if (status.LaserOn) {
    m_laserStatus->setText("ON");
    m_laserStatus->setStyleSheet("QLabel { color: #f39c12; font-weight: bold; }");
  }
  else {
    m_laserStatus->setText("OFF");
    m_laserStatus->setStyleSheet("QLabel { color: #e74c3c; font-weight: bold; }");
  }
}

void TECControlPage::enableControls(bool enabled)
{
  m_connectButton->setEnabled(!enabled);
  m_disconnectButton->setEnabled(enabled);
  m_temperatureSlider->setEnabled(enabled);
  m_tecToggle->setEnabled(enabled);
  m_laserCurrentSlider->setEnabled(enabled);
  m_laserToggle->setEnabled(enabled);
  m_preset1Button->setEnabled(enabled);
  m_preset2Button->setEnabled(enabled);
}