// ACSControllerPage.cpp
#include "ACSControllerPage.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <iostream>
#include <sstream>

ACSControllerPage::ACSControllerPage(ApplicationContext* context, QWidget* parent)
  : MotionControllerPageBase(context, parent)
  , m_deviceSelector(nullptr)
  , m_turtleButton(nullptr)
  , m_rabbitButton(nullptr)
  , m_applyButton(nullptr)
  , m_axisStatusTimer(nullptr)
{
  std::cout << "[ACSControllerPage] Initialized" << std::endl;

  // Store button styles for later use
  m_enabledStyle =
    "QPushButton { "
    "  background-color: #27ae60; "
    "  color: white; "
    "  border: none; "
    "  border-radius: 5px; "
    "  padding: 10px 20px; "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "  min-width: 120px; "
    "} "
    "QPushButton:hover { background-color: #229954; }";

  m_disabledStyle =
    "QPushButton { "
    "  background-color: #e74c3c; "
    "  color: white; "
    "  border: none; "
    "  border-radius: 5px; "
    "  padding: 10px 20px; "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "  min-width: 120px; "
    "} "
    "QPushButton:hover { background-color: #c0392b; }";

  // NOW safe to call virtual functions - derived class is constructed
  initialize();
}

std::string ACSControllerPage::getControllerType() const
{
  return "ACS";
}

std::vector<std::string> ACSControllerPage::getAxisNames() const
{
  // Return axes for the currently selected device, or default X,Y,Z
  std::string deviceName = m_deviceSelector ? m_deviceSelector->currentText().toStdString() : "";
  if (!deviceName.empty()) {
    return const_cast<ACSControllerPage*>(this)->getAvailableAxesForDevice(deviceName);
  }
  return { "X", "Y", "Z" };
}

QString ACSControllerPage::getPageTitle() const
{
  return "ACS Motion Controllers";
}

std::vector<std::string> ACSControllerPage::getAvailableAxesForDevice(const std::string& deviceName)
{
  std::vector<std::string> axes;

  UnifiedMotionManager* motionMgr = getMotionManager();
  if (!motionMgr || deviceName.empty()) {
    return axes;
  }

  std::string installedAxes = motionMgr->GetInstalledAxes(deviceName);
  if (installedAxes.empty()) {
    return axes;
  }

  // Parse space-separated axes string (e.g., "X Y Z" or "Y")
  std::istringstream iss(installedAxes);
  std::string axis;
  while (iss >> axis) {
    axes.push_back(axis);
  }

  return axes;
}

void ACSControllerPage::setupUI()
{
  // Call base class to setup the device table
  MotionControllerPageBase::setupUI();

  // Add velocity controls
  createVelocityControls();

  // Add axis enable/disable controls
  createAxisControlGroup();
}

void ACSControllerPage::createVelocityControls()
{
  // Get main layout (created by base class)
  QVBoxLayout* mainLayout = qobject_cast<QVBoxLayout*>(layout());
  if (!mainLayout) return;

  // Create velocity control group
  QGroupBox* velocityGroup = new QGroupBox("Axis Velocity Control");
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
  connect(m_deviceSelector, &QComboBox::currentTextChanged, this, &ACSControllerPage::onDeviceSelected);
  deviceRow->addWidget(m_deviceSelector);
  deviceRow->addStretch();

  groupLayout->addLayout(deviceRow);

  // Velocity spinboxes row - create all possible axes
  QHBoxLayout* velocityRow = new QHBoxLayout();

  QString spinBoxStyle =
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
    "}";

  QString labelStyle = "QLabel { font-size: 14px; font-weight: normal; color: #d4d4d4; }";

  // Create widgets for all possible axes (X, Y, Z, U, V, W)
  std::vector<std::string> allAxes = {"X", "Y", "Z", "U", "V", "W"};
  for (const auto& axis : allAxes) {
    QLabel* label = new QLabel(QString::fromStdString(axis + " Velocity:"));
    label->setStyleSheet(labelStyle);
    label->setVisible(false);  // Initially hidden
    m_velocityLabels[axis] = label;
    velocityRow->addWidget(label);

    QDoubleSpinBox* spinBox = new QDoubleSpinBox();
    spinBox->setRange(1.0, 80.0);
    spinBox->setValue(10.0);
    spinBox->setSuffix(" mm/s");
    spinBox->setDecimals(1);
    spinBox->setStyleSheet(spinBoxStyle);
    spinBox->setVisible(false);  // Initially hidden
    m_velocitySpinBoxes[axis] = spinBox;
    velocityRow->addWidget(spinBox);

    velocityRow->addSpacing(20);
  }

  velocityRow->addStretch();
  groupLayout->addLayout(velocityRow);

  // Preset buttons and apply button row
  QHBoxLayout* buttonRow = new QHBoxLayout();

  QLabel* presetLabel = new QLabel("Quick Presets:");
  presetLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: normal; color: #d4d4d4; }");
  buttonRow->addWidget(presetLabel);

  m_turtleButton = new QPushButton("Turtle (10 mm/s)");
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
  connect(m_turtleButton, &QPushButton::clicked, this, &ACSControllerPage::onPresetTurtleClicked);
  buttonRow->addWidget(m_turtleButton);

  m_rabbitButton = new QPushButton("Rabbit (50 mm/s)");
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
  connect(m_rabbitButton, &QPushButton::clicked, this, &ACSControllerPage::onPresetRabbitClicked);
  buttonRow->addWidget(m_rabbitButton);

  buttonRow->addStretch();

  m_applyButton = new QPushButton("Apply Velocity");
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
  connect(m_applyButton, &QPushButton::clicked, this, &ACSControllerPage::onApplyVelocityClicked);
  buttonRow->addWidget(m_applyButton);

  groupLayout->addLayout(buttonRow);

  // Insert velocity group before the device table
  mainLayout->insertWidget(1, velocityGroup);

  // Initialize device list
  updateVelocityDisplay();
}

void ACSControllerPage::updateVelocityDisplay()
{
  UnifiedMotionManager* motionMgr = getMotionManager();
  if (!motionMgr) return;

  std::vector<std::string> devices = motionMgr->GetDeviceNamesByType("ACS");

  m_deviceSelector->clear();
  for (const auto& device : devices) {
    m_deviceSelector->addItem(QString::fromStdString(device));
  }

  // Update UI for the first device if any
  if (!devices.empty()) {
    updateUIForSelectedDevice();
  }
}

void ACSControllerPage::onDeviceSelected(const QString& deviceName)
{
  std::cout << "[ACSControllerPage] Device selected: " << deviceName.toStdString() << std::endl;
  updateUIForSelectedDevice();
}

void ACSControllerPage::updateUIForSelectedDevice()
{
  std::string deviceName = m_deviceSelector->currentText().toStdString();
  std::vector<std::string> availableAxes = getAvailableAxesForDevice(deviceName);

  // Hide all velocity widgets first
  for (auto& [axis, label] : m_velocityLabels) {
    label->setVisible(false);
  }
  for (auto& [axis, spinBox] : m_velocitySpinBoxes) {
    spinBox->setVisible(false);
  }

  // Show only available axes
  for (const auto& axis : availableAxes) {
    if (m_velocityLabels.count(axis)) {
      m_velocityLabels[axis]->setVisible(true);
    }
    if (m_velocitySpinBoxes.count(axis)) {
      m_velocitySpinBoxes[axis]->setVisible(true);
    }
  }

  // Hide all enable buttons first
  for (auto& [axis, btn] : m_enableButtons) {
    btn->setVisible(false);
  }

  // Show only available axes
  for (const auto& axis : availableAxes) {
    if (m_enableButtons.count(axis)) {
      m_enableButtons[axis]->setVisible(true);
    }
  }

  std::cout << "[ACSControllerPage] Updated UI for device: " << deviceName
            << " (axes: ";
  for (size_t i = 0; i < availableAxes.size(); ++i) {
    std::cout << availableAxes[i];
    if (i < availableAxes.size() - 1) std::cout << ", ";
  }
  std::cout << ")" << std::endl;
}

void ACSControllerPage::onVelocityChanged()
{
  // Optional: Auto-apply on change (currently using Apply button)
}

void ACSControllerPage::onPresetTurtleClicked()
{
  // Set velocity for all visible spinboxes
  for (auto& [axis, spinBox] : m_velocitySpinBoxes) {
    if (spinBox->isVisible()) {
      spinBox->setValue(10.0);
    }
  }
  std::cout << "[ACSControllerPage] Turtle preset applied (10 mm/s)" << std::endl;
}

void ACSControllerPage::onPresetRabbitClicked()
{
  // Set velocity for all visible spinboxes
  for (auto& [axis, spinBox] : m_velocitySpinBoxes) {
    if (spinBox->isVisible()) {
      spinBox->setValue(50.0);
    }
  }
  std::cout << "[ACSControllerPage] Rabbit preset applied (50 mm/s)" << std::endl;
}

void ACSControllerPage::onApplyVelocityClicked()
{
  UnifiedMotionManager* motionMgr = getMotionManager();
  if (!motionMgr) {
    std::cerr << "[ACSControllerPage] No motion manager available!" << std::endl;
    QMessageBox::critical(this, "Error", "Motion manager not available!");
    return;
  }

  std::string deviceName = m_deviceSelector->currentText().toStdString();
  if (deviceName.empty()) {
    std::cerr << "[ACSControllerPage] No device selected!" << std::endl;
    QMessageBox::warning(this, "Warning", "Please select a device first!");
    return;
  }

  std::vector<std::string> availableAxes = getAvailableAxesForDevice(deviceName);

  bool allSuccess = true;
  QString successMsg = QString("Velocity set successfully for %1:\n\n").arg(QString::fromStdString(deviceName));
  QString failMsg;

  for (const auto& axis : availableAxes) {
    if (m_velocitySpinBoxes.count(axis) && m_velocitySpinBoxes[axis]->isVisible()) {
      double velocity = m_velocitySpinBoxes[axis]->value();
      bool success = motionMgr->SetVelocity(deviceName, axis, velocity);

      if (success) {
        successMsg += QString("%1: %2 mm/s\n").arg(QString::fromStdString(axis)).arg(velocity, 0, 'f', 1);
        std::cout << "[ACSControllerPage] Velocity applied to " << deviceName
                  << " - " << axis << ": " << velocity << " mm/s" << std::endl;
      } else {
        allSuccess = false;
        failMsg += QString("- %1 axis failed\n").arg(QString::fromStdString(axis));
        std::cerr << "[ACSControllerPage] Failed to apply velocity for " << axis << std::endl;
      }
    }
  }

  if (allSuccess) {
    QMessageBox::information(this, "Success", successMsg);
  } else {
    QMessageBox::critical(this, "Error",
      QString("Failed to set velocity for %1:\n\n%2\nPlease check:\n- Device is connected\n- Device is ready")
        .arg(QString::fromStdString(deviceName))
        .arg(failMsg));
  }
}

void ACSControllerPage::createAxisControlGroup()
{
  QVBoxLayout* mainLayout = qobject_cast<QVBoxLayout*>(layout());
  if (!mainLayout) return;

  // Create axis control group
  QGroupBox* axisGroup = new QGroupBox("Axis Enable/Disable");
  axisGroup->setStyleSheet(
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

  QHBoxLayout* axisLayout = new QHBoxLayout(axisGroup);
  axisLayout->setSpacing(20);

  // Create buttons for all possible axes (X, Y, Z, U, V, W)
  std::vector<std::string> allAxes = {"X", "Y", "Z", "U", "V", "W"};
  for (const auto& axis : allAxes) {
    QPushButton* btn = new QPushButton(QString::fromStdString(axis + ": --"));
    btn->setStyleSheet(m_disabledStyle);
    btn->setProperty("axis", QString::fromStdString(axis));
    btn->setProperty("servoOn", false);
    btn->setVisible(false);  // Initially hidden

    connect(btn, &QPushButton::clicked, this, [this, axis]() {
      if (m_enableButtons.count(axis)) {
        bool currentState = m_enableButtons[axis]->property("servoOn").toBool();
        onAxisEnableClicked(QString::fromStdString(axis), !currentState);
      }
    });

    m_enableButtons[axis] = btn;
    axisLayout->addWidget(btn);
  }

  axisLayout->addStretch();

  // Insert axis control group after velocity controls (index 2)
  mainLayout->insertWidget(2, axisGroup);

  // Setup timer to periodically update axis status
  m_axisStatusTimer = new QTimer(this);
  connect(m_axisStatusTimer, &QTimer::timeout, this, &ACSControllerPage::updateAxisControlButtons);
  m_axisStatusTimer->start(500);  // Update every 500ms
}

void ACSControllerPage::updateAxisControlButtons()
{
  UnifiedMotionManager* motionMgr = getMotionManager();
  if (!motionMgr) return;

  std::string deviceName = m_deviceSelector->currentText().toStdString();
  if (deviceName.empty()) {
    // No device selected - show "--" for visible buttons
    for (auto& [axis, btn] : m_enableButtons) {
      if (btn->isVisible()) {
        btn->setText(QString::fromStdString(axis + ": --"));
      }
    }
    return;
  }

  // Check connection status
  std::map<std::string, bool> status = motionMgr->GetConnectionStatus();
  if (!status[deviceName]) {
    // Not connected - show "N/C" for visible buttons
    for (auto& [axis, btn] : m_enableButtons) {
      if (btn->isVisible()) {
        btn->setText(QString::fromStdString(axis + ": N/C"));
      }
    }
    return;
  }

  // Get available axes for this device
  std::vector<std::string> availableAxes = getAvailableAxesForDevice(deviceName);

  // Only query status for available axes
  for (const auto& axis : availableAxes) {
    if (m_enableButtons.count(axis) && m_enableButtons[axis]->isVisible()) {
      QPushButton* btn = m_enableButtons[axis];
      bool isServoOn = motionMgr->IsServoEnabled(deviceName, axis);
      btn->setProperty("servoOn", isServoOn);

      if (isServoOn) {
        btn->setText(QString::fromStdString(axis + ": Enabled"));
        btn->setStyleSheet(m_enabledStyle);
      } else {
        btn->setText(QString::fromStdString(axis + ": Disabled"));
        btn->setStyleSheet(m_disabledStyle);
      }
    }
  }
}

void ACSControllerPage::onAxisEnableClicked(const QString& axis, bool enable)
{
  UnifiedMotionManager* motionMgr = getMotionManager();
  if (!motionMgr) {
    QMessageBox::critical(this, "Error", "Motion manager not available!");
    return;
  }

  std::string deviceName = m_deviceSelector->currentText().toStdString();
  if (deviceName.empty()) {
    QMessageBox::warning(this, "Warning", "Please select a device first!");
    return;
  }

  std::cout << "[ACSControllerPage] " << (enable ? "Enabling" : "Disabling")
            << " axis " << axis.toStdString() << " on " << deviceName << std::endl;

  bool success = motionMgr->EnableServo(deviceName, axis.toStdString(), enable);

  if (success) {
    std::cout << "[ACSControllerPage] Axis " << axis.toStdString()
              << " " << (enable ? "enabled" : "disabled") << " successfully" << std::endl;

    // Delay before updating UI to allow controller state to settle
    QTimer::singleShot(100, this, &ACSControllerPage::updateAxisControlButtons);
  }
  else {
    std::cerr << "[ACSControllerPage] Failed to " << (enable ? "enable" : "disable")
              << " axis " << axis.toStdString() << std::endl;
    QMessageBox::critical(this, "Error",
      QString("Failed to %1 axis %2.\n\nPossible causes:\n"
              "- Motor fault present\n"
              "- Hardware safety interlock\n"
              "- Motor in motion\n"
              "- Check ACS controller logs")
        .arg(enable ? "enable" : "disable")
        .arg(axis));

    // Still update the buttons to show actual state
    QTimer::singleShot(100, this, &ACSControllerPage::updateAxisControlButtons);
  }
}
