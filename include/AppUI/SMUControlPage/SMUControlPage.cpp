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
#include <cmath>

namespace {
// Format a value with SI prefix (e.g. 0.001 -> "1.000 m", 0.000002 -> "2.000 u")
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

  // Create horizontal splitter for master-detail layout
  QSplitter* splitter = new QSplitter(Qt::Horizontal);
  splitter->setHandleWidth(1);
  splitter->setStyleSheet("QSplitter::handle { background-color: #3f3f46; }");

  // LEFT PANEL: Device list and connection controls
  QWidget* leftPanel = new QWidget();
  leftPanel->setMinimumWidth(200);
  leftPanel->setMaximumWidth(280);
  QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
  leftLayout->setContentsMargins(10, 10, 10, 10);
  leftLayout->setSpacing(10);

  // Device list title
  QLabel* listLabel = new QLabel("SMU Devices");
  listLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #ffffff; }");
  leftLayout->addWidget(listLabel);

  // Device list
  m_deviceList = new QListWidget();
  m_deviceList->setMinimumHeight(120);
  leftLayout->addWidget(m_deviceList);
  connect(m_deviceList, &QListWidget::currentRowChanged, this, &SMUControlPage::onDeviceSelected);

  // Connection status
  m_connectionStatus = new QLabel("● Not connected");
  m_connectionStatus->setStyleSheet("QLabel { font-size: 13px; font-weight: bold; color: #e74c3c; padding: 8px; }");
  m_connectionStatus->setAlignment(Qt::AlignCenter);
  leftLayout->addWidget(m_connectionStatus);

  // Device info
  m_deviceInfoLabel = new QLabel("");
  m_deviceInfoLabel->setStyleSheet("QLabel { font-size: 11px; color: #888888; padding: 8px; background-color: #1e1e1e; border-radius: 4px; }");
  m_deviceInfoLabel->setWordWrap(true);
  m_deviceInfoLabel->setAlignment(Qt::AlignLeft);
  leftLayout->addWidget(m_deviceInfoLabel);

  // Connect/Disconnect buttons
  m_connectButton = new QPushButton("Connect");
  m_connectButton->setMinimumHeight(40);
  m_connectButton->setStyleSheet("QPushButton { background-color: #27ae60; font-size: 13px; font-weight: bold; }");
  m_disconnectButton = new QPushButton("Disconnect");
  m_disconnectButton->setMinimumHeight(40);
  m_disconnectButton->setStyleSheet("QPushButton { background-color: #e74c3c; font-size: 13px; font-weight: bold; }");
  m_disconnectButton->setEnabled(false);

  connect(m_connectButton, &QPushButton::clicked, this, &SMUControlPage::onConnectClicked);
  connect(m_disconnectButton, &QPushButton::clicked, this, &SMUControlPage::onDisconnectClicked);

  leftLayout->addWidget(m_connectButton);
  leftLayout->addWidget(m_disconnectButton);
  leftLayout->addStretch();

  splitter->addWidget(leftPanel);

  // RIGHT PANEL: Tab widget for Simple and Polling
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
      padding: 10px 20px;
      margin-right: 4px;
      border-top-left-radius: 6px;
      border-top-right-radius: 6px;
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

  // Create Simple and Polling tabs (no Connection tab needed)
  createSimpleTab();
  createPollingTab();

  splitter->addWidget(m_tabWidget);
  splitter->setStretchFactor(0, 0);  // Left panel doesn't stretch
  splitter->setStretchFactor(1, 1);  // Right panel stretches

  mainLayout->addWidget(splitter);

  // Status label (hidden — messages go to console only)
  m_statusLabel = new QLabel("");
  m_statusLabel->hide();
}

// ========== TAB 1: SIMPLE (Controls) ==========
void SMUControlPage::createSimpleTab()
{
  QWidget* tab = new QWidget();
  QVBoxLayout* tabLayout = new QVBoxLayout(tab);
  tabLayout->setContentsMargins(10, 10, 10, 10);
  tabLayout->setSpacing(6);

  // Device info
  QHBoxLayout* deviceInfoLayout = new QHBoxLayout();
  m_simpleDeviceLabel = new QLabel("Device: ---");
  m_simpleDeviceLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #d4d4d4; }");
  deviceInfoLayout->addWidget(m_simpleDeviceLabel);
  m_simpleConnectionLabel = new QLabel("Disconnected");
  m_simpleConnectionLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #e74c3c; }");
  deviceInfoLayout->addWidget(m_simpleConnectionLabel);
  deviceInfoLayout->addStretch();
  tabLayout->addLayout(deviceInfoLayout);

  // Channel tabs — content goes INSIDE each tab (built by rebuildSimpleTabs)
  m_channelTabWidget = new QTabWidget();
  m_channelTabWidget->setStyleSheet(R"(
    QTabWidget::pane { border: 1px solid #3f3f46; background-color: #2d2d30; }
    QTabBar::tab {
      background-color: #3c3c3c; color: #d4d4d4;
      padding: 6px 18px; margin-right: 2px;
      border-top-left-radius: 4px; border-top-right-radius: 4px;
      font-size: 13px; font-weight: bold;
    }
    QTabBar::tab:selected { background-color: #0e639c; color: white; }
    QTabBar::tab:hover { background-color: #4a4a4a; }
  )");
  connect(m_channelTabWidget, &QTabWidget::currentChanged, this, &SMUControlPage::onChannelTabChanged);
  tabLayout->addWidget(m_channelTabWidget);

  // Hidden compliance spinboxes (shared, not per-channel)
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
  m_setCurrentComplianceButton = new QPushButton(tab);
  connect(m_setCurrentComplianceButton, &QPushButton::clicked, this, &SMUControlPage::onSetCurrentComplianceClicked);
  m_setCurrentComplianceButton->hide();

  // Hidden channel combo (for compatibility)
  m_channelComboBox = new QComboBox(tab);
  connect(m_channelComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &SMUControlPage::onChannelSelected);
  m_channelComboBox->hide();
  m_channelCountLabel = new QLabel(tab);
  m_channelCountLabel->hide();

  m_tabWidget->addTab(tab, "Simple");
}

QWidget* SMUControlPage::createSMUChannelTab(int channel)
{
  QWidget* tabWidget = new QWidget();
  QVBoxLayout* layout = new QVBoxLayout(tabWidget);
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(6);

  SMUChannelWidgets ctrl;
  ctrl.channel = channel;

  // Shared styles
  const QString spinBoxStyle =
    "QDoubleSpinBox { background-color: #3c3c3c; color: #ffffff; border: 1px solid #555555; "
    "border-radius: 4px; padding: 4px; font-size: 16px; font-weight: bold; }";
  const QString setButtonStyle =
    "QPushButton { background-color: #0e639c; color: white; font-weight: bold; font-size: 13px; "
    "border: none; border-radius: 4px; padding: 6px 14px; }"
    "QPushButton:hover { background-color: #1177bb; }"
    "QPushButton:pressed { background-color: #0c5280; }";
  const QString presetStyle =
    "QPushButton { background-color: #2a4a6a; color: #aaccff; font-size: 12px; font-weight: bold; "
    "border: 1px solid #3a6a9a; border-radius: 3px; padding: 4px 0px; }"
    "QPushButton:hover { background-color: #3a6a9a; color: white; }"
    "QPushButton:pressed { background-color: #1a3a5a; }";

  // ---- Top row: Blue measurement panel (left) + Controls (right) ----
  QHBoxLayout* topRow = new QHBoxLayout();
  topRow->setSpacing(6);

  // Left: Blue measurement panel (fixed width to prevent layout jumps)
  QFrame* measFrame = new QFrame();
  measFrame->setFixedWidth(240);
  measFrame->setStyleSheet(
    "QFrame { background-color: #0a1e3d; border: 2px solid #1565c0; border-radius: 8px; }");
  QVBoxLayout* measLayout = new QVBoxLayout(measFrame);
  measLayout->setContentsMargins(14, 10, 14, 10);
  measLayout->setSpacing(2);

  ctrl.outputStateLabel = new QLabel("Output: ---");
  ctrl.outputStateLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #888888; background: transparent;");
  measLayout->addWidget(ctrl.outputStateLabel);

  ctrl.voltageLabel = new QLabel("V: --- V");
  ctrl.voltageLabel->setStyleSheet("font-size: 26px; font-weight: bold; color: #ffffff; background: transparent;");
  measLayout->addWidget(ctrl.voltageLabel);

  ctrl.currentLabel = new QLabel("A: --- A");
  ctrl.currentLabel->setStyleSheet("font-size: 26px; font-weight: bold; color: #ffffff; background: transparent;");
  measLayout->addWidget(ctrl.currentLabel);

  ctrl.refreshButton = new QPushButton("Refresh");
  ctrl.refreshButton->setMaximumWidth(80);
  ctrl.refreshButton->setStyleSheet(
    "QPushButton { background-color: #1a3a6a; color: #aaccff; font-size: 11px; "
    "border: 1px solid #1565c0; border-radius: 3px; padding: 3px 8px; }"
    "QPushButton:hover { background-color: #254b82; color: white; }");
  connect(ctrl.refreshButton, &QPushButton::clicked, this, [this, channel]() {
    onRefreshClicked(channel);
  });
  measLayout->addWidget(ctrl.refreshButton);

  topRow->addWidget(measFrame, 0);

  // Right: V/A inputs + Output buttons (fixed width)
  QWidget* controlPanel = new QWidget();
  controlPanel->setFixedWidth(380);
  QVBoxLayout* ctrlLayout = new QVBoxLayout(controlPanel);
  ctrlLayout->setContentsMargins(0, 0, 0, 0);
  ctrlLayout->setSpacing(4);

  // Voltage row
  QHBoxLayout* voltageRow = new QHBoxLayout();
  voltageRow->setSpacing(4);
  QLabel* vLabel = new QLabel("V:");
  vLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #d4d4d4;");
  vLabel->setFixedWidth(26);
  voltageRow->addWidget(vLabel);
  ctrl.voltageSpinBox = new QDoubleSpinBox();
  ctrl.voltageSpinBox->setRange(-100.0, 100.0);
  ctrl.voltageSpinBox->setDecimals(3);
  ctrl.voltageSpinBox->setSingleStep(0.1);
  ctrl.voltageSpinBox->setValue(0.0);
  ctrl.voltageSpinBox->setStyleSheet(spinBoxStyle);
  voltageRow->addWidget(ctrl.voltageSpinBox, 1);
  ctrl.setVoltageButton = new QPushButton("Set");
  ctrl.setVoltageButton->setStyleSheet(setButtonStyle);
  ctrl.setVoltageButton->setFixedWidth(60);
  connect(ctrl.setVoltageButton, &QPushButton::clicked, this, [this, channel]() {
    onSetVoltageClicked(channel);
  });
  voltageRow->addWidget(ctrl.setVoltageButton);
  ctrlLayout->addLayout(voltageRow);

  // Quick voltage preset buttons
  QHBoxLayout* presetRow = new QHBoxLayout();
  presetRow->setSpacing(4);
  QLabel* presetSpacer = new QLabel();
  presetSpacer->setFixedWidth(26);
  presetRow->addWidget(presetSpacer);
  for (int v = 1; v <= 4; ++v) {
    QPushButton* btn = new QPushButton(QString("%1V").arg(v));
    btn->setStyleSheet(presetStyle);
    connect(btn, &QPushButton::clicked, this, [this, channel, v]() {
      for (auto& c : m_channelControls) {
        if (c.channel == channel) {
          c.voltageSpinBox->setValue(static_cast<double>(v));
          break;
        }
      }
      onSetVoltageClicked(channel);
    });
    presetRow->addWidget(btn, 1);
  }
  QPushButton* zeroBtn = new QPushButton("0V");
  zeroBtn->setStyleSheet(presetStyle);
  connect(zeroBtn, &QPushButton::clicked, this, [this, channel]() {
    for (auto& c : m_channelControls) {
      if (c.channel == channel) {
        c.voltageSpinBox->setValue(0.0);
        break;
      }
    }
    onSetVoltageClicked(channel);
  });
  presetRow->addWidget(zeroBtn, 1);
  QWidget* presetEndSpacer = new QWidget();
  presetEndSpacer->setFixedWidth(60);
  presetRow->addWidget(presetEndSpacer);
  ctrlLayout->addLayout(presetRow);

  // Current row
  QHBoxLayout* currentRow = new QHBoxLayout();
  currentRow->setSpacing(4);
  QLabel* aLabel = new QLabel("A:");
  aLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #d4d4d4;");
  aLabel->setFixedWidth(26);
  currentRow->addWidget(aLabel);
  ctrl.currentSpinBox = new QDoubleSpinBox();
  ctrl.currentSpinBox->setRange(-1.0, 1.0);
  ctrl.currentSpinBox->setDecimals(6);
  ctrl.currentSpinBox->setSingleStep(0.001);
  ctrl.currentSpinBox->setValue(0.0);
  ctrl.currentSpinBox->setStyleSheet(spinBoxStyle);
  currentRow->addWidget(ctrl.currentSpinBox, 1);
  ctrl.setCurrentButton = new QPushButton("Set");
  ctrl.setCurrentButton->setStyleSheet(setButtonStyle);
  ctrl.setCurrentButton->setFixedWidth(60);
  connect(ctrl.setCurrentButton, &QPushButton::clicked, this, [this, channel]() {
    onSetCurrentClicked(channel);
  });
  currentRow->addWidget(ctrl.setCurrentButton);
  ctrlLayout->addLayout(currentRow);

  // Output ON/OFF row
  ctrl.outputControlGroup = new QGroupBox();
  ctrl.outputControlGroup->setStyleSheet("QGroupBox { border: none; margin: 0; padding: 0; background: transparent; }");
  QHBoxLayout* outputRow = new QHBoxLayout(ctrl.outputControlGroup);
  outputRow->setContentsMargins(0, 0, 0, 0);
  outputRow->setSpacing(4);
  ctrl.outputOnButton = new QPushButton("OUTPUT ON");
  ctrl.outputOnButton->setMinimumHeight(36);
  ctrl.outputOnButton->setStyleSheet(
    "QPushButton { background-color: #27ae60; color: white; font-size: 15px; font-weight: bold; "
    "border: none; border-radius: 4px; }"
    "QPushButton:hover { background-color: #2ecc71; }"
    "QPushButton:pressed { background-color: #1e8449; }");
  connect(ctrl.outputOnButton, &QPushButton::clicked, this, [this, channel]() {
    onOutputOnClicked(channel);
  });
  ctrl.outputOffButton = new QPushButton("OUTPUT OFF");
  ctrl.outputOffButton->setMinimumHeight(36);
  ctrl.outputOffButton->setStyleSheet(
    "QPushButton { background-color: #e74c3c; color: white; font-size: 15px; font-weight: bold; "
    "border: none; border-radius: 4px; }"
    "QPushButton:hover { background-color: #c0392b; }"
    "QPushButton:pressed { background-color: #a93226; }");
  connect(ctrl.outputOffButton, &QPushButton::clicked, this, [this, channel]() {
    onOutputOffClicked(channel);
  });
  outputRow->addWidget(ctrl.outputOnButton);
  outputRow->addWidget(ctrl.outputOffButton);
  ctrlLayout->addWidget(ctrl.outputControlGroup);

  topRow->addWidget(controlPanel, 0);
  topRow->addStretch();
  layout->addLayout(topRow);
  layout->addStretch();

  m_channelControls.push_back(ctrl);
  return tabWidget;
}

void SMUControlPage::rebuildSimpleTabs()
{
  m_channelTabWidget->clear();
  m_channelControls.clear();

  if (m_currentDevice.isEmpty() || !m_smuManager) {
    return;
  }

  auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
  if (!smu) {
    return;
  }

  auto enabledChannels = smu->GetEnabledChannels();
  for (int ch : enabledChannels) {
    QWidget* tab = createSMUChannelTab(ch);
    m_channelTabWidget->addTab(tab, QString("Ch%1").arg(ch));
  }
}

// ========== TAB 2: POLLING ==========
void SMUControlPage::createPollingTab()
{
  QWidget* tab = new QWidget();
  QVBoxLayout* tabLayout = new QVBoxLayout(tab);
  tabLayout->setContentsMargins(10, 10, 10, 10);
  tabLayout->setSpacing(6);

  // ---- Two-column layout ----
  QHBoxLayout* columnsLayout = new QHBoxLayout();
  columnsLayout->setSpacing(10);

  // ==================== LEFT COLUMN ====================
  QWidget* leftCol = new QWidget();
  leftCol->setFixedWidth(400);
  QVBoxLayout* leftLayout = new QVBoxLayout(leftCol);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  leftLayout->setSpacing(8);

  // Title
  QLabel* title = new QLabel("Continuous Polling");
  title->setStyleSheet("QLabel { font-size: 18px; font-weight: bold; color: #ffffff; }");
  leftLayout->addWidget(title);

  // Device indicator
  m_pollingDeviceLabel = new QLabel("No device selected");
  m_pollingDeviceLabel->setStyleSheet("QLabel { "
    "font-size: 13px; font-weight: bold; color: #0e639c; "
    "background-color: #1e1e1e; border: 2px solid #0e639c; "
    "border-radius: 4px; padding: 8px; }");
  m_pollingDeviceLabel->setAlignment(Qt::AlignCenter);
  leftLayout->addWidget(m_pollingDeviceLabel);

  // Config group
  QGroupBox* configGroup = new QGroupBox("Add New Polling Session");
  QGridLayout* configGrid = new QGridLayout(configGroup);
  configGrid->setSpacing(6);

  configGrid->addWidget(new QLabel("Channel:"), 0, 0);
  m_pollingChannelComboBox = new QComboBox();
  configGrid->addWidget(m_pollingChannelComboBox, 0, 1);

  configGrid->addWidget(new QLabel("Measurement Mode:"), 1, 0);
  m_pollingModeComboBox = new QComboBox();
  m_pollingModeComboBox->addItem("ACCURATE (1.0 NPLC, ~10 Hz)");
  m_pollingModeComboBox->addItem("FAST (0.1 NPLC, ~80 Hz)");
  m_pollingModeComboBox->addItem("ULTRA_FAST (0.01 NPLC, ~150 Hz)");
  m_pollingModeComboBox->setCurrentIndex(1);
  connect(m_pollingModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &SMUControlPage::onModeChanged);
  configGrid->addWidget(m_pollingModeComboBox, 1, 1);

  configGrid->addWidget(new QLabel("Measurement Type:"), 2, 0);
  m_pollingTypeComboBox = new QComboBox();
  m_pollingTypeComboBox->addItem("Current (A)");
  m_pollingTypeComboBox->addItem("Voltage (V)");
  m_pollingTypeComboBox->setCurrentIndex(0);
  connect(m_pollingTypeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &SMUControlPage::onMeasurementTypeChanged);
  configGrid->addWidget(m_pollingTypeComboBox, 2, 1);

  configGrid->addWidget(new QLabel("Range:"), 3, 0);
  m_pollingRangeComboBox = new QComboBox();
  m_pollingRangeComboBox->addItem("Auto", QVariant::fromValue(-1.0));
  m_pollingRangeComboBox->addItem("2 nA",   QVariant::fromValue(2e-9));
  m_pollingRangeComboBox->addItem("20 nA",  QVariant::fromValue(20e-9));
  m_pollingRangeComboBox->addItem("200 nA", QVariant::fromValue(200e-9));
  m_pollingRangeComboBox->addItem("2 uA",   QVariant::fromValue(2e-6));
  m_pollingRangeComboBox->addItem("20 uA",  QVariant::fromValue(20e-6));
  m_pollingRangeComboBox->addItem("200 uA", QVariant::fromValue(200e-6));
  m_pollingRangeComboBox->addItem("2 mA",   QVariant::fromValue(2e-3));
  m_pollingRangeComboBox->addItem("20 mA",  QVariant::fromValue(20e-3));
  m_pollingRangeComboBox->setCurrentIndex(0);
  configGrid->addWidget(m_pollingRangeComboBox, 3, 1);

  m_addPollingButton = new QPushButton("Add Polling Session");
  m_addPollingButton->setMinimumHeight(40);
  m_addPollingButton->setStyleSheet("QPushButton { background-color: #27ae60; font-weight: bold; font-size: 14px; }");
  connect(m_addPollingButton, &QPushButton::clicked, this, &SMUControlPage::onAddPollingClicked);
  configGrid->addWidget(m_addPollingButton, 4, 0, 1, 2);

  leftLayout->addWidget(configGroup);
  leftLayout->addStretch();

  columnsLayout->addWidget(leftCol, 0);

  // ==================== RIGHT COLUMN ====================
  QWidget* rightCol = new QWidget();
  QVBoxLayout* rightLayout = new QVBoxLayout(rightCol);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  rightLayout->setSpacing(8);

  // Active sessions group
  QGroupBox* sessionsGroup = new QGroupBox("Active Polling Sessions");
  QVBoxLayout* sessionsLayout = new QVBoxLayout(sessionsGroup);
  sessionsLayout->setSpacing(6);

  m_activeSessionsList = new QListWidget();
  m_activeSessionsList->setMinimumHeight(120);
  m_activeSessionsList->setStyleSheet("QListWidget::item { padding: 5px; }");
  sessionsLayout->addWidget(m_activeSessionsList);

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
  rightLayout->addWidget(sessionsGroup);

  // Live measurements display
  QGroupBox* displayGroup = new QGroupBox("Live Measurements");
  QVBoxLayout* displayLayout = new QVBoxLayout(displayGroup);
  displayLayout->setSpacing(6);

  QLabel* valueTitle = new QLabel("Current Value:");
  valueTitle->setStyleSheet("QLabel { font-weight: bold; }");
  displayLayout->addWidget(valueTitle);

  m_pollingValueLabel = new QLabel("--- A");
  m_pollingValueLabel->setStyleSheet("QLabel { "
    "font-size: 32px; font-weight: bold; color: #3498db; "
    "background-color: #1e1e1e; border: 2px solid #3f3f46; "
    "border-radius: 4px; padding: 15px; }");
  m_pollingValueLabel->setAlignment(Qt::AlignCenter);
  m_pollingValueLabel->setMinimumHeight(80);
  displayLayout->addWidget(m_pollingValueLabel);

  m_pollingSampleLabel = new QLabel("Samples: 0");
  m_pollingSampleLabel->setStyleSheet("QLabel { font-size: 12px; color: #888888; padding: 4px; }");
  m_pollingSampleLabel->setAlignment(Qt::AlignCenter);
  displayLayout->addWidget(m_pollingSampleLabel);

  rightLayout->addWidget(displayGroup);
  rightLayout->addStretch();

  columnsLayout->addWidget(rightCol, 1);

  tabLayout->addLayout(columnsLayout);
  m_tabWidget->addTab(tab, "Polling");
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

// Channel tab changed - just update the selected channel (no widget reparenting)
void SMUControlPage::onChannelTabChanged(int index)
{
  if (index < 0 || index >= static_cast<int>(m_channelControls.size())) return;

  int channel = m_channelControls[index].channel;
  std::cout << "[SMUControlPage] Channel tab changed: " << channel << std::endl;

  // Sync hidden combo
  if (m_channelComboBox && index < m_channelComboBox->count()) {
    m_channelComboBox->blockSignals(true);
    m_channelComboBox->setCurrentIndex(index);
    m_channelComboBox->blockSignals(false);
  }

  // Update worker polling channel
  if (m_parameterWorker) {
    m_parameterWorker->setPollingChannel(channel);
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

// Output control (per-channel)
void SMUControlPage::onOutputOnClicked(int channel)
{
  if (m_currentDevice.isEmpty() || !m_parameterWorker) return;
  auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
  if (!smu || !smu->IsConnected()) return;

  // Optimistic UI update
  m_cachedOutputOn = true;
  m_outputCommandPending = 5;
  for (auto& ctrl : m_channelControls) {
    if (ctrl.channel == channel) {
      ctrl.outputStateLabel->setText("Output: ON");
      ctrl.outputStateLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #4cff4c; background: transparent;");
      break;
    }
  }
  m_parameterWorker->requestOutputOn(channel);
}

void SMUControlPage::onOutputOffClicked(int channel)
{
  if (m_currentDevice.isEmpty() || !m_parameterWorker) return;
  auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
  if (!smu || !smu->IsConnected()) return;

  m_cachedOutputOn = false;
  m_outputCommandPending = 5;
  for (auto& ctrl : m_channelControls) {
    if (ctrl.channel == channel) {
      ctrl.outputStateLabel->setText("Output: OFF");
      ctrl.outputStateLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #ff6666; background: transparent;");
      break;
    }
  }
  m_parameterWorker->requestOutputOff(channel);
}

void SMUControlPage::updateOutputState()
{
  if (m_channelControls.empty()) return;

  for (auto& ctrl : m_channelControls) {
    if (m_currentDevice.isEmpty()) {
      ctrl.outputStateLabel->setText("Output: ---");
      ctrl.outputStateLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #888888; background: transparent;");
    } else if (m_cachedOutputOn) {
      ctrl.outputStateLabel->setText("Output: ON");
      ctrl.outputStateLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #4cff4c; background: transparent;");
    } else {
      ctrl.outputStateLabel->setText("Output: OFF");
      ctrl.outputStateLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #ff6666; background: transparent;");
    }
  }

  if (m_parameterWorker && !m_currentDevice.isEmpty()) {
    int index = m_channelTabWidget->currentIndex();
    int channel = (index >= 0 && index < static_cast<int>(m_channelControls.size()))
      ? m_channelControls[index].channel : 1;
    m_parameterWorker->requestRefreshAll(channel);
  }
}

// Set voltage/current (per-channel)
void SMUControlPage::onSetVoltageClicked(int channel)
{
  if (m_currentDevice.isEmpty() || !m_parameterWorker) return;
  auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
  if (!smu || !smu->IsConnected()) return;

  for (const auto& ctrl : m_channelControls) {
    if (ctrl.channel == channel) {
      double voltage = ctrl.voltageSpinBox->value();
      m_parameterWorker->requestSetVoltage(channel, voltage);
      m_parameterWorker->requestRefreshAll(channel);
      break;
    }
  }
}

void SMUControlPage::onSetCurrentClicked(int channel)
{
  if (m_currentDevice.isEmpty() || !m_parameterWorker) return;
  auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
  if (!smu || !smu->IsConnected()) return;

  for (const auto& ctrl : m_channelControls) {
    if (ctrl.channel == channel) {
      double current = ctrl.currentSpinBox->value();
      m_parameterWorker->requestSetCurrent(channel, current);
      m_parameterWorker->requestRefreshAll(channel);
      break;
    }
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
void SMUControlPage::onPollingMeasurement(QString deviceName, int channel, double value, QString /*timestamp*/)
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
void SMUControlPage::onRefreshClicked(int channel)
{
  if (m_currentDevice.isEmpty() || !m_parameterWorker) return;
  m_parameterWorker->requestRefreshAll(channel);
}

void SMUControlPage::updateMeasurements()
{
  if (m_channelControls.empty() || m_currentDevice.isEmpty() || !m_parameterWorker) {
    return;
  }

  int index = m_channelTabWidget->currentIndex();
  int channel = (index >= 0 && index < static_cast<int>(m_channelControls.size()))
    ? m_channelControls[index].channel : 1;
  m_parameterWorker->requestRefreshAll(channel);
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

  if (m_currentDevice.isEmpty()) {
    m_channelCountLabel->setText("0");
    rebuildSimpleTabs();
    return;
  }

  auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
  if (!smu || !smu->IsConnected()) {
    m_channelCountLabel->setText("0");
    rebuildSimpleTabs();
    return;
  }

  int channelCount = smu->GetChannelCount();
  auto enabledChannels = smu->GetEnabledChannels();
  m_channelCountLabel->setText(QString::number(channelCount));

  // Populate hidden combo + polling combo
  for (int ch : enabledChannels) {
    m_channelComboBox->addItem(QString("Channel %1").arg(ch));
    m_pollingChannelComboBox->addItem(QString("Channel %1").arg(ch));
  }

  // Build per-channel tabs with full controls inside
  rebuildSimpleTabs();
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
  // Hide/show output controls per channel based on device mode
  bool showOutput = (mode != SMUMode::MEASURE_ONLY);
  for (auto& ctrl : m_channelControls) {
    if (ctrl.outputControlGroup) {
      ctrl.outputControlGroup->setVisible(showOutput);
    }
  }
  std::cout << "[SMUControlPage] Device mode: " << (showOutput ? "source" : "MEASURE_ONLY") << std::endl;

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
  if (m_channelControls.empty()) return;

  for (auto& ctrl : m_channelControls) {
    if (ctrl.channel == channel) {
      // Update voltage with SI prefix
      if (voltage != -999.0) {
        ctrl.voltageLabel->setText("V: " + formatSI(voltage, "V"));
      } else {
        ctrl.voltageLabel->setText("V: --- V");
      }
      ctrl.voltageLabel->setStyleSheet("font-size: 26px; font-weight: bold; color: #ffffff; background: transparent;");

      // Update current with SI prefix
      if (current != -999.0) {
        ctrl.currentLabel->setText("A: " + formatSI(current, "A"));
      } else {
        ctrl.currentLabel->setText("A: --- A");
      }
      ctrl.currentLabel->setStyleSheet("font-size: 26px; font-weight: bold; color: #ffffff; background: transparent;");

      // Skip output state while command in flight
      if (m_outputCommandPending > 0) {
        --m_outputCommandPending;
        return;
      }

      m_cachedOutputOn = outputOn;
      if (outputOn) {
        ctrl.outputStateLabel->setText("Output: ON");
        ctrl.outputStateLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #4cff4c; background: transparent;");
      } else {
        ctrl.outputStateLabel->setText("Output: OFF");
        ctrl.outputStateLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #ff6666; background: transparent;");
      }
      break;
    }
  }
}

void SMUControlPage::onOutputStateChangedByWorker(bool on)
{
  m_outputCommandPending = 0;
  m_cachedOutputOn = on;

  // Update all channel displays (output state is device-wide, not per-channel)
  for (auto& ctrl : m_channelControls) {
    if (on) {
      ctrl.outputStateLabel->setText("Output: ON");
      ctrl.outputStateLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #4cff4c; background: transparent;");
    } else {
      ctrl.outputStateLabel->setText("Output: OFF");
      ctrl.outputStateLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #ff6666; background: transparent;");
    }
  }

  // Request refresh for active channel
  if (m_parameterWorker && !m_currentDevice.isEmpty()) {
    int index = m_channelTabWidget->currentIndex();
    int channel = (index >= 0 && index < static_cast<int>(m_channelControls.size()))
      ? m_channelControls[index].channel : 1;
    m_parameterWorker->requestRefreshAll(channel);
  }
}

void SMUControlPage::onParameterCommandError(QString operation, QString error)
{
  showError(operation + ": " + error);
}

void SMUControlPage::onParameterCommandSuccess(QString /*operation*/, QString message)
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

      // Request initial measurements for active channel
      int index = m_channelTabWidget->currentIndex();
      int channel = (index >= 0 && index < static_cast<int>(m_channelControls.size()))
        ? m_channelControls[index].channel : 1;
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
