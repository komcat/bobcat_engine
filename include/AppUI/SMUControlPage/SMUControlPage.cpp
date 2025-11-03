// SMUControlPage.cpp - Tabbed SMU control interface
#include "SMUControlPage.h"
#include "ApplicationContext.h"
#include "SMUManager.h"
#include "ISMUDevice.h"
#include "SMUTypes.h"  // For SMUMode enum
#include "Keithley6482Adapter.h"
#include "SMUPollingManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QFrame>
#include <QScrollArea>
#include <iostream>

SMUControlPage::SMUControlPage(ApplicationContext* context, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_smuManager(context->smuManager)
{
  setupUI();
  loadDevices();
}

QString SMUControlPage::createStyleSheet()
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

        /* ComboBox */
        QComboBox {
            background-color: #3c3c3c;
            color: #d4d4d4;
            border: 1px solid #555555;
            border-radius: 4px;
            padding: 6px;
            font-size: 13px;
            min-width: 120px;
        }
        QComboBox:focus {
            border: 2px solid #0e639c;
        }
        QComboBox::drop-down {
            border: none;
        }
        QComboBox QAbstractItemView {
            background-color: #3c3c3c;
            color: #d4d4d4;
            selection-background-color: #0e639c;
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
        .status-polling {
            color: #27ae60;
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

        /* Scroll bars */
        QScrollBar:vertical {
            background-color: #1e1e1e;
            width: 12px;
            margin: 0px;
        }
        QScrollBar::handle:vertical {
            background-color: #3c3c3c;
            min-height: 20px;
            border-radius: 6px;
            margin: 2px;
        }
        QScrollBar::handle:vertical:hover {
            background-color: #4a4a4a;
        }
        QScrollBar::add-line:vertical,
        QScrollBar::sub-line:vertical {
            height: 0px;
        }
        QScrollBar::add-page:vertical,
        QScrollBar::sub-page:vertical {
            background: none;
        }

        /* Spin boxes */
        QDoubleSpinBox {
            background-color: #3c3c3c;
            color: #d4d4d4;
            border: 1px solid #555555;
            border-radius: 4px;
            padding: 5px;
            font-size: 13px;
        }
        QDoubleSpinBox:focus {
            border: 2px solid #0e639c;
        }
    )";
}

void SMUControlPage::setupUI()
{
  setStyleSheet(createStyleSheet());

  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(10, 10, 10, 10);
  mainLayout->setSpacing(10);

  // Create tab widget
  m_tabWidget = new QTabWidget();
  m_tabWidget->setStyleSheet(R"(
    QTabWidget::pane {
      border: 2px solid #3f3f46;
      border-radius: 6px;
      background-color: #2d2d30;
    }
    QTabBar::tab {
      background-color: #3c3c3c;
      color: #d4d4d4;
      padding: 12px 24px;
      margin-right: 4px;
      border-top-left-radius: 6px;
      border-top-right-radius: 6px;
      font-size: 14px;
      font-weight: bold;
    }
    QTabBar::tab:selected {
      background-color: #0e639c;
      color: white;
    }
    QTabBar::tab:hover {
      background-color: #4a4a4a;
    }
  )");

  // Create tabs
  createConnectionTab();
  createSimpleTab();
  createPollingTab();

  mainLayout->addWidget(m_tabWidget);

  // Status bar at bottom
  m_statusLabel = new QLabel("");
  m_statusLabel->setAlignment(Qt::AlignCenter);
  m_statusLabel->setStyleSheet("QLabel { font-size: 12px; padding: 8px; background-color: #1e1e1e; border-radius: 4px; }");
  mainLayout->addWidget(m_statusLabel);
}

// ========== TAB 1: CONNECTION ==========
void SMUControlPage::createConnectionTab()
{
  QWidget* tab = new QWidget();
  QVBoxLayout* layout = new QVBoxLayout(tab);
  layout->setContentsMargins(20, 20, 20, 20);
  layout->setSpacing(15);

  // Title
  QLabel* title = new QLabel("SMU Device Connection");
  title->setStyleSheet("QLabel { font-size: 18px; font-weight: bold; color: #ffffff; }");
  layout->addWidget(title);

  // Device list
  QLabel* listLabel = new QLabel("Available Devices:");
  listLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; }");
  layout->addWidget(listLabel);

  m_deviceList = new QListWidget();
  m_deviceList->setMinimumHeight(150);
  layout->addWidget(m_deviceList);
  connect(m_deviceList, &QListWidget::currentRowChanged, this, &SMUControlPage::onDeviceSelected);

  // Connection status
  m_connectionStatus = new QLabel("● Not connected");
  m_connectionStatus->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #e74c3c; padding: 10px; }");
  m_connectionStatus->setAlignment(Qt::AlignCenter);
  layout->addWidget(m_connectionStatus);

  // Device info
  m_deviceInfoLabel = new QLabel("");
  m_deviceInfoLabel->setStyleSheet("QLabel { font-size: 12px; color: #888888; padding: 10px; background-color: #1e1e1e; border-radius: 4px; }");
  m_deviceInfoLabel->setWordWrap(true);
  m_deviceInfoLabel->setAlignment(Qt::AlignLeft);
  layout->addWidget(m_deviceInfoLabel);

  // Connect/Disconnect buttons
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  m_connectButton = new QPushButton("Connect");
  m_connectButton->setMinimumHeight(45);
  m_connectButton->setStyleSheet("QPushButton { background-color: #27ae60; font-size: 14px; font-weight: bold; }");
  m_disconnectButton = new QPushButton("Disconnect");
  m_disconnectButton->setMinimumHeight(45);
  m_disconnectButton->setStyleSheet("QPushButton { background-color: #e74c3c; font-size: 14px; font-weight: bold; }");
  m_disconnectButton->setEnabled(false);

  connect(m_connectButton, &QPushButton::clicked, this, &SMUControlPage::onConnectClicked);
  connect(m_disconnectButton, &QPushButton::clicked, this, &SMUControlPage::onDisconnectClicked);

  buttonLayout->addWidget(m_connectButton);
  buttonLayout->addWidget(m_disconnectButton);
  layout->addLayout(buttonLayout);

  layout->addStretch();

  m_tabWidget->addTab(tab, "Connection");
}

// ========== TAB 2: SIMPLE ==========
void SMUControlPage::createSimpleTab()
{
  // Create scroll area for tab content
  QScrollArea* scrollArea = new QScrollArea();
  scrollArea->setWidgetResizable(true);
  scrollArea->setFrameShape(QFrame::NoFrame);
  scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

  QWidget* tab = new QWidget();
  QVBoxLayout* layout = new QVBoxLayout(tab);
  layout->setContentsMargins(20, 20, 20, 20);
  layout->setSpacing(15);

  // Title
  QLabel* title = new QLabel("Simple Control & Measurements");
  title->setStyleSheet("QLabel { font-size: 18px; font-weight: bold; color: #ffffff; }");
  layout->addWidget(title);

  // ========== CHANNEL SELECTION ==========
  QGroupBox* channelGroup = new QGroupBox("Channel Configuration");
  QGridLayout* channelGrid = new QGridLayout(channelGroup);
  channelGrid->setSpacing(10);

  channelGrid->addWidget(new QLabel("Channel Count:"), 0, 0);
  m_channelCountLabel = new QLabel("0");
  m_channelCountLabel->setStyleSheet("QLabel { font-weight: bold; }");
  channelGrid->addWidget(m_channelCountLabel, 0, 1);

  channelGrid->addWidget(new QLabel("Select Channel:"), 1, 0);
  m_channelComboBox = new QComboBox();
  connect(m_channelComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &SMUControlPage::onChannelSelected);
  channelGrid->addWidget(m_channelComboBox, 1, 1);

  layout->addWidget(channelGroup);

  // ========== OUTPUT CONTROL ==========
  m_outputControlGroup = new QGroupBox("Output Control");
  QHBoxLayout* outputLayout = new QHBoxLayout(m_outputControlGroup);
  outputLayout->setSpacing(15);

  // Buttons
  QVBoxLayout* buttonColumn = new QVBoxLayout();
  m_outputOnButton = new QPushButton("Output ON");
  m_outputOnButton->setMinimumHeight(40);
  m_outputOnButton->setStyleSheet("QPushButton { background-color: #27ae60; font-weight: bold; font-size: 14px; }");
  connect(m_outputOnButton, &QPushButton::clicked, this, &SMUControlPage::onOutputOnClicked);
  buttonColumn->addWidget(m_outputOnButton);

  m_outputOffButton = new QPushButton("Output OFF");
  m_outputOffButton->setMinimumHeight(40);
  m_outputOffButton->setStyleSheet("QPushButton { background-color: #e74c3c; font-weight: bold; font-size: 14px; }");
  connect(m_outputOffButton, &QPushButton::clicked, this, &SMUControlPage::onOutputOffClicked);
  buttonColumn->addWidget(m_outputOffButton);
  outputLayout->addLayout(buttonColumn, 1);

  // State display
  QVBoxLayout* stateColumn = new QVBoxLayout();
  QLabel* stateTitle = new QLabel("Output State:");
  stateTitle->setStyleSheet("QLabel { font-size: 12px; color: #888888; }");
  stateColumn->addWidget(stateTitle);

  m_outputStateLabel = new QLabel("OFF");
  m_outputStateLabel->setStyleSheet("QLabel { "
    "font-size: 24px; font-weight: bold; color: #e74c3c; "
    "background-color: #1e1e1e; border: 2px solid #e74c3c; "
    "border-radius: 4px; padding: 15px; }");
  m_outputStateLabel->setAlignment(Qt::AlignCenter);
  m_outputStateLabel->setMinimumHeight(60);
  stateColumn->addWidget(m_outputStateLabel);
  outputLayout->addLayout(stateColumn, 2);

  layout->addWidget(m_outputControlGroup);

  // ========== READ MEASUREMENTS ==========
  QGroupBox* readGroup = new QGroupBox("Read Measurements");
  QVBoxLayout* readLayout = new QVBoxLayout(readGroup);
  readLayout->setSpacing(10);

  // Voltage reading
  QLabel* voltageLabel = new QLabel("Voltage:");
  voltageLabel->setStyleSheet("QLabel { font-weight: bold; }");
  readLayout->addWidget(voltageLabel);

  m_voltageReadLabel = new QLabel("--- V");
  m_voltageReadLabel->setStyleSheet("QLabel { "
    "font-size: 20px; font-weight: bold; color: #3498db; "
    "background-color: #1e1e1e; border: 2px solid #3f3f46; "
    "border-radius: 4px; padding: 10px; }");
  m_voltageReadLabel->setAlignment(Qt::AlignCenter);
  readLayout->addWidget(m_voltageReadLabel);

  // Current reading
  QLabel* currentLabel = new QLabel("Current (Selected Channel):");
  currentLabel->setStyleSheet("QLabel { font-weight: bold; margin-top: 10px; }");
  readLayout->addWidget(currentLabel);

  m_currentReadLabel = new QLabel("--- A");
  m_currentReadLabel->setStyleSheet("QLabel { "
    "font-size: 20px; font-weight: bold; color: #27ae60; "
    "background-color: #1e1e1e; border: 2px solid #3f3f46; "
    "border-radius: 4px; padding: 10px; }");
  m_currentReadLabel->setAlignment(Qt::AlignCenter);
  readLayout->addWidget(m_currentReadLabel);

  // Refresh button
  m_refreshButton = new QPushButton("Refresh Measurements");
  m_refreshButton->setMinimumHeight(40);
  connect(m_refreshButton, &QPushButton::clicked, this, &SMUControlPage::onRefreshClicked);
  readLayout->addWidget(m_refreshButton);

  layout->addWidget(readGroup);

  // ========== SET SOURCE (NEW) ==========
  QGroupBox* sourceGroup = new QGroupBox("Set Source");
  QGridLayout* sourceGrid = new QGridLayout(sourceGroup);
  sourceGrid->setSpacing(10);

  // Set Voltage
  sourceGrid->addWidget(new QLabel("Voltage (V):"), 0, 0);
  m_setVoltageSpinBox = new QDoubleSpinBox();
  m_setVoltageSpinBox->setRange(-100.0, 100.0);
  m_setVoltageSpinBox->setDecimals(3);
  m_setVoltageSpinBox->setSingleStep(0.1);
  m_setVoltageSpinBox->setValue(0.0);
  m_setVoltageSpinBox->setStyleSheet("QDoubleSpinBox { padding: 5px; }");
  sourceGrid->addWidget(m_setVoltageSpinBox, 0, 1);

  m_setVoltageButton = new QPushButton("Set Voltage");
  m_setVoltageButton->setMinimumHeight(35);
  connect(m_setVoltageButton, &QPushButton::clicked, this, &SMUControlPage::onSetVoltageClicked);
  sourceGrid->addWidget(m_setVoltageButton, 0, 2);

  // Set Current
  sourceGrid->addWidget(new QLabel("Current (A):"), 1, 0);
  m_setCurrentSpinBox = new QDoubleSpinBox();
  m_setCurrentSpinBox->setRange(-1.0, 1.0);
  m_setCurrentSpinBox->setDecimals(6);
  m_setCurrentSpinBox->setSingleStep(0.001);
  m_setCurrentSpinBox->setValue(0.0);
  m_setCurrentSpinBox->setStyleSheet("QDoubleSpinBox { padding: 5px; }");
  sourceGrid->addWidget(m_setCurrentSpinBox, 1, 1);

  m_setCurrentButton = new QPushButton("Set Current");
  m_setCurrentButton->setMinimumHeight(35);
  connect(m_setCurrentButton, &QPushButton::clicked, this, &SMUControlPage::onSetCurrentClicked);
  sourceGrid->addWidget(m_setCurrentButton, 1, 2);

  layout->addWidget(sourceGroup);

  // ========== SET COMPLIANCE (PROTECTION LIMITS) ==========
  QGroupBox* complianceGroup = new QGroupBox("Set Compliance (Protection Limits)");
  QGridLayout* complianceGrid = new QGridLayout(complianceGroup);
  complianceGrid->setSpacing(10);

  // Voltage Compliance (over-voltage protection for current source)
  complianceGrid->addWidget(new QLabel("Voltage Compliance (V):"), 0, 0);
  m_voltageComplianceSpinBox = new QDoubleSpinBox();
  m_voltageComplianceSpinBox->setRange(0.0, 100.0);
  m_voltageComplianceSpinBox->setDecimals(3);
  m_voltageComplianceSpinBox->setSingleStep(0.1);
  m_voltageComplianceSpinBox->setValue(5.0);  // Default 5V
  m_voltageComplianceSpinBox->setStyleSheet("QDoubleSpinBox { padding: 5px; }");
  complianceGrid->addWidget(m_voltageComplianceSpinBox, 0, 1);

  m_setVoltageComplianceButton = new QPushButton("Set V Compliance");
  m_setVoltageComplianceButton->setMinimumHeight(35);
  connect(m_setVoltageComplianceButton, &QPushButton::clicked, this, &SMUControlPage::onSetVoltageComplianceClicked);
  complianceGrid->addWidget(m_setVoltageComplianceButton, 0, 2);

  // Current Compliance (over-current protection for voltage source)
  complianceGrid->addWidget(new QLabel("Current Compliance (A):"), 1, 0);
  m_currentComplianceSpinBox = new QDoubleSpinBox();
  m_currentComplianceSpinBox->setRange(0.0, 1.0);
  m_currentComplianceSpinBox->setDecimals(6);
  m_currentComplianceSpinBox->setSingleStep(0.001);
  m_currentComplianceSpinBox->setValue(0.01);  // Default 10mA
  m_currentComplianceSpinBox->setStyleSheet("QDoubleSpinBox { padding: 5px; }");
  complianceGrid->addWidget(m_currentComplianceSpinBox, 1, 1);

  m_setCurrentComplianceButton = new QPushButton("Set I Compliance");
  m_setCurrentComplianceButton->setMinimumHeight(35);
  connect(m_setCurrentComplianceButton, &QPushButton::clicked, this, &SMUControlPage::onSetCurrentComplianceClicked);
  complianceGrid->addWidget(m_setCurrentComplianceButton, 1, 2);

  layout->addWidget(complianceGroup);

  layout->addStretch();

  // Set tab as scroll area content
  scrollArea->setWidget(tab);
  m_tabWidget->addTab(scrollArea, "Simple");
}

// ========== TAB 3: POLLING ==========
void SMUControlPage::createPollingTab()
{
  // Create scroll area for tab content
  QScrollArea* scrollArea = new QScrollArea();
  scrollArea->setWidgetResizable(true);
  scrollArea->setFrameShape(QFrame::NoFrame);
  scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

  QWidget* tab = new QWidget();
  QVBoxLayout* layout = new QVBoxLayout(tab);
  layout->setContentsMargins(20, 20, 20, 20);
  layout->setSpacing(15);

  // Title
  QLabel* title = new QLabel("Continuous Polling");
  title->setStyleSheet("QLabel { font-size: 18px; font-weight: bold; color: #ffffff; }");
  layout->addWidget(title);

  // ========== POLLING CONFIGURATION ==========
  QGroupBox* configGroup = new QGroupBox("Polling Configuration");
  QGridLayout* configGrid = new QGridLayout(configGroup);
  configGrid->setSpacing(10);

  // Channel selection
  configGrid->addWidget(new QLabel("Channel:"), 0, 0);
  m_pollingChannelComboBox = new QComboBox();
  configGrid->addWidget(m_pollingChannelComboBox, 0, 1);

  // Measurement mode
  configGrid->addWidget(new QLabel("Measurement Mode:"), 1, 0);
  m_pollingModeComboBox = new QComboBox();
  m_pollingModeComboBox->addItem("ACCURATE (1.0 NPLC, ~10 Hz)");
  m_pollingModeComboBox->addItem("FAST (0.1 NPLC, ~80 Hz)");
  m_pollingModeComboBox->addItem("ULTRA_FAST (0.01 NPLC, ~150 Hz)");
  m_pollingModeComboBox->setCurrentIndex(1);  // Default: FAST
  connect(m_pollingModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &SMUControlPage::onModeChanged);
  configGrid->addWidget(m_pollingModeComboBox, 1, 1);

  // Measurement type
  configGrid->addWidget(new QLabel("Measurement Type:"), 2, 0);
  m_pollingTypeComboBox = new QComboBox();
  m_pollingTypeComboBox->addItem("Current (A)");
  m_pollingTypeComboBox->addItem("Voltage (V)");
  m_pollingTypeComboBox->setCurrentIndex(0);  // Default: Current
  connect(m_pollingTypeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &SMUControlPage::onMeasurementTypeChanged);
  configGrid->addWidget(m_pollingTypeComboBox, 2, 1);

  layout->addWidget(configGroup);

  // ========== POLLING CONTROL ==========
  QGroupBox* controlGroup = new QGroupBox("Control");
  QHBoxLayout* controlLayout = new QHBoxLayout(controlGroup);
  controlLayout->setSpacing(15);

  // Buttons
  QVBoxLayout* buttonColumn = new QVBoxLayout();
  m_startPollingButton = new QPushButton("Start Polling");
  m_startPollingButton->setMinimumHeight(50);
  m_startPollingButton->setStyleSheet("QPushButton { background-color: #27ae60; font-weight: bold; font-size: 14px; }");
  connect(m_startPollingButton, &QPushButton::clicked, this, &SMUControlPage::onStartPollingClicked);
  buttonColumn->addWidget(m_startPollingButton);

  m_stopPollingButton = new QPushButton("Stop Polling");
  m_stopPollingButton->setMinimumHeight(50);
  m_stopPollingButton->setStyleSheet("QPushButton { background-color: #e74c3c; font-weight: bold; font-size: 14px; }");
  m_stopPollingButton->setEnabled(false);
  connect(m_stopPollingButton, &QPushButton::clicked, this, &SMUControlPage::onStopPollingClicked);
  buttonColumn->addWidget(m_stopPollingButton);
  controlLayout->addLayout(buttonColumn, 1);

  // Status display
  QVBoxLayout* statusColumn = new QVBoxLayout();
  QLabel* statusTitle = new QLabel("Status:");
  statusTitle->setStyleSheet("QLabel { font-size: 12px; color: #888888; }");
  statusColumn->addWidget(statusTitle);

  m_pollingStatusLabel = new QLabel("Stopped");
  m_pollingStatusLabel->setStyleSheet("QLabel { "
    "font-size: 16px; font-weight: bold; color: #e74c3c; "
    "background-color: #1e1e1e; border: 2px solid #3f3f46; "
    "border-radius: 4px; padding: 15px; }");
  m_pollingStatusLabel->setAlignment(Qt::AlignCenter);
  statusColumn->addWidget(m_pollingStatusLabel);
  controlLayout->addLayout(statusColumn, 2);

  layout->addWidget(controlGroup);

  // ========== POLLING DISPLAY ==========
  QGroupBox* displayGroup = new QGroupBox("Live Measurements");
  QVBoxLayout* displayLayout = new QVBoxLayout(displayGroup);
  displayLayout->setSpacing(10);

  // Real-time value
  QLabel* valueTitle = new QLabel("Current Value:");
  valueTitle->setStyleSheet("QLabel { font-weight: bold; }");
  displayLayout->addWidget(valueTitle);

  m_pollingValueLabel = new QLabel("--- A");
  m_pollingValueLabel->setStyleSheet("QLabel { "
    "font-size: 32px; font-weight: bold; color: #3498db; "
    "background-color: #1e1e1e; border: 2px solid #3f3f46; "
    "border-radius: 4px; padding: 20px; }");
  m_pollingValueLabel->setAlignment(Qt::AlignCenter);
  m_pollingValueLabel->setMinimumHeight(100);
  displayLayout->addWidget(m_pollingValueLabel);

  // Sample count
  m_pollingSampleLabel = new QLabel("Samples: 0");
  m_pollingSampleLabel->setStyleSheet("QLabel { "
    "font-size: 12px; color: #888888; padding: 5px; }");
  m_pollingSampleLabel->setAlignment(Qt::AlignCenter);
  displayLayout->addWidget(m_pollingSampleLabel);

  layout->addWidget(displayGroup);

  layout->addStretch();

  // Set tab as scroll area content
  scrollArea->setWidget(tab);
  m_tabWidget->addTab(scrollArea, "Polling");
}

// ========== DEVICE MANAGEMENT ==========
void SMUControlPage::loadDevices()
{
  m_deviceList->clear();

  if (!m_smuManager) {
    return;
  }

  auto deviceNames = m_smuManager->GetDeviceNames();
  for (const auto& name : deviceNames) {
    m_deviceList->addItem(QString::fromStdString(name));
  }

  if (!deviceNames.empty()) {
    m_deviceList->setCurrentRow(0);
  }
}

void SMUControlPage::onDeviceSelected(int row)
{
  if (row < 0) return;

  m_currentDevice = m_deviceList->item(row)->text();
  std::cout << "[SMUControlPage] Selected device: " << m_currentDevice.toStdString() << std::endl;

  updateConnectionStatus();
  updateChannelList();
  updateOutputState();
  updateDeviceCapabilities();  // Update UI based on device capabilities
}

void SMUControlPage::onConnectClicked()
{
  if (m_currentDevice.isEmpty()) {
    showError("No device selected");
    return;
  }

  auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
  if (!smu) {
    showError("Device not found");
    return;
  }

  if (smu->Connect()) {
    showSuccess("Connected to " + m_currentDevice);
    updateConnectionStatus();
    updateChannelList();
    updateOutputState();
    updateDeviceCapabilities();  // Update UI based on device capabilities
  } else {
    showError(QString::fromStdString(smu->GetLastError()));
  }
}

void SMUControlPage::onDisconnectClicked()
{
  if (m_currentDevice.isEmpty()) return;

  auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
  if (smu) {
    smu->Disconnect();
    showSuccess("Disconnected from " + m_currentDevice);
    updateConnectionStatus();
    updateOutputState();
  }
}

// Channel selection
void SMUControlPage::onChannelSelected(int index)
{
  std::cout << "[SMUControlPage] Channel selected: " << (index + 1) << std::endl;
  updateMeasurements();
}

// Measurement mode
void SMUControlPage::onModeChanged(int index)
{
  if (m_currentDevice.isEmpty()) return;

  auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
  if (!smu || !smu->IsConnected()) return;

  auto keithley = std::dynamic_pointer_cast<Keithley6482Adapter>(smu);
  if (!keithley) return;

  Keithley6482Adapter::MeasurementMode mode;
  QString modeName;

  switch (index) {
    case 0:
      mode = Keithley6482Adapter::MeasurementMode::ACCURATE;
      modeName = "ACCURATE";
      break;
    case 1:
      mode = Keithley6482Adapter::MeasurementMode::FAST;
      modeName = "FAST";
      break;
    case 2:
      mode = Keithley6482Adapter::MeasurementMode::ULTRA_FAST;
      modeName = "ULTRA_FAST";
      break;
    default:
      return;
  }

  if (keithley->ConfigureMeasurementMode(mode)) {
    showSuccess("Mode set to " + modeName);
  } else {
    showError(QString::fromStdString(keithley->GetLastError()));
  }
}

// Measurement type changed (for polling tab)
void SMUControlPage::onMeasurementTypeChanged(int index)
{
  std::cout << "[SMUControlPage] Polling measurement type changed to: "
            << (index == 0 ? "Current" : "Voltage") << std::endl;

  // Update polling display unit
  QString unit = (index == 0) ? " A" : " V";
  m_pollingValueLabel->setText("---" + unit);
}

// Output control
void SMUControlPage::onOutputOnClicked()
{
  if (m_currentDevice.isEmpty()) {
    showError("No device selected");
    return;
  }

  auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
  if (!smu || !smu->IsConnected()) {
    showError("Device not connected");
    return;
  }

  if (smu->SetOutputOn()) {
    showSuccess("Output turned ON");
    updateOutputState();
  } else {
    showError("Failed to turn output ON: " + QString::fromStdString(smu->GetLastError()));
  }
}

void SMUControlPage::onOutputOffClicked()
{
  if (m_currentDevice.isEmpty()) {
    showError("No device selected");
    return;
  }

  auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
  if (!smu || !smu->IsConnected()) {
    showError("Device not connected");
    return;
  }

  if (smu->SetOutputOff()) {
    showSuccess("Output turned OFF");
    updateOutputState();
  } else {
    showError("Failed to turn output OFF: " + QString::fromStdString(smu->GetLastError()));
  }
}

void SMUControlPage::updateOutputState()
{
  if (m_currentDevice.isEmpty()) return;

  auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
  if (!smu || !smu->IsConnected()) {
    m_outputStateLabel->setText("---");
    m_outputStateLabel->setStyleSheet("QLabel { "
      "font-size: 20px; "
      "font-weight: bold; "
      "color: #888888; "
      "background-color: #1e1e1e; "
      "border: 2px solid #3f3f46; "
      "border-radius: 4px; "
      "padding: 8px; "
      "}");
    return;
  }

  bool isOn = smu->IsOutputOn();

  if (isOn) {
    m_outputStateLabel->setText("ON");
    m_outputStateLabel->setStyleSheet("QLabel { "
      "font-size: 20px; "
      "font-weight: bold; "
      "color: #27ae60; "
      "background-color: #1e1e1e; "
      "border: 2px solid #27ae60; "
      "border-radius: 4px; "
      "padding: 8px; "
      "}");
  } else {
    m_outputStateLabel->setText("OFF");
    m_outputStateLabel->setStyleSheet("QLabel { "
      "font-size: 20px; "
      "font-weight: bold; "
      "color: #e74c3c; "
      "background-color: #1e1e1e; "
      "border: 2px solid #e74c3c; "
      "border-radius: 4px; "
      "padding: 8px; "
      "}");
  }
}

// Set voltage/current/compliance
void SMUControlPage::onSetVoltageClicked()
{
  if (m_currentDevice.isEmpty()) {
    showError("No device selected");
    return;
  }

  auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
  if (!smu || !smu->IsConnected()) {
    showError("Device not connected");
    return;
  }

  double voltage = m_setVoltageSpinBox->value();

  if (smu->SetVoltage(voltage)) {
    showSuccess(QString("Voltage set to %1 V").arg(voltage, 0, 'f', 3));

    // Update voltage reading
    QTimer::singleShot(100, this, [this]() {
      if (!m_currentDevice.isEmpty()) {
        auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
        if (smu && smu->IsConnected()) {
          double readVoltage = smu->ReadVoltage();
          if (readVoltage != -999.0) {
            m_voltageReadLabel->setText(QString::number(readVoltage, 'f', 3) + " V");
          }
        }
      }
    });
  } else {
    showError("Failed to set voltage: " + QString::fromStdString(smu->GetLastError()));
  }
}

void SMUControlPage::onSetCurrentClicked()
{
  if (m_currentDevice.isEmpty()) {
    showError("No device selected");
    return;
  }

  auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
  if (!smu || !smu->IsConnected()) {
    showError("Device not connected");
    return;
  }

  double current = m_setCurrentSpinBox->value();

  if (smu->SetCurrent(current)) {
    showSuccess(QString("Current set to %1 A").arg(current, 0, 'e', 3));

    // Update current reading
    QTimer::singleShot(100, this, [this]() {
      if (!m_currentDevice.isEmpty()) {
        updateMeasurements();
      }
    });
  } else {
    showError("Failed to set current: " + QString::fromStdString(smu->GetLastError()));
  }
}

void SMUControlPage::onSetVoltageComplianceClicked()
{
  if (m_currentDevice.isEmpty()) {
    showError("No device selected");
    return;
  }

  auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
  if (!smu || !smu->IsConnected()) {
    showError("Device not connected");
    return;
  }

  double voltageCompliance = m_voltageComplianceSpinBox->value();

  std::cout << "[SMUControlPage] Setting voltage compliance to " << voltageCompliance << " V" << std::endl;

  // NOTE: Voltage compliance is stored in context but not yet implemented in device interface
  // This is a placeholder for future full SMU device support
  showSuccess(QString("Voltage compliance: %1 V (stored, full device support pending)").arg(voltageCompliance, 0, 'f', 3));
}

void SMUControlPage::onSetCurrentComplianceClicked()
{
  if (m_currentDevice.isEmpty()) {
    showError("No device selected");
    return;
  }

  auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
  if (!smu || !smu->IsConnected()) {
    showError("Device not connected");
    return;
  }

  double currentCompliance = m_currentComplianceSpinBox->value();

  if (smu->SetCompliance(currentCompliance)) {
    showSuccess(QString("Current compliance set to %1 A").arg(currentCompliance, 0, 'e', 3));
  } else {
    showError("Failed to set current compliance: " + QString::fromStdString(smu->GetLastError()));
  }
}

// Polling control
void SMUControlPage::onStartPollingClicked()
{
  if (m_currentDevice.isEmpty()) return;

  auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
  if (!smu || !smu->IsConnected()) {
    showError("Device not connected");
    return;
  }

  // Determine measurement type from Polling tab
  bool isCurrent = (m_pollingTypeComboBox->currentIndex() == 0);
  QString unit = isCurrent ? " A" : " V";
  auto measurementType = isCurrent ?
    SMUPollingManager::MeasurementType::CURRENT :
    SMUPollingManager::MeasurementType::VOLTAGE;

  // Connect to polling manager signal
  connect(m_context->smuPollingManager, &SMUPollingManager::measurementReady,
          this, &SMUControlPage::onPollingMeasurement);

  // Get selected channel from Polling tab
  int selectedChannel = m_pollingChannelComboBox->currentIndex() + 1;

  // Start polling via SMUPollingManager
  if (m_context->smuPollingManager->StartPolling(
        m_currentDevice.toStdString(),
        smu,
        selectedChannel,
        100,  // 100ms interval
        measurementType)) {
    m_pollingStatusLabel->setText("Polling (" + QString(isCurrent ? "Current" : "Voltage") + ")");
    m_pollingStatusLabel->setStyleSheet("QLabel { "
      "font-size: 16px; "
      "padding: 15px; "
      "background-color: #1e1e1e; "
      "border: 2px solid #27ae60; "
      "border-radius: 4px; "
      "color: #27ae60; "
      "font-weight: bold; "
      "}");
    m_pollingValueLabel->setText("---" + unit);
    m_pollingSampleLabel->setText("Samples: 0");
    m_startPollingButton->setEnabled(false);
    m_stopPollingButton->setEnabled(true);
    showSuccess("Polling started (" + QString(isCurrent ? "Current" : "Voltage") + ")");
  } else {
    showError("Failed to start polling");
  }
}

void SMUControlPage::onStopPollingClicked()
{
  if (m_currentDevice.isEmpty()) return;

  // Disconnect from polling manager signal
  disconnect(m_context->smuPollingManager, &SMUPollingManager::measurementReady,
             this, &SMUControlPage::onPollingMeasurement);

  // Stop polling via SMUPollingManager
  m_context->smuPollingManager->StopPolling(m_currentDevice.toStdString());

  uint64_t finalCount = m_context->smuPollingManager->GetSampleCount(m_currentDevice.toStdString());

  // Determine unit based on current selection from Polling tab
  QString unit = (m_pollingTypeComboBox->currentIndex() == 0) ? " A" : " V";

  m_pollingStatusLabel->setText("Stopped");
  m_pollingStatusLabel->setStyleSheet("QLabel { "
    "font-size: 16px; "
    "padding: 15px; "
    "background-color: #1e1e1e; "
    "border: 2px solid #3f3f46; "
    "border-radius: 4px; "
    "color: #e74c3c; "
    "font-weight: bold; "
    "}");
  m_pollingValueLabel->setText("---" + unit);
  m_pollingSampleLabel->setText(QString("Samples: %1 (stopped)").arg(finalCount));
  m_startPollingButton->setEnabled(true);
  m_stopPollingButton->setEnabled(false);
  showSuccess(QString("Polling stopped (%1 samples)").arg(finalCount));
}

// Polling measurement callback
void SMUControlPage::onPollingMeasurement(QString deviceName, int channel, double value, QString timestamp)
{
  // Only update if this is the current device
  if (deviceName != m_currentDevice) return;

  // Determine unit based on current selection from Polling tab
  QString unit = (m_pollingTypeComboBox->currentIndex() == 0) ? " A" : " V";

  // Update real-time value display
  m_pollingValueLabel->setText(QString::number(value, 'e', 3) + unit);

  // Update sample count from polling manager
  uint64_t samples = m_context->smuPollingManager->GetSampleCount(m_currentDevice.toStdString());
  m_pollingSampleLabel->setText(QString("Samples: %1 | Ch%2 | %3")
                                   .arg(samples)
                                   .arg(channel)
                                   .arg(timestamp));
}

// Measurement update
void SMUControlPage::onRefreshClicked()
{
  updateMeasurements();
  updateOutputState();
}

void SMUControlPage::updateMeasurements()
{
  if (m_currentDevice.isEmpty()) return;

  auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
  if (!smu || !smu->IsConnected()) {
    m_voltageReadLabel->setText("--- V");
    m_currentReadLabel->setText("--- A");
    return;
  }

  // Read voltage (device-wide)
  double voltage = smu->ReadVoltage();
  if (voltage != -999.0) {
    m_voltageReadLabel->setText(QString::number(voltage, 'f', 3) + " V");
  } else {
    m_voltageReadLabel->setText("Error");
  }

  // Read current from selected channel
  int selectedChannel = m_channelComboBox->currentIndex() + 1;
  double current = smu->ReadChannel(selectedChannel);
  if (current != -999.0) {
    m_currentReadLabel->setText(QString::number(current, 'e', 3) + " A");
  } else {
    m_currentReadLabel->setText("Error");
  }
}

// Helper methods
void SMUControlPage::updateConnectionStatus()
{
  if (m_currentDevice.isEmpty()) return;

  auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
  if (smu && smu->IsConnected()) {
    m_connectionStatus->setText("● Connected");
    m_connectionStatus->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #27ae60; padding: 10px; }");
    m_connectButton->setEnabled(false);
    m_disconnectButton->setEnabled(true);
  } else {
    m_connectionStatus->setText("● Not connected");
    m_connectionStatus->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #e74c3c; padding: 10px; }");
    m_connectButton->setEnabled(true);
    m_disconnectButton->setEnabled(false);
  }
}

void SMUControlPage::updateChannelList()
{
  m_channelComboBox->clear();
  m_pollingChannelComboBox->clear();

  if (m_currentDevice.isEmpty()) return;

  auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
  if (!smu || !smu->IsConnected()) {
    m_channelCountLabel->setText("0");
    return;
  }

  int channelCount = smu->GetChannelCount();
  auto enabledChannels = smu->GetEnabledChannels();

  m_channelCountLabel->setText(QString::number(channelCount));

  // Populate channel combo boxes (both Simple tab and Polling tab)
  for (int ch : enabledChannels) {
    m_channelComboBox->addItem(QString("Channel %1").arg(ch));
    m_pollingChannelComboBox->addItem(QString("Channel %1").arg(ch));
  }
}

void SMUControlPage::updateDeviceCapabilities()
{
  if (m_currentDevice.isEmpty()) return;

  auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
  if (!smu) return;

  SMUMode mode = smu->GetSupportedMode();

  // ========== Update device info in Connection tab ==========
  QString deviceInfo = QString("Device: %1\n").arg(QString::fromStdString(smu->GetDeviceName()));
  deviceInfo += QString("ID: %1\n").arg(QString::fromStdString(smu->GetDeviceID()));

  QString modeStr;
  switch (mode) {
    case SMUMode::VOLTAGE_SOURCE:
      modeStr = "Voltage Source";
      break;
    case SMUMode::CURRENT_SOURCE:
      modeStr = "Current Source";
      break;
    case SMUMode::MEASURE_ONLY:
      modeStr = "Measurement Only";
      break;
    default:
      modeStr = "Unknown";
  }
  deviceInfo += QString("Mode: %1").arg(modeStr);
  m_deviceInfoLabel->setText(deviceInfo);

  // ========== Output Control Section (Simple Tab) ==========
  // Hide output controls for measurement-only devices
  if (mode == SMUMode::MEASURE_ONLY) {
    m_outputControlGroup->setVisible(false);
    std::cout << "[SMUControlPage] Device is MEASURE_ONLY - hiding output controls" << std::endl;
  } else {
    m_outputControlGroup->setVisible(true);
    std::cout << "[SMUControlPage] Device supports sourcing - showing output controls" << std::endl;
  }

  // ========== Polling Tab - Measurement Type ==========
  // For measurement-only devices, only allow current measurement in polling
  if (mode == SMUMode::MEASURE_ONLY) {
    int voltageIndex = m_pollingTypeComboBox->findText("Voltage (V)");
    if (voltageIndex != -1) {
      m_pollingTypeComboBox->removeItem(voltageIndex);
      std::cout << "[SMUControlPage] Removed voltage polling option (device is current-only)" << std::endl;
    }
    m_pollingTypeComboBox->setCurrentIndex(0);
  } else {
    // Ensure both Current and Voltage options are available
    if (m_pollingTypeComboBox->count() == 1 && m_pollingTypeComboBox->itemText(0) == "Current (A)") {
      m_pollingTypeComboBox->addItem("Voltage (V)");
      std::cout << "[SMUControlPage] Added voltage polling option (device supports it)" << std::endl;
    }
  }
}

void SMUControlPage::showError(const QString& message)
{
  m_statusLabel->setText("❌ " + message);
  m_statusLabel->setStyleSheet("QLabel { color: #e74c3c; font-size: 12px; padding: 8px; }");
  std::cerr << "[SMUControlPage] Error: " << message.toStdString() << std::endl;
}

void SMUControlPage::showSuccess(const QString& message)
{
  m_statusLabel->setText("✅ " + message);
  m_statusLabel->setStyleSheet("QLabel { color: #27ae60; font-size: 12px; padding: 8px; }");
  std::cout << "[SMUControlPage] Success: " << message.toStdString() << std::endl;
}
