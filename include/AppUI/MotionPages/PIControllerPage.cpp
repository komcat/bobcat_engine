// PIControllerPage.cpp
#include "PIControllerPage.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <iostream>

PIControllerPage::PIControllerPage(ApplicationContext* context, QWidget* parent)
  : MotionControllerPageBase(context, parent)
  , m_deviceSelector(nullptr)
  , m_systemVelocity(nullptr)
  , m_turtleButton(nullptr)
  , m_rabbitButton(nullptr)
  , m_applyButton(nullptr)
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

  // Add velocity controls
  createVelocityControls();
}

void PIControllerPage::createVelocityControls()
{
  // Get main layout (created by base class)
  QVBoxLayout* mainLayout = qobject_cast<QVBoxLayout*>(layout());
  if (!mainLayout) return;

  // Create velocity control group
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

  // Insert velocity group before the device table
  mainLayout->insertWidget(1, velocityGroup);

  // Initialize device list
  updateVelocityDisplay();
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