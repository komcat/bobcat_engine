// ACSControllerPage.cpp
#include "ACSControllerPage.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <iostream>

ACSControllerPage::ACSControllerPage(ApplicationContext* context, QWidget* parent)
  : MotionControllerPageBase(context, parent)
  , m_deviceSelector(nullptr)
  , m_velocityX(nullptr)
  , m_velocityY(nullptr)
  , m_velocityZ(nullptr)
  , m_turtleButton(nullptr)
  , m_rabbitButton(nullptr)
  , m_applyButton(nullptr)
{
  std::cout << "[ACSControllerPage] Initialized" << std::endl;

  // NOW safe to call virtual functions - derived class is constructed
  initialize();
}

std::string ACSControllerPage::getControllerType() const
{
  return "ACS";
}

std::vector<std::string> ACSControllerPage::getAxisNames() const
{
  // ACS can have X,Y,Z or just X,Y depending on config
  // Show all 3 columns - will display "--" for missing axes
  return { "X", "Y", "Z" };
}

QString ACSControllerPage::getPageTitle() const
{
  return "ACS Motion Controllers";
}

void ACSControllerPage::setupUI()
{
  // Call base class to setup the device table
  MotionControllerPageBase::setupUI();

  // Add velocity controls
  createVelocityControls();
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

  // Velocity spinboxes row
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

  // X axis
  QLabel* labelX = new QLabel("X Velocity:");
  labelX->setStyleSheet("QLabel { font-size: 14px; font-weight: normal; color: #d4d4d4; }");
  velocityRow->addWidget(labelX);

  m_velocityX = new QDoubleSpinBox();
  m_velocityX->setRange(1.0, 80.0);
  m_velocityX->setValue(10.0);
  m_velocityX->setSuffix(" mm/s");
  m_velocityX->setDecimals(1);
  m_velocityX->setStyleSheet(spinBoxStyle);
  velocityRow->addWidget(m_velocityX);

  velocityRow->addSpacing(20);

  // Y axis
  QLabel* labelY = new QLabel("Y Velocity:");
  labelY->setStyleSheet("QLabel { font-size: 14px; font-weight: normal; color: #d4d4d4; }");
  velocityRow->addWidget(labelY);

  m_velocityY = new QDoubleSpinBox();
  m_velocityY->setRange(1.0, 80.0);
  m_velocityY->setValue(10.0);
  m_velocityY->setSuffix(" mm/s");
  m_velocityY->setDecimals(1);
  m_velocityY->setStyleSheet(spinBoxStyle);
  velocityRow->addWidget(m_velocityY);

  velocityRow->addSpacing(20);

  // Z axis
  QLabel* labelZ = new QLabel("Z Velocity:");
  labelZ->setStyleSheet("QLabel { font-size: 14px; font-weight: normal; color: #d4d4d4; }");
  velocityRow->addWidget(labelZ);

  m_velocityZ = new QDoubleSpinBox();
  m_velocityZ->setRange(1.0, 80.0);
  m_velocityZ->setValue(10.0);
  m_velocityZ->setSuffix(" mm/s");
  m_velocityZ->setDecimals(1);
  m_velocityZ->setStyleSheet(spinBoxStyle);
  velocityRow->addWidget(m_velocityZ);

  velocityRow->addStretch();

  groupLayout->addLayout(velocityRow);

  // Preset buttons and apply button row
  QHBoxLayout* buttonRow = new QHBoxLayout();

  QLabel* presetLabel = new QLabel("Quick Presets:");
  presetLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: normal; color: #d4d4d4; }");
  buttonRow->addWidget(presetLabel);

  m_turtleButton = new QPushButton("🐢 Turtle (10 mm/s)");
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

  m_rabbitButton = new QPushButton("🐇 Rabbit (50 mm/s)");
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
}

void ACSControllerPage::onDeviceSelected(const QString& deviceName)
{
  // Could load current velocity settings here if needed
  std::cout << "[ACSControllerPage] Device selected: " << deviceName.toStdString() << std::endl;
}

void ACSControllerPage::onVelocityChanged()
{
  // Optional: Auto-apply on change (currently using Apply button)
}

void ACSControllerPage::onPresetTurtleClicked()
{
  m_velocityX->setValue(10.0);
  m_velocityY->setValue(10.0);
  m_velocityZ->setValue(10.0);
  std::cout << "[ACSControllerPage] Turtle preset applied (10 mm/s)" << std::endl;
}

void ACSControllerPage::onPresetRabbitClicked()
{
  m_velocityX->setValue(50.0);
  m_velocityY->setValue(50.0);
  m_velocityZ->setValue(50.0);
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

  double velX = m_velocityX->value();
  double velY = m_velocityY->value();
  double velZ = m_velocityZ->value();

  bool successX = motionMgr->SetVelocity(deviceName, "X", velX);
  bool successY = motionMgr->SetVelocity(deviceName, "Y", velY);
  bool successZ = motionMgr->SetVelocity(deviceName, "Z", velZ);

  if (successX && successY && successZ) {
    std::cout << "[ACSControllerPage] Velocity applied to " << deviceName
              << " - X:" << velX << " Y:" << velY << " Z:" << velZ << " mm/s" << std::endl;

    QMessageBox::information(this, "Success",
      QString("Velocity set successfully for %1:\n\n"
              "X: %2 mm/s\n"
              "Y: %3 mm/s\n"
              "Z: %4 mm/s")
        .arg(QString::fromStdString(deviceName))
        .arg(velX, 0, 'f', 1)
        .arg(velY, 0, 'f', 1)
        .arg(velZ, 0, 'f', 1));
  }
  else {
    std::cerr << "[ACSControllerPage] Failed to apply velocity (X:"
              << (successX ? "OK" : "FAIL") << " Y:" << (successY ? "OK" : "FAIL")
              << " Z:" << (successZ ? "OK" : "FAIL") << ")" << std::endl;

    QString errorMsg = QString("Failed to set velocity for %1:\n\n").arg(QString::fromStdString(deviceName));
    if (!successX) errorMsg += "✗ X axis failed\n";
    if (!successY) errorMsg += "✗ Y axis failed\n";
    if (!successZ) errorMsg += "✗ Z axis failed\n";
    errorMsg += "\nPlease check:\n- Device is connected\n- Device is ready";

    QMessageBox::critical(this, "Error", errorMsg);
  }
}