// ManualAdjustWidget.cpp
#include "ManualAdjustWidget.h"
#include "unified_motion_manager.h"
#include "TransformationMatrixLoader.h"
#include "CoordinateTransformer.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QTimer>
#include <cmath>
#include <iostream>

ManualAdjustWidget::ManualAdjustWidget(
  UnifiedMotionManager* motionManager,
  TransformationMatrixLoader* matrixLoader,
  QWidget* parent)
  : QWidget(parent)
  , m_motionManager(motionManager)
  , m_matrixLoader(matrixLoader)
  , m_stepSizeUm(10.0)
  , m_enableX(true)
  , m_enableY(true)
  , m_enableZ(true)
{
  setupUI();

  // Create position update timer
  m_positionTimer = new QTimer(this);
  connect(m_positionTimer, &QTimer::timeout, this, &ManualAdjustWidget::onUpdatePosition);
}

void ManualAdjustWidget::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Create centered container
  QWidget* container = new QWidget();
  container->setMaximumWidth(900);
  container->setStyleSheet(
    "QWidget { "
    "  background-color: #1e1e1e; "  // ✅ Dark theme background
    "  border: 2px solid #0e639c; "  // ✅ Dark theme border (blue accent)
    "}");

  QVBoxLayout* containerLayout = new QVBoxLayout(container);
  containerLayout->setContentsMargins(20, 15, 20, 15);
  containerLayout->setSpacing(10);

  // Title
  QLabel* titleLabel = new QLabel("Manual Adjustment Required");
  titleLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 16px; "
    "  font-weight: bold; "
    "  color: #0e639c; "  // ✅ Dark theme blue accent
    "  background: transparent; "
    "  border: none; "
    "}");
  containerLayout->addWidget(titleLabel);

  // Message
  m_messageLabel = new QLabel("Please adjust the position as needed");
  m_messageLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 13px; "
    "  color: #d4d4d4; "  // ✅ Dark theme secondary text
    "  background: transparent; "
    "  border: none; "
    "  padding: 5px 0px; "
    "}");
  m_messageLabel->setWordWrap(true);
  containerLayout->addWidget(m_messageLabel);

  // Device and step size info
  QHBoxLayout* infoLayout = new QHBoxLayout();

  m_deviceLabel = new QLabel("Device: gantry-main");
  m_deviceLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 12px; "
    "  font-weight: bold; "
    "  color: #1177bb; "  // ✅ Dark theme blue
    "  background: transparent; "
    "  border: none; "
    "}");
  infoLayout->addWidget(m_deviceLabel);

  infoLayout->addSpacing(15);

  m_stepSizeLabel = new QLabel("Step: 10 μm");
  m_stepSizeLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 12px; "
    "  font-weight: bold; "
    "  color: #27ae60; "  // ✅ Dark theme green
    "  background: transparent; "
    "  border: none; "
    "}");
  infoLayout->addWidget(m_stepSizeLabel);

  infoLayout->addStretch();
  containerLayout->addLayout(infoLayout);

  // Current position display
  m_positionLabel = new QLabel("Position: X: 0.000 | Y: 0.000 | Z: 0.000 mm");
  m_positionLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 12px; "
    "  color: #ffffff; "  // ✅ Dark theme white text
    "  background: #3c3c3c; "  // ✅ Dark theme input background
    "  border: 1px solid #555555; "  // ✅ Dark theme border
    "  padding: 5px; "
    "}");
  containerLayout->addWidget(m_positionLabel);

  // Jog controls label
  QLabel* jogLabel = new QLabel("Jog Controls:");
  jogLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 12px; "
    "  font-weight: bold; "
    "  color: #ffffff; "  // ✅ Dark theme white text
    "  background: transparent; "
    "  border: none; "
    "  margin-top: 3px; "
    "}");
  containerLayout->addWidget(jogLabel);

  // Jog controls - Single row layout
  QHBoxLayout* jogLayout = new QHBoxLayout();
  jogLayout->setSpacing(5);

  // Minimal button style
  QString buttonStyle =
    "QPushButton { "
    "  background-color: #0e639c; "  // ✅ Dark theme primary blue
    "  color: white; "
    "  font-size: 12px; "
    "  font-weight: normal; "
    "  border: 1px solid #0c5280; "
    "  padding: 5px 10px; "
    "  min-width: 60px; "
    "  min-height: 30px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #1177bb; "  // ✅ Dark theme primary hover
    "} "
    "QPushButton:pressed { "
    "  background-color: #005a9e; "  // ✅ Dark theme pressed
    "} "
    "QPushButton:disabled { "
    "  background-color: #555555; "  // ✅ Dark theme disabled
    "  color: #888888; "
    "  border: 1px solid #444444; "
    "}";

  // Create jog buttons with minimal text
  m_btnXMinus = new QPushButton("← X-");
  m_btnXMinus->setStyleSheet(buttonStyle);
  connect(m_btnXMinus, &QPushButton::clicked, this, &ManualAdjustWidget::onJogXMinus);

  m_btnXPlus = new QPushButton("X+ →");
  m_btnXPlus->setStyleSheet(buttonStyle);
  connect(m_btnXPlus, &QPushButton::clicked, this, &ManualAdjustWidget::onJogXPlus);

  m_btnYMinus = new QPushButton("⊗ Y- In");
  m_btnYMinus->setStyleSheet(buttonStyle);
  connect(m_btnYMinus, &QPushButton::clicked, this, &ManualAdjustWidget::onJogYMinus);

  m_btnYPlus = new QPushButton("⊙ Y+ Out");
  m_btnYPlus->setStyleSheet(buttonStyle);
  connect(m_btnYPlus, &QPushButton::clicked, this, &ManualAdjustWidget::onJogYPlus);

  m_btnZMinus = new QPushButton("↓ Z-");
  m_btnZMinus->setStyleSheet(buttonStyle);
  connect(m_btnZMinus, &QPushButton::clicked, this, &ManualAdjustWidget::onJogZMinus);

  m_btnZPlus = new QPushButton("Z+ ↑");
  m_btnZPlus->setStyleSheet(buttonStyle);
  connect(m_btnZPlus, &QPushButton::clicked, this, &ManualAdjustWidget::onJogZPlus);

  // Add buttons to horizontal layout
  jogLayout->addWidget(m_btnXMinus);
  jogLayout->addWidget(m_btnXPlus);
  jogLayout->addWidget(m_btnYMinus);
  jogLayout->addWidget(m_btnYPlus);
  jogLayout->addWidget(m_btnZMinus);
  jogLayout->addWidget(m_btnZPlus);

  containerLayout->addLayout(jogLayout);

  containerLayout->addSpacing(10);

  // OK / Cancel buttons
  QHBoxLayout* actionLayout = new QHBoxLayout();
  actionLayout->setSpacing(10);

  m_okButton = new QPushButton("✓ OK (Complete)");
  m_okButton->setMinimumHeight(35);
  m_okButton->setMinimumWidth(150);
  m_okButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #27ae60; "  // ✅ Dark theme success green
    "  color: white; "
    "  font-size: 13px; "
    "  font-weight: normal; "
    "  border: 1px solid #229954; "
    "  padding: 8px 20px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #2ecc71; "  // ✅ Dark theme success hover
    "} "
    "QPushButton:pressed { "
    "  background-color: #1e8449; "  // ✅ Dark theme success pressed
    "}");
  connect(m_okButton, &QPushButton::clicked, this, &ManualAdjustWidget::onOkClicked);

  m_cancelButton = new QPushButton("✗ Cancel (Stop Sequence)");
  m_cancelButton->setMinimumHeight(35);
  m_cancelButton->setMinimumWidth(150);
  m_cancelButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #e74c3c; "  // ✅ Dark theme danger red
    "  color: white; "
    "  font-size: 13px; "
    "  font-weight: normal; "
    "  border: 1px solid #c0392b; "
    "  padding: 8px 20px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #ec7063; "  // ✅ Dark theme danger hover
    "} "
    "QPushButton:pressed { "
    "  background-color: #c0392b; "  // ✅ Dark theme danger pressed
    "}");
  connect(m_cancelButton, &QPushButton::clicked, this, &ManualAdjustWidget::onCancelClicked);

  actionLayout->addStretch();
  actionLayout->addWidget(m_okButton);
  actionLayout->addWidget(m_cancelButton);
  actionLayout->addStretch();

  containerLayout->addLayout(actionLayout);

  // Center the container
  mainLayout->addStretch();
  QHBoxLayout* centerLayout = new QHBoxLayout();
  centerLayout->addStretch();
  centerLayout->addWidget(container);
  centerLayout->addStretch();
  mainLayout->addLayout(centerLayout);
  mainLayout->addStretch();

  // Start hidden
  hide();
}

void ManualAdjustWidget::showAdjustment(
  const QString& message,
  const QString& deviceName,
  double stepSizeUm,
  bool enableX,
  bool enableY,
  bool enableZ)
{
  m_messageLabel->setText(message);
  m_deviceLabel->setText("Device: " + deviceName);
  m_stepSizeLabel->setText(QString("Step: %1 μm").arg(stepSizeUm, 0, 'f', 1));

  m_currentDevice = deviceName.toStdString();
  m_stepSizeUm = stepSizeUm;
  m_enableX = enableX;
  m_enableY = enableY;
  m_enableZ = enableZ;

  // Debug output
  std::cout << "[ManualAdjustWidget] showAdjustment called:" << std::endl;
  std::cout << "  Device: " << m_currentDevice << std::endl;
  std::cout << "  Step size: " << m_stepSizeUm << " um" << std::endl;
  std::cout << "  m_motionManager: " << (m_motionManager ? "valid" : "NULL") << std::endl;
  std::cout << "  m_matrixLoader: " << (m_matrixLoader ? "valid" : "NULL") << std::endl;

  // Enable/disable buttons based on axis enables
  m_btnXMinus->setEnabled(enableX);
  m_btnXPlus->setEnabled(enableX);
  m_btnYMinus->setEnabled(enableY);
  m_btnYPlus->setEnabled(enableY);
  m_btnZMinus->setEnabled(enableZ);
  m_btnZPlus->setEnabled(enableZ);

  // Update position display immediately
  updatePositionDisplay();

  // Start position update timer (100ms refresh)
  m_positionTimer->start(100);

  show();

  // Set focus to OK button
  m_okButton->setFocus();
}

void ManualAdjustWidget::hideAdjustment()
{
  m_positionTimer->stop();
  hide();
}

void ManualAdjustWidget::onJogXMinus()
{
  sendJogCommand("X", -1.0);
}

void ManualAdjustWidget::onJogXPlus()
{
  sendJogCommand("X", 1.0);
}

void ManualAdjustWidget::onJogYMinus()
{
  sendJogCommand("Y", -1.0);
}

void ManualAdjustWidget::onJogYPlus()
{
  sendJogCommand("Y", 1.0);
}

void ManualAdjustWidget::onJogZMinus()
{
  sendJogCommand("Z", -1.0);
}

void ManualAdjustWidget::onJogZPlus()
{
  sendJogCommand("Z", 1.0);
}

void ManualAdjustWidget::onOkClicked()
{
  emit responseReceived(true);
  hideAdjustment();
}

void ManualAdjustWidget::onCancelClicked()
{
  emit responseReceived(false);
  hideAdjustment();
}

void ManualAdjustWidget::onUpdatePosition()
{
  updatePositionDisplay();
}

void ManualAdjustWidget::sendJogCommand(const std::string& axis, double direction)
{
  // Debug: Check what's failing
  if (m_currentDevice.empty()) {
    std::cerr << "[ManualAdjustWidget] Invalid state: m_currentDevice is empty" << std::endl;
    return;
  }
  if (!m_motionManager) {
    std::cerr << "[ManualAdjustWidget] Invalid state: m_motionManager is null" << std::endl;
    return;
  }
  if (!m_matrixLoader) {
    std::cerr << "[ManualAdjustWidget] Invalid state: m_matrixLoader is null" << std::endl;
    return;
  }

  // Create global movement vector (in microns)
  CoordinateTransformer::Vector6D globalMove(0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
  double stepInMicrons = m_stepSizeUm * direction;

  if (axis == "X") {
    globalMove.x = stepInMicrons;
  }
  else if (axis == "Y") {
    globalMove.y = stepInMicrons;
  }
  else if (axis == "Z") {
    globalMove.z = stepInMicrons;
  }

  // Get device transformation matrix
  auto matrix = m_matrixLoader->GetMatrix(m_currentDevice);
  if (!matrix) {
    std::cerr << "[ManualAdjustWidget] No transformation matrix for device: "
              << m_currentDevice << std::endl;
    return;
  }

  // Transform to local coordinates
  auto localMove = CoordinateTransformer::TransformGlobalToLocal(globalMove, *matrix);

  // Convert microns to millimeters
  double xMM = localMove.x / 1000.0;
  double yMM = localMove.y / 1000.0;
  double zMM = localMove.z / 1000.0;

  // Send move commands (non-blocking)
  bool success = true;
  if (std::abs(xMM) > 0.0001) {
    success &= m_motionManager->MoveRelative(m_currentDevice, "X", xMM, false);
  }
  if (std::abs(yMM) > 0.0001) {
    success &= m_motionManager->MoveRelative(m_currentDevice, "Y", yMM, false);
  }
  if (std::abs(zMM) > 0.0001) {
    success &= m_motionManager->MoveRelative(m_currentDevice, "Z", zMM, false);
  }

  if (!success) {
    std::cerr << "[ManualAdjustWidget] Failed to execute jog command" << std::endl;
  }
}

void ManualAdjustWidget::updatePositionDisplay()
{
  if (m_currentDevice.empty() || !m_motionManager) {
    return;
  }

  double x = 0.0, y = 0.0, z = 0.0;
  if (!m_motionManager->GetPosition(m_currentDevice, "X", x) ||
      !m_motionManager->GetPosition(m_currentDevice, "Y", y) ||
      !m_motionManager->GetPosition(m_currentDevice, "Z", z)) {
    return; // Skip UI update on position read failure
  }

  m_positionLabel->setText(
    QString("Position: X: %1 | Y: %2 | Z: %3 mm")
      .arg(x, 0, 'f', 3)
      .arg(y, 0, 'f', 3)
      .arg(z, 0, 'f', 3)
  );
}
