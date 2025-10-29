// PowerSupplyControlPage.cpp - Redesigned with 2-panel layout
#include "PowerSupplyControlPage.h"
#include "ApplicationContext.h"
#include "PowerSupplyManager.h"
#include "IPowerSupply.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QFrame>
#include <iostream>

PowerSupplyControlPage::PowerSupplyControlPage(ApplicationContext* context, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_psuManager(context->powerSupplyManager)
  , m_refreshTimer(new QTimer(this))
{
  setupUI();
  loadDevices();

  // Auto-refresh measurements every 2 seconds when connected
  connect(m_refreshTimer, &QTimer::timeout, this, &PowerSupplyControlPage::updateMeasurements);
}

QString PowerSupplyControlPage::createStyleSheet()
{
  return R"(
        /* Main widget - Dark theme */
        QWidget {
            background-color: #2d2d30;
            color: #d4d4d4;
            font-family: "Segoe UI", Arial, sans-serif;
        }

        /* List widget */
        QListWidget {
            background-color: #1e1e1e;
            border: 1px solid #3f3f46;
            border-radius: 4px;
            padding: 4px;
            font-size: 13px;
            color: #d4d4d4;
        }
        QListWidget::item {
            padding: 8px;
            border-radius: 3px;
            margin: 2px 0px;
        }
        QListWidget::item:selected {
            background-color: #0e639c;
            color: white;
        }
        QListWidget::item:hover {
            background-color: #3c3c3c;
        }

        /* Labels */
        QLabel {
            color: #d4d4d4;
            font-size: 13px;
        }

        /* Group boxes */
        QGroupBox {
            font-weight: bold;
            font-size: 13px;
            color: #ffffff;
            border: 2px solid #3f3f46;
            border-radius: 6px;
            margin-top: 12px;
            padding-top: 12px;
            background-color: #1e1e1e;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            padding: 4px 10px;
            background-color: #1e1e1e;
            color: #ffffff;
        }

        /* Spin boxes */
        QDoubleSpinBox {
            background-color: #3c3c3c;
            color: #d4d4d4;
            border: 1px solid #555555;
            border-radius: 4px;
            padding: 6px;
            font-size: 13px;
            min-width: 100px;
        }
        QDoubleSpinBox:focus {
            border: 2px solid #0e639c;
        }
        QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {
            background-color: #2d2d30;
            border: 1px solid #555555;
        }
        QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover {
            background-color: #3c3c3c;
        }

        /* Buttons - Primary */
        QPushButton {
            background-color: #0e639c;
            color: white;
            border: none;
            border-radius: 4px;
            padding: 8px 16px;
            font-size: 13px;
            font-weight: 500;
            min-height: 32px;
        }
        QPushButton:hover {
            background-color: #1177bb;
        }
        QPushButton:pressed {
            background-color: #0c5280;
        }
        QPushButton:disabled {
            background-color: #3c3c3c;
            color: #666666;
        }

        /* Status indicators */
        .status-connected {
            color: #27ae60;
            font-weight: bold;
        }
        .status-disconnected {
            color: #e74c3c;
            font-weight: bold;
        }
        .status-info {
            color: #0e639c;
        }

        /* Frames */
        QFrame {
            background-color: #1e1e1e;
            border: 1px solid #3f3f46;
            border-radius: 6px;
        }
    )";
}

void PowerSupplyControlPage::setupUI()
{
  setStyleSheet(createStyleSheet());

  QHBoxLayout* mainLayout = new QHBoxLayout(this);
  mainLayout->setContentsMargins(10, 10, 10, 10);
  mainLayout->setSpacing(10);

  // Create 2-panel splitter
  QSplitter* splitter = new QSplitter(Qt::Horizontal);

  // Left panel - Device list (30% width)
  QWidget* leftPanel = new QWidget();
  leftPanel->setMinimumWidth(200);
  leftPanel->setMaximumWidth(300);
  createLeftPanel(leftPanel);

  // Right panel - Controls (70% width)
  QWidget* rightPanel = new QWidget();
  createRightPanel(rightPanel);

  splitter->addWidget(leftPanel);
  splitter->addWidget(rightPanel);
  splitter->setStretchFactor(0, 3);  // Left: 30%
  splitter->setStretchFactor(1, 7);  // Right: 70%

  mainLayout->addWidget(splitter);

  // Initially disable control panel
  m_controlPanel->setEnabled(false);
}

void PowerSupplyControlPage::createLeftPanel(QWidget* leftPanel)
{
  QVBoxLayout* layout = new QVBoxLayout(leftPanel);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  // Title
  QLabel* titleLabel = new QLabel("Power Supply Units");
  titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #ffffff; padding: 8px;");
  layout->addWidget(titleLabel);

  // Device list
  m_deviceList = new QListWidget();
  m_deviceList->setAlternatingRowColors(false);
  layout->addWidget(m_deviceList);

  connect(m_deviceList, &QListWidget::currentRowChanged,
    this, &PowerSupplyControlPage::onDeviceSelected);

  // Connection controls
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(6);

  m_connectButton = new QPushButton("Connect");
  m_connectButton->setStyleSheet(
    "QPushButton { background-color: #27ae60; }"
    "QPushButton:hover { background-color: #2ecc71; }"
    "QPushButton:pressed { background-color: #229954; }");
  connect(m_connectButton, &QPushButton::clicked, this, &PowerSupplyControlPage::onConnectClicked);

  m_disconnectButton = new QPushButton("Disconnect");
  m_disconnectButton->setEnabled(false);
  m_disconnectButton->setStyleSheet(
    "QPushButton { background-color: #e74c3c; }"
    "QPushButton:hover { background-color: #c0392b; }"
    "QPushButton:pressed { background-color: #a93226; }");
  connect(m_disconnectButton, &QPushButton::clicked, this, &PowerSupplyControlPage::onDisconnectClicked);

  buttonLayout->addWidget(m_connectButton);
  buttonLayout->addWidget(m_disconnectButton);
  layout->addLayout(buttonLayout);

  // Connection status
  m_connectionStatus = new QLabel("● Disconnected");
  m_connectionStatus->setStyleSheet("color: #e74c3c; font-size: 12px; font-weight: bold; padding: 4px;");
  m_connectionStatus->setAlignment(Qt::AlignCenter);
  layout->addWidget(m_connectionStatus);

  // Status label at bottom
  m_statusLabel = new QLabel("Ready");
  m_statusLabel->setStyleSheet("font-size: 11px; color: #888888; padding: 4px;");
  m_statusLabel->setWordWrap(true);
  layout->addWidget(m_statusLabel);
}

void PowerSupplyControlPage::createRightPanel(QWidget* rightPanel)
{
  QVBoxLayout* layout = new QVBoxLayout(rightPanel);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  m_controlPanel = new QWidget();
  QVBoxLayout* controlLayout = new QVBoxLayout(m_controlPanel);
  controlLayout->setContentsMargins(0, 0, 0, 0);
  controlLayout->setSpacing(10);

  createControlSection(controlLayout);
  createMeasurementSection(controlLayout);

  layout->addWidget(m_controlPanel);
  layout->addStretch();
}

void PowerSupplyControlPage::createControlSection(QVBoxLayout* layout)
{
  // Output Control
  QGroupBox* outputGroup = new QGroupBox("Output Control");
  QHBoxLayout* outputLayout = new QHBoxLayout(outputGroup);
  outputLayout->setSpacing(10);
  outputLayout->setContentsMargins(12, 20, 12, 12);

  m_outputOnButton = new QPushButton("OUTPUT ON");
  m_outputOnButton->setMinimumHeight(45);
  m_outputOnButton->setStyleSheet(
    "QPushButton { background-color: #27ae60; font-size: 14px; font-weight: bold; }"
    "QPushButton:hover { background-color: #2ecc71; }"
    "QPushButton:pressed { background-color: #229954; }");
  connect(m_outputOnButton, &QPushButton::clicked, this, &PowerSupplyControlPage::onOutputOnClicked);

  m_outputOffButton = new QPushButton("OUTPUT OFF");
  m_outputOffButton->setMinimumHeight(45);
  m_outputOffButton->setStyleSheet(
    "QPushButton { background-color: #e74c3c; font-size: 14px; font-weight: bold; }"
    "QPushButton:hover { background-color: #ec7063; }"
    "QPushButton:pressed { background-color: #c0392b; }");
  connect(m_outputOffButton, &QPushButton::clicked, this, &PowerSupplyControlPage::onOutputOffClicked);

  outputLayout->addWidget(m_outputOnButton);
  outputLayout->addWidget(m_outputOffButton);
  layout->addWidget(outputGroup);

  // Voltage & Current Settings
  QGroupBox* settingsGroup = new QGroupBox("Voltage & Current Settings");
  QGridLayout* settingsLayout = new QGridLayout(settingsGroup);
  settingsLayout->setContentsMargins(12, 20, 12, 12);
  settingsLayout->setSpacing(8);
  settingsLayout->setColumnStretch(1, 1);

  // Voltage
  QLabel* voltageLabel = new QLabel("Voltage (V):");
  m_voltageSpinBox = new QDoubleSpinBox();
  m_voltageSpinBox->setRange(0.0, 20.0);
  m_voltageSpinBox->setDecimals(2);
  m_voltageSpinBox->setSingleStep(0.1);
  m_voltageSpinBox->setValue(5.0);
  m_setVoltageButton = new QPushButton("Set");
  m_setVoltageButton->setMinimumWidth(80);
  connect(m_setVoltageButton, &QPushButton::clicked, this, &PowerSupplyControlPage::onSetVoltageClicked);

  settingsLayout->addWidget(voltageLabel, 0, 0);
  settingsLayout->addWidget(m_voltageSpinBox, 0, 1);
  settingsLayout->addWidget(m_setVoltageButton, 0, 2);

  // Current
  QLabel* currentLabel = new QLabel("Current (A):");
  m_currentSpinBox = new QDoubleSpinBox();
  m_currentSpinBox->setRange(0.0, 6.0);
  m_currentSpinBox->setDecimals(3);
  m_currentSpinBox->setSingleStep(0.1);
  m_currentSpinBox->setValue(1.0);
  m_setCurrentButton = new QPushButton("Set");
  m_setCurrentButton->setMinimumWidth(80);
  connect(m_setCurrentButton, &QPushButton::clicked, this, &PowerSupplyControlPage::onSetCurrentClicked);

  settingsLayout->addWidget(currentLabel, 1, 0);
  settingsLayout->addWidget(m_currentSpinBox, 1, 1);
  settingsLayout->addWidget(m_setCurrentButton, 1, 2);

  layout->addWidget(settingsGroup);

  // Protection Settings
  QGroupBox* protectionGroup = new QGroupBox("Protection Settings");
  QGridLayout* protectionLayout = new QGridLayout(protectionGroup);
  protectionLayout->setContentsMargins(12, 20, 12, 12);
  protectionLayout->setSpacing(8);
  protectionLayout->setColumnStretch(1, 1);

  // OVP
  QLabel* ovpLabel = new QLabel("OVP (V):");
  m_ovpSpinBox = new QDoubleSpinBox();
  m_ovpSpinBox->setRange(0.0, 22.0);
  m_ovpSpinBox->setDecimals(2);
  m_ovpSpinBox->setSingleStep(0.1);
  m_ovpSpinBox->setValue(6.0);
  m_setOVPButton = new QPushButton("Set");
  m_setOVPButton->setMinimumWidth(80);
  m_setOVPButton->setStyleSheet(
    "QPushButton { background-color: #f39c12; color: white; }"
    "QPushButton:hover { background-color: #e67e22; }");
  connect(m_setOVPButton, &QPushButton::clicked, this, &PowerSupplyControlPage::onSetOVPClicked);

  protectionLayout->addWidget(ovpLabel, 0, 0);
  protectionLayout->addWidget(m_ovpSpinBox, 0, 1);
  protectionLayout->addWidget(m_setOVPButton, 0, 2);

  // OCP
  QLabel* ocpLabel = new QLabel("OCP (A):");
  m_ocpSpinBox = new QDoubleSpinBox();
  m_ocpSpinBox->setRange(0.0, 6.5);
  m_ocpSpinBox->setDecimals(3);
  m_ocpSpinBox->setSingleStep(0.1);
  m_ocpSpinBox->setValue(1.2);
  m_setOCPButton = new QPushButton("Set");
  m_setOCPButton->setMinimumWidth(80);
  m_setOCPButton->setStyleSheet(
    "QPushButton { background-color: #f39c12; color: white; }"
    "QPushButton:hover { background-color: #e67e22; }");
  connect(m_setOCPButton, &QPushButton::clicked, this, &PowerSupplyControlPage::onSetOCPClicked);

  protectionLayout->addWidget(ocpLabel, 1, 0);
  protectionLayout->addWidget(m_ocpSpinBox, 1, 1);
  protectionLayout->addWidget(m_setOCPButton, 1, 2);

  layout->addWidget(protectionGroup);
}

void PowerSupplyControlPage::createMeasurementSection(QVBoxLayout* layout)
{
  QGroupBox* measurementGroup = new QGroupBox("Live Measurements");
  QVBoxLayout* measurementLayout = new QVBoxLayout(measurementGroup);
  measurementLayout->setContentsMargins(12, 20, 12, 12);
  measurementLayout->setSpacing(8);

  // Voltage reading
  m_voltageReadLabel = new QLabel("Voltage: --- V");
  m_voltageReadLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #27ae60; padding: 4px;");
  measurementLayout->addWidget(m_voltageReadLabel);

  // Current reading
  m_currentReadLabel = new QLabel("Current: --- A");
  m_currentReadLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #0e639c; padding: 4px;");
  measurementLayout->addWidget(m_currentReadLabel);

  // Refresh button
  m_refreshButton = new QPushButton("🔄 Refresh Now");
  m_refreshButton->setStyleSheet(
    "QPushButton { background-color: #0e639c; }"
    "QPushButton:hover { background-color: #1177bb; }");
  connect(m_refreshButton, &QPushButton::clicked, this, &PowerSupplyControlPage::onRefreshClicked);
  measurementLayout->addWidget(m_refreshButton);

  layout->addWidget(measurementGroup);
}

void PowerSupplyControlPage::loadDevices()
{
  m_deviceList->clear();

  if (!m_psuManager) {
    return;
  }

  auto deviceNames = m_psuManager->getDeviceNames();
  for (const auto& name : deviceNames) {
    m_deviceList->addItem(QString::fromStdString(name));
  }

  if (m_deviceList->count() == 0) {
    m_statusLabel->setText("No PSU devices configured");
    m_statusLabel->setStyleSheet("font-size: 11px; color: #e74c3c; padding: 4px;");
  }
  else {
    m_deviceList->setCurrentRow(0);
  }
}

void PowerSupplyControlPage::updateDeviceList()
{
  // Update visual indicators for connected devices
  for (int i = 0; i < m_deviceList->count(); ++i) {
    QListWidgetItem* item = m_deviceList->item(i);
    QString deviceName = item->text();

    auto psu = m_psuManager->getPowerSupply(deviceName.toStdString());
    if (psu && psu->isConnected()) {
      item->setForeground(QColor("#27ae60"));  // Green for connected
    }
    else {
      item->setForeground(QColor("#888888"));  // Gray for disconnected
    }
  }
}

void PowerSupplyControlPage::onDeviceSelected(int row)
{
  if (row < 0) {
    m_currentDevice = "";
    return;
  }

  m_currentDevice = m_deviceList->item(row)->text();
  updateConnectionStatus();
}

void PowerSupplyControlPage::onConnectClicked()
{
  if (m_currentDevice.isEmpty()) {
    showError("No device selected");
    return;
  }

  auto psu = m_psuManager->getPowerSupply(m_currentDevice.toStdString());
  if (!psu) {
    showError("Device not found");
    return;
  }

  if (psu->connect()) {
    showSuccess("Connected to " + m_currentDevice);
    updateConnectionStatus();
    updateDeviceList();
    m_refreshTimer->start(2000);
  }
  else {
    showError("Connection failed: " + QString::fromStdString(psu->getLastError()));
  }
}

void PowerSupplyControlPage::onDisconnectClicked()
{
  if (m_currentDevice.isEmpty()) {
    return;
  }

  auto psu = m_psuManager->getPowerSupply(m_currentDevice.toStdString());
  if (!psu) {
    return;
  }

  m_refreshTimer->stop();
  psu->disconnect();
  showSuccess("Disconnected from " + m_currentDevice);
  updateConnectionStatus();
  updateDeviceList();
}

void PowerSupplyControlPage::onOutputOnClicked()
{
  auto psu = m_psuManager->getPowerSupply(m_currentDevice.toStdString());
  if (!psu) return;

  if (psu->setOutputOn()) {
    showSuccess("Output turned ON");
  }
  else {
    showError("Failed: " + QString::fromStdString(psu->getLastError()));
  }
}

void PowerSupplyControlPage::onOutputOffClicked()
{
  auto psu = m_psuManager->getPowerSupply(m_currentDevice.toStdString());
  if (!psu) return;

  if (psu->setOutputOff()) {
    showSuccess("Output turned OFF");
  }
  else {
    showError("Failed: " + QString::fromStdString(psu->getLastError()));
  }
}

void PowerSupplyControlPage::onSetVoltageClicked()
{
  auto psu = m_psuManager->getPowerSupply(m_currentDevice.toStdString());
  if (!psu) return;

  double voltage = m_voltageSpinBox->value();
  if (psu->setVoltage(voltage)) {
    showSuccess(QString("Voltage → %1 V").arg(voltage, 0, 'f', 2));
  }
  else {
    showError("Failed: " + QString::fromStdString(psu->getLastError()));
  }
}

void PowerSupplyControlPage::onSetCurrentClicked()
{
  auto psu = m_psuManager->getPowerSupply(m_currentDevice.toStdString());
  if (!psu) return;

  double current = m_currentSpinBox->value();
  if (psu->setCurrent(current)) {
    showSuccess(QString("Current → %1 A").arg(current, 0, 'f', 3));
  }
  else {
    showError("Failed: " + QString::fromStdString(psu->getLastError()));
  }
}

void PowerSupplyControlPage::onSetOVPClicked()
{
  auto psu = m_psuManager->getPowerSupply(m_currentDevice.toStdString());
  if (!psu) return;

  double ovp = m_ovpSpinBox->value();
  if (psu->setOVP(ovp)) {
    showSuccess(QString("OVP → %1 V").arg(ovp, 0, 'f', 2));
  }
  else {
    showError("Failed: " + QString::fromStdString(psu->getLastError()));
  }
}

void PowerSupplyControlPage::onSetOCPClicked()
{
  auto psu = m_psuManager->getPowerSupply(m_currentDevice.toStdString());
  if (!psu) return;

  double ocp = m_ocpSpinBox->value();
  if (psu->setOCP(ocp)) {
    showSuccess(QString("OCP → %1 A").arg(ocp, 0, 'f', 3));
  }
  else {
    showError("Failed: " + QString::fromStdString(psu->getLastError()));
  }
}

void PowerSupplyControlPage::onRefreshClicked()
{
  updateMeasurements();
}

void PowerSupplyControlPage::updateMeasurements()
{
  if (m_currentDevice.isEmpty()) {
    return;
  }

  auto psu = m_psuManager->getPowerSupply(m_currentDevice.toStdString());
  if (!psu || !psu->isConnected()) {
    return;
  }

  double voltage = psu->readVoltage();
  double current = psu->readCurrent();

  if (voltage >= 0) {
    m_voltageReadLabel->setText(QString("Voltage: %1 V").arg(voltage, 0, 'f', 3));
  }
  else {
    m_voltageReadLabel->setText("Voltage: Error");
    m_voltageReadLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #e74c3c; padding: 4px;");
  }

  if (current >= 0) {
    m_currentReadLabel->setText(QString("Current: %1 A").arg(current, 0, 'f', 4));
  }
  else {
    m_currentReadLabel->setText("Current: Error");
    m_currentReadLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #e74c3c; padding: 4px;");
  }
}

void PowerSupplyControlPage::updateConnectionStatus()
{
  if (m_currentDevice.isEmpty()) {
    m_connectButton->setEnabled(false);
    m_disconnectButton->setEnabled(false);
    m_connectionStatus->setText("● No device selected");
    m_connectionStatus->setStyleSheet("color: #666666; font-size: 12px; padding: 4px;");
    m_controlPanel->setEnabled(false);
    return;
  }

  auto psu = m_psuManager->getPowerSupply(m_currentDevice.toStdString());
  if (!psu) {
    return;
  }

  bool connected = psu->isConnected();

  m_connectButton->setEnabled(!connected);
  m_disconnectButton->setEnabled(connected);

  if (connected) {
    m_connectionStatus->setText("● Connected");
    m_connectionStatus->setStyleSheet("color: #27ae60; font-size: 12px; font-weight: bold; padding: 4px;");
    m_controlPanel->setEnabled(true);
  }
  else {
    m_connectionStatus->setText("● Disconnected");
    m_connectionStatus->setStyleSheet("color: #e74c3c; font-size: 12px; font-weight: bold; padding: 4px;");
    m_controlPanel->setEnabled(false);
  }
}

void PowerSupplyControlPage::showError(const QString& message)
{
  m_statusLabel->setText("❌ " + message);
  m_statusLabel->setStyleSheet("font-size: 11px; color: #e74c3c; font-weight: bold; padding: 4px;");
  std::cerr << "[PowerSupplyControlPage] " << message.toStdString() << std::endl;
}

void PowerSupplyControlPage::showSuccess(const QString& message)
{
  m_statusLabel->setText("✓ " + message);
  m_statusLabel->setStyleSheet("font-size: 11px; color: #27ae60; padding: 4px;");
  std::cout << "[PowerSupplyControlPage] " << message.toStdString() << std::endl;
}