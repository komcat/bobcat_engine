// PIControllerPage.cpp
#include "PIControllerPage.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "pi_controller.h"  // ✅ NEW: For GetPivotPoint
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <iostream>

PIControllerPage::PIControllerPage(ApplicationContext* context, QWidget* parent)
  : MotionControllerPageBase(context, parent)
  , m_tabWidget(nullptr)  // ✅ NEW: Initialize tab widget
  , m_deviceSelector(nullptr)
  , m_systemVelocity(nullptr)
  , m_turtleButton(nullptr)
  , m_rabbitButton(nullptr)
  , m_applyButton(nullptr)
  , m_pivotDisplayWidget(nullptr)
  , m_pivotDisplayLayout(nullptr)
{
  std::cout << "[PIControllerPage] Initialized" << std::endl;

  // NOW safe to call virtual functions - derived class is constructed
  initialize();
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

void PIControllerPage::setupUI()
{
  // Call base class to setup the device table
  MotionControllerPageBase::setupUI();

  // Get main layout (created by base class)
  QVBoxLayout* mainLayout = qobject_cast<QVBoxLayout*>(layout());
  if (!mainLayout) return;

  // ✅ NEW: Create tab widget
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

  // Create tabs
  createMainTab();
  createPivotPointTab();

  // Connect tab change signal
  connect(m_tabWidget, &QTabWidget::currentChanged, this, &PIControllerPage::onTabChanged);

  // Insert tab widget before the device table (position 1)
  mainLayout->insertWidget(1, m_tabWidget);
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

// ✅ NEW: Create pivot point tab
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

// ✅ NEW: Handle tab changes
void PIControllerPage::onTabChanged(int index)
{
  std::cout << "[PIControllerPage] Tab changed to index: " << index << std::endl;

  // If switching to pivot point tab (index 1), refresh the display
  if (index == 1) {
    updatePivotPointDisplay();
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