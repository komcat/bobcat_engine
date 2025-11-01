// SMUControlPage.cpp - Simple 2-panel SMU control interface
#include "SMUControlPage.h"
#include "ApplicationContext.h"
#include "SMUManager.h"
#include "ISMUDevice.h"
#include "Keithley6482Adapter.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QFrame>
#include <iostream>

SMUControlPage::SMUControlPage(ApplicationContext* context, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_smuManager(context->smuManager)
  , m_refreshTimer(new QTimer(this))
{
  setupUI();
  loadDevices();

  // Auto-refresh measurements every 2 seconds when connected
  connect(m_refreshTimer, &QTimer::timeout, this, &SMUControlPage::updateMeasurements);
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
    )";
}

void SMUControlPage::setupUI()
{
  setStyleSheet(createStyleSheet());

  QHBoxLayout* mainLayout = new QHBoxLayout(this);
  mainLayout->setContentsMargins(10, 10, 10, 10);
  mainLayout->setSpacing(10);

  // Left panel (30%) - Device list
  QWidget* leftPanel = new QWidget();
  leftPanel->setMinimumWidth(250);
  leftPanel->setMaximumWidth(350);
  createLeftPanel(leftPanel);
  mainLayout->addWidget(leftPanel);

  // Right panel (70%) - Controls
  m_controlPanel = new QWidget();
  createRightPanel(m_controlPanel);
  mainLayout->addWidget(m_controlPanel, 1);
}

void SMUControlPage::createLeftPanel(QWidget* leftPanel)
{
  QVBoxLayout* layout = new QVBoxLayout(leftPanel);
  layout->setContentsMargins(5, 5, 5, 5);
  layout->setSpacing(10);

  // Title
  QLabel* title = new QLabel("SMU Devices");
  title->setStyleSheet("QLabel { font-size: 16px; font-weight: bold; color: #ffffff; }");
  layout->addWidget(title);

  // Device list
  m_deviceList = new QListWidget();
  layout->addWidget(m_deviceList);
  connect(m_deviceList, &QListWidget::currentRowChanged, this, &SMUControlPage::onDeviceSelected);

  // Connection status
  m_connectionStatus = new QLabel("Not connected");
  m_connectionStatus->setProperty("class", "status-disconnected");
  m_connectionStatus->setAlignment(Qt::AlignCenter);
  layout->addWidget(m_connectionStatus);

  // Connect/Disconnect buttons
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  m_connectButton = new QPushButton("Connect");
  m_disconnectButton = new QPushButton("Disconnect");
  m_disconnectButton->setEnabled(false);

  connect(m_connectButton, &QPushButton::clicked, this, &SMUControlPage::onConnectClicked);
  connect(m_disconnectButton, &QPushButton::clicked, this, &SMUControlPage::onDisconnectClicked);

  buttonLayout->addWidget(m_connectButton);
  buttonLayout->addWidget(m_disconnectButton);
  layout->addLayout(buttonLayout);
}

void SMUControlPage::createRightPanel(QWidget* rightPanel)
{
  QVBoxLayout* layout = new QVBoxLayout(rightPanel);
  layout->setContentsMargins(5, 5, 5, 5);
  layout->setSpacing(15);

  // Title
  QLabel* title = new QLabel("SMU Control");
  title->setStyleSheet("QLabel { font-size: 18px; font-weight: bold; color: #ffffff; }");
  layout->addWidget(title);

  // Channel section
  createChannelSection(layout);

  // Measurement section
  createMeasurementSection(layout);

  // Polling section
  createPollingSection(layout);

  // Status
  m_statusLabel = new QLabel("");
  m_statusLabel->setAlignment(Qt::AlignCenter);
  m_statusLabel->setStyleSheet("QLabel { font-size: 12px; padding: 8px; }");
  layout->addWidget(m_statusLabel);

  layout->addStretch();
}

void SMUControlPage::createChannelSection(QVBoxLayout* layout)
{
  QGroupBox* channelGroup = new QGroupBox("Channel Configuration");
  QGridLayout* gridLayout = new QGridLayout(channelGroup);
  gridLayout->setSpacing(10);

  // Channel count
  gridLayout->addWidget(new QLabel("Channel Count:"), 0, 0);
  m_channelCountLabel = new QLabel("0");
  m_channelCountLabel->setStyleSheet("QLabel { font-weight: bold; }");
  gridLayout->addWidget(m_channelCountLabel, 0, 1);

  // Channel selection
  gridLayout->addWidget(new QLabel("Select Channel:"), 1, 0);
  m_channelComboBox = new QComboBox();
  connect(m_channelComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &SMUControlPage::onChannelSelected);
  gridLayout->addWidget(m_channelComboBox, 1, 1);

  // Measurement mode
  gridLayout->addWidget(new QLabel("Measurement Mode:"), 2, 0);
  m_modeComboBox = new QComboBox();
  m_modeComboBox->addItem("ACCURATE (1.0 NPLC, ~10 Hz)");
  m_modeComboBox->addItem("FAST (0.1 NPLC, ~80 Hz)");
  m_modeComboBox->addItem("ULTRA_FAST (0.01 NPLC, ~150 Hz)");
  m_modeComboBox->setCurrentIndex(0);  // Default: ACCURATE
  connect(m_modeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &SMUControlPage::onModeChanged);
  gridLayout->addWidget(m_modeComboBox, 2, 1);

  layout->addWidget(channelGroup);
}

void SMUControlPage::createMeasurementSection(QVBoxLayout* layout)
{
  QGroupBox* measurementGroup = new QGroupBox("Current Measurements");
  QVBoxLayout* groupLayout = new QVBoxLayout(measurementGroup);

  // Current channel reading (large display)
  QLabel* currentLabel = new QLabel("Selected Channel:");
  groupLayout->addWidget(currentLabel);

  m_currentChannelReadLabel = new QLabel("--- A");
  m_currentChannelReadLabel->setStyleSheet("QLabel { "
    "font-size: 24px; "
    "font-weight: bold; "
    "color: #27ae60; "
    "background-color: #1e1e1e; "
    "border: 2px solid #3f3f46; "
    "border-radius: 4px; "
    "padding: 10px; "
    "}");
  m_currentChannelReadLabel->setAlignment(Qt::AlignCenter);
  groupLayout->addWidget(m_currentChannelReadLabel);

  // All channels (smaller display)
  QLabel* allLabel = new QLabel("All Channels:");
  allLabel->setStyleSheet("QLabel { margin-top: 10px; }");
  groupLayout->addWidget(allLabel);

  // Placeholder for channel labels (will be populated dynamically)
  QWidget* channelContainer = new QWidget();
  QVBoxLayout* channelLayout = new QVBoxLayout(channelContainer);
  channelLayout->setSpacing(5);
  channelLayout->setContentsMargins(0, 0, 0, 0);
  groupLayout->addWidget(channelContainer);

  // Refresh button
  m_refreshButton = new QPushButton("Refresh");
  connect(m_refreshButton, &QPushButton::clicked, this, &SMUControlPage::onRefreshClicked);
  groupLayout->addWidget(m_refreshButton);

  layout->addWidget(measurementGroup);
}

void SMUControlPage::createPollingSection(QVBoxLayout* layout)
{
  QGroupBox* pollingGroup = new QGroupBox("Continuous Polling");
  QVBoxLayout* groupLayout = new QVBoxLayout(pollingGroup);

  // Polling status
  m_pollingStatusLabel = new QLabel("Status: Stopped");
  m_pollingStatusLabel->setProperty("class", "status-disconnected");
  groupLayout->addWidget(m_pollingStatusLabel);

  // Real-time polled value (large display)
  QLabel* valueTitle = new QLabel("Current Value:");
  groupLayout->addWidget(valueTitle);

  m_pollingValueLabel = new QLabel("--- A");
  m_pollingValueLabel->setStyleSheet("QLabel { "
    "font-size: 20px; "
    "font-weight: bold; "
    "color: #3498db; "
    "background-color: #1e1e1e; "
    "border: 2px solid #3f3f46; "
    "border-radius: 4px; "
    "padding: 8px; "
    "}");
  m_pollingValueLabel->setAlignment(Qt::AlignCenter);
  groupLayout->addWidget(m_pollingValueLabel);

  // Sample count
  m_pollingSampleLabel = new QLabel("Samples: 0");
  m_pollingSampleLabel->setStyleSheet("QLabel { font-size: 11px; color: #888888; }");
  m_pollingSampleLabel->setAlignment(Qt::AlignCenter);
  groupLayout->addWidget(m_pollingSampleLabel);

  // Start/Stop buttons
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  m_startPollingButton = new QPushButton("Start Polling");
  m_stopPollingButton = new QPushButton("Stop Polling");
  m_stopPollingButton->setEnabled(false);

  connect(m_startPollingButton, &QPushButton::clicked, this, &SMUControlPage::onStartPollingClicked);
  connect(m_stopPollingButton, &QPushButton::clicked, this, &SMUControlPage::onStopPollingClicked);

  buttonLayout->addWidget(m_startPollingButton);
  buttonLayout->addWidget(m_stopPollingButton);
  groupLayout->addLayout(buttonLayout);

  layout->addWidget(pollingGroup);
}

// Device management
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

// Polling control
void SMUControlPage::onStartPollingClicked()
{
  if (m_currentDevice.isEmpty()) return;

  auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
  if (!smu || !smu->IsConnected()) {
    showError("Device not connected");
    return;
  }

  auto keithley = std::dynamic_pointer_cast<Keithley6482Adapter>(smu);
  if (!keithley) return;

  // Connect to measurement signal
  connect(keithley.get(), &Keithley6482Adapter::measurementReady,
          this, &SMUControlPage::onPollingMeasurement);

  // Reset sample count
  keithley->ResetSampleCount();

  if (keithley->StartPolling(100)) {  // 100ms = 10Hz update
    m_pollingStatusLabel->setText("Status: Polling");
    m_pollingStatusLabel->setProperty("class", "status-polling");
    m_pollingValueLabel->setText("--- A");
    m_pollingSampleLabel->setText("Samples: 0");
    m_startPollingButton->setEnabled(false);
    m_stopPollingButton->setEnabled(true);
    showSuccess("Polling started");
  } else {
    showError("Failed to start polling");
  }
}

void SMUControlPage::onStopPollingClicked()
{
  if (m_currentDevice.isEmpty()) return;

  auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
  if (!smu) return;

  auto keithley = std::dynamic_pointer_cast<Keithley6482Adapter>(smu);
  if (keithley) {
    // Disconnect signal
    disconnect(keithley.get(), &Keithley6482Adapter::measurementReady,
               this, &SMUControlPage::onPollingMeasurement);

    keithley->StopPolling();

    uint64_t finalCount = keithley->GetSampleCount();

    m_pollingStatusLabel->setText("Status: Stopped");
    m_pollingStatusLabel->setProperty("class", "status-disconnected");
    m_pollingValueLabel->setText("--- A");
    m_pollingSampleLabel->setText(QString("Samples: %1 (stopped)").arg(finalCount));
    m_startPollingButton->setEnabled(true);
    m_stopPollingButton->setEnabled(false);
    showSuccess(QString("Polling stopped (%1 samples)").arg(finalCount));
  }
}

// Polling measurement callback
void SMUControlPage::onPollingMeasurement(QString deviceName, int channel, double current, QString timestamp)
{
  // Only update if this is the current device
  if (deviceName != m_currentDevice) return;

  // Update real-time value display
  m_pollingValueLabel->setText(QString::number(current, 'e', 3) + " A");

  // Update sample count
  auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
  if (smu) {
    auto keithley = std::dynamic_pointer_cast<Keithley6482Adapter>(smu);
    if (keithley) {
      uint64_t samples = keithley->GetSampleCount();
      m_pollingSampleLabel->setText(QString("Samples: %1 | Ch%2 | %3")
                                       .arg(samples)
                                       .arg(channel)
                                       .arg(timestamp));
    }
  }
}

// Measurement update
void SMUControlPage::onRefreshClicked()
{
  updateMeasurements();
}

void SMUControlPage::updateMeasurements()
{
  if (m_currentDevice.isEmpty()) return;

  auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
  if (!smu || !smu->IsConnected()) {
    m_currentChannelReadLabel->setText("--- A");
    return;
  }

  // Read selected channel
  int selectedChannel = m_channelComboBox->currentIndex() + 1;
  double current = smu->ReadChannel(selectedChannel);

  if (current != -999.0) {
    m_currentChannelReadLabel->setText(QString::number(current, 'e', 3) + " A");
  } else {
    m_currentChannelReadLabel->setText("Error");
  }

  // Update all channel labels
  auto allChannels = smu->ReadAllChannels();
  for (size_t i = 0; i < allChannels.size() && i < m_channelReadLabels.size(); ++i) {
    if (allChannels[i] != -999.0) {
      m_channelReadLabels[i]->setText(QString("Ch%1: %2 A").arg(i + 1).arg(allChannels[i], 0, 'e', 2));
    } else {
      m_channelReadLabels[i]->setText(QString("Ch%1: Error").arg(i + 1));
    }
  }
}

// Helper methods
void SMUControlPage::updateConnectionStatus()
{
  if (m_currentDevice.isEmpty()) return;

  auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
  if (smu && smu->IsConnected()) {
    m_connectionStatus->setText("Connected");
    m_connectionStatus->setProperty("class", "status-connected");
    m_connectButton->setEnabled(false);
    m_disconnectButton->setEnabled(true);
    m_refreshTimer->start(2000);  // Auto-refresh every 2 seconds
  } else {
    m_connectionStatus->setText("Not connected");
    m_connectionStatus->setProperty("class", "status-disconnected");
    m_connectButton->setEnabled(true);
    m_disconnectButton->setEnabled(false);
    m_refreshTimer->stop();
  }
}

void SMUControlPage::updateChannelList()
{
  m_channelComboBox->clear();
  m_channelReadLabels.clear();

  if (m_currentDevice.isEmpty()) return;

  auto smu = m_smuManager->GetDevice(m_currentDevice.toStdString());
  if (!smu || !smu->IsConnected()) {
    m_channelCountLabel->setText("0");
    return;
  }

  int channelCount = smu->GetChannelCount();
  auto enabledChannels = smu->GetEnabledChannels();

  m_channelCountLabel->setText(QString::number(channelCount));

  // Populate channel combo box
  for (int ch : enabledChannels) {
    m_channelComboBox->addItem(QString("Channel %1").arg(ch));

    // Create label for this channel
    QLabel* channelLabel = new QLabel(QString("Ch%1: --- A").arg(ch));
    channelLabel->setStyleSheet("QLabel { padding: 5px; background-color: #2d2d30; border: 1px solid #3f3f46; border-radius: 3px; }");
    m_channelReadLabels.push_back(channelLabel);
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
