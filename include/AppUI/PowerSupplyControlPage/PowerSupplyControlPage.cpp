// PowerSupplyControlPage.cpp - Two-panel layout (device list + channel tabs with embedded measurements)
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
#include <iostream>
#include <cmath>

namespace {
QString formatSI(double value, const QString& unit, int decimals = 3)
{
  double absVal = std::abs(value);
  const char* prefix = "";
  double scale = 1.0;

  if (absVal >= 1e6)       { scale = 1e6;  prefix = "M"; }
  else if (absVal >= 1e3)  { scale = 1e3;  prefix = "k"; }
  else if (absVal >= 1.0)  { scale = 1.0;  prefix = "";  }
  else if (absVal >= 1e-3) { scale = 1e-3; prefix = "m"; }
  else if (absVal >= 1e-6) { scale = 1e-6; prefix = "u"; }
  else if (absVal >= 1e-9) { scale = 1e-9; prefix = "n"; }
  else if (absVal >= 1e-12){ scale = 1e-12; prefix = "p"; }
  else if (absVal == 0.0)  { scale = 1.0;  prefix = "";  }
  else                     { scale = 1e-12; prefix = "p"; }

  return QString("%1 %2%3")
    .arg(value / scale, 0, 'f', decimals)
    .arg(prefix)
    .arg(unit);
}
} // anonymous namespace

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

  // Middle panel - Channel tabs (controls + live measurements)
  m_middlePanel = new QWidget();
  m_middlePanel->setMinimumWidth(400);
  createMiddlePanel(m_middlePanel);

  splitter->addWidget(leftPanel);
  splitter->addWidget(m_middlePanel);
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);

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


QWidget* PowerSupplyControlPage::createChannelTab(int channel)
{
  QWidget* tabWidget = new QWidget();
  QVBoxLayout* layout = new QVBoxLayout(tabWidget);
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(8);

  ChannelControlWidgets controls;
  controls.channel = channel;

  // Shared styles
  const QString setButtonStyle =
    "QPushButton { background-color: #0e639c; color: white; font-weight: bold; font-size: 13px; "
    "border: none; border-radius: 4px; padding: 6px 14px; }"
    "QPushButton:hover { background-color: #1177bb; }"
    "QPushButton:pressed { background-color: #0c5280; }";
  const QString protSetButtonStyle =
    "QPushButton { background-color: #f39c12; color: white; font-weight: bold; font-size: 12px; "
    "border: none; border-radius: 4px; padding: 5px 10px; }"
    "QPushButton:hover { background-color: #e67e22; }"
    "QPushButton:pressed { background-color: #d35400; }";
  const QString groupBoxStyle =
    "QGroupBox { background-color: #1e1e1e; border: 2px solid #3f3f46; border-radius: 6px; "
    "margin-top: 8px; padding-top: 8px; }"
    "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 2px 8px; "
    "color: #ffffff; font-weight: bold; background-color: #1e1e1e; }";

  // ================================================================
  // Top row: Live Measurements (left, big) | Controls (right)
  // ================================================================
  QHBoxLayout* topRow = new QHBoxLayout();
  topRow->setSpacing(6);

  // --- Left: Big measurement display (fixed width to prevent layout jumps) ---
  QFrame* measFrame = new QFrame();
  measFrame->setFixedWidth(240);
  measFrame->setStyleSheet(
    "QFrame { background-color: #0a1e3d; border: 2px solid #1565c0; border-radius: 8px; }");
  QVBoxLayout* measLayout = new QVBoxLayout(measFrame);
  measLayout->setContentsMargins(14, 10, 14, 10);
  measLayout->setSpacing(2);

  controls.outputStateLabel = new QLabel("Output: ---");
  controls.outputStateLabel->setStyleSheet(
    "font-size: 13px; font-weight: bold; color: #888888; background: transparent;");
  measLayout->addWidget(controls.outputStateLabel);

  controls.voltageLabel = new QLabel("V: --- V");
  controls.voltageLabel->setStyleSheet(
    "font-size: 26px; font-weight: bold; color: #ffffff; background: transparent;");
  measLayout->addWidget(controls.voltageLabel);

  controls.currentLabel = new QLabel("A: --- A");
  controls.currentLabel->setStyleSheet(
    "font-size: 26px; font-weight: bold; color: #ffffff; background: transparent;");
  measLayout->addWidget(controls.currentLabel);

  controls.refreshButton = new QPushButton("Refresh");
  controls.refreshButton->setMaximumWidth(80);
  controls.refreshButton->setStyleSheet(
    "QPushButton { background-color: #1a3a6a; color: #aaccff; font-size: 11px; "
    "border: 1px solid #1565c0; border-radius: 3px; padding: 3px 8px; }"
    "QPushButton:hover { background-color: #254b82; color: white; }");
  connect(controls.refreshButton, &QPushButton::clicked, this, [this, channel]() {
    if (m_parameterWorker && m_useParameterWorker) {
      m_parameterWorker->requestReadMeasurements(channel);
    } else {
      updateMeasurements();
    }
  });
  measLayout->addWidget(controls.refreshButton);

  topRow->addWidget(measFrame, 0);

  // --- Right: Voltage/Current inputs + Output buttons (fixed width, left-aligned) ---
  const QString spinBoxStyle =
    "QDoubleSpinBox { background-color: #3c3c3c; color: #ffffff; border: 1px solid #555555; "
    "border-radius: 4px; padding: 4px; font-size: 16px; font-weight: bold; }";

  QWidget* controlPanel = new QWidget();
  controlPanel->setFixedWidth(380);
  QVBoxLayout* controlLayout = new QVBoxLayout(controlPanel);
  controlLayout->setContentsMargins(0, 0, 0, 0);
  controlLayout->setSpacing(4);

  // Voltage row
  QHBoxLayout* voltageRow = new QHBoxLayout();
  voltageRow->setSpacing(4);
  QLabel* vLabel = new QLabel("V:");
  vLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #d4d4d4;");
  vLabel->setFixedWidth(26);
  voltageRow->addWidget(vLabel);
  controls.voltageSpinBox = new QDoubleSpinBox();
  controls.voltageSpinBox->setRange(0.0, 20.0);
  controls.voltageSpinBox->setDecimals(2);
  controls.voltageSpinBox->setSingleStep(0.1);
  controls.voltageSpinBox->setValue(2.0);
  controls.voltageSpinBox->setStyleSheet(spinBoxStyle);
  voltageRow->addWidget(controls.voltageSpinBox, 1);
  controls.setVoltageButton = new QPushButton("Set");
  controls.setVoltageButton->setStyleSheet(setButtonStyle);
  controls.setVoltageButton->setFixedWidth(60);
  connect(controls.setVoltageButton, &QPushButton::clicked, this, [this, channel]() {
    onSetVoltageClicked(channel);
  });
  voltageRow->addWidget(controls.setVoltageButton);
  controlLayout->addLayout(voltageRow);

  // Current row
  QHBoxLayout* currentRow = new QHBoxLayout();
  currentRow->setSpacing(4);
  QLabel* aLabel = new QLabel("A:");
  aLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #d4d4d4;");
  aLabel->setFixedWidth(26);
  currentRow->addWidget(aLabel);
  controls.currentSpinBox = new QDoubleSpinBox();
  controls.currentSpinBox->setRange(0.0, 6.0);
  controls.currentSpinBox->setDecimals(3);
  controls.currentSpinBox->setSingleStep(0.1);
  controls.currentSpinBox->setValue(0.400);
  controls.currentSpinBox->setStyleSheet(spinBoxStyle);
  currentRow->addWidget(controls.currentSpinBox, 1);
  controls.setCurrentButton = new QPushButton("Set");
  controls.setCurrentButton->setStyleSheet(setButtonStyle);
  controls.setCurrentButton->setFixedWidth(60);
  connect(controls.setCurrentButton, &QPushButton::clicked, this, [this, channel]() {
    onSetCurrentClicked(channel);
  });
  currentRow->addWidget(controls.setCurrentButton);
  controlLayout->addLayout(currentRow);

  // Output ON/OFF row
  QHBoxLayout* outputRow = new QHBoxLayout();
  outputRow->setSpacing(4);
  controls.outputOnButton = new QPushButton("OUTPUT ON");
  controls.outputOnButton->setMinimumHeight(36);
  controls.outputOnButton->setStyleSheet(
    "QPushButton { background-color: #27ae60; color: white; font-size: 15px; font-weight: bold; "
    "border: none; border-radius: 4px; }"
    "QPushButton:hover { background-color: #2ecc71; }"
    "QPushButton:pressed { background-color: #1e8449; }");
  connect(controls.outputOnButton, &QPushButton::clicked, this, [this, channel]() {
    onOutputOnClicked(channel);
  });
  controls.outputOffButton = new QPushButton("OUTPUT OFF");
  controls.outputOffButton->setMinimumHeight(36);
  controls.outputOffButton->setStyleSheet(
    "QPushButton { background-color: #e74c3c; color: white; font-size: 15px; font-weight: bold; "
    "border: none; border-radius: 4px; }"
    "QPushButton:hover { background-color: #c0392b; }"
    "QPushButton:pressed { background-color: #a93226; }");
  connect(controls.outputOffButton, &QPushButton::clicked, this, [this, channel]() {
    onOutputOffClicked(channel);
  });
  outputRow->addWidget(controls.outputOnButton);
  outputRow->addWidget(controls.outputOffButton);
  controlLayout->addLayout(outputRow);

  topRow->addWidget(controlPanel, 0);
  topRow->addStretch();
  layout->addLayout(topRow);

  // ================================================================
  // Protection Settings (fixed width, left-aligned)
  // ================================================================
  QGroupBox* protectionGroup = new QGroupBox("Protection");
  protectionGroup->setStyleSheet(groupBoxStyle);
  protectionGroup->setFixedWidth(380);
  QGridLayout* protectionLayout = new QGridLayout(protectionGroup);
  protectionLayout->setContentsMargins(8, 12, 8, 6);
  protectionLayout->setSpacing(4);

  protectionLayout->addWidget(new QLabel("OVP (V):"), 0, 0);
  controls.ovpSpinBox = new QDoubleSpinBox();
  controls.ovpSpinBox->setRange(0.0, 22.0);
  controls.ovpSpinBox->setDecimals(2);
  controls.ovpSpinBox->setValue(6.0);
  controls.ovpSpinBox->setFixedWidth(100);
  protectionLayout->addWidget(controls.ovpSpinBox, 0, 1);

  controls.setOVPButton = new QPushButton("Set");
  controls.setOVPButton->setFixedWidth(50);
  controls.setOVPButton->setStyleSheet(protSetButtonStyle);
  connect(controls.setOVPButton, &QPushButton::clicked, this, [this, channel]() {
    onSetOVPClicked(channel);
  });
  protectionLayout->addWidget(controls.setOVPButton, 0, 2);

  protectionLayout->addWidget(new QLabel("OCP (A):"), 1, 0);
  controls.ocpSpinBox = new QDoubleSpinBox();
  controls.ocpSpinBox->setRange(0.0, 6.5);
  controls.ocpSpinBox->setDecimals(3);
  controls.ocpSpinBox->setValue(1.2);
  controls.ocpSpinBox->setFixedWidth(100);
  protectionLayout->addWidget(controls.ocpSpinBox, 1, 1);

  controls.setOCPButton = new QPushButton("Set");
  controls.setOCPButton->setFixedWidth(50);
  controls.setOCPButton->setStyleSheet(protSetButtonStyle);
  connect(controls.setOCPButton, &QPushButton::clicked, this, [this, channel]() {
    onSetOCPClicked(channel);
  });
  protectionLayout->addWidget(controls.setOCPButton, 1, 2);

  // Left-align the protection group (don't stretch)
  QHBoxLayout* protRow = new QHBoxLayout();
  protRow->addWidget(protectionGroup);
  protRow->addStretch();
  layout->addLayout(protRow);

  layout->addStretch();

  m_channelControls.push_back(controls);
  return tabWidget;
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

  // Route through worker for non-blocking connect
  if (m_parameterWorker && m_useParameterWorker) {
    m_statusLabel->setText("Connecting...");
    m_connectButton->setEnabled(false);
    m_parameterWorker->requestConnect();
    return;
  }

  // Fallback: blocking connect (only if worker not available)
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

  // Route through worker for non-blocking disconnect
  if (m_parameterWorker && m_useParameterWorker) {
    m_disconnectButton->setEnabled(false);
    m_parameterWorker->requestDisconnect();
    return;
  }

  // Fallback: blocking disconnect
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
    // Optimistic UI update + suppress polling overwrite
    m_outputCommandPending = 5;
    for (auto& ctrl : m_channelControls) {
      if (ctrl.channel == channel) {
        ctrl.outputStateLabel->setText("Output: ON");
        ctrl.outputStateLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #4cff4c; background: transparent;");
        break;
      }
    }
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
    // Optimistic UI update + suppress polling overwrite
    m_outputCommandPending = 5;
    for (auto& ctrl : m_channelControls) {
      if (ctrl.channel == channel) {
        ctrl.outputStateLabel->setText("Output: OFF");
        ctrl.outputStateLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #ff6666; background: transparent;");
        break;
      }
    }
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

  // Use worker for non-blocking measurement reads (off UI thread)
  if (m_parameterWorker && m_useParameterWorker) {
    // Request measurements via worker - UI update happens in onWorkerMeasurementsUpdated()
    m_parameterWorker->requestReadAllChannels();
  } else {
    // Fallback: direct calls (blocks UI - only used if worker not available)
    for (auto& ctrl : m_channelControls) {
      bool outputOn = psu->readOutputState(ctrl.channel);
      if (outputOn) {
        ctrl.outputStateLabel->setText("Output: ON");
        ctrl.outputStateLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #4cff4c; background: transparent;");
      }
      else {
        ctrl.outputStateLabel->setText("Output: OFF");
        ctrl.outputStateLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #ff6666; background: transparent;");
      }

      double voltage = psu->readVoltage(ctrl.channel);
      ctrl.voltageLabel->setText("V: " + formatSI(voltage, "V"));
      ctrl.voltageLabel->setStyleSheet("font-size: 26px; font-weight: bold; color: #ffffff; background: transparent;");

      double current = psu->readCurrent(ctrl.channel);
      ctrl.currentLabel->setText("A: " + formatSI(current, "A"));
      ctrl.currentLabel->setStyleSheet("font-size: 26px; font-weight: bold; color: #ffffff; background: transparent;");
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
      // Enable worker polling when page is visible
      if (m_parameterWorker && m_useParameterWorker) {
        m_parameterWorker->setMeasurementPollingEnabled(true);
        std::cout << "[PowerSupplyControlPage] Page shown - enabled status polling" << std::endl;
      } else {
        // Fallback to old refresh timer if NOT using the worker
        m_refreshTimer->start(2000);
      }
    }
  }
}

void PowerSupplyControlPage::hideEvent(QHideEvent* event)
{
  QWidget::hideEvent(event);

  // Disable worker polling when page is hidden to save resources
  if (m_parameterWorker) {
    m_parameterWorker->setMeasurementPollingEnabled(false);
    std::cout << "[PowerSupplyControlPage] Page hidden - disabled status polling" << std::endl;
  }

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
  m_outputCommandPending = 0; // Command confirmed, resume polling updates
  showSuccess(QString("Ch %1: Output %2").arg(channel).arg(on ? "ON" : "OFF"));
}

void PowerSupplyControlPage::onWorkerMeasurementsUpdated(int channel, double voltage, double current, bool outputOn)
{
  for (auto& ctrl : m_channelControls) {
    if (ctrl.channel == channel) {
      // Skip output state update while command is in flight (prevents flicker)
      if (m_outputCommandPending > 0) {
        --m_outputCommandPending;
      } else {
        if (outputOn) {
          ctrl.outputStateLabel->setText("Output: ON");
          ctrl.outputStateLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #4cff4c; background: transparent;");
        }
        else {
          ctrl.outputStateLabel->setText("Output: OFF");
          ctrl.outputStateLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #ff6666; background: transparent;");
        }
      }

      ctrl.voltageLabel->setText("V: " + formatSI(voltage, "V"));
      ctrl.voltageLabel->setStyleSheet("font-size: 26px; font-weight: bold; color: #ffffff; background: transparent;");

      ctrl.currentLabel->setText("A: " + formatSI(current, "A"));
      ctrl.currentLabel->setStyleSheet("font-size: 26px; font-weight: bold; color: #ffffff; background: transparent;");
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

  // Enable polling if page is visible
  if (m_parameterWorker && isVisible()) {
    m_parameterWorker->setMeasurementPollingEnabled(true);
    std::cout << "[PowerSupplyControlPage] Enabled status polling after connection" << std::endl;
  }
}

void PowerSupplyControlPage::onWorkerDeviceDisconnected(QString deviceName)
{
  showSuccess("Disconnected from " + deviceName);

  // Disable polling since device is disconnected
  if (m_parameterWorker) {
    m_parameterWorker->setMeasurementPollingEnabled(false);
    std::cout << "[PowerSupplyControlPage] Disabled status polling after disconnection" << std::endl;
  }

  updateConnectionStatus();
  updateDeviceList();
}
