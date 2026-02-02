// PIControllerPage.cpp
#include "PIControllerPage.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "pi_controller.h"
#include "PIAlignExecutor.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QScrollArea>
#include <QFrame>
#include <iostream>

PIControllerPage::PIControllerPage(ApplicationContext* context, QWidget* parent)
  : MotionControllerPageBase(context, parent)
  , m_tabWidget(nullptr)
  , m_deviceSelector(nullptr)
  , m_systemVelocity(nullptr)
  , m_turtleButton(nullptr)
  , m_rabbitButton(nullptr)
  , m_applyButton(nullptr)
  , m_deviceStatusWidget(nullptr)
  , m_deviceStatusLayout(nullptr)
  , m_pivotDisplayWidget(nullptr)
  , m_pivotDisplayLayout(nullptr)
  , m_analogDisplayWidget(nullptr)
  , m_analogDisplayLayout(nullptr)
  , m_analogDeviceSelector(nullptr)
  , m_analogRefreshButton(nullptr)
  , m_analogPollTimer(nullptr)
  , m_fsmSettingsGroup(nullptr)
  , m_fsmAxis1Selector(nullptr)
  , m_fsmLength1SpinBox(nullptr)
  , m_fsmAxis2Selector(nullptr)
  , m_fsmLength2SpinBox(nullptr)
  , m_fsmThresholdSpinBox(nullptr)
  , m_fsmDistanceSpinBox(nullptr)
  , m_zDirectionSelector(nullptr)
  , m_zRangeSpinBox(nullptr)
  , m_zStepSpinBox(nullptr)
  , m_alignExecutor(nullptr)
  , m_currentAlignChannel(0)
  , m_zScanProgressWidget(nullptr)
  , m_zScanProgressBar(nullptr)
  , m_zScanPeakLabel(nullptr)
{
  std::cout << "[PIControllerPage] Initialized" << std::endl;

  // NOW safe to call virtual functions - derived class is constructed
  initialize();
}

PIControllerPage::~PIControllerPage()
{
  // Stop analog poll timer
  if (m_analogPollTimer) {
    m_analogPollTimer->stop();
  }

  // Clean up align executor
  if (m_alignExecutor) {
    m_alignExecutor->requestStop();
    delete m_alignExecutor;
    m_alignExecutor = nullptr;
  }

  std::cout << "[PIControllerPage] Destroyed" << std::endl;
}

std::string PIControllerPage::getControllerType() const
{
  return "PI";
}

std::vector<std::string> PIControllerPage::getAxisNames() const
{
  // PI Hexapod has 6 axes: X, Y, Z, U, V, W
  return { "X", "Y", "Z", "U", "V", "W" };
}

QString PIControllerPage::getPageTitle() const
{
  return "PI Hexapod Controllers";
}

void PIControllerPage::showEvent(QShowEvent* event)
{
  QWidget::showEvent(event);
  std::cout << "[PIControllerPage] Page shown - refreshing device status" << std::endl;

  // Refresh device status display
  updateDeviceStatusDisplay();

  // Start position polling
  if (m_pollTimer) {
    m_pollTimer->start(POLL_INTERVAL_MS);
  }
}

void PIControllerPage::hideEvent(QHideEvent* event)
{
  QWidget::hideEvent(event);
  std::cout << "[PIControllerPage] Page hidden - stopping polling" << std::endl;

  // Stop polling
  if (m_pollTimer) {
    m_pollTimer->stop();
  }
}

// Override base class updatePositions - we use vertical cards instead of m_deviceTable
void PIControllerPage::updatePositions()
{
  // Redirect to our custom position update method
  onPositionPollTimer();
}

void PIControllerPage::setupUI()
{
  // Create our own main layout (don't call base class setupUI which adds horizontal table)
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(20, 20, 20, 20);
  mainLayout->setSpacing(15);

  // Title bar with refresh button
  QHBoxLayout* titleLayout = new QHBoxLayout();

  QLabel* titleLabel = new QLabel(getPageTitle());
  titleLabel->setStyleSheet("QLabel { font-size: 20px; font-weight: bold; color: #ffffff; }");
  titleLayout->addWidget(titleLabel);

  titleLayout->addStretch();

  m_refreshButton = new QPushButton("🔄 Refresh");
  m_refreshButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #0e639c; "
    "  color: white; "
    "  border: 1px solid #0c5280; "
    "  border-radius: 5px; "
    "  padding: 8px 15px; "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { background-color: #1177bb; }");
  connect(m_refreshButton, &QPushButton::clicked, this, [this]() {
    std::cout << "[PIControllerPage] Manual refresh requested" << std::endl;
    updateDeviceStatusDisplay();
    updateVelocityDisplay();
  });
  titleLayout->addWidget(m_refreshButton);

  mainLayout->addLayout(titleLayout);

  // Create tab widget
  m_tabWidget = new QTabWidget();
  m_tabWidget->setStyleSheet(
    "QTabWidget::pane { "
    "  border: 1px solid #3f3f46; "
    "  background-color: #1e1e1e; "
    "} "
    "QTabBar::tab { "
    "  background-color: #2d2d30; "
    "  color: #d4d4d4; "
    "  padding: 8px 20px; "
    "  border: 1px solid #3f3f46; "
    "  border-bottom: none; "
    "  margin-right: 2px; "
    "} "
    "QTabBar::tab:selected { "
    "  background-color: #1e1e1e; "
    "  color: #ffffff; "
    "  font-weight: bold; "
    "} "
    "QTabBar::tab:hover { "
    "  background-color: #3c3c3c; "
    "}");

  // Create tabs - Device Status first, then others
  createDeviceStatusTab();  // New: Vertical table with positions and connection
  createMainTab();          // Velocity Control
  createPivotPointTab();
  createAnalogInputTab();

  // Connect tab change signal
  connect(m_tabWidget, &QTabWidget::currentChanged, this, &PIControllerPage::onTabChanged);

  mainLayout->addWidget(m_tabWidget);

  // Dark background
  setStyleSheet("QWidget { background-color: #2d2d30; color: #d4d4d4; }");

  // Note: Base class initialize() will call setupPolling() after setupUI()
  // which creates m_pollTimer and connects to updatePositions() (our override)
}

// ✅ NEW: Create main tab with velocity controls
void PIControllerPage::createMainTab()
{
  QWidget* mainTab = new QWidget();
  QVBoxLayout* mainTabLayout = new QVBoxLayout(mainTab);
  mainTabLayout->setContentsMargins(10, 10, 10, 10);
  mainTabLayout->setSpacing(10);

  // Create velocity control group for main tab
  QGroupBox* velocityGroup = new QGroupBox("System Velocity Control");
  velocityGroup->setStyleSheet(
    "QGroupBox { "
    "  font-size: 16px; "
    "  font-weight: bold; "
    "  color: #ffffff; "
    "  background-color: #1e1e1e; "
    "  border: 2px solid #3f3f46; "
    "  border-radius: 8px; "
    "  margin-top: 10px; "
    "  padding-top: 15px; "
    "} "
    "QGroupBox::title { "
    "  subcontrol-origin: margin; "
    "  left: 10px; "
    "  padding: 0 5px; "
    "  color: #ffffff; "
    "}");

  QVBoxLayout* groupLayout = new QVBoxLayout(velocityGroup);
  groupLayout->setSpacing(10);

  // Device selector row
  QHBoxLayout* deviceRow = new QHBoxLayout();
  QLabel* deviceLabel = new QLabel("Device:");
  deviceLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: normal; color: #d4d4d4; }");
  deviceRow->addWidget(deviceLabel);

  m_deviceSelector = new QComboBox();
  m_deviceSelector->setMinimumWidth(200);
  m_deviceSelector->setStyleSheet(
    "QComboBox { "
    "  background-color: #3c3c3c; "
    "  color: #d4d4d4; "
    "  border: 1px solid #555555; "
    "  border-radius: 4px; "
    "  padding: 5px; "
    "  font-size: 14px; "
    "} "
    "QComboBox:hover { "
    "  border-color: #0e639c; "
    "} "
    "QComboBox QAbstractItemView { "
    "  background-color: #1e1e1e; "
    "  color: #d4d4d4; "
    "  selection-background-color: #0e639c; "
    "  border: 1px solid #3f3f46; "
    "}");
  connect(m_deviceSelector, &QComboBox::currentTextChanged, this, &PIControllerPage::onDeviceSelected);
  deviceRow->addWidget(m_deviceSelector);
  deviceRow->addStretch();

  groupLayout->addLayout(deviceRow);

  // System velocity row
  QHBoxLayout* velocityRow = new QHBoxLayout();

  QLabel* velocityLabel = new QLabel("System Velocity:");
  velocityLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: normal; color: #d4d4d4; }");
  velocityRow->addWidget(velocityLabel);

  m_systemVelocity = new QDoubleSpinBox();
  m_systemVelocity->setRange(1.0, 10.0);
  m_systemVelocity->setValue(5.0);
  m_systemVelocity->setSuffix(" mm/s");
  m_systemVelocity->setDecimals(1);
  m_systemVelocity->setStyleSheet(
    "QDoubleSpinBox { "
    "  background-color: #3c3c3c; "
    "  color: #d4d4d4; "
    "  border: 1px solid #555555; "
    "  border-radius: 3px; "
    "  padding: 5px; "
    "} "
    "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { "
    "  background-color: #2d2d30; "
    "  border: 1px solid #555555; "
    "} "
    "QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover { "
    "  background-color: #3c3c3c; "
    "}");
  velocityRow->addWidget(m_systemVelocity);

  velocityRow->addStretch();

  groupLayout->addLayout(velocityRow);

  // Preset buttons and apply button row
  QHBoxLayout* buttonRow = new QHBoxLayout();

  QLabel* presetLabel = new QLabel("Quick Presets:");
  presetLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: normal; color: #d4d4d4; }");
  buttonRow->addWidget(presetLabel);

  m_turtleButton = new QPushButton("🐢 Turtle (5 mm/s)");
  m_turtleButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #3498db; "
    "  color: white; "
    "  border: none; "
    "  border-radius: 5px; "
    "  padding: 8px 15px; "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { background-color: #2980b9; }");
  connect(m_turtleButton, &QPushButton::clicked, this, &PIControllerPage::onPresetTurtleClicked);
  buttonRow->addWidget(m_turtleButton);

  m_rabbitButton = new QPushButton("🐇 Rabbit (10 mm/s)");
  m_rabbitButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #e74c3c; "
    "  color: white; "
    "  border: none; "
    "  border-radius: 5px; "
    "  padding: 8px 15px; "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { background-color: #c0392b; }");
  connect(m_rabbitButton, &QPushButton::clicked, this, &PIControllerPage::onPresetRabbitClicked);
  buttonRow->addWidget(m_rabbitButton);

  buttonRow->addStretch();

  m_applyButton = new QPushButton("✓ Apply Velocity");
  m_applyButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #27ae60; "
    "  color: white; "
    "  border: none; "
    "  border-radius: 5px; "
    "  padding: 10px 20px; "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { background-color: #229954; }");
  connect(m_applyButton, &QPushButton::clicked, this, &PIControllerPage::onApplyVelocityClicked);
  buttonRow->addWidget(m_applyButton);

  groupLayout->addLayout(buttonRow);

  // Add velocity group to main tab layout
  mainTabLayout->addWidget(velocityGroup);
  mainTabLayout->addStretch();

  // Add tab to tab widget
  m_tabWidget->addTab(mainTab, "Velocity Control");

  // Initialize device list
  updateVelocityDisplay();
}

// Create device status tab with vertical table (transposed layout)
void PIControllerPage::createDeviceStatusTab()
{
  QWidget* statusTab = new QWidget();
  QHBoxLayout* statusTabLayout = new QHBoxLayout(statusTab);
  statusTabLayout->setContentsMargins(10, 10, 10, 10);
  statusTabLayout->setSpacing(10);
  statusTabLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);  // Align to left, don't stretch

  // Create scroll area for device cards
  QScrollArea* scrollArea = new QScrollArea();
  scrollArea->setWidgetResizable(true);
  scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  scrollArea->setStyleSheet(
    "QScrollArea { "
    "  background-color: transparent; "
    "  border: none; "
    "} "
    "QScrollArea > QWidget > QWidget { "
    "  background-color: transparent; "
    "}");

  // Container widget for all device cards
  m_deviceStatusWidget = new QWidget();
  m_deviceStatusLayout = new QHBoxLayout(m_deviceStatusWidget);
  m_deviceStatusLayout->setContentsMargins(0, 0, 0, 0);
  m_deviceStatusLayout->setSpacing(15);
  m_deviceStatusLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);  // Left-align cards

  scrollArea->setWidget(m_deviceStatusWidget);
  statusTabLayout->addWidget(scrollArea);

  // Add tab to tab widget
  m_tabWidget->addTab(statusTab, "Device Status");

  // Initial population
  updateDeviceStatusDisplay();
}

// Update device status display with vertical cards per device
void PIControllerPage::updateDeviceStatusDisplay()
{
  if (!m_deviceStatusLayout) return;

  UnifiedMotionManager* motionMgr = getMotionManager();
  if (!motionMgr) return;

  // Clear existing widgets and label references
  QLayoutItem* item;
  while ((item = m_deviceStatusLayout->takeAt(0)) != nullptr) {
    if (item->widget()) {
      delete item->widget();
    }
    delete item;
  }
  m_positionLabels.clear();

  // Get PI devices
  std::vector<std::string> piDevices = motionMgr->GetDeviceNamesByType("PI");

  if (piDevices.empty()) {
    QLabel* noDevicesLabel = new QLabel("No PI devices found");
    noDevicesLabel->setStyleSheet("QLabel { color: #e74c3c; font-size: 14px; font-style: italic; }");
    m_deviceStatusLayout->addWidget(noDevicesLabel);
    return;
  }

  // Get connection status
  std::map<std::string, bool> connectionStatus = motionMgr->GetConnectionStatus();

  // Style definitions
  QString cardStyle =
    "QWidget { "
    "  background-color: #1e1e1e; "
    "  border: 2px solid #3f3f46; "
    "  border-radius: 8px; "
    "}";

  QString labelStyle = "QLabel { font-size: 13px; color: #888888; border: none; background: transparent; }";
  QString valueStyle =
    "QLabel { "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "  font-family: monospace; "
    "  color: #d4d4d4; "
    "  border: none; "
    "  background: transparent; "
    "  min-width: 80px; "
    "}";

  std::vector<std::string> axisNames = getAxisNames();  // X, Y, Z, U, V, W

  // Create a vertical card for each device
  for (const auto& deviceName : piDevices) {
    bool isConnected = connectionStatus[deviceName];

    // Card container
    QWidget* deviceCard = new QWidget();
    deviceCard->setStyleSheet(cardStyle);
    deviceCard->setFixedWidth(200);  // Fixed width for consistent sizing

    QVBoxLayout* cardLayout = new QVBoxLayout(deviceCard);
    cardLayout->setContentsMargins(12, 12, 12, 12);
    cardLayout->setSpacing(8);

    // Device name header
    QLabel* nameLabel = new QLabel(QString::fromStdString(deviceName));
    nameLabel->setStyleSheet(
      "QLabel { "
      "  font-size: 16px; "
      "  font-weight: bold; "
      "  color: #14ffec; "
      "  border: none; "
      "  background: transparent; "
      "  padding-bottom: 5px; "
      "  border-bottom: 1px solid #3f3f46; "
      "}");
    cardLayout->addWidget(nameLabel);

    // Connection status row
    QHBoxLayout* connRow = new QHBoxLayout();
    QLabel* connLabel = new QLabel("Status:");
    connLabel->setStyleSheet(labelStyle);
    connRow->addWidget(connLabel);

    QLabel* connValue = new QLabel(isConnected ? "● Connected" : "● Disconnected");
    connValue->setStyleSheet(
      QString("QLabel { font-size: 13px; font-weight: bold; color: %1; border: none; background: transparent; }")
        .arg(isConnected ? "#27ae60" : "#e74c3c"));
    connRow->addWidget(connValue);
    connRow->addStretch();
    cardLayout->addLayout(connRow);

    // Separator
    QFrame* separator = new QFrame();
    separator->setFrameShape(QFrame::HLine);
    separator->setStyleSheet("QFrame { background-color: #3f3f46; max-height: 1px; border: none; }");
    cardLayout->addWidget(separator);

    // Initialize position labels map for this device
    m_positionLabels[deviceName] = std::map<std::string, QLabel*>();

    // Position rows - one row per axis (vertical layout)
    for (const auto& axis : axisNames) {
      QHBoxLayout* axisRow = new QHBoxLayout();

      QLabel* axisLabel = new QLabel(QString::fromStdString(axis) + ":");
      axisLabel->setStyleSheet(labelStyle);
      axisLabel->setFixedWidth(25);
      axisRow->addWidget(axisLabel);

      // Get position value
      QString posText = "--";
      if (isConnected) {
        double position = 0.0;
        if (motionMgr->GetPosition(deviceName, axis, position)) {
          posText = QString::number(position, 'f', 3);
        }
      }

      QLabel* posValue = new QLabel(posText);
      posValue->setStyleSheet(valueStyle);
      posValue->setAlignment(Qt::AlignRight);
      axisRow->addWidget(posValue);

      // Store reference for polling updates
      m_positionLabels[deviceName][axis] = posValue;

      cardLayout->addLayout(axisRow);
    }

    // Spacer
    cardLayout->addSpacing(5);

    // Connect/Disconnect button
    QPushButton* actionButton = new QPushButton(isConnected ? "Disconnect" : "Connect");
    actionButton->setStyleSheet(
      QString(
        "QPushButton { "
        "  background-color: %1; "
        "  color: white; "
        "  border: none; "
        "  border-radius: 4px; "
        "  padding: 8px 16px; "
        "  font-weight: bold; "
        "  font-size: 13px; "
        "} "
        "QPushButton:hover { "
        "  background-color: %2; "
        "}")
        .arg(isConnected ? "#e74c3c" : "#27ae60")
        .arg(isConnected ? "#c0392b" : "#229954"));

    // Capture device name for lambda
    std::string devName = deviceName;
    if (isConnected) {
      connect(actionButton, &QPushButton::clicked, this, [this, devName]() {
        onDisconnectClicked(devName);
        updateDeviceStatusDisplay();  // Refresh after action
      });
    } else {
      connect(actionButton, &QPushButton::clicked, this, [this, devName]() {
        onConnectClicked(devName);
        updateDeviceStatusDisplay();  // Refresh after action
      });
    }
    cardLayout->addWidget(actionButton);

    cardLayout->addStretch();

    // Add card to layout
    m_deviceStatusLayout->addWidget(deviceCard);
  }

  // Add stretch at end to keep cards left-aligned
  m_deviceStatusLayout->addStretch();
}

// Update positions in device status cards (called by poll timer)
void PIControllerPage::onPositionPollTimer()
{
  // Only update if Device Status tab is active (index 0)
  if (m_tabWidget && m_tabWidget->currentIndex() != 0) {
    return;
  }

  UnifiedMotionManager* motionMgr = getMotionManager();
  if (!motionMgr) return;

  std::map<std::string, bool> connectionStatus = motionMgr->GetConnectionStatus();

  // Update each device's position labels
  for (auto& [deviceName, axisLabels] : m_positionLabels) {
    bool isConnected = connectionStatus[deviceName];

    for (auto& [axis, label] : axisLabels) {
      if (!label) continue;

      if (!isConnected) {
        label->setText("--");
        continue;
      }

      double position = 0.0;
      if (motionMgr->GetPosition(deviceName, axis, position)) {
        label->setText(QString::number(position, 'f', 3));
      } else {
        label->setText("--");
      }
    }
  }
}

// Create pivot point tab
void PIControllerPage::createPivotPointTab()
{
  QWidget* pivotTab = new QWidget();
  QVBoxLayout* pivotTabLayout = new QVBoxLayout(pivotTab);
  pivotTabLayout->setContentsMargins(10, 10, 10, 10);
  pivotTabLayout->setSpacing(10);

  // Create pivot point display group
  QGroupBox* pivotGroup = new QGroupBox("📍 Pivot Point Settings (from Controller)");
  pivotGroup->setStyleSheet(
    "QGroupBox { "
    "  font-size: 16px; "
    "  font-weight: bold; "
    "  color: #ffffff; "
    "  background-color: #1e1e1e; "
    "  border: 2px solid #3f3f46; "
    "  border-radius: 8px; "
    "  margin-top: 10px; "
    "  padding-top: 15px; "
    "} "
    "QGroupBox::title { "
    "  subcontrol-origin: margin; "
    "  left: 10px; "
    "  padding: 0 5px; "
    "  color: #ffffff; "
    "}");

  QVBoxLayout* groupLayout = new QVBoxLayout(pivotGroup);
  groupLayout->setSpacing(10);

  // Create container widget for dynamic pivot point displays
  m_pivotDisplayWidget = new QWidget();
  m_pivotDisplayLayout = new QVBoxLayout(m_pivotDisplayWidget);
  m_pivotDisplayLayout->setSpacing(5);
  m_pivotDisplayLayout->setContentsMargins(0, 0, 0, 0);

  groupLayout->addWidget(m_pivotDisplayWidget);

  // Add note about updating pivot point
  QLabel* noteLabel = new QLabel("ℹ️ To update pivot point: Edit config/pivot_points.json and restart the program");
  noteLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 12px; "
    "  font-style: italic; "
    "  color: #f39c12; "
    "  background-color: #3c3c3c; "
    "  border: 1px solid #f39c12; "
    "  border-radius: 3px; "
    "  padding: 5px 10px; "
    "  margin-top: 5px; "
    "}");
  noteLabel->setWordWrap(true);
  groupLayout->addWidget(noteLabel);

  // Add pivot group to pivot tab layout
  pivotTabLayout->addWidget(pivotGroup);
  pivotTabLayout->addStretch();

  // Add tab to tab widget
  m_tabWidget->addTab(pivotTab, "Pivot Point Settings");

  // Initial update of pivot points
  updatePivotPointDisplay();
}

// Create analog input tab
void PIControllerPage::createAnalogInputTab()
{
  QWidget* analogTab = new QWidget();
  QVBoxLayout* analogTabLayout = new QVBoxLayout(analogTab);
  analogTabLayout->setContentsMargins(10, 10, 10, 10);
  analogTabLayout->setSpacing(10);

  // ===== FSM Alignment Settings Group =====
  m_fsmSettingsGroup = new QGroupBox("FSM Alignment Settings (Fast Scan Maximum)");
  m_fsmSettingsGroup->setStyleSheet(
    "QGroupBox { "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "  color: #14ffec; "
    "  background-color: #1e1e1e; "
    "  border: 2px solid #0e639c; "
    "  border-radius: 8px; "
    "  margin-top: 10px; "
    "  padding-top: 15px; "
    "} "
    "QGroupBox::title { "
    "  subcontrol-origin: margin; "
    "  left: 10px; "
    "  padding: 0 5px; "
    "  color: #14ffec; "
    "}");

  QVBoxLayout* fsmLayout = new QVBoxLayout(m_fsmSettingsGroup);
  fsmLayout->setSpacing(8);

  // Common spinbox stylesheet
  QString spinBoxStyle =
    "QDoubleSpinBox { "
    "  background-color: #3c3c3c; "
    "  color: #d4d4d4; "
    "  border: 1px solid #555555; "
    "  border-radius: 3px; "
    "  padding: 3px; "
    "  min-width: 70px; "
    "} "
    "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { "
    "  background-color: #2d2d30; "
    "  border: 1px solid #555555; "
    "} "
    "QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover { "
    "  background-color: #3c3c3c; "
    "}";

  QString comboBoxStyle =
    "QComboBox { "
    "  background-color: #3c3c3c; "
    "  color: #d4d4d4; "
    "  border: 1px solid #555555; "
    "  border-radius: 3px; "
    "  padding: 3px; "
    "  min-width: 50px; "
    "} "
    "QComboBox:hover { "
    "  border-color: #0e639c; "
    "} "
    "QComboBox QAbstractItemView { "
    "  background-color: #1e1e1e; "
    "  color: #d4d4d4; "
    "  selection-background-color: #0e639c; "
    "  border: 1px solid #3f3f46; "
    "}";

  QString labelStyle = "QLabel { font-size: 13px; font-weight: normal; color: #d4d4d4; }";

  // Row 1: Axis selectors and lengths
  QHBoxLayout* axisRow = new QHBoxLayout();

  QLabel* axis1Label = new QLabel("Axis 1:");
  axis1Label->setStyleSheet(labelStyle);
  axisRow->addWidget(axis1Label);

  m_fsmAxis1Selector = new QComboBox();
  m_fsmAxis1Selector->addItems({"X", "Y", "Z"});
  m_fsmAxis1Selector->setCurrentText("X");
  m_fsmAxis1Selector->setStyleSheet(comboBoxStyle);
  axisRow->addWidget(m_fsmAxis1Selector);

  QLabel* length1Label = new QLabel("Length:");
  length1Label->setStyleSheet(labelStyle);
  axisRow->addWidget(length1Label);

  m_fsmLength1SpinBox = new QDoubleSpinBox();
  m_fsmLength1SpinBox->setRange(0.001, 10.0);
  m_fsmLength1SpinBox->setValue(0.020);  // Default: 0.020 mm
  m_fsmLength1SpinBox->setDecimals(3);
  m_fsmLength1SpinBox->setSuffix(" mm");
  m_fsmLength1SpinBox->setStyleSheet(spinBoxStyle);
  axisRow->addWidget(m_fsmLength1SpinBox);

  axisRow->addSpacing(20);

  QLabel* axis2Label = new QLabel("Axis 2:");
  axis2Label->setStyleSheet(labelStyle);
  axisRow->addWidget(axis2Label);

  m_fsmAxis2Selector = new QComboBox();
  m_fsmAxis2Selector->addItems({"X", "Y", "Z"});
  m_fsmAxis2Selector->setCurrentText("Y");
  m_fsmAxis2Selector->setStyleSheet(comboBoxStyle);
  axisRow->addWidget(m_fsmAxis2Selector);

  QLabel* length2Label = new QLabel("Length:");
  length2Label->setStyleSheet(labelStyle);
  axisRow->addWidget(length2Label);

  m_fsmLength2SpinBox = new QDoubleSpinBox();
  m_fsmLength2SpinBox->setRange(0.001, 10.0);
  m_fsmLength2SpinBox->setValue(0.020);  // Default: 0.020 mm
  m_fsmLength2SpinBox->setDecimals(3);
  m_fsmLength2SpinBox->setSuffix(" mm");
  m_fsmLength2SpinBox->setStyleSheet(spinBoxStyle);
  axisRow->addWidget(m_fsmLength2SpinBox);

  axisRow->addStretch();
  fsmLayout->addLayout(axisRow);

  // Row 2: Threshold, Distance
  QHBoxLayout* paramRow = new QHBoxLayout();

  QLabel* thresholdLabel = new QLabel("Threshold:");
  thresholdLabel->setStyleSheet(labelStyle);
  paramRow->addWidget(thresholdLabel);

  m_fsmThresholdSpinBox = new QDoubleSpinBox();
  m_fsmThresholdSpinBox->setRange(0.0, 10.0);
  m_fsmThresholdSpinBox->setValue(0.01);
  m_fsmThresholdSpinBox->setDecimals(3);
  m_fsmThresholdSpinBox->setSuffix(" V");
  m_fsmThresholdSpinBox->setStyleSheet(spinBoxStyle);
  paramRow->addWidget(m_fsmThresholdSpinBox);

  paramRow->addSpacing(15);

  QLabel* distanceLabel = new QLabel("Line Dist:");
  distanceLabel->setStyleSheet(labelStyle);
  paramRow->addWidget(distanceLabel);

  m_fsmDistanceSpinBox = new QDoubleSpinBox();
  m_fsmDistanceSpinBox->setRange(0.001, 1.0);
  m_fsmDistanceSpinBox->setValue(0.001);  // Default: 0.001 mm
  m_fsmDistanceSpinBox->setDecimals(3);
  m_fsmDistanceSpinBox->setSuffix(" mm");
  m_fsmDistanceSpinBox->setStyleSheet(spinBoxStyle);
  paramRow->addWidget(m_fsmDistanceSpinBox);

  paramRow->addStretch();
  fsmLayout->addLayout(paramRow);

  // Row 3: Z-scan parameters (for 3D search)
  QHBoxLayout* zScanRow = new QHBoxLayout();

  QLabel* zDirLabel = new QLabel("Z Dir:");
  zDirLabel->setStyleSheet(labelStyle);
  zScanRow->addWidget(zDirLabel);

  m_zDirectionSelector = new QComboBox();
  m_zDirectionSelector->addItems({"Z+", "Z-"});
  m_zDirectionSelector->setCurrentText("Z-");
  m_zDirectionSelector->setStyleSheet(comboBoxStyle);
  zScanRow->addWidget(m_zDirectionSelector);

  zScanRow->addSpacing(15);

  QLabel* zRangeLabel = new QLabel("Z Range:");
  zRangeLabel->setStyleSheet(labelStyle);
  zScanRow->addWidget(zRangeLabel);

  m_zRangeSpinBox = new QDoubleSpinBox();
  m_zRangeSpinBox->setRange(0.001, 1.0);
  m_zRangeSpinBox->setValue(0.005);  // Default: 0.005 mm
  m_zRangeSpinBox->setDecimals(4);
  m_zRangeSpinBox->setSuffix(" mm");
  m_zRangeSpinBox->setStyleSheet(spinBoxStyle);
  zScanRow->addWidget(m_zRangeSpinBox);

  zScanRow->addSpacing(15);

  QLabel* zStepLabel = new QLabel("Z Step:");
  zStepLabel->setStyleSheet(labelStyle);
  zScanRow->addWidget(zStepLabel);

  m_zStepSpinBox = new QDoubleSpinBox();
  m_zStepSpinBox->setRange(0.0001, 0.1);
  m_zStepSpinBox->setValue(0.001);  // Default: 0.001 mm
  m_zStepSpinBox->setDecimals(4);
  m_zStepSpinBox->setSuffix(" mm");
  m_zStepSpinBox->setStyleSheet(spinBoxStyle);
  zScanRow->addWidget(m_zStepSpinBox);

  zScanRow->addStretch();
  fsmLayout->addLayout(zScanRow);

  // Z-scan progress display (hidden by default, shown during Z-scan)
  m_zScanProgressWidget = new QWidget();
  QHBoxLayout* progressLayout = new QHBoxLayout(m_zScanProgressWidget);
  progressLayout->setContentsMargins(0, 5, 0, 5);
  progressLayout->setSpacing(10);

  QLabel* progressLabel = new QLabel("Progress:");
  progressLabel->setStyleSheet(labelStyle);
  progressLayout->addWidget(progressLabel);

  m_zScanProgressBar = new QProgressBar();
  m_zScanProgressBar->setRange(0, 100);
  m_zScanProgressBar->setValue(0);
  m_zScanProgressBar->setTextVisible(true);
  m_zScanProgressBar->setFormat("%v / %m");
  m_zScanProgressBar->setStyleSheet(
    "QProgressBar { "
    "  background-color: #3c3c3c; "
    "  border: 1px solid #555555; "
    "  border-radius: 4px; "
    "  height: 20px; "
    "  text-align: center; "
    "  color: #ffffff; "
    "  min-width: 120px; "
    "} "
    "QProgressBar::chunk { "
    "  background-color: #27ae60; "
    "  border-radius: 3px; "
    "}");
  progressLayout->addWidget(m_zScanProgressBar);

  progressLayout->addSpacing(20);

  QLabel* peakLabel = new QLabel("Peak:");
  peakLabel->setStyleSheet(labelStyle);
  progressLayout->addWidget(peakLabel);

  m_zScanPeakLabel = new QLabel("-- V @ Z=-- mm");
  m_zScanPeakLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "  font-family: monospace; "
    "  color: #f1c40f; "
    "  background-color: #2d2d30; "
    "  border: 1px solid #f1c40f; "
    "  border-radius: 4px; "
    "  padding: 4px 10px; "
    "  min-width: 180px; "
    "}");
  progressLayout->addWidget(m_zScanPeakLabel);

  progressLayout->addStretch();

  m_zScanProgressWidget->setVisible(false);  // Hidden by default
  fsmLayout->addWidget(m_zScanProgressWidget);

  analogTabLayout->addWidget(m_fsmSettingsGroup);

  // ===== Analog Input Channels Group =====
  QGroupBox* analogGroup = new QGroupBox("Analog Input Channels");
  analogGroup->setStyleSheet(
    "QGroupBox { "
    "  font-size: 16px; "
    "  font-weight: bold; "
    "  color: #ffffff; "
    "  background-color: #1e1e1e; "
    "  border: 2px solid #3f3f46; "
    "  border-radius: 8px; "
    "  margin-top: 10px; "
    "  padding-top: 15px; "
    "} "
    "QGroupBox::title { "
    "  subcontrol-origin: margin; "
    "  left: 10px; "
    "  padding: 0 5px; "
    "  color: #ffffff; "
    "}");

  QVBoxLayout* groupLayout = new QVBoxLayout(analogGroup);
  groupLayout->setSpacing(10);

  // Device selector row
  QHBoxLayout* deviceRow = new QHBoxLayout();
  QLabel* deviceLabel = new QLabel("Device:");
  deviceLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: normal; color: #d4d4d4; }");
  deviceRow->addWidget(deviceLabel);

  m_analogDeviceSelector = new QComboBox();
  m_analogDeviceSelector->setMinimumWidth(200);
  m_analogDeviceSelector->setStyleSheet(
    "QComboBox { "
    "  background-color: #3c3c3c; "
    "  color: #d4d4d4; "
    "  border: 1px solid #555555; "
    "  border-radius: 4px; "
    "  padding: 5px; "
    "  font-size: 14px; "
    "} "
    "QComboBox:hover { "
    "  border-color: #0e639c; "
    "} "
    "QComboBox QAbstractItemView { "
    "  background-color: #1e1e1e; "
    "  color: #d4d4d4; "
    "  selection-background-color: #0e639c; "
    "  border: 1px solid #3f3f46; "
    "}");
  connect(m_analogDeviceSelector, &QComboBox::currentTextChanged, this, [this](const QString&) {
    updateAnalogDisplay();
  });
  deviceRow->addWidget(m_analogDeviceSelector);

  deviceRow->addSpacing(20);

  m_analogRefreshButton = new QPushButton("Refresh");
  m_analogRefreshButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #0e639c; "
    "  color: white; "
    "  border: none; "
    "  border-radius: 5px; "
    "  padding: 8px 15px; "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { background-color: #1177bb; }");
  connect(m_analogRefreshButton, &QPushButton::clicked, this, &PIControllerPage::onAnalogRefreshClicked);
  deviceRow->addWidget(m_analogRefreshButton);

  deviceRow->addStretch();

  groupLayout->addLayout(deviceRow);

  // Create container widget for analog channel values
  m_analogDisplayWidget = new QWidget();
  m_analogDisplayLayout = new QVBoxLayout(m_analogDisplayWidget);
  m_analogDisplayLayout->setSpacing(8);
  m_analogDisplayLayout->setContentsMargins(0, 10, 0, 0);

  groupLayout->addWidget(m_analogDisplayWidget);

  // Add note
  QLabel* noteLabel = new QLabel("ℹ️ Analog channels show voltage readings from PI controller ADC inputs");
  noteLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 12px; "
    "  font-style: italic; "
    "  color: #888888; "
    "  margin-top: 10px; "
    "}");
  noteLabel->setWordWrap(true);
  groupLayout->addWidget(noteLabel);

  analogTabLayout->addWidget(analogGroup);
  analogTabLayout->addStretch();

  // Add tab to tab widget
  m_tabWidget->addTab(analogTab, "Analog Inputs");

  // Setup poll timer (updates every 500ms when tab is visible)
  m_analogPollTimer = new QTimer(this);
  connect(m_analogPollTimer, &QTimer::timeout, this, &PIControllerPage::onAnalogPollTimer);

  // Populate device selector
  UnifiedMotionManager* motionMgr = getMotionManager();
  if (motionMgr) {
    std::vector<std::string> piDevices = motionMgr->GetDeviceNamesByType("PI");
    for (const auto& device : piDevices) {
      m_analogDeviceSelector->addItem(QString::fromStdString(device));
    }

    // Create FSM alignment executor (thread-safe execution)
    m_alignExecutor = new PIAlignExecutor(motionMgr, this);

    // Connect executor signals for thread-safe UI updates
    connect(m_alignExecutor, &PIAlignExecutor::alignmentStarted,
            this, &PIControllerPage::onAlignmentStarted, Qt::QueuedConnection);
    connect(m_alignExecutor, &PIAlignExecutor::alignmentCompleted,
            this, &PIControllerPage::onAlignmentCompleted, Qt::QueuedConnection);
    connect(m_alignExecutor, &PIAlignExecutor::alignmentFailed,
            this, &PIControllerPage::onAlignmentFailed, Qt::QueuedConnection);
    connect(m_alignExecutor, &PIAlignExecutor::busyStateChanged,
            this, &PIControllerPage::onAlignBusyStateChanged, Qt::QueuedConnection);
    connect(m_alignExecutor, &PIAlignExecutor::zScanProgress,
            this, &PIControllerPage::onZScanProgress, Qt::QueuedConnection);

    std::cout << "[PIControllerPage] FSM Executor created and connected" << std::endl;
  }
}

// Handle tab changes
// Tab indices: 0=Device Status, 1=Velocity Control, 2=Pivot Point, 3=Analog Inputs
void PIControllerPage::onTabChanged(int index)
{
  std::cout << "[PIControllerPage] Tab changed to index: " << index << std::endl;

  // If switching to Device Status tab (index 0), refresh the display
  if (index == 0) {
    updateDeviceStatusDisplay();
  }

  // If switching to pivot point tab (index 2), refresh the display
  if (index == 2) {
    updatePivotPointDisplay();
  }

  // If switching to analog input tab (index 3), start polling
  if (index == 3) {
    updateAnalogDisplay();
    if (m_analogPollTimer && !m_analogPollTimer->isActive()) {
      m_analogPollTimer->start(500);  // Poll every 500ms
    }
  } else {
    // Stop polling when not on analog tab
    if (m_analogPollTimer && m_analogPollTimer->isActive()) {
      m_analogPollTimer->stop();
    }
  }
}

void PIControllerPage::updateVelocityDisplay()
{
  UnifiedMotionManager* motionMgr = getMotionManager();
  if (!motionMgr) return;

  std::vector<std::string> devices = motionMgr->GetDeviceNamesByType("PI");

  m_deviceSelector->clear();
  for (const auto& device : devices) {
    m_deviceSelector->addItem(QString::fromStdString(device));
  }
}

void PIControllerPage::onDeviceSelected(const QString& deviceName)
{
  // Could load current velocity settings here if needed
  std::cout << "[PIControllerPage] Device selected: " << deviceName.toStdString() << std::endl;
}

void PIControllerPage::onPresetTurtleClicked()
{
  m_systemVelocity->setValue(5.0);
  std::cout << "[PIControllerPage] Turtle preset applied (5 mm/s)" << std::endl;
}

void PIControllerPage::onPresetRabbitClicked()
{
  m_systemVelocity->setValue(10.0);
  std::cout << "[PIControllerPage] Rabbit preset applied (10 mm/s)" << std::endl;
}

void PIControllerPage::onApplyVelocityClicked()
{
  UnifiedMotionManager* motionMgr = getMotionManager();
  if (!motionMgr) {
    std::cerr << "[PIControllerPage] No motion manager available!" << std::endl;
    QMessageBox::critical(this, "Error", "Motion manager not available!");
    return;
  }

  std::string deviceName = m_deviceSelector->currentText().toStdString();
  if (deviceName.empty()) {
    std::cerr << "[PIControllerPage] No device selected!" << std::endl;
    QMessageBox::warning(this, "Warning", "Please select a device first!");
    return;
  }

  double velocity = m_systemVelocity->value();

  // PI hexapods use system velocity command (not per-axis)
  bool success = motionMgr->SetSystemVelocity(deviceName, velocity);

  if (success) {
    std::cout << "[PIControllerPage] System velocity applied to " << deviceName
              << " - " << velocity << " mm/s (all axes)" << std::endl;

    QMessageBox::information(this, "Success",
      QString("System velocity set to %1 mm/s for %2")
        .arg(velocity, 0, 'f', 1)
        .arg(QString::fromStdString(deviceName)));
  }
  else {
    std::cerr << "[PIControllerPage] Failed to apply system velocity" << std::endl;
    QMessageBox::critical(this, "Error",
      QString("Failed to set velocity for %1\n\nPlease check:\n- Device is connected\n- Device is ready")
        .arg(QString::fromStdString(deviceName)));
  }
}

// ✅ Update pivot point display by reading from ALL PI controllers
void PIControllerPage::updatePivotPointDisplay()
{
  if (!m_pivotDisplayLayout) {
    return;
  }

  UnifiedMotionManager* motionMgr = getMotionManager();
  if (!motionMgr) {
    return;
  }

  // Clear existing displays
  QLayoutItem* item;
  while ((item = m_pivotDisplayLayout->takeAt(0)) != nullptr) {
    if (item->widget()) {
      delete item->widget();
    }
    delete item;
  }

  // Get all PI devices
  std::vector<std::string> piDevices = motionMgr->GetDeviceNamesByType("PI");

  if (piDevices.empty()) {
    QLabel* noDevicesLabel = new QLabel("No PI devices found");
    noDevicesLabel->setStyleSheet("QLabel { color: #e74c3c; font-size: 14px; font-style: italic; }");
    m_pivotDisplayLayout->addWidget(noDevicesLabel);
    return;
  }

  // Create display for each PI device
  for (const auto& deviceName : piDevices) {
    // Create horizontal layout for this device
    QHBoxLayout* deviceLayout = new QHBoxLayout();
    deviceLayout->setSpacing(15);

    // Device name label
    QLabel* deviceLabel = new QLabel(QString::fromStdString(deviceName) + ":");
    deviceLabel->setStyleSheet(
      "QLabel { "
      "  font-size: 14px; "
      "  font-weight: bold; "
      "  color: #ffffff; "
      "  min-width: 120px; "
      "}");
    deviceLayout->addWidget(deviceLabel);

    // Get PI controller
    PIController* piController = motionMgr->GetPIController(deviceName);

    if (!piController || !piController->IsConnected()) {
      QLabel* statusLabel = new QLabel("Not Connected");
      statusLabel->setStyleSheet("QLabel { color: #e74c3c; font-size: 14px; font-style: italic; }");
      deviceLayout->addWidget(statusLabel);
      deviceLayout->addStretch();
    }
    else {
      // Read pivot point from controller
      double pivotX = 0.0, pivotY = 0.0, pivotZ = 0.0;
      bool success = piController->GetPivotPoint(pivotX, pivotY, pivotZ);

      if (success) {
        // X value
        QLabel* xLabel = new QLabel("X:");
        xLabel->setStyleSheet("QLabel { font-size: 13px; font-weight: bold; color: #14ffec; }");
        deviceLayout->addWidget(xLabel);

        QLabel* xValue = new QLabel(QString::number(pivotX, 'f', 3) + " mm");
        xValue->setStyleSheet(
          "QLabel { "
          "  font-size: 13px; "
          "  font-weight: bold; "
          "  font-family: monospace; "
          "  color: #ffffff; "
          "  background-color: #3c3c3c; "
          "  border: 1px solid #555555; "
          "  border-radius: 3px; "
          "  padding: 3px 8px; "
          "  min-width: 70px; "
          "}");
        deviceLayout->addWidget(xValue);

        // Y value
        QLabel* yLabel = new QLabel("Y:");
        yLabel->setStyleSheet("QLabel { font-size: 13px; font-weight: bold; color: #14ffec; }");
        deviceLayout->addWidget(yLabel);

        QLabel* yValue = new QLabel(QString::number(pivotY, 'f', 3) + " mm");
        yValue->setStyleSheet(xValue->styleSheet());
        deviceLayout->addWidget(yValue);

        // Z value
        QLabel* zLabel = new QLabel("Z:");
        zLabel->setStyleSheet("QLabel { font-size: 13px; font-weight: bold; color: #14ffec; }");
        deviceLayout->addWidget(zLabel);

        QLabel* zValue = new QLabel(QString::number(pivotZ, 'f', 3) + " mm");
        zValue->setStyleSheet(xValue->styleSheet());
        deviceLayout->addWidget(zValue);

        deviceLayout->addStretch();

        std::cout << "📍 [PIControllerPage] Pivot point for " << deviceName
                  << ": X=" << pivotX << ", Y=" << pivotY << ", Z=" << pivotZ << std::endl;
      }
      else {
        QLabel* errorLabel = new QLabel("Read Failed");
        errorLabel->setStyleSheet("QLabel { color: #e74c3c; font-size: 14px; font-style: italic; }");
        deviceLayout->addWidget(errorLabel);
        deviceLayout->addStretch();

        std::cerr << "❌ [PIControllerPage] Failed to read pivot point from " << deviceName << std::endl;
      }
    }

    // Add this device's layout to the main display
    QWidget* deviceWidget = new QWidget();
    deviceWidget->setLayout(deviceLayout);
    deviceWidget->setStyleSheet(
      "QWidget { "
      "  background-color: #2d2d30; "
      "  border: 1px solid #3f3f46; "
      "  border-radius: 4px; "
      "  padding: 5px; "
      "}");
    m_pivotDisplayLayout->addWidget(deviceWidget);
  }

  m_pivotDisplayLayout->addStretch();
}

// Update analog channel display
void PIControllerPage::updateAnalogDisplay()
{
  if (!m_analogDisplayLayout || !m_analogDeviceSelector) {
    return;
  }

  UnifiedMotionManager* motionMgr = getMotionManager();
  if (!motionMgr) {
    return;
  }

  std::string deviceName = m_analogDeviceSelector->currentText().toStdString();
  if (deviceName.empty()) {
    return;
  }

  // Get PI controller
  PIController* piController = motionMgr->GetPIController(deviceName);

  if (!piController || !piController->IsConnected()) {
    // Clear existing and show not connected message
    QLayoutItem* item;
    while ((item = m_analogDisplayLayout->takeAt(0)) != nullptr) {
      if (item->widget()) delete item->widget();
      delete item;
    }
    m_analogValueLabels.clear();

    QLabel* errorLabel = new QLabel("Device not connected");
    errorLabel->setStyleSheet("QLabel { color: #e74c3c; font-size: 14px; font-style: italic; }");
    m_analogDisplayLayout->addWidget(errorLabel);
    return;
  }

  // Get number of analog channels
  int numChannels = 0;
  if (!piController->GetAnalogChannelCount(numChannels)) {
    // Clear and show error
    QLayoutItem* item;
    while ((item = m_analogDisplayLayout->takeAt(0)) != nullptr) {
      if (item->widget()) delete item->widget();
      delete item;
    }
    m_analogValueLabels.clear();

    QLabel* errorLabel = new QLabel("Failed to query analog channels");
    errorLabel->setStyleSheet("QLabel { color: #e74c3c; font-size: 14px; font-style: italic; }");
    m_analogDisplayLayout->addWidget(errorLabel);
    return;
  }

  // If we don't have labels created yet, create them
  if (m_analogValueLabels.empty()) {
    // Clear existing widgets first
    QLayoutItem* item;
    while ((item = m_analogDisplayLayout->takeAt(0)) != nullptr) {
      if (item->widget()) delete item->widget();
      delete item;
    }
    m_fsmAlignButtons.clear();
    m_zScanButtons.clear();

    // Header label
    QLabel* headerLabel = new QLabel(QString("Found %1 analog channel(s)").arg(numChannels));
    headerLabel->setStyleSheet("QLabel { font-size: 13px; color: #27ae60; font-weight: bold; margin-bottom: 5px; }");
    m_analogDisplayLayout->addWidget(headerLabel);

    // Create label for each channel
    for (int ch = 1; ch <= numChannels; ++ch) {
      QHBoxLayout* channelRow = new QHBoxLayout();
      channelRow->setSpacing(15);

      // Channel name
      QLabel* channelLabel = new QLabel(QString("Channel %1:").arg(ch));
      channelLabel->setStyleSheet(
        "QLabel { "
        "  font-size: 14px; "
        "  font-weight: bold; "
        "  color: #14ffec; "
        "  min-width: 100px; "
        "}");
      channelRow->addWidget(channelLabel);

      // Value label
      QLabel* valueLabel = new QLabel("-- V");
      valueLabel->setStyleSheet(
        "QLabel { "
        "  font-size: 16px; "
        "  font-weight: bold; "
        "  font-family: monospace; "
        "  color: #ffffff; "
        "  background-color: #3c3c3c; "
        "  border: 1px solid #555555; "
        "  border-radius: 4px; "
        "  padding: 5px 15px; "
        "  min-width: 100px; "
        "}");
      channelRow->addWidget(valueLabel);
      m_analogValueLabels[ch] = valueLabel;

      // Only show Align and Z Scan buttons for channels 5 and 6
      if (ch == 5 || ch == 6) {
        // FSM Align button
        QPushButton* alignButton = new QPushButton("Align");
        alignButton->setStyleSheet(
          "QPushButton { "
          "  background-color: #9b59b6; "
          "  color: white; "
          "  border: none; "
          "  border-radius: 4px; "
          "  padding: 5px 12px; "
          "  font-size: 13px; "
          "  font-weight: bold; "
          "} "
          "QPushButton:hover { background-color: #8e44ad; } "
          "QPushButton:disabled { background-color: #555555; color: #888888; }");
        alignButton->setToolTip(QString("Start FSM (Fast Scan Maximum) alignment using Channel %1").arg(ch));
        m_fsmAlignButtons[ch] = alignButton;

        // Capture channel value for lambda
        int channel = ch;
        connect(alignButton, &QPushButton::clicked, this, [this, channel]() {
          onFSMAlignClicked(channel);
        });
        channelRow->addWidget(alignButton);

        // Z Scan button (iterative FSM at multiple Z levels)
        QPushButton* zScanButton = new QPushButton("Z Scan");
        zScanButton->setStyleSheet(
          "QPushButton { "
          "  background-color: #e67e22; "
          "  color: white; "
          "  border: none; "
          "  border-radius: 4px; "
          "  padding: 5px 12px; "
          "  font-size: 13px; "
          "  font-weight: bold; "
          "} "
          "QPushButton:hover { background-color: #d35400; } "
          "QPushButton:disabled { background-color: #555555; color: #888888; }");
        zScanButton->setToolTip(QString("3D alignment: FSM at multiple Z levels using Channel %1").arg(ch));
        m_zScanButtons[ch] = zScanButton;

        connect(zScanButton, &QPushButton::clicked, this, [this, channel]() {
          onZScanClicked(channel);
        });
        channelRow->addWidget(zScanButton);
      }

      channelRow->addStretch();

      QWidget* rowWidget = new QWidget();
      rowWidget->setLayout(channelRow);
      m_analogDisplayLayout->addWidget(rowWidget);
    }

    m_analogDisplayLayout->addStretch();
  }

  // Read and update voltage values
  for (int ch = 1; ch <= numChannels; ++ch) {
    double voltage = 0.0;
    if (piController->GetAnalogVoltage(ch, voltage)) {
      if (m_analogValueLabels.count(ch)) {
        m_analogValueLabels[ch]->setText(QString("%1 V").arg(voltage, 0, 'f', 4));
      }
    } else {
      if (m_analogValueLabels.count(ch)) {
        m_analogValueLabels[ch]->setText("Read Error");
        m_analogValueLabels[ch]->setStyleSheet(
          m_analogValueLabels[ch]->styleSheet().replace("color: #ffffff", "color: #e74c3c"));
      }
    }
  }
}

void PIControllerPage::onAnalogRefreshClicked()
{
  // Clear cached labels to force rebuild
  m_analogValueLabels.clear();
  updateAnalogDisplay();
  std::cout << "[PIControllerPage] Analog display refreshed" << std::endl;
}

void PIControllerPage::onAnalogPollTimer()
{
  // Only update values, don't rebuild UI
  if (m_analogValueLabels.empty()) {
    updateAnalogDisplay();
    return;
  }

  UnifiedMotionManager* motionMgr = getMotionManager();
  if (!motionMgr) return;

  std::string deviceName = m_analogDeviceSelector->currentText().toStdString();
  if (deviceName.empty()) return;

  PIController* piController = motionMgr->GetPIController(deviceName);
  if (!piController || !piController->IsConnected()) return;

  // Update values only
  for (auto& [channel, label] : m_analogValueLabels) {
    double voltage = 0.0;
    if (piController->GetAnalogVoltage(channel, voltage)) {
      label->setText(QString("%1 V").arg(voltage, 0, 'f', 4));
      // Ensure normal styling
      if (!label->styleSheet().contains("color: #ffffff")) {
        label->setStyleSheet(
          "QLabel { "
          "  font-size: 16px; "
          "  font-weight: bold; "
          "  font-family: monospace; "
          "  color: #ffffff; "
          "  background-color: #3c3c3c; "
          "  border: 1px solid #555555; "
          "  border-radius: 4px; "
          "  padding: 5px 15px; "
          "  min-width: 100px; "
          "}");
      }
    }
  }
}

void PIControllerPage::onFSMAlignClicked(int channel)
{
  if (!m_alignExecutor) {
    std::cerr << "[PIControllerPage] Align executor not initialized!" << std::endl;
    QMessageBox::critical(this, "Error", "Alignment executor not available!");
    return;
  }

  if (m_alignExecutor->isRunning()) {
    QMessageBox::warning(this, "Alignment In Progress",
      "An alignment is already running. Please wait for it to complete.");
    return;
  }

  QString deviceName = m_analogDeviceSelector->currentText();
  if (deviceName.isEmpty()) {
    QMessageBox::warning(this, "Warning", "Please select a device first!");
    return;
  }

  // Validate axes are different
  QString axis1 = m_fsmAxis1Selector->currentText();
  QString axis2 = m_fsmAxis2Selector->currentText();
  if (axis1 == axis2) {
    QMessageBox::warning(this, "Invalid Parameters",
      "Axis 1 and Axis 2 must be different for 2D alignment!");
    return;
  }

  // Build FSM parameters from UI
  FSMParameters params;
  params.axis1 = axis1.toStdString();
  params.length1 = m_fsmLength1SpinBox->value();
  params.axis2 = axis2.toStdString();
  params.length2 = m_fsmLength2SpinBox->value();
  params.threshold = m_fsmThresholdSpinBox->value();
  params.distance = m_fsmDistanceSpinBox->value();
  params.analogChannel = channel;

  // Track which channel is being aligned
  m_currentAlignChannel = channel;

  std::cout << "[PIControllerPage] Requesting FSM alignment on " << deviceName.toStdString()
            << " using Channel " << channel << std::endl;

  // Start alignment in background thread
  m_alignExecutor->startAlignment(deviceName, params);
}

// ============================================================================
// FSM Executor Signal Handlers (called on UI thread via QueuedConnection)
// ============================================================================

void PIControllerPage::onAlignmentStarted(QString deviceName, int channel)
{
  std::cout << "[PIControllerPage] Alignment started on " << deviceName.toStdString()
            << " Channel " << channel << std::endl;

  // Disable the button and show status
  if (m_fsmAlignButtons.count(channel)) {
    m_fsmAlignButtons[channel]->setEnabled(false);
    m_fsmAlignButtons[channel]->setText("Aligning...");
  }

  // Disable all other align buttons while one is running
  for (auto& [ch, btn] : m_fsmAlignButtons) {
    if (ch != channel) {
      btn->setEnabled(false);
    }
  }
}

void PIControllerPage::onAlignmentCompleted(QString deviceName, int channel)
{
  std::cout << "[PIControllerPage] Alignment completed on " << deviceName.toStdString()
            << " Channel " << channel << std::endl;

  QMessageBox::information(this, "FSM Completed",
    QString("Fast Scan Maximum completed successfully!\n\n"
            "Device: %1\n"
            "Channel: %2\n\n"
            "The hexapod has moved to the position of maximum signal.")
      .arg(deviceName)
      .arg(channel));

  m_currentAlignChannel = 0;
}

void PIControllerPage::onAlignmentFailed(QString deviceName, int channel, QString errorMessage)
{
  std::cerr << "[PIControllerPage] Alignment failed on " << deviceName.toStdString()
            << " Channel " << channel << ": " << errorMessage.toStdString() << std::endl;

  QMessageBox::critical(this, "FSM Failed",
    QString("Fast Scan Maximum failed!\n\n"
            "Device: %1\n"
            "Channel: %2\n"
            "Error: %3")
      .arg(deviceName)
      .arg(channel)
      .arg(errorMessage));

  m_currentAlignChannel = 0;
}

void PIControllerPage::onAlignBusyStateChanged(bool busy)
{
  std::cout << "[PIControllerPage] Align busy state: " << (busy ? "busy" : "idle") << std::endl;

  // Re-enable all Align buttons when not busy
  for (auto& [channel, btn] : m_fsmAlignButtons) {
    btn->setEnabled(!busy);
    if (!busy) {
      btn->setText("Align");
    }
  }

  // Re-enable all Z Scan buttons when not busy
  for (auto& [channel, btn] : m_zScanButtons) {
    btn->setEnabled(!busy);
    if (!busy) {
      btn->setText("Z Scan");
    }
  }

  // Hide Z-scan progress widget when finished
  if (!busy && m_zScanProgressWidget) {
    // Keep it visible for a moment so user can see final result
    // It will be hidden when starting next scan
  }
}

void PIControllerPage::onZScanClicked(int channel)
{
  if (!m_alignExecutor) {
    std::cerr << "[PIControllerPage] Align executor not initialized!" << std::endl;
    QMessageBox::critical(this, "Error", "Alignment executor not available!");
    return;
  }

  if (m_alignExecutor->isRunning()) {
    QMessageBox::warning(this, "Alignment In Progress",
      "An alignment is already running. Please wait for it to complete.");
    return;
  }

  QString deviceName = m_analogDeviceSelector->currentText();
  if (deviceName.isEmpty()) {
    QMessageBox::warning(this, "Warning", "Please select a device first!");
    return;
  }

  // Validate axes are different
  QString axis1 = m_fsmAxis1Selector->currentText();
  QString axis2 = m_fsmAxis2Selector->currentText();
  if (axis1 == axis2) {
    QMessageBox::warning(this, "Invalid Parameters",
      "Axis 1 and Axis 2 must be different for 2D alignment!");
    return;
  }

  // Build FSM parameters with Z-scan enabled
  FSMParameters params;
  params.axis1 = axis1.toStdString();
  params.length1 = m_fsmLength1SpinBox->value();
  params.axis2 = axis2.toStdString();
  params.length2 = m_fsmLength2SpinBox->value();
  params.threshold = m_fsmThresholdSpinBox->value();
  params.distance = m_fsmDistanceSpinBox->value();
  params.analogChannel = channel;

  // Z-scan specific parameters
  params.zScanEnabled = true;
  params.zDirection = (m_zDirectionSelector->currentText() == "Z+") ? 1 : -1;
  params.zRange = m_zRangeSpinBox->value();
  params.zStep = m_zStepSpinBox->value();

  int numIterations = static_cast<int>(params.zRange / params.zStep) + 1;

  // Track which channel is being aligned
  m_currentAlignChannel = channel;

  std::cout << "[PIControllerPage] Requesting Z-scan FSM on " << deviceName.toStdString()
            << " using Channel " << channel << std::endl;
  std::cout << "  Z Direction: " << (params.zDirection > 0 ? "Z+" : "Z-") << std::endl;
  std::cout << "  Z Range: " << params.zRange << " mm" << std::endl;
  std::cout << "  Z Step: " << params.zStep << " mm" << std::endl;
  std::cout << "  Iterations: " << numIterations << std::endl;

  // Update button to show scanning
  if (m_zScanButtons.count(channel)) {
    m_zScanButtons[channel]->setText("Scanning...");
  }

  // Show progress widget for Z-scan
  if (m_zScanProgressWidget) {
    m_zScanProgressBar->setRange(0, numIterations);
    m_zScanProgressBar->setValue(0);
    m_zScanPeakLabel->setText("-- V @ Z=-- mm");
    m_zScanProgressWidget->setVisible(true);
  }

  // Start alignment in background thread
  m_alignExecutor->startAlignment(deviceName, params);
}

void PIControllerPage::onZScanProgress(QString deviceName, int iteration, int total, double currentZ, double peakZ, double peakVoltage)
{
  // Update progress bar
  if (m_zScanProgressBar) {
    m_zScanProgressBar->setRange(0, total);
    m_zScanProgressBar->setValue(iteration);
  }

  // Update peak value label
  if (m_zScanPeakLabel) {
    if (peakVoltage > -9990.0) {  // Check if we have a valid peak
      m_zScanPeakLabel->setText(QString("%1 V @ Z=%2 mm")
        .arg(peakVoltage, 0, 'f', 4)
        .arg(peakZ, 0, 'f', 4));
    }
  }
}