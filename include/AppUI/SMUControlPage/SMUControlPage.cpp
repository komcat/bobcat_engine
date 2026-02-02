// SMUControlPage.cpp - Tabbed SMU control interface
#include "SMUControlPage.h"
#include "ApplicationContext.h"
#include "SMUManager.h"
#include "ISMUDevice.h"
#include "SMUTypes.h"  // For SMUMode enum
#include "Keithley6482Adapter.h"
#include "SMUPollingManager.h"
#include "SMUConnectionWorker.h"
#include "SMUParameterWorker.h"
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
  , m_connectionWorker(nullptr)
  , m_connectionPending(false)
  , m_parameterWorker(nullptr)
{
  setupUI();
  loadDevices();

  // Initialize connection worker
  m_connectionWorker = new SMUConnectionWorker(m_smuManager, nullptr);

  // Connect connection worker signals (use Qt::QueuedConnection for cross-thread safety)
  connect(m_connectionWorker, &SMUConnectionWorker::connectionSuccess,
          this, &SMUControlPage::onConnectionSuccess, Qt::QueuedConnection);
  connect(m_connectionWorker, &SMUConnectionWorker::connectionFailed,
          this, &SMUControlPage::onConnectionFailed, Qt::QueuedConnection);
  connect(m_connectionWorker, &SMUConnectionWorker::disconnectionComplete,
          this, &SMUControlPage::onDisconnectionComplete, Qt::QueuedConnection);
  connect(m_connectionWorker, &SMUConnectionWorker::operationStarted,
          this, &SMUControlPage::onConnectionOperationStarted, Qt::QueuedConnection);

  // Start the connection worker thread
  m_connectionWorker->start();

  // Initialize parameter worker
  m_parameterWorker = new SMUParameterWorker(m_smuManager, nullptr);

  // Connect parameter worker signals
  connect(m_parameterWorker, &SMUParameterWorker::measurementsUpdated,
          this, &SMUControlPage::onMeasurementsUpdated, Qt::QueuedConnection);
  connect(m_parameterWorker, &SMUParameterWorker::outputStateChanged,
          this, &SMUControlPage::onOutputStateChangedByWorker, Qt::QueuedConnection);
  connect(m_parameterWorker, &SMUParameterWorker::commandError,
          this, &SMUControlPage::onParameterCommandError, Qt::QueuedConnection);
  connect(m_parameterWorker, &SMUParameterWorker::commandSuccess,
          this, &SMUControlPage::onParameterCommandSuccess, Qt::QueuedConnection);

  // Start the parameter worker thread
  m_parameterWorker->start();
}

SMUControlPage::~SMUControlPage()
{
  // Stop the connection worker thread
  if (m_connectionWorker) {
    m_connectionWorker->stop();
    delete m_connectionWorker;
    m_connectionWorker = nullptr;
  }

  // Stop the parameter worker thread
  if (m_parameterWorker) {
    m_parameterWorker->stop();
    delete m_parameterWorker;
    m_parameterWorker = nullptr;
  }
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
  QHBoxLayout* tabLayout = new QHBoxLayout(tab);
  tabLayout->setContentsMargins(20, 20, 20, 20);

  // Fixed-width content container
  QWidget* contentWidget = new QWidget();
  contentWidget->setMaximumWidth(450);
  QVBoxLayout* layout = new QVBoxLayout(contentWidget);
  layout->setContentsMargins(0, 0, 0, 0);
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

  // Align content to left
  tabLayout->addWidget(contentWidget, 0, Qt::AlignLeft | Qt::AlignTop);
  tabLayout->addStretch();

  m_tabWidget->addTab(tab, "Connection");
}

// ========== TAB 2: SIMPLE ==========
void SMUControlPage::createSimpleTab()
{
  QWidget* tab = new QWidget();
  QHBoxLayout* outerLayout = new QHBoxLayout(tab);
  outerLayout->setContentsMargins(15, 15, 15, 15);

  // Fixed-width content container (aligned left)
  QWidget* contentWidget = new QWidget();
  contentWidget->setMaximumWidth(550);
  QVBoxLayout* mainLayout = new QVBoxLayout(contentWidget);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(10);

  // ===== DEVICE INFO (Top) =====
  QHBoxLayout* deviceInfoLayout = new QHBoxLayout();

  m_simpleDeviceLabel = new QLabel("Device: ---");
  m_simpleDeviceLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #d4d4d4; }");
  deviceInfoLayout->addWidget(m_simpleDeviceLabel);

  m_simpleConnectionLabel = new QLabel("Disconnected");
  m_simpleConnectionLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #e74c3c; }");
  deviceInfoLayout->addWidget(m_simpleConnectionLabel);
  deviceInfoLayout->addStretch();

  mainLayout->addLayout(deviceInfoLayout);

  // ===== CHANNEL TABS (controls inside each tab) =====
  m_channelTabWidget = new QTabWidget();
  m_channelTabWidget->setStyleSheet(R"(
    QTabWidget::pane {
      border: 1px solid #3f3f46;
      background-color: #2d2d30;
      padding: 10px;
    }
    QTabBar::tab {
      background-color: #3c3c3c;
      color: #d4d4d4;
      padding: 8px 20px;
      margin-right: 2px;
      border-top-left-radius: 4px;
      border-top-right-radius: 4px;
      font-size: 13px;
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
  connect(m_channelTabWidget, &QTabWidget::currentChanged, this, &SMUControlPage::onChannelTabChanged);
  mainLayout->addWidget(m_channelTabWidget);  // No stretch - fixed width container

  // Create a template widget with controls (will be shown in each channel tab)
  // We'll create the controls here and they'll be shared across channel tabs
  m_channelControlsWidget = new QWidget();
  QVBoxLayout* controlsLayout = new QVBoxLayout(m_channelControlsWidget);
  controlsLayout->setContentsMargins(5, 5, 5, 5);
  controlsLayout->setSpacing(10);

  // ===== OUTPUT ROW =====
  m_outputControlGroup = new QGroupBox();
  m_outputControlGroup->setStyleSheet("QGroupBox { border: none; margin: 0; padding: 0; background: transparent; }");
  QHBoxLayout* outputLayout = new QHBoxLayout(m_outputControlGroup);
  outputLayout->setContentsMargins(0, 5, 0, 5);
  outputLayout->setSpacing(10);

  QLabel* outputLabel = new QLabel("Output");
  outputLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #d4d4d4; min-width: 60px; }");
  outputLayout->addWidget(outputLabel);

  m_outputOnButton = new QPushButton("On");
  m_outputOnButton->setMinimumSize(60, 30);
  m_outputOnButton->setStyleSheet("QPushButton { background-color: #27ae60; font-weight: bold; }");
  connect(m_outputOnButton, &QPushButton::clicked, this, &SMUControlPage::onOutputOnClicked);
  outputLayout->addWidget(m_outputOnButton);

  m_outputOffButton = new QPushButton("Off");
  m_outputOffButton->setMinimumSize(60, 30);
  m_outputOffButton->setStyleSheet("QPushButton { background-color: #e74c3c; font-weight: bold; }");
  connect(m_outputOffButton, &QPushButton::clicked, this, &SMUControlPage::onOutputOffClicked);
  outputLayout->addWidget(m_outputOffButton);

  m_outputStateLabel = new QLabel("OFF");
  m_outputStateLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #e74c3c; }");
  outputLayout->addWidget(m_outputStateLabel);

  outputLayout->addStretch();
  controlsLayout->addWidget(m_outputControlGroup);

  // ===== READ ROW =====
  QGroupBox* readGroup = new QGroupBox();
  readGroup->setStyleSheet("QGroupBox { border: none; margin: 0; padding: 0; background: transparent; }");
  QGridLayout* readGrid = new QGridLayout(readGroup);
  readGrid->setContentsMargins(0, 5, 0, 5);
  readGrid->setHorizontalSpacing(15);
  readGrid->setVerticalSpacing(5);

  QLabel* readLabel = new QLabel("Read");
  readLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #d4d4d4; }");
  readGrid->addWidget(readLabel, 0, 0);

  m_voltageReadLabel = new QLabel("Voltage = 0.000 V");
  m_voltageReadLabel->setStyleSheet("QLabel { font-size: 13px; color: #3498db; }");
  readGrid->addWidget(m_voltageReadLabel, 0, 1);

  m_currentReadLabel = new QLabel("Current = 0.000 A");
  m_currentReadLabel->setStyleSheet("QLabel { font-size: 13px; color: #27ae60; }");
  readGrid->addWidget(m_currentReadLabel, 1, 1);

  m_refreshButton = new QPushButton("Refresh");
  m_refreshButton->setMinimumSize(70, 30);
  connect(m_refreshButton, &QPushButton::clicked, this, &SMUControlPage::onRefreshClicked);
  readGrid->addWidget(m_refreshButton, 0, 2, 2, 1, Qt::AlignVCenter);

  controlsLayout->addWidget(readGroup);

  // ===== SET VOLTAGE ROW =====
  QGroupBox* setVoltageGroup = new QGroupBox();
  setVoltageGroup->setStyleSheet("QGroupBox { border: none; margin: 0; padding: 0; background: transparent; }");
  QHBoxLayout* setVoltageLayout = new QHBoxLayout(setVoltageGroup);
  setVoltageLayout->setContentsMargins(0, 5, 0, 5);
  setVoltageLayout->setSpacing(10);

  QLabel* setVoltageLabel = new QLabel("Set");
  setVoltageLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #d4d4d4; min-width: 60px; }");
  setVoltageLayout->addWidget(setVoltageLabel);

  QLabel* voltageTitleLabel = new QLabel("Voltage");
  voltageTitleLabel->setStyleSheet("QLabel { font-size: 13px; color: #d4d4d4; }");
  setVoltageLayout->addWidget(voltageTitleLabel);

  m_setVoltageSpinBox = new QDoubleSpinBox();
  m_setVoltageSpinBox->setRange(-100.0, 100.0);
  m_setVoltageSpinBox->setDecimals(3);
  m_setVoltageSpinBox->setSingleStep(0.1);
  m_setVoltageSpinBox->setValue(0.0);
  m_setVoltageSpinBox->setSuffix(" V");
  m_setVoltageSpinBox->setMinimumWidth(100);
  setVoltageLayout->addWidget(m_setVoltageSpinBox);

  m_setVoltageButton = new QPushButton("(Apply)");
  m_setVoltageButton->setMinimumSize(60, 30);
  connect(m_setVoltageButton, &QPushButton::clicked, this, &SMUControlPage::onSetVoltageClicked);
  setVoltageLayout->addWidget(m_setVoltageButton);

  setVoltageLayout->addStretch();
  controlsLayout->addWidget(setVoltageGroup);

  // ===== SET CURRENT ROW =====
  QGroupBox* setCurrentGroup = new QGroupBox();
  setCurrentGroup->setStyleSheet("QGroupBox { border: none; margin: 0; padding: 0; background: transparent; }");
  QHBoxLayout* setCurrentLayout = new QHBoxLayout(setCurrentGroup);
  setCurrentLayout->setContentsMargins(0, 5, 0, 5);
  setCurrentLayout->setSpacing(10);

  QLabel* setCurrentLabel = new QLabel("Set");
  setCurrentLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #d4d4d4; min-width: 60px; }");
  setCurrentLayout->addWidget(setCurrentLabel);

  QLabel* currentTitleLabel = new QLabel("Current");
  currentTitleLabel->setStyleSheet("QLabel { font-size: 13px; color: #d4d4d4; }");
  setCurrentLayout->addWidget(currentTitleLabel);

  m_setCurrentSpinBox = new QDoubleSpinBox();
  m_setCurrentSpinBox->setRange(-1.0, 1.0);
  m_setCurrentSpinBox->setDecimals(6);
  m_setCurrentSpinBox->setSingleStep(0.001);
  m_setCurrentSpinBox->setValue(0.0);
  m_setCurrentSpinBox->setSuffix(" A");
  m_setCurrentSpinBox->setMinimumWidth(100);
  setCurrentLayout->addWidget(m_setCurrentSpinBox);

  m_setCurrentButton = new QPushButton("(Apply)");
  m_setCurrentButton->setMinimumSize(60, 30);
  connect(m_setCurrentButton, &QPushButton::clicked, this, &SMUControlPage::onSetCurrentClicked);
  setCurrentLayout->addWidget(m_setCurrentButton);

  setCurrentLayout->addStretch();
  controlsLayout->addWidget(setCurrentGroup);

  controlsLayout->addStretch();

  // Initialize hidden compliance spinboxes (keep functionality but hide UI)
  m_voltageComplianceSpinBox = new QDoubleSpinBox(tab);
  m_voltageComplianceSpinBox->setRange(0.0, 100.0);
  m_voltageComplianceSpinBox->setValue(5.0);
  m_voltageComplianceSpinBox->hide();

  m_currentComplianceSpinBox = new QDoubleSpinBox(tab);
  m_currentComplianceSpinBox->setRange(0.0, 1.0);
  m_currentComplianceSpinBox->setValue(0.01);
  m_currentComplianceSpinBox->hide();

  m_setVoltageComplianceButton = new QPushButton(tab);
  connect(m_setVoltageComplianceButton, &QPushButton::clicked, this, &SMUControlPage::onSetVoltageComplianceClicked);
  m_setVoltageComplianceButton->hide();

  m_setCurrentComplianceButton = new QPushButton(contentWidget);
  connect(m_setCurrentComplianceButton, &QPushButton::clicked, this, &SMUControlPage::onSetCurrentComplianceClicked);
  m_setCurrentComplianceButton->hide();

  // Hidden channel combo and label (for compatibility)
  m_channelComboBox = new QComboBox(contentWidget);
  connect(m_channelComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &SMUControlPage::onChannelSelected);
  m_channelComboBox->hide();
  m_channelCountLabel = new QLabel(contentWidget);
  m_channelCountLabel->hide();

  // Align content to left (don't stretch to fill width)
  outerLayout->addWidget(contentWidget, 0, Qt::AlignLeft | Qt::AlignTop);
  outerLayout->addStretch();

  m_tabWidget->addTab(tab, "Simple");
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

  // Container for horizontal layout (align left)
  QWidget* outerTab = new QWidget();
  QHBoxLayout* outerLayout = new QHBoxLayout(outerTab);
  outerLayout->setContentsMargins(20, 20, 20, 20);

  // Fixed-width content container
  QWidget* tab = new QWidget();
  tab->setMaximumWidth(450);
  QVBoxLayout* layout = new QVBoxLayout(tab);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(15);

  // Title
  QLabel* title = new QLabel("Continuous Polling");
  title->setStyleSheet("QLabel { font-size: 18px; font-weight: bold; color: #ffffff; }");
  layout->addWidget(title);

  // ✅ NEW: Selected Device Indicator
  m_pollingDeviceLabel = new QLabel("No device selected");
  m_pollingDeviceLabel->setStyleSheet("QLabel { "
    "font-size: 14px; font-weight: bold; color: #0e639c; "
    "background-color: #1e1e1e; border: 2px solid #0e639c; "
    "border-radius: 4px; padding: 10px; }");
  m_pollingDeviceLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(m_pollingDeviceLabel);

  // ========== ADD NEW POLLING SESSION ==========
  QGroupBox* configGroup = new QGroupBox("Add New Polling Session");
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

  // Measurement range
  configGrid->addWidget(new QLabel("Range:"), 3, 0);
  m_pollingRangeComboBox = new QComboBox();
  m_pollingRangeComboBox->addItem("Auto", QVariant::fromValue(-1.0));  // Auto-range
  m_pollingRangeComboBox->addItem("2 nA",   QVariant::fromValue(2e-9));
  m_pollingRangeComboBox->addItem("20 nA",  QVariant::fromValue(20e-9));
  m_pollingRangeComboBox->addItem("200 nA", QVariant::fromValue(200e-9));
  m_pollingRangeComboBox->addItem("2 µA",   QVariant::fromValue(2e-6));
  m_pollingRangeComboBox->addItem("20 µA",  QVariant::fromValue(20e-6));
  m_pollingRangeComboBox->addItem("200 µA", QVariant::fromValue(200e-6));
  m_pollingRangeComboBox->addItem("2 mA",   QVariant::fromValue(2e-3));
  m_pollingRangeComboBox->addItem("20 mA",  QVariant::fromValue(20e-3));
  m_pollingRangeComboBox->setCurrentIndex(0);  // Default: Auto
  configGrid->addWidget(m_pollingRangeComboBox, 3, 1);

  // Add polling button
  m_addPollingButton = new QPushButton("Add Polling Session");
  m_addPollingButton->setMinimumHeight(45);
  m_addPollingButton->setStyleSheet("QPushButton { background-color: #27ae60; font-weight: bold; font-size: 14px; }");
  connect(m_addPollingButton, &QPushButton::clicked, this, &SMUControlPage::onAddPollingClicked);
  configGrid->addWidget(m_addPollingButton, 4, 0, 1, 2);

  layout->addWidget(configGroup);

  // ========== ACTIVE POLLING SESSIONS ==========
  QGroupBox* sessionsGroup = new QGroupBox("Active Polling Sessions");
  QVBoxLayout* sessionsLayout = new QVBoxLayout(sessionsGroup);
  sessionsLayout->setSpacing(10);

  m_activeSessionsList = new QListWidget();
  m_activeSessionsList->setMinimumHeight(150);
  m_activeSessionsList->setStyleSheet("QListWidget::item { padding: 5px; }");
  sessionsLayout->addWidget(m_activeSessionsList);

  // Session control buttons
  QHBoxLayout* sessionButtonsLayout = new QHBoxLayout();
  m_stopSelectedSessionButton = new QPushButton("Stop Selected");
  m_stopSelectedSessionButton->setStyleSheet("QPushButton { background-color: #e67e22; font-weight: bold; }");
  connect(m_stopSelectedSessionButton, &QPushButton::clicked, this, &SMUControlPage::onStopSelectedSessionClicked);
  sessionButtonsLayout->addWidget(m_stopSelectedSessionButton);

  m_stopAllSessionsButton = new QPushButton("Stop All");
  m_stopAllSessionsButton->setStyleSheet("QPushButton { background-color: #e74c3c; font-weight: bold; }");
  connect(m_stopAllSessionsButton, &QPushButton::clicked, this, &SMUControlPage::onStopAllSessionsClicked);
  sessionButtonsLayout->addWidget(m_stopAllSessionsButton);

  sessionsLayout->addLayout(sessionButtonsLayout);
  layout->addWidget(sessionsGroup);

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

  // Align content to left
  outerLayout->addWidget(tab, 0, Qt::AlignLeft | Qt::AlignTop);
  outerLayout->addStretch();

  // Set tab as scroll area content
  scrollArea->setWidget(outerTab);
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

  // Update workers with selected device
  if (m_parameterWorker) {
    m_parameterWorker->setCurrentDevice(m_currentDevice.toStdString());
  }

  updateConnectionStatus();
  updateChannelList();
  updateOutputState();
  updateDeviceCapabilities();  // Update UI based on device capabilities
  updateDeviceLabels();  // Update device name labels in Simple and Polling tabs
  refreshActiveSessionsList();  // Refresh polling sessions list to highlight current device
}

void SMUControlPage::onConnectClicked()
{
  if (m_currentDevice.isEmpty()) {
    showError("No device selected");
    return;
  }

  if (m_connectionPending) {
    showError("Connection operation in progress...");
    return;
  }

  // Disable buttons and show connecting status
  m_connectionPending = true;
  m_connectButton->setEnabled(false);
  m_disconnectButton->setEnabled(false);
  m_connectionStatus->setText("● Connecting...");
  m_connectionStatus->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #f39c12; padding: 10px; }");

  // Request connection via worker thread (non-blocking)
  m_connectionWorker->requestConnect(m_currentDevice);
}

void SMUControlPage::onDisconnectClicked()
{
  if (m_currentDevice.isEmpty()) return;

  if (m_connectionPending) {
    showError("Connection operation in progress...");
    return;
  }

  // Disable buttons and show disconnecting status
  m_connectionPending = true;
  m_connectButton->setEnabled(false);
  m_disconnectButton->setEnabled(false);
  m_connectionStatus->setText("● Disconnecting...");
  m_connectionStatus->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #f39c12; padding: 10px; }");

  // Request disconnection via worker thread (non-blocking)
  m_connectionWorker->requestDisconnect(m_currentDevice);
}

// Channel selection (combo box - hidden)
void SMUControlPage::onChannelSelected(int index)
{
  int channel = index + 1;
  std::cout << "[SMUControlPage] Channel selected: " << channel << std::endl;

  // Update parameter worker with selected channel
  if (m_parameterWorker) {
    m_parameterWorker->setPollingChannel(channel);
    // Request measurements for the newly selected channel
    m_parameterWorker->requestRefreshAll(channel);
  }
}

// Channel tab changed (new simplified UI)
void SMUControlPage::onChannelTabChanged(int index)
{
  if (index < 0) return;

  // Get channel number from tab data
  QWidget* tabWidget = m_channelTabWidget->widget(index);
  if (!tabWidget) return;

  int channel = tabWidget->property("channelNumber").toInt();
  if (channel <= 0) channel = index + 1;

  std::cout << "[SMUControlPage] Channel tab changed: " << channel << std::endl;

  // Move the controls widget to the newly selected tab
  if (m_channelControlsWidget) {
    // Get layout of new tab
    QVBoxLayout* tabLayout = qobject_cast<QVBoxLayout*>(tabWidget->layout());
    if (tabLayout) {
      // Reparent controls to new tab
      m_channelControlsWidget->setParent(tabWidget);
      tabLayout->addWidget(m_channelControlsWidget);
    }
  }

  // Update the hidden combo box to stay in sync
  if (m_channelComboBox && index < m_channelComboBox->count()) {
    m_channelComboBox->blockSignals(true);
    m_channelComboBox->setCurrentIndex(index);
    m_channelComboBox->blockSignals(false);
  }

  // Update parameter worker with selected channel
  if (m_parameterWorker) {
    m_parameterWorker->setPollingChannel(channel);
    // Request measurements for the newly selected channel
    m_parameterWorker->requestRefreshAll(channel);
  }
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

  // Use parameter worker for non-blocking operation
  if (m_parameterWorker) {
    m_parameterWorker->requestOutputOn();
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

  // Use parameter worker for non-blocking operation
  if (m_parameterWorker) {
    m_parameterWorker->requestOutputOff();
  }
}

void SMUControlPage::updateOutputState()
{
  if (m_currentDevice.isEmpty()) {
    m_outputStateLabel->setText("---");
    m_outputStateLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #888888; }");
    return;
  }

  auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
  if (!smu || !smu->IsConnected()) {
    m_outputStateLabel->setText("---");
    m_outputStateLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #888888; }");
    return;
  }

  // Use cached state from worker instead of direct SMU call (non-blocking)
  // The cached state is updated by onMeasurementsUpdated() callback
  if (m_cachedOutputOn) {
    m_outputStateLabel->setText("ON");
    m_outputStateLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #27ae60; }");
  } else {
    m_outputStateLabel->setText("OFF");
    m_outputStateLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #e74c3c; }");
  }

  // Request fresh data from worker (off-thread)
  if (m_parameterWorker) {
    int channel = m_channelTabWidget->currentIndex() + 1;
    if (channel <= 0) channel = 1;
    m_parameterWorker->requestRefreshAll(channel);
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

  // Use parameter worker for non-blocking operation
  if (m_parameterWorker) {
    m_parameterWorker->requestSetVoltage(voltage);
    // Request a refresh after setting
    int channel = m_channelTabWidget->currentIndex() + 1;
    if (channel <= 0) channel = m_channelComboBox->currentIndex() + 1;
    m_parameterWorker->requestRefreshAll(channel);
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

  // Use parameter worker for non-blocking operation
  if (m_parameterWorker) {
    m_parameterWorker->requestSetCurrent(current);
    // Request a refresh after setting
    int channel = m_channelTabWidget->currentIndex() + 1;
    if (channel <= 0) channel = m_channelComboBox->currentIndex() + 1;
    m_parameterWorker->requestRefreshAll(channel);
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

  // Use parameter worker for non-blocking operation
  if (m_parameterWorker) {
    m_parameterWorker->requestSetVoltageCompliance(voltageCompliance);
  }
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

  // Use parameter worker for non-blocking operation
  if (m_parameterWorker) {
    m_parameterWorker->requestSetCurrentCompliance(currentCompliance);
  }
}

// Polling control - NEW multi-session support
void SMUControlPage::onAddPollingClicked()
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

  // Get selected channel
  int selectedChannel = m_pollingChannelComboBox->currentIndex() + 1;

  // Check if this channel is already polling
  if (m_context->smuPollingManager->IsPollingChannel(m_currentDevice.toStdString(), selectedChannel)) {
    showError(QString("Channel %1 is already polling").arg(selectedChannel));
    return;
  }

  // Determine measurement type
  bool isCurrent = (m_pollingTypeComboBox->currentIndex() == 0);
  auto measurementType = isCurrent ?
    SMUPollingManager::MeasurementType::CURRENT :
    SMUPollingManager::MeasurementType::VOLTAGE;

  // Set measurement range before starting polling
  double rangeValue = m_pollingRangeComboBox->currentData().toDouble();
  bool autoRange = (rangeValue < 0);  // -1 indicates auto-range

  auto keithley = std::dynamic_pointer_cast<Keithley6482Adapter>(smu);
  if (keithley) {
    if (keithley->SetChannelMeasurementRange(selectedChannel, rangeValue, autoRange)) {
      QString rangeText = autoRange ? "Auto" : m_pollingRangeComboBox->currentText();
      std::cout << "[SMUControlPage] Set range to " << rangeText.toStdString()
                << " for channel " << selectedChannel << std::endl;
    } else {
      showError("Failed to set measurement range");
      return;
    }
  }

  // Connect to polling manager signal (only once)
  static bool signalConnected = false;
  if (!signalConnected) {
    connect(m_context->smuPollingManager, &SMUPollingManager::measurementReady,
            this, &SMUControlPage::onPollingMeasurement);
    signalConnected = true;
  }

  // Start polling via SMUPollingManager
  if (m_context->smuPollingManager->StartPolling(
        m_currentDevice.toStdString(),
        smu,
        selectedChannel,
        100,  // 100ms interval
        measurementType)) {
    showSuccess(QString("Started polling Ch%1 (%2)")
      .arg(selectedChannel)
      .arg(isCurrent ? "Current" : "Voltage"));
    refreshActiveSessionsList();
  } else {
    showError("Failed to start polling");
  }
}

void SMUControlPage::onStopSelectedSessionClicked()
{
  QListWidgetItem* selectedItem = m_activeSessionsList->currentItem();
  if (!selectedItem) {
    showError("No session selected");
    return;
  }

  // Session key format: "DeviceName_Ch1"
  QString sessionKey = selectedItem->data(Qt::UserRole).toString();
  QStringList parts = sessionKey.split("_Ch");
  if (parts.size() != 2) return;

  QString deviceName = parts[0];
  int channel = parts[1].toInt();

  m_context->smuPollingManager->StopPollingChannel(deviceName.toStdString(), channel);

  showSuccess(QString("Stopped polling %1 Ch%2").arg(deviceName).arg(channel));
  refreshActiveSessionsList();
}

void SMUControlPage::onStopAllSessionsClicked()
{
  m_context->smuPollingManager->StopAllPolling();
  showSuccess("All polling sessions stopped");
  refreshActiveSessionsList();
}

void SMUControlPage::refreshActiveSessionsList()
{
  m_activeSessionsList->clear();

  auto sessions = m_context->smuPollingManager->GetAllActiveSessions();

  for (const auto& [sessionKey, sessionInfo] : sessions) {
    auto [deviceName, channel, measurementType] = sessionInfo;

    QString typeStr = (measurementType == SMUPollingManager::MeasurementType::CURRENT) ? "Current" : "Voltage";
    uint64_t samples = m_context->smuPollingManager->GetSampleCount(deviceName, channel);

    QString displayText = QString("%1 Ch%2 [%3] - %4 samples")
      .arg(QString::fromStdString(deviceName))
      .arg(channel)
      .arg(typeStr)
      .arg(samples);

    QListWidgetItem* item = new QListWidgetItem(displayText);
    item->setData(Qt::UserRole, QString::fromStdString(sessionKey));

    // Highlight current device's sessions
    if (deviceName == m_currentDevice.toStdString()) {
      item->setBackground(QBrush(QColor(14, 99, 156, 50)));  // Semi-transparent blue
    }

    m_activeSessionsList->addItem(item);
  }

  m_pollingSampleLabel->setText(QString("Total active sessions: %1").arg(sessions.size()));
}

// Polling measurement callback
void SMUControlPage::onPollingMeasurement(QString deviceName, int channel, double value, QString timestamp)
{
  // Update live display for current device
  if (deviceName == m_currentDevice) {
    // Determine unit - check if current or voltage
    bool isCurrent = (value < 1.0);  // Simple heuristic, or we could track measurement type
    QString unit = isCurrent ? " A" : " V";

    // Update real-time value display
    m_pollingValueLabel->setText(QString("%1: %2%3")
      .arg(QString("Ch%1").arg(channel))
      .arg(QString::number(value, 'e', 3))
      .arg(unit));
  }

  // Periodically refresh the active sessions list to update sample counts
  static int updateCounter = 0;
  if (++updateCounter >= 10) {  // Refresh every 10 measurements (~1 second at 100ms interval)
    refreshActiveSessionsList();
    updateCounter = 0;
  }
}

// Measurement update
void SMUControlPage::onRefreshClicked()
{
  if (m_currentDevice.isEmpty()) {
    showError("No device selected");
    return;
  }

  auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
  if (!smu || !smu->IsConnected()) {
    m_voltageReadLabel->setText("Voltage = --- V");
    m_currentReadLabel->setText("Current = --- A");
    showError("Device not connected");
    return;
  }

  // Use parameter worker for non-blocking operation
  if (m_parameterWorker) {
    int channel = m_channelTabWidget->currentIndex() + 1;
    if (channel <= 0) channel = m_channelComboBox->currentIndex() + 1;
    m_parameterWorker->requestRefreshAll(channel);
  }
}

void SMUControlPage::updateMeasurements()
{
  // All SMU reads are now handled by the parameter worker (off-thread)
  // This function only requests a refresh - actual UI update happens in onMeasurementsUpdated()

  if (m_currentDevice.isEmpty()) {
    m_voltageReadLabel->setText("Voltage = --- V");
    m_currentReadLabel->setText("Current = --- A");
    return;
  }

  auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
  if (!smu || !smu->IsConnected()) {
    m_voltageReadLabel->setText("Voltage = --- V");
    m_currentReadLabel->setText("Current = --- A");
    return;
  }

  // Request refresh via worker (non-blocking, off UI thread)
  if (m_parameterWorker) {
    int channel = m_channelTabWidget->currentIndex() + 1;
    if (channel <= 0) channel = m_channelComboBox->currentIndex() + 1;
    m_parameterWorker->requestRefreshAll(channel);
  } else {
    // Worker not available - show placeholder (don't block UI with direct calls)
    m_voltageReadLabel->setText("Voltage = --- V");
    m_currentReadLabel->setText("Current = --- A");
    std::cerr << "[SMUControlPage] Warning: Parameter worker not available" << std::endl;
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

  // Safely remove controls widget from current tab before clearing (to prevent deletion)
  // QPointer will be null if widget was already deleted
  if (m_channelControlsWidget) {
    // Remove from layout first (this is critical - setParent alone doesn't remove from layout)
    if (QWidget* parent = m_channelControlsWidget->parentWidget()) {
      if (QLayout* layout = parent->layout()) {
        layout->removeWidget(m_channelControlsWidget);
      }
    }
    // Then orphan the widget so it's not deleted with the tab
    m_channelControlsWidget->setParent(nullptr);
    m_channelControlsWidget->hide();  // Hide while orphaned
  }

  // Clear channel tabs
  while (m_channelTabWidget->count() > 0) {
    QWidget* w = m_channelTabWidget->widget(0);
    m_channelTabWidget->removeTab(0);
    delete w;
  }

  if (m_currentDevice.isEmpty()) return;

  auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
  if (!smu || !smu->IsConnected()) {
    m_channelCountLabel->setText("0");
    return;
  }

  int channelCount = smu->GetChannelCount();
  auto enabledChannels = smu->GetEnabledChannels();

  m_channelCountLabel->setText(QString::number(channelCount));

  // Populate channel combo boxes and tabs
  bool firstTab = true;
  for (int ch : enabledChannels) {
    // Hidden combo box (for compatibility)
    m_channelComboBox->addItem(QString("Channel %1").arg(ch));
    m_pollingChannelComboBox->addItem(QString("Channel %1").arg(ch));

    // Channel tab with layout to hold controls
    QWidget* tabContent = new QWidget();
    tabContent->setProperty("channelNumber", ch);
    QVBoxLayout* tabLayout = new QVBoxLayout(tabContent);
    tabLayout->setContentsMargins(0, 0, 0, 0);

    // Add controls to first tab (QPointer ensures null check is safe)
    if (firstTab && m_channelControlsWidget) {
      tabLayout->addWidget(m_channelControlsWidget);
      m_channelControlsWidget->show();  // Show again after being hidden while orphaned
      firstTab = false;
    }

    m_channelTabWidget->addTab(tabContent, QString("Ch%1").arg(ch));
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

// Update device name labels in Simple and Polling tabs
void SMUControlPage::updateDeviceLabels()
{
  if (m_currentDevice.isEmpty()) {
    // Simple tab (new format)
    m_simpleDeviceLabel->setText("Device: ---");
    m_simpleDeviceLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #888888; }");
    m_simpleConnectionLabel->setText("Disconnected");
    m_simpleConnectionLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #e74c3c; }");

    // Polling tab
    m_pollingDeviceLabel->setText("No device selected");
    m_pollingDeviceLabel->setStyleSheet("QLabel { "
      "font-size: 14px; font-weight: bold; color: #e74c3c; "
      "background-color: #1e1e1e; border: 2px solid #e74c3c; "
      "border-radius: 4px; padding: 10px; }");
    return;
  }

  auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
  if (!smu) return;

  // Get connection status
  bool isConnected = smu->IsConnected();

  // Update Simple tab labels (new simplified format)
  m_simpleDeviceLabel->setText(QString("Device: %1").arg(m_currentDevice));
  m_simpleDeviceLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #d4d4d4; }");

  if (isConnected) {
    m_simpleConnectionLabel->setText("Connected");
    m_simpleConnectionLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #27ae60; }");
  } else {
    m_simpleConnectionLabel->setText("Disconnected");
    m_simpleConnectionLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #e74c3c; }");
  }

  // Update Polling tab label (original format)
  QString statusColor = isConnected ? "#27ae60" : "#f39c12";
  QString statusText = isConnected ? "Connected" : "Disconnected";
  QString labelText = QString("Device: %1 [%2]")
    .arg(m_currentDevice)
    .arg(statusText);

  m_pollingDeviceLabel->setText(labelText);
  m_pollingDeviceLabel->setStyleSheet(QString("QLabel { "
    "font-size: 14px; font-weight: bold; color: %1; "
    "background-color: #1e1e1e; border: 2px solid %1; "
    "border-radius: 4px; padding: 10px; }").arg(statusColor));
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

// ========== CONNECTION WORKER CALLBACKS ==========

void SMUControlPage::onConnectionSuccess(QString deviceName, QString deviceId)
{
  std::cout << "[SMUControlPage] Connection success: " << deviceName.toStdString()
            << " (ID: " << deviceId.toStdString() << ")" << std::endl;

  m_connectionPending = false;

  // Only update UI if this is still the selected device
  if (deviceName == m_currentDevice) {
    showSuccess("Connected to " + deviceName);
    updateConnectionStatus();
    updateChannelList();
    updateDeviceLabels();
    updateDeviceCapabilities();

    // Configure parameter worker for this device
    if (m_parameterWorker) {
      m_parameterWorker->setCurrentDevice(deviceName.toStdString());

      // Get channel count and set on worker
      auto smu = m_smuManager->GetDevice(deviceName.toStdString());
      if (smu) {
        m_parameterWorker->setChannelCount(smu->GetChannelCount());
      }

      // Enable status polling if page is visible
      if (isVisible()) {
        m_parameterWorker->setMeasurementPollingEnabled(true);
        std::cout << "[SMUControlPage] Enabled status polling after connection" << std::endl;
      }

      // Request initial measurements
      int channel = m_channelTabWidget->currentIndex() + 1;
      if (channel <= 0) channel = m_channelComboBox->currentIndex() + 1;
      m_parameterWorker->requestRefreshAll(channel);
    }
  }
}

void SMUControlPage::onConnectionFailed(QString deviceName, QString error)
{
  std::cerr << "[SMUControlPage] Connection failed: " << deviceName.toStdString()
            << " - " << error.toStdString() << std::endl;

  m_connectionPending = false;

  // Only update UI if this is still the selected device
  if (deviceName == m_currentDevice) {
    showError("Connection failed: " + error);
    updateConnectionStatus();
  }
}

void SMUControlPage::onDisconnectionComplete(QString deviceName)
{
  std::cout << "[SMUControlPage] Disconnection complete: " << deviceName.toStdString() << std::endl;

  m_connectionPending = false;

  // Only update UI if this is still the selected device
  if (deviceName == m_currentDevice) {
    showSuccess("Disconnected from " + deviceName);

    // Disable polling since device is disconnected
    if (m_parameterWorker) {
      m_parameterWorker->setMeasurementPollingEnabled(false);
      std::cout << "[SMUControlPage] Disabled status polling after disconnection" << std::endl;
    }

    // Reset cached state
    m_cachedOutputOn = false;

    updateConnectionStatus();
    updateOutputState();
    updateDeviceLabels();
  }
}

void SMUControlPage::onConnectionOperationStarted(QString operation, QString deviceName)
{
  std::cout << "[SMUControlPage] " << operation.toStdString()
            << " started for: " << deviceName.toStdString() << std::endl;

  m_statusLabel->setText("⏳ " + operation + " " + deviceName + "...");
  m_statusLabel->setStyleSheet("QLabel { color: #f39c12; font-size: 12px; padding: 8px; }");
}

// ========== PARAMETER WORKER CALLBACKS ==========

void SMUControlPage::onMeasurementsUpdated(int channel, double voltage, double current, bool outputOn)
{
  // Update cached output state (for use by updateOutputState())
  m_cachedOutputOn = outputOn;

  // Update voltage display (new format: "Voltage = X.XXX V")
  if (voltage != -999.0) {
    m_voltageReadLabel->setText(QString("Voltage = %1 V").arg(QString::number(voltage, 'f', 3)));
  } else {
    m_voltageReadLabel->setText("Voltage = --- V");
  }

  // Update current display (new format: "Current = X.XXX A")
  if (current != -999.0) {
    m_currentReadLabel->setText(QString("Current = %1 A").arg(QString::number(current, 'f', 3)));
  } else {
    m_currentReadLabel->setText("Current = --- A");
  }

  // Update output state display (simplified)
  if (outputOn) {
    m_outputStateLabel->setText("ON");
    m_outputStateLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #27ae60; }");
  } else {
    m_outputStateLabel->setText("OFF");
    m_outputStateLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #e74c3c; }");
  }
}

void SMUControlPage::onOutputStateChangedByWorker(bool on)
{
  // Update cached output state
  m_cachedOutputOn = on;

  // Update output state display (simplified)
  if (on) {
    m_outputStateLabel->setText("ON");
    m_outputStateLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #27ae60; }");
  } else {
    m_outputStateLabel->setText("OFF");
    m_outputStateLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #e74c3c; }");
  }

  // Request a full measurement refresh after output state change
  if (m_parameterWorker && !m_currentDevice.isEmpty()) {
    int channel = m_channelTabWidget->currentIndex() + 1;
    if (channel <= 0) channel = m_channelComboBox->currentIndex() + 1;
    m_parameterWorker->requestRefreshAll(channel);
  }
}

void SMUControlPage::onParameterCommandError(QString operation, QString error)
{
  showError(operation + ": " + error);
}

void SMUControlPage::onParameterCommandSuccess(QString operation, QString message)
{
  showSuccess(message);
}

// ============================================================================
// PAGE VISIBILITY - Enable/disable status polling
// ============================================================================

void SMUControlPage::showEvent(QShowEvent* event)
{
  QWidget::showEvent(event);

  // Enable measurement polling when page becomes visible
  if (m_parameterWorker && !m_currentDevice.isEmpty()) {
    auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
    if (smu && smu->IsConnected()) {
      m_parameterWorker->setMeasurementPollingEnabled(true);
      std::cout << "[SMUControlPage] Page shown - enabled status polling" << std::endl;

      // Request initial measurements
      int channel = m_channelTabWidget->currentIndex() + 1;
      if (channel <= 0) channel = 1;
      m_parameterWorker->requestRefreshAll(channel);
    }
  }
}

void SMUControlPage::hideEvent(QHideEvent* event)
{
  QWidget::hideEvent(event);

  // Disable measurement polling when page is hidden to save resources
  if (m_parameterWorker) {
    m_parameterWorker->setMeasurementPollingEnabled(false);
    std::cout << "[SMUControlPage] Page hidden - disabled status polling" << std::endl;
  }
}
