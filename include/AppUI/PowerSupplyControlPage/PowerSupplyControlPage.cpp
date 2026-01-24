// PowerSupplyControlPage.cpp - Redesigned with 3-panel layout
#include "PowerSupplyControlPage.h"
#include "ApplicationContext.h"
#include "PowerSupplyManager.h"
#include "PSUParameterWorker.h"
#include "IPowerSupply.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QFrame>
#include <QScrollArea>
#include <iostream>

PowerSupplyControlPage::PowerSupplyControlPage(ApplicationContext* context, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_psuManager(context->powerSupplyManager)
  , m_refreshTimer(new QTimer(this))
{
  setupUI();

  // Create parameter worker for non-blocking operations BEFORE loadDevices
  // so that onDeviceSelected can set the device on the worker
  if (m_psuManager && m_useParameterWorker) {
    m_parameterWorker = new PSUParameterWorker(m_psuManager, nullptr);

    // Connect worker signals to UI handlers (using Qt::QueuedConnection for thread safety)
    connect(m_parameterWorker, &PSUParameterWorker::voltageApplied,
      this, &PowerSupplyControlPage::onWorkerVoltageApplied, Qt::QueuedConnection);
    connect(m_parameterWorker, &PSUParameterWorker::currentApplied,
      this, &PowerSupplyControlPage::onWorkerCurrentApplied, Qt::QueuedConnection);
    connect(m_parameterWorker, &PSUParameterWorker::ovpApplied,
      this, &PowerSupplyControlPage::onWorkerOVPApplied, Qt::QueuedConnection);
    connect(m_parameterWorker, &PSUParameterWorker::ocpApplied,
      this, &PowerSupplyControlPage::onWorkerOCPApplied, Qt::QueuedConnection);
    connect(m_parameterWorker, &PSUParameterWorker::outputStateChanged,
      this, &PowerSupplyControlPage::onWorkerOutputChanged, Qt::QueuedConnection);
    connect(m_parameterWorker, &PSUParameterWorker::measurementsUpdated,
      this, &PowerSupplyControlPage::onWorkerMeasurementsUpdated, Qt::QueuedConnection);
    connect(m_parameterWorker, &PSUParameterWorker::commandError,
      this, &PowerSupplyControlPage::onWorkerError, Qt::QueuedConnection);
    connect(m_parameterWorker, &PSUParameterWorker::deviceConnected,
      this, &PowerSupplyControlPage::onWorkerDeviceConnected, Qt::QueuedConnection);
    connect(m_parameterWorker, &PSUParameterWorker::deviceDisconnected,
      this, &PowerSupplyControlPage::onWorkerDeviceDisconnected, Qt::QueuedConnection);

    m_parameterWorker->start();
    std::cout << "[PowerSupplyControlPage] Parameter worker started" << std::endl;
  }

  // Load devices after worker is ready
  loadDevices();

  // Connect refresh timer - now used only when worker is disabled
  connect(m_refreshTimer, &QTimer::timeout, this, &PowerSupplyControlPage::updateMeasurements);
}

PowerSupplyControlPage::~PowerSupplyControlPage()
{
  if (m_parameterWorker) {
    m_parameterWorker->stop();
    delete m_parameterWorker;
    m_parameterWorker = nullptr;
    std::cout << "[PowerSupplyControlPage] Parameter worker stopped and deleted" << std::endl;
  }
}

QString PowerSupplyControlPage::createStyleSheet()
{
  return R"(
    QWidget {
      background-color: #2d2d30;
      color: #d4d4d4;
      font-family: "Segoe UI", Arial, sans-serif;
    }
    QListWidget {
      background-color: #1e1e1e;
      border: 1px solid #3f3f46;
      border-radius: 4px;
      padding: 4px;
      font-size: 13px;
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
    QLabel {
      color: #d4d4d4;
      font-size: 13px;
    }
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
    }
    QDoubleSpinBox {
      background-color: #3c3c3c;
      color: #d4d4d4;
      border: 1px solid #555555;
      border-radius: 4px;
      padding: 6px;
      font-size: 13px;
      min-width: 80px;
    }
    QDoubleSpinBox:focus {
      border: 2px solid #0e639c;
    }
    QPushButton {
      background-color: #0e639c;
      color: white;
      border: none;
      border-radius: 4px;
      padding: 8px 16px;
      font-size: 13px;
      font-weight: 500;
      min-height: 28px;
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
    QTabWidget::pane {
      border: 1px solid #3f3f46;
      background-color: #1e1e1e;
      border-radius: 4px;
    }
    QTabBar::tab {
      background-color: #2d2d30;
      color: #d4d4d4;
      padding: 8px 16px;
      margin-right: 2px;
      border-top-left-radius: 4px;
      border-top-right-radius: 4px;
    }
    QTabBar::tab:selected {
      background-color: #0e639c;
      color: white;
    }
    QTabBar::tab:hover:!selected {
      background-color: #3c3c3c;
    }
  )";
}

void PowerSupplyControlPage::setupUI()
{
  setStyleSheet(createStyleSheet());

  QHBoxLayout* mainLayout = new QHBoxLayout(this);
  mainLayout->setContentsMargins(10, 10, 10, 10);
  mainLayout->setSpacing(10);

  QSplitter* splitter = new QSplitter(Qt::Horizontal);

  // Left panel - Device list
  QWidget* leftPanel = new QWidget();
  leftPanel->setMinimumWidth(180);
  leftPanel->setMaximumWidth(250);
  createLeftPanel(leftPanel);

  // Middle panel - Channel tabs
  m_middlePanel = new QWidget();
  m_middlePanel->setMinimumWidth(350);
  createMiddlePanel(m_middlePanel);

  // Right panel - All measurements
  m_rightPanel = new QWidget();
  m_rightPanel->setMinimumWidth(200);
  createRightPanel(m_rightPanel);

  splitter->addWidget(leftPanel);
  splitter->addWidget(m_middlePanel);
  splitter->addWidget(m_rightPanel);
  splitter->setStretchFactor(0, 1);
  splitter->setStretchFactor(1, 2);
  splitter->setStretchFactor(2, 1);

  mainLayout->addWidget(splitter);

  // Initially disable control panels
  m_middlePanel->setEnabled(false);
}

void PowerSupplyControlPage::createLeftPanel(QWidget* leftPanel)
{
  QVBoxLayout* layout = new QVBoxLayout(leftPanel);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  QLabel* titleLabel = new QLabel("Power Supply Units");
  titleLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #ffffff; padding: 4px;");
  layout->addWidget(titleLabel);

  m_deviceList = new QListWidget();
  layout->addWidget(m_deviceList);

  connect(m_deviceList, &QListWidget::currentRowChanged,
    this, &PowerSupplyControlPage::onDeviceSelected);

  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(4);

  m_connectButton = new QPushButton("Connect");
  m_connectButton->setStyleSheet(
    "QPushButton { background-color: #27ae60; }"
    "QPushButton:hover { background-color: #2ecc71; }");
  connect(m_connectButton, &QPushButton::clicked, this, &PowerSupplyControlPage::onConnectClicked);

  m_disconnectButton = new QPushButton("Disconnect");
  m_disconnectButton->setEnabled(false);
  m_disconnectButton->setStyleSheet(
    "QPushButton { background-color: #e74c3c; }"
    "QPushButton:hover { background-color: #c0392b; }");
  connect(m_disconnectButton, &QPushButton::clicked, this, &PowerSupplyControlPage::onDisconnectClicked);

  buttonLayout->addWidget(m_connectButton);
  buttonLayout->addWidget(m_disconnectButton);
  layout->addLayout(buttonLayout);

  m_connectionStatus = new QLabel("● Disconnected");
  m_connectionStatus->setStyleSheet("color: #e74c3c; font-size: 11px; font-weight: bold; padding: 2px;");
  m_connectionStatus->setAlignment(Qt::AlignCenter);
  layout->addWidget(m_connectionStatus);

  m_deviceInfoLabel = new QLabel("Select a device");
  m_deviceInfoLabel->setStyleSheet(
    "font-size: 10px; color: #aaaaaa; padding: 4px; "
    "background-color: #252526; border-radius: 4px;");
  m_deviceInfoLabel->setWordWrap(true);
  layout->addWidget(m_deviceInfoLabel);

  m_statusLabel = new QLabel("Ready");
  m_statusLabel->setStyleSheet("font-size: 10px; color: #888888; padding: 2px;");
  m_statusLabel->setWordWrap(true);
  layout->addWidget(m_statusLabel);
}

void PowerSupplyControlPage::createMiddlePanel(QWidget* middlePanel)
{
  QVBoxLayout* layout = new QVBoxLayout(middlePanel);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  QLabel* titleLabel = new QLabel("Channel Settings");
  titleLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #ffffff; padding: 4px;");
  layout->addWidget(titleLabel);

  m_channelTabs = new QTabWidget();
  layout->addWidget(m_channelTabs);

  // Create default single channel tab
  rebuildChannelTabs();
}

void PowerSupplyControlPage::createRightPanel(QWidget* rightPanel)
{
  QVBoxLayout* layout = new QVBoxLayout(rightPanel);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  QLabel* titleLabel = new QLabel("Live Measurements");
  titleLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #ffffff; padding: 4px;");
  layout->addWidget(titleLabel);

  // Scrollable container for measurements
  QScrollArea* scrollArea = new QScrollArea();
  scrollArea->setWidgetResizable(true);
  scrollArea->setStyleSheet("QScrollArea { border: none; background-color: transparent; }");

  m_measurementContainer = new QWidget();
  m_measurementLayout = new QVBoxLayout(m_measurementContainer);
  m_measurementLayout->setContentsMargins(0, 0, 0, 0);
  m_measurementLayout->setSpacing(8);

  scrollArea->setWidget(m_measurementContainer);
  layout->addWidget(scrollArea);

  m_refreshButton = new QPushButton("Refresh All");
  m_refreshButton->setStyleSheet(
    "QPushButton { background-color: #0e639c; }"
    "QPushButton:hover { background-color: #1177bb; }");
  connect(m_refreshButton, &QPushButton::clicked, this, &PowerSupplyControlPage::onRefreshClicked);
  layout->addWidget(m_refreshButton);

  rebuildMeasurementPanel();
}

QWidget* PowerSupplyControlPage::createChannelTab(int channel)
{
  QWidget* tabWidget = new QWidget();
  QVBoxLayout* layout = new QVBoxLayout(tabWidget);
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(8);

  ChannelControlWidgets controls;
  controls.channel = channel;

  // Output Control
  QGroupBox* outputGroup = new QGroupBox("Output Control");
  QHBoxLayout* outputLayout = new QHBoxLayout(outputGroup);
  outputLayout->setContentsMargins(8, 16, 8, 8);

  controls.outputOnButton = new QPushButton("OUTPUT ON");
  controls.outputOnButton->setMinimumHeight(40);
  controls.outputOnButton->setStyleSheet(
    "QPushButton { background-color: #27ae60; font-weight: bold; }"
    "QPushButton:hover { background-color: #2ecc71; }");
  connect(controls.outputOnButton, &QPushButton::clicked, this, [this, channel]() {
    onOutputOnClicked(channel);
  });

  controls.outputOffButton = new QPushButton("OUTPUT OFF");
  controls.outputOffButton->setMinimumHeight(40);
  controls.outputOffButton->setStyleSheet(
    "QPushButton { background-color: #e74c3c; font-weight: bold; }"
    "QPushButton:hover { background-color: #c0392b; }");
  connect(controls.outputOffButton, &QPushButton::clicked, this, [this, channel]() {
    onOutputOffClicked(channel);
  });

  outputLayout->addWidget(controls.outputOnButton);
  outputLayout->addWidget(controls.outputOffButton);
  layout->addWidget(outputGroup);

  // Voltage & Current Settings
  QGroupBox* settingsGroup = new QGroupBox("Voltage / Current");
  QGridLayout* settingsLayout = new QGridLayout(settingsGroup);
  settingsLayout->setContentsMargins(8, 16, 8, 8);
  settingsLayout->setSpacing(6);

  settingsLayout->addWidget(new QLabel("Voltage (V):"), 0, 0);
  controls.voltageSpinBox = new QDoubleSpinBox();
  controls.voltageSpinBox->setRange(0.0, 20.0);
  controls.voltageSpinBox->setDecimals(2);
  controls.voltageSpinBox->setSingleStep(0.1);
  controls.voltageSpinBox->setValue(5.0);
  settingsLayout->addWidget(controls.voltageSpinBox, 0, 1);

  controls.setVoltageButton = new QPushButton("Set");
  controls.setVoltageButton->setMinimumWidth(60);
  connect(controls.setVoltageButton, &QPushButton::clicked, this, [this, channel]() {
    onSetVoltageClicked(channel);
  });
  settingsLayout->addWidget(controls.setVoltageButton, 0, 2);

  settingsLayout->addWidget(new QLabel("Current (A):"), 1, 0);
  controls.currentSpinBox = new QDoubleSpinBox();
  controls.currentSpinBox->setRange(0.0, 6.0);
  controls.currentSpinBox->setDecimals(3);
  controls.currentSpinBox->setSingleStep(0.1);
  controls.currentSpinBox->setValue(1.0);
  settingsLayout->addWidget(controls.currentSpinBox, 1, 1);

  controls.setCurrentButton = new QPushButton("Set");
  controls.setCurrentButton->setMinimumWidth(60);
  connect(controls.setCurrentButton, &QPushButton::clicked, this, [this, channel]() {
    onSetCurrentClicked(channel);
  });
  settingsLayout->addWidget(controls.setCurrentButton, 1, 2);

  settingsLayout->setColumnStretch(1, 1);
  layout->addWidget(settingsGroup);

  // Protection Settings
  QGroupBox* protectionGroup = new QGroupBox("Protection");
  QGridLayout* protectionLayout = new QGridLayout(protectionGroup);
  protectionLayout->setContentsMargins(8, 16, 8, 8);
  protectionLayout->setSpacing(6);

  protectionLayout->addWidget(new QLabel("OVP (V):"), 0, 0);
  controls.ovpSpinBox = new QDoubleSpinBox();
  controls.ovpSpinBox->setRange(0.0, 22.0);
  controls.ovpSpinBox->setDecimals(2);
  controls.ovpSpinBox->setValue(6.0);
  protectionLayout->addWidget(controls.ovpSpinBox, 0, 1);

  controls.setOVPButton = new QPushButton("Set");
  controls.setOVPButton->setMinimumWidth(60);
  controls.setOVPButton->setStyleSheet(
    "QPushButton { background-color: #f39c12; }"
    "QPushButton:hover { background-color: #e67e22; }");
  connect(controls.setOVPButton, &QPushButton::clicked, this, [this, channel]() {
    onSetOVPClicked(channel);
  });
  protectionLayout->addWidget(controls.setOVPButton, 0, 2);

  protectionLayout->addWidget(new QLabel("OCP (A):"), 1, 0);
  controls.ocpSpinBox = new QDoubleSpinBox();
  controls.ocpSpinBox->setRange(0.0, 6.5);
  controls.ocpSpinBox->setDecimals(3);
  controls.ocpSpinBox->setValue(1.2);
  protectionLayout->addWidget(controls.ocpSpinBox, 1, 1);

  controls.setOCPButton = new QPushButton("Set");
  controls.setOCPButton->setMinimumWidth(60);
  controls.setOCPButton->setStyleSheet(
    "QPushButton { background-color: #f39c12; }"
    "QPushButton:hover { background-color: #e67e22; }");
  connect(controls.setOCPButton, &QPushButton::clicked, this, [this, channel]() {
    onSetOCPClicked(channel);
  });
  protectionLayout->addWidget(controls.setOCPButton, 1, 2);

  protectionLayout->setColumnStretch(1, 1);
  layout->addWidget(protectionGroup);

  layout->addStretch();

  m_channelControls.push_back(controls);
  return tabWidget;
}

QWidget* PowerSupplyControlPage::createChannelMeasurementWidget(int channel)
{
  QGroupBox* group = new QGroupBox(QString("Channel %1").arg(channel));
  group->setStyleSheet(
    "QGroupBox { background-color: #252526; border: 1px solid #3f3f46; }"
    "QGroupBox::title { color: #0e639c; }");

  QVBoxLayout* layout = new QVBoxLayout(group);
  layout->setContentsMargins(8, 16, 8, 8);
  layout->setSpacing(4);

  ChannelMeasurementWidgets widgets;
  widgets.channel = channel;

  widgets.outputStateLabel = new QLabel("Output: ---");
  widgets.outputStateLabel->setStyleSheet("font-size: 12px; font-weight: bold; color: #888888;");
  layout->addWidget(widgets.outputStateLabel);

  widgets.voltageLabel = new QLabel("V: --- V");
  widgets.voltageLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #27ae60;");
  layout->addWidget(widgets.voltageLabel);

  widgets.currentLabel = new QLabel("I: --- A");
  widgets.currentLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #3498db;");
  layout->addWidget(widgets.currentLabel);

  m_channelMeasurements.push_back(widgets);
  return group;
}

void PowerSupplyControlPage::rebuildChannelTabs()
{
  // Clear existing tabs and controls
  m_channelTabs->clear();
  m_channelControls.clear();

  // Create tabs for each channel
  for (int i = 1; i <= m_currentChannelCount; i++) {
    QWidget* tab = createChannelTab(i);
    m_channelTabs->addTab(tab, QString("Ch %1").arg(i));
  }
}

void PowerSupplyControlPage::rebuildMeasurementPanel()
{
  // Clear existing measurements
  m_channelMeasurements.clear();

  // Remove all widgets from layout
  QLayoutItem* item;
  while ((item = m_measurementLayout->takeAt(0)) != nullptr) {
    if (item->widget()) {
      delete item->widget();
    }
    delete item;
  }

  // Create measurement widgets for each channel
  for (int i = 1; i <= m_currentChannelCount; i++) {
    QWidget* widget = createChannelMeasurementWidget(i);
    m_measurementLayout->addWidget(widget);
  }

  m_measurementLayout->addStretch();
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
    m_statusLabel->setStyleSheet("font-size: 10px; color: #e74c3c; padding: 2px;");
  }
  else {
    m_deviceList->setCurrentRow(0);
  }
}

void PowerSupplyControlPage::updateDeviceList()
{
  for (int i = 0; i < m_deviceList->count(); ++i) {
    QListWidgetItem* item = m_deviceList->item(i);
    QString deviceName = item->text();

    auto psu = m_psuManager->getPowerSupply(deviceName.toStdString());
    if (psu && psu->isConnected()) {
      item->setForeground(QColor("#27ae60"));
    }
    else {
      item->setForeground(QColor("#888888"));
    }
  }
}

void PowerSupplyControlPage::onDeviceSelected(int row)
{
  if (row < 0) {
    m_currentDevice = "";
    m_deviceInfoLabel->setText("Select a device");
    return;
  }

  m_currentDevice = m_deviceList->item(row)->text();

  // Update device info display
  auto info = m_psuManager->getDeviceInfo(m_currentDevice.toStdString());
  QString infoText = QString("Type: %1\nIP: %2:%3\nChannels: %4")
    .arg(QString::fromStdString(info.type))
    .arg(QString::fromStdString(info.ip))
    .arg(info.port)
    .arg(info.channels);
  m_deviceInfoLabel->setText(infoText);

  // Update channel count and rebuild UI if needed
  int newChannelCount = m_psuManager->getDeviceChannelCount(m_currentDevice.toStdString());
  if (newChannelCount != m_currentChannelCount) {
    m_currentChannelCount = newChannelCount;
    rebuildChannelTabs();
    rebuildMeasurementPanel();
  }

  // Update worker with new device
  if (m_parameterWorker) {
    m_parameterWorker->setCurrentDevice(m_currentDevice.toStdString());
    m_parameterWorker->setChannelCount(m_currentChannelCount);
  }

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

    // Only start the old refresh timer if NOT using the worker
    // Worker has its own measurement polling
    if (!m_parameterWorker || !m_useParameterWorker) {
      m_refreshTimer->start(2000);
    }
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

void PowerSupplyControlPage::onOutputOnClicked(int channel)
{
  if (m_parameterWorker && m_useParameterWorker) {
    // Non-blocking request via worker
    m_parameterWorker->requestOutputOn(channel);
    return;
  }

  // Fallback to direct blocking call
  auto psu = m_psuManager->getPowerSupply(m_currentDevice.toStdString());
  if (!psu || !psu->isConnected()) {
    showError("Device not connected");
    return;
  }

  if (psu->setOutputOn(channel)) {
    showSuccess(QString("Ch %1: Output ON").arg(channel));
  }
  else {
    showError(QString("Ch %1: Failed - %2").arg(channel).arg(QString::fromStdString(psu->getLastError())));
  }
}

void PowerSupplyControlPage::onOutputOffClicked(int channel)
{
  if (m_parameterWorker && m_useParameterWorker) {
    // Non-blocking request via worker
    m_parameterWorker->requestOutputOff(channel);
    return;
  }

  // Fallback to direct blocking call
  auto psu = m_psuManager->getPowerSupply(m_currentDevice.toStdString());
  if (!psu || !psu->isConnected()) {
    showError("Device not connected");
    return;
  }

  if (psu->setOutputOff(channel)) {
    showSuccess(QString("Ch %1: Output OFF").arg(channel));
  }
  else {
    showError(QString("Ch %1: Failed - %2").arg(channel).arg(QString::fromStdString(psu->getLastError())));
  }
}

void PowerSupplyControlPage::onSetVoltageClicked(int channel)
{
  // Find the control widgets for this channel
  for (const auto& ctrl : m_channelControls) {
    if (ctrl.channel == channel) {
      double voltage = ctrl.voltageSpinBox->value();

      if (m_parameterWorker && m_useParameterWorker) {
        // Non-blocking request via worker
        m_parameterWorker->requestSetVoltage(channel, voltage);
        return;
      }

      // Fallback to direct blocking call
      auto psu = m_psuManager->getPowerSupply(m_currentDevice.toStdString());
      if (!psu || !psu->isConnected()) {
        showError("Device not connected");
        return;
      }

      if (psu->setVoltage(voltage, channel)) {
        showSuccess(QString("Ch %1: Voltage -> %2 V").arg(channel).arg(voltage, 0, 'f', 2));
      }
      else {
        showError(QString("Ch %1: Failed - %2").arg(channel).arg(QString::fromStdString(psu->getLastError())));
      }
      break;
    }
  }
}

void PowerSupplyControlPage::onSetCurrentClicked(int channel)
{
  for (const auto& ctrl : m_channelControls) {
    if (ctrl.channel == channel) {
      double current = ctrl.currentSpinBox->value();

      if (m_parameterWorker && m_useParameterWorker) {
        // Non-blocking request via worker
        m_parameterWorker->requestSetCurrent(channel, current);
        return;
      }

      // Fallback to direct blocking call
      auto psu = m_psuManager->getPowerSupply(m_currentDevice.toStdString());
      if (!psu || !psu->isConnected()) {
        showError("Device not connected");
        return;
      }

      if (psu->setCurrent(current, channel)) {
        showSuccess(QString("Ch %1: Current -> %2 A").arg(channel).arg(current, 0, 'f', 3));
      }
      else {
        showError(QString("Ch %1: Failed - %2").arg(channel).arg(QString::fromStdString(psu->getLastError())));
      }
      break;
    }
  }
}

void PowerSupplyControlPage::onSetOVPClicked(int channel)
{
  for (const auto& ctrl : m_channelControls) {
    if (ctrl.channel == channel) {
      double ovp = ctrl.ovpSpinBox->value();

      if (m_parameterWorker && m_useParameterWorker) {
        // Non-blocking request via worker
        m_parameterWorker->requestSetOVP(channel, ovp);
        return;
      }

      // Fallback to direct blocking call
      auto psu = m_psuManager->getPowerSupply(m_currentDevice.toStdString());
      if (!psu || !psu->isConnected()) {
        showError("Device not connected");
        return;
      }

      if (psu->setOVP(ovp, channel)) {
        showSuccess(QString("Ch %1: OVP -> %2 V").arg(channel).arg(ovp, 0, 'f', 2));
      }
      else {
        showError(QString("Ch %1: Failed - %2").arg(channel).arg(QString::fromStdString(psu->getLastError())));
      }
      break;
    }
  }
}

void PowerSupplyControlPage::onSetOCPClicked(int channel)
{
  for (const auto& ctrl : m_channelControls) {
    if (ctrl.channel == channel) {
      double ocp = ctrl.ocpSpinBox->value();

      if (m_parameterWorker && m_useParameterWorker) {
        // Non-blocking request via worker
        m_parameterWorker->requestSetOCP(channel, ocp);
        return;
      }

      // Fallback to direct blocking call
      auto psu = m_psuManager->getPowerSupply(m_currentDevice.toStdString());
      if (!psu || !psu->isConnected()) {
        showError("Device not connected");
        return;
      }

      if (psu->setOCP(ocp, channel)) {
        showSuccess(QString("Ch %1: OCP -> %2 A").arg(channel).arg(ocp, 0, 'f', 3));
      }
      else {
        showError(QString("Ch %1: Failed - %2").arg(channel).arg(QString::fromStdString(psu->getLastError())));
      }
      break;
    }
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

  // Update all channel measurements
  for (auto& meas : m_channelMeasurements) {
    // Read output state
    bool outputOn = psu->readOutputState(meas.channel);
    if (outputOn) {
      meas.outputStateLabel->setText("Output: ON");
      meas.outputStateLabel->setStyleSheet("font-size: 12px; font-weight: bold; color: #27ae60;");
    }
    else {
      meas.outputStateLabel->setText("Output: OFF");
      meas.outputStateLabel->setStyleSheet("font-size: 12px; font-weight: bold; color: #e74c3c;");
    }

    // Read voltage
    double voltage = psu->readVoltage(meas.channel);
    if (voltage >= 0) {
      meas.voltageLabel->setText(QString("V: %1 V").arg(voltage, 0, 'f', 3));
      meas.voltageLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #27ae60;");
    }
    else {
      meas.voltageLabel->setText("V: Error");
      meas.voltageLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #e74c3c;");
    }

    // Read current
    double current = psu->readCurrent(meas.channel);
    if (current >= 0) {
      meas.currentLabel->setText(QString("I: %1 A").arg(current, 0, 'f', 4));
      meas.currentLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #3498db;");
    }
    else {
      meas.currentLabel->setText("I: Error");
      meas.currentLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #e74c3c;");
    }
  }
}

void PowerSupplyControlPage::updateConnectionStatus()
{
  if (m_currentDevice.isEmpty()) {
    m_connectButton->setEnabled(false);
    m_disconnectButton->setEnabled(false);
    m_connectionStatus->setText("● No device selected");
    m_connectionStatus->setStyleSheet("color: #666666; font-size: 11px; padding: 2px;");
    m_middlePanel->setEnabled(false);
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
    m_connectionStatus->setStyleSheet("color: #27ae60; font-size: 11px; font-weight: bold; padding: 2px;");
    m_middlePanel->setEnabled(true);
  }
  else {
    m_connectionStatus->setText("● Disconnected");
    m_connectionStatus->setStyleSheet("color: #e74c3c; font-size: 11px; font-weight: bold; padding: 2px;");
    m_middlePanel->setEnabled(false);
  }
}

void PowerSupplyControlPage::showError(const QString& message)
{
  m_statusLabel->setText("X " + message);
  m_statusLabel->setStyleSheet("font-size: 10px; color: #e74c3c; font-weight: bold; padding: 2px;");
  std::cerr << "[PowerSupplyControlPage] " << message.toStdString() << std::endl;
}

void PowerSupplyControlPage::showSuccess(const QString& message)
{
  m_statusLabel->setText("OK " + message);
  m_statusLabel->setStyleSheet("font-size: 10px; color: #27ae60; padding: 2px;");
  std::cout << "[PowerSupplyControlPage] " << message.toStdString() << std::endl;
}

void PowerSupplyControlPage::showEvent(QShowEvent* event)
{
  QWidget::showEvent(event);

  if (!m_currentDevice.isEmpty()) {
    auto psu = m_psuManager->getPowerSupply(m_currentDevice.toStdString());
    if (psu && psu->isConnected()) {
      // Only start the old refresh timer if NOT using the worker
      if (!m_parameterWorker || !m_useParameterWorker) {
        m_refreshTimer->start(2000);
      }
    }
  }
}

void PowerSupplyControlPage::hideEvent(QHideEvent* event)
{
  QWidget::hideEvent(event);
  m_refreshTimer->stop();
}

// ============================================================================
// Worker Signal Handlers
// ============================================================================

void PowerSupplyControlPage::onWorkerVoltageApplied(int channel, double voltage)
{
  showSuccess(QString("Ch %1: Voltage -> %2 V").arg(channel).arg(voltage, 0, 'f', 2));
}

void PowerSupplyControlPage::onWorkerCurrentApplied(int channel, double current)
{
  showSuccess(QString("Ch %1: Current -> %2 A").arg(channel).arg(current, 0, 'f', 3));
}

void PowerSupplyControlPage::onWorkerOVPApplied(int channel, double ovp)
{
  showSuccess(QString("Ch %1: OVP -> %2 V").arg(channel).arg(ovp, 0, 'f', 2));
}

void PowerSupplyControlPage::onWorkerOCPApplied(int channel, double ocp)
{
  showSuccess(QString("Ch %1: OCP -> %2 A").arg(channel).arg(ocp, 0, 'f', 3));
}

void PowerSupplyControlPage::onWorkerOutputChanged(int channel, bool on)
{
  showSuccess(QString("Ch %1: Output %2").arg(channel).arg(on ? "ON" : "OFF"));
}

void PowerSupplyControlPage::onWorkerMeasurementsUpdated(int channel, double voltage, double current, bool outputOn)
{
  // Find the measurement widgets for this channel and update them
  for (auto& meas : m_channelMeasurements) {
    if (meas.channel == channel) {
      // Update output state
      if (outputOn) {
        meas.outputStateLabel->setText("Output: ON");
        meas.outputStateLabel->setStyleSheet("font-size: 12px; font-weight: bold; color: #27ae60;");
      }
      else {
        meas.outputStateLabel->setText("Output: OFF");
        meas.outputStateLabel->setStyleSheet("font-size: 12px; font-weight: bold; color: #e74c3c;");
      }

      // Update voltage
      if (voltage >= 0) {
        meas.voltageLabel->setText(QString("V: %1 V").arg(voltage, 0, 'f', 3));
        meas.voltageLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #27ae60;");
      }
      else {
        meas.voltageLabel->setText("V: Error");
        meas.voltageLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #e74c3c;");
      }

      // Update current
      if (current >= 0) {
        meas.currentLabel->setText(QString("I: %1 A").arg(current, 0, 'f', 4));
        meas.currentLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #3498db;");
      }
      else {
        meas.currentLabel->setText("I: Error");
        meas.currentLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #e74c3c;");
      }
      break;
    }
  }
}

void PowerSupplyControlPage::onWorkerError(int channel, QString operation, QString error)
{
  if (channel > 0) {
    showError(QString("Ch %1: %2 failed - %3").arg(channel).arg(operation).arg(error));
  }
  else {
    showError(QString("%1 failed - %2").arg(operation).arg(error));
  }
}

void PowerSupplyControlPage::onWorkerDeviceConnected(QString deviceName)
{
  showSuccess("Connected to " + deviceName);
  updateConnectionStatus();
  updateDeviceList();
}

void PowerSupplyControlPage::onWorkerDeviceDisconnected(QString deviceName)
{
  showSuccess("Disconnected from " + deviceName);
  updateConnectionStatus();
  updateDeviceList();
}
