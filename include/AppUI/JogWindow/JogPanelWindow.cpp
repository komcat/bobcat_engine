// JogPanelWindow.cpp - Compact Two-Column Design
#include "JogPanelWindow.h"
#include "TransformationMatrixLoader.h"
#include "CoordinateTransformer.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "LogManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QCloseEvent>
#include <QTimer>
#include <algorithm>
#include <QStyle>

JogPanelWindow::JogPanelWindow(
  ApplicationContext* context,
  TransformationMatrixLoader* matrixLoader,
  QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_matrixLoader(matrixLoader)
  , m_keyboardIndicator(nullptr)
  , m_deviceButtonGroup(nullptr)
  , m_stepSizeButtonGroup(nullptr)
  , m_currentStepLabel(nullptr)
  , m_currentStepSize(10.0)  // Default 10um
  , m_keyboardActive(false)
{
  setWindowTitle("Jog Panel");
  setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);

  setupUI();

  // Start hidden
  hide();
}

JogPanelWindow::~JogPanelWindow()
{
}

void JogPanelWindow::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(4, 8, 4, 8);
  mainLayout->setSpacing(6);

  // Keyboard indicator banner at top
  m_keyboardIndicator = new QLabel("⌨ Keyboard Control: INACTIVE");
  m_keyboardIndicator->setAlignment(Qt::AlignCenter);
  m_keyboardIndicator->setStyleSheet(
    "QLabel { "
    "  background-color: #555555; "
    "  color: #888888; "
    "  font-weight: bold; "
    "  font-size: 11px; "
    "  padding: 2px; "
    "  border-radius: 2px; "
    "}"
  );
  mainLayout->addWidget(m_keyboardIndicator);

  // Two-column layout
  QHBoxLayout* columnsLayout = new QHBoxLayout();
  columnsLayout->setSpacing(8);

  // LEFT COLUMN: Device + Step Size
  QWidget* leftColumn = new QWidget();
  QVBoxLayout* leftLayout = new QVBoxLayout(leftColumn);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  leftLayout->setSpacing(6);
  leftColumn->setMaximumWidth(140);

  createDevicePanel(leftColumn);
  createStepSizePanel(leftColumn);

  columnsLayout->addWidget(leftColumn);

  // RIGHT COLUMN: Movement Controls
  QWidget* rightColumn = new QWidget();
  createMovementPanel(rightColumn);


  columnsLayout->addWidget(rightColumn);

  mainLayout->addLayout(columnsLayout);

  // Global styling
  setStyleSheet(R"(
    QWidget {
      background-color: #2b2b2b;
      color: white;
    }
    QPushButton {
      background-color: #3a3a3a;
      color: white;
      border: 1px solid #555;
      padding: 4px;
      font-size: 10px;
      min-width: 40px;
      min-height: 24px;
    }
    QPushButton:hover {
      background-color: #4a4a4a;
      border-color: #777;
    }
    QPushButton:pressed {
      background-color: #2a2a2a;
    }
    QPushButton:checked {
      background-color: #0d7377;
      border: 2px solid #14ffec;
    }
    QGroupBox {
      border: 1px solid #555;
      margin-top: 8px;
      padding-top: 6px;
      font-weight: bold;
      font-size: 11px;
    }
    QGroupBox::title {
      color: white;
      subcontrol-origin: margin;
      left: 8px;
      padding: 0 4px;
    }
  )");

  setFixedSize(280, 380);
}

void JogPanelWindow::createDevicePanel(QWidget* parent)
{
  QVBoxLayout* layout = qobject_cast<QVBoxLayout*>(parent->layout());
  if (!layout) {
    layout = new QVBoxLayout(parent);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
  }

  QGroupBox* groupBox = new QGroupBox("Device");
  QVBoxLayout* groupLayout = new QVBoxLayout(groupBox);
  groupLayout->setSpacing(3);
  groupLayout->setContentsMargins(4, 8, 4, 4);

  m_deviceButtonGroup = new QButtonGroup(this);
  m_deviceButtonGroup->setExclusive(true);

  // Get device list from UnifiedMotionManager
  if (m_context && m_context->motionManager && m_matrixLoader) {
    std::vector<std::string> piDevices = m_context->motionManager->GetDeviceNamesByType("PI");
    std::vector<std::string> acsDevices = m_context->motionManager->GetDeviceNamesByType("ACS");

    std::vector<std::string> allDevices;
    allDevices.insert(allDevices.end(), piDevices.begin(), piDevices.end());
    allDevices.insert(allDevices.end(), acsDevices.begin(), acsDevices.end());

    std::vector<std::string> validDevices;
    for (const auto& deviceId : allDevices) {
      if (m_matrixLoader->HasDevice(deviceId)) {
        validDevices.push_back(deviceId);
      }
      else {
        if (m_context->logManager) {
          m_context->logManager->Warning(
            "Device '" + deviceId + "' has no transformation matrix - skipping",
            "JogPanel"
          );
        }
      }
    }

    // Create compact push buttons
    for (const auto& deviceId : validDevices) {
      QPushButton* btn = new QPushButton(QString::fromStdString(deviceId));
      btn->setCheckable(true);
      btn->setMinimumHeight(26);
      btn->setMaximumHeight(26);
      btn->setStyleSheet(
        "QPushButton { "
        "  background-color: #3a3a3a; "
        "  color: white; "
        "  border: 1px solid #555; "
        "  border-radius: 3px; "
        "  padding: 2px 2px; "
        "  font-size: 10px; "
        "  font-weight: bold; "
        "  text-align: left; "
        "} "
        "QPushButton:hover { "
        "  background-color: #4a4a4a; "
        "  border-color: #777; "
        "} "
        "QPushButton:checked { "
        "  background-color: #f0ad4e; "
        "  color: #000000; "
        "  border: 2px solid #ffcc00; "
        "}"
      );

      m_deviceButtonGroup->addButton(btn);
      groupLayout->addWidget(btn);

      connect(btn, &QPushButton::clicked, this, &JogPanelWindow::onDeviceSelected);
    }

    // Select first device by default
    if (!validDevices.empty()) {
      auto firstButton = qobject_cast<QPushButton*>(m_deviceButtonGroup->buttons().first());
      if (firstButton) {
        firstButton->setChecked(true);
        m_selectedDevice = validDevices[0];
      }
    }

    if (m_context->logManager) {
      m_context->logManager->Info(
        "Jog panel loaded " + std::to_string(validDevices.size()) + " devices",
        "JogPanel"
      );
    }
  }

  groupLayout->addStretch();
  layout->addWidget(groupBox);
}

void JogPanelWindow::createStepSizePanel(QWidget* parent)
{
  QVBoxLayout* layout = qobject_cast<QVBoxLayout*>(parent->layout());
  if (!layout) {
    layout = new QVBoxLayout(parent);
  }

  QGroupBox* groupBox = new QGroupBox("Step Size");
  QVBoxLayout* groupLayout = new QVBoxLayout(groupBox);
  groupLayout->setSpacing(3);
  groupLayout->setContentsMargins(4, 8, 4, 4);

  // Current step display
  m_currentStepLabel = new QLabel();
  m_currentStepLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 12px; "
    "  font-weight: bold; "
    "  color: #14ffec; "
    "  padding: 2px; "
    "}"
  );
  m_currentStepLabel->setAlignment(Qt::AlignCenter);
  updateStepSizeDisplay();
  groupLayout->addWidget(m_currentStepLabel);

  // Step size buttons in compact grid
  m_stepSizeButtonGroup = new QButtonGroup(this);
  m_stepSizeButtonGroup->setExclusive(true);

  QGridLayout* gridLayout = new QGridLayout();
  gridLayout->setSpacing(2);
  gridLayout->setContentsMargins(0, 0, 0, 0);

  const std::vector<QString> labels = {
    "1µ", "2µ", "5µ", "10µ", "20µ", "50µ",
    "100µ", "200µ", "500µ", "1mm", "2mm"
  };

  int row = 0, col = 0;
  for (size_t i = 0; i < m_stepSizes.size(); ++i) {
    QPushButton* btn = new QPushButton(labels[i]);
    btn->setCheckable(true);
    btn->setProperty("stepSize", m_stepSizes[i]);
    btn->setMinimumHeight(22);
    btn->setMaximumHeight(22);
    btn->setStyleSheet(
      "QPushButton { "
      "  font-size: 9px; "
      "  padding: 2px; "
      "  min-width: 25px; "
      "}"
    );

    m_stepSizeButtonGroup->addButton(btn, i);
    gridLayout->addWidget(btn, row, col);

    connect(btn, &QPushButton::clicked, this, &JogPanelWindow::onStepSizeSelected);

    // Default to 10um
    if (m_stepSizes[i] == 10.0) {
      btn->setChecked(true);
    }

    col++;
    if (col >= 2) {  // 2 columns instead of 3
      col = 0;
      row++;
    }
  }

  groupLayout->addLayout(gridLayout);
  groupLayout->addStretch();
  layout->addWidget(groupBox);
}

void JogPanelWindow::createMovementPanel(QWidget* parent)
{
  QVBoxLayout* layout = new QVBoxLayout(parent);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(6);

  QGroupBox* groupBox = new QGroupBox("Movement Controls");
  QVBoxLayout* groupLayout = new QVBoxLayout(groupBox);
  groupLayout->setSpacing(6);
  groupLayout->setContentsMargins(4, 8, 4, 4);

  // XYZ Controls - Compact grid
  QLabel* xyzLabel = new QLabel("XYZ Axes");
  xyzLabel->setStyleSheet("QLabel { font-weight: bold; color: #14ffec; font-size: 10px; }");
  groupLayout->addWidget(xyzLabel);

  QGridLayout* xyzGrid = new QGridLayout();
  xyzGrid->setSpacing(4);

  // Y+ (top)
  m_btnYPlus = new QPushButton("Y+");
  m_btnYPlus->setMinimumSize(30, 28);
  xyzGrid->addWidget(m_btnYPlus, 0, 1);
  connect(m_btnYPlus, &QPushButton::clicked, this, [this]() {
    flashButton(m_btnYPlus);
    sendJogCommand("Y", 1.0);
  });

  // X-, X+
  m_btnXMinus = new QPushButton("X-");
  m_btnXMinus->setMinimumSize(30, 28);
  xyzGrid->addWidget(m_btnXMinus, 1, 0);
  connect(m_btnXMinus, &QPushButton::clicked, this, [this]() {
    flashButton(m_btnXMinus);
    sendJogCommand("X", -1.0);
  });

  m_btnXPlus = new QPushButton("X+");
  m_btnXPlus->setMinimumSize(30, 28);
  xyzGrid->addWidget(m_btnXPlus, 1, 2);
  connect(m_btnXPlus, &QPushButton::clicked, this, [this]() {
    flashButton(m_btnXPlus);
    sendJogCommand("X", 1.0);
  });

  // Y- (bottom)
  m_btnYMinus = new QPushButton("Y-");
  m_btnYMinus->setMinimumSize(30, 28);
  xyzGrid->addWidget(m_btnYMinus, 2, 1);
  connect(m_btnYMinus, &QPushButton::clicked, this, [this]() {
    flashButton(m_btnYMinus);
    sendJogCommand("Y", -1.0);
  });

  // Z controls
  QHBoxLayout* zLayout = new QHBoxLayout();
  zLayout->setSpacing(4);

  m_btnZPlus = new QPushButton("Z+");
  m_btnZPlus->setMinimumSize(30, 28);
  connect(m_btnZPlus, &QPushButton::clicked, this, [this]() {
    flashButton(m_btnZPlus);
    sendJogCommand("Z", 1.0);
  });

  m_btnZMinus = new QPushButton("Z-");
  m_btnZMinus->setMinimumSize(30, 28);
  connect(m_btnZMinus, &QPushButton::clicked, this, [this]() {
    flashButton(m_btnZMinus);
    sendJogCommand("Z", -1.0);
  });

  zLayout->addWidget(m_btnZMinus);
  zLayout->addWidget(m_btnZPlus);
  xyzGrid->addLayout(zLayout, 1, 1);

  groupLayout->addLayout(xyzGrid);

  // UVW Controls - Compact grid
  QLabel* uvwLabel = new QLabel("UVW Axes");
  uvwLabel->setStyleSheet("QLabel { font-weight: bold; color: #14ffec; font-size: 10px; }");
  groupLayout->addWidget(uvwLabel);

  QGridLayout* uvwGrid = new QGridLayout();
  uvwGrid->setSpacing(4);

  // U controls
  m_btnUMinus = new QPushButton("U-");
  m_btnUMinus->setMinimumSize(30, 28);
  connect(m_btnUMinus, &QPushButton::clicked, this, [this]() {
    flashButton(m_btnUMinus);
    sendJogCommand("U", -1.0);
  });

  m_btnUPlus = new QPushButton("U+");
  m_btnUPlus->setMinimumSize(30, 28);
  connect(m_btnUPlus, &QPushButton::clicked, this, [this]() {
    flashButton(m_btnUPlus);
    sendJogCommand("U", 1.0);
  });

  uvwGrid->addWidget(m_btnUMinus, 0, 0);
  uvwGrid->addWidget(m_btnUPlus, 0, 1);

  // V controls
  m_btnVMinus = new QPushButton("V-");
  m_btnVMinus->setMinimumSize(30, 28);
  connect(m_btnVMinus, &QPushButton::clicked, this, [this]() {
    flashButton(m_btnVMinus);
    sendJogCommand("V", -1.0);
  });

  m_btnVPlus = new QPushButton("V+");
  m_btnVPlus->setMinimumSize(30, 28);
  connect(m_btnVPlus, &QPushButton::clicked, this, [this]() {
    flashButton(m_btnVPlus);
    sendJogCommand("V", 1.0);
  });

  uvwGrid->addWidget(m_btnVMinus, 1, 0);
  uvwGrid->addWidget(m_btnVPlus, 1, 1);

  // W controls
  m_btnWMinus = new QPushButton("W-");
  m_btnWMinus->setMinimumSize(30, 28);
  connect(m_btnWMinus, &QPushButton::clicked, this, [this]() {
    flashButton(m_btnWMinus);
    sendJogCommand("W", -1.0);
  });

  m_btnWPlus = new QPushButton("W+");
  m_btnWPlus->setMinimumSize(30, 28);
  connect(m_btnWPlus, &QPushButton::clicked, this, [this]() {
    flashButton(m_btnWPlus);
    sendJogCommand("W", 1.0);
  });

  uvwGrid->addWidget(m_btnWMinus, 2, 0);
  uvwGrid->addWidget(m_btnWPlus, 2, 1);

  groupLayout->addLayout(uvwGrid);
  groupLayout->addStretch();

  layout->addWidget(groupBox);
}


void JogPanelWindow::flashButton(QPushButton* button)
{
  if (!button) return;

  // Flash green to indicate movement
  button->setStyleSheet(
    "QPushButton { "
    "  background-color: #28a745; "
    "  color: white; "
    "  border: 2px solid #5cb85c; "
    "}"
  );

  // Reset after 150ms
  QTimer::singleShot(150, this, [button]() {
    button->setStyleSheet("");  // Clear inline style
    button->style()->unpolish(button);  // Remove old style
    button->style()->polish(button);    // Reapply global style
    button->update();  // Force repaint
  });
}

void JogPanelWindow::setKeyboardActive(bool active)
{
  m_keyboardActive = active;

  if (active) {
    m_keyboardIndicator->setText("⌨ Keyboard Control: ACTIVE");
    m_keyboardIndicator->setStyleSheet(
      "QLabel { "
      "  background-color: #28a745; "
      "  color: white; "
      "  font-weight: bold; "
      "  font-size: 11px; "
      "  padding: 4px; "
      "  border-radius: 3px; "
      "}"
    );
  }
  else {
    m_keyboardIndicator->setText("⌨ Keyboard Control: INACTIVE");
    m_keyboardIndicator->setStyleSheet(
      "QLabel { "
      "  background-color: #555555; "
      "  color: #888888; "
      "  font-weight: bold; "
      "  font-size: 11px; "
      "  padding: 4px; "
      "  border-radius: 3px; "
      "}"
    );
  }
}

void JogPanelWindow::onDeviceSelected()
{
  QPushButton* btn = qobject_cast<QPushButton*>(sender());
  if (btn && btn->isChecked()) {
    m_selectedDevice = btn->text().toStdString();

    if (m_context && m_context->logManager) {
      m_context->logManager->Info(
        "Device selected: " + m_selectedDevice,
        "JogPanel"
      );
    }
  }
}

void JogPanelWindow::onStepSizeSelected()
{
  QPushButton* btn = qobject_cast<QPushButton*>(sender());
  if (btn) {
    m_currentStepSize = btn->property("stepSize").toDouble();
    updateStepSizeDisplay();

    if (m_context && m_context->logManager) {
      m_context->logManager->Info(
        "Step size: " + QString::number(m_currentStepSize).toStdString() + "um",
        "JogPanel"
      );
    }
  }
}

void JogPanelWindow::updateStepSizeDisplay()
{
  if (!m_currentStepLabel) return;

  QString text;
  if (m_currentStepSize >= 1000.0) {
    text = QString::number(m_currentStepSize / 1000.0, 'f', 0) + " mm";
  }
  else {
    text = QString::number(m_currentStepSize, 'f', 0) + " µm";
  }

  m_currentStepLabel->setText("⚙ " + text);
}

void JogPanelWindow::sendJogCommand(const std::string& axis, double direction)
{
  if (m_selectedDevice.empty()) {
    if (m_context && m_context->logManager) {
      m_context->logManager->Warning("No device selected", "JogPanel");
    }
    return;
  }

  if (!m_context || !m_context->motionManager || !m_matrixLoader) {
    return;
  }

  // Create global movement vector (in microns)
  CoordinateTransformer::Vector6D globalMove(0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
  double stepInMicrons = m_currentStepSize * direction;

  if (axis == "X") {
    globalMove.x = stepInMicrons;
  }
  else if (axis == "Y") {
    globalMove.y = stepInMicrons;
  }
  else if (axis == "Z") {
    globalMove.z = stepInMicrons;
  }
  else if (axis == "U") {
    globalMove.u = stepInMicrons;
  }
  else if (axis == "V") {
    globalMove.v = stepInMicrons;
  }
  else if (axis == "W") {
    globalMove.w = stepInMicrons;
  }

  // Get device transformation matrix
  auto matrix = m_matrixLoader->GetMatrix(m_selectedDevice);
  if (!matrix) {
    if (m_context->logManager) {
      m_context->logManager->Error(
        "No transformation matrix for device: " + m_selectedDevice,
        "JogPanel"
      );
    }
    return;
  }

  // Transform to local coordinates
  auto localMove = CoordinateTransformer::TransformGlobalToLocal(globalMove, *matrix);

  // Convert microns to millimeters
  double xMM = localMove.x / 1000.0;
  double yMM = localMove.y / 1000.0;
  double zMM = localMove.z / 1000.0;
  double uMM = localMove.u / 1000.0;
  double vMM = localMove.v / 1000.0;
  double wMM = localMove.w / 1000.0;

  // Send commands for each non-zero axis
  bool success = true;
  if (std::abs(xMM) > 0.0001) {
    success &= m_context->motionManager->MoveRelative(m_selectedDevice, "X", xMM, false);
  }
  if (std::abs(yMM) > 0.0001) {
    success &= m_context->motionManager->MoveRelative(m_selectedDevice, "Y", yMM, false);
  }
  if (std::abs(zMM) > 0.0001) {
    success &= m_context->motionManager->MoveRelative(m_selectedDevice, "Z", zMM, false);
  }
  if (std::abs(uMM) > 0.0001) {
    success &= m_context->motionManager->MoveRelative(m_selectedDevice, "U", uMM, false);
  }
  if (std::abs(vMM) > 0.0001) {
    success &= m_context->motionManager->MoveRelative(m_selectedDevice, "V", vMM, false);
  }
  if (std::abs(wMM) > 0.0001) {
    success &= m_context->motionManager->MoveRelative(m_selectedDevice, "W", wMM, false);
  }

  if (m_context->logManager) {
    if (success) {
      std::string dirStr = (direction > 0) ? "+" : "-";
      m_context->logManager->Info(
        "Jog " + axis + dirStr + " " + std::to_string(m_currentStepSize) + "µm",
        "JogPanel"
      );
    }
    else {
      m_context->logManager->Error("Jog command failed", "JogPanel");
    }
  }
}

void JogPanelWindow::showPanel()
{
  show();
  raise();
  activateWindow();
  emit visibilityChanged(true);
}

void JogPanelWindow::hidePanel()
{
  hide();
  emit visibilityChanged(false);
}

bool JogPanelWindow::isPanelVisible() const
{
  return isVisible();
}

std::string JogPanelWindow::getSelectedDevice() const
{
  return m_selectedDevice;
}

double JogPanelWindow::getCurrentStepSize() const
{
  return m_currentStepSize;
}

void JogPanelWindow::closeEvent(QCloseEvent* event)
{
  hidePanel();
  event->accept();
}

void JogPanelWindow::jogX(double direction)
{
  if (direction > 0) flashButton(m_btnXPlus);
  else flashButton(m_btnXMinus);
  sendJogCommand("X", direction);
}

void JogPanelWindow::jogY(double direction)
{
  if (direction > 0) flashButton(m_btnYPlus);
  else flashButton(m_btnYMinus);
  sendJogCommand("Y", direction);
}

void JogPanelWindow::jogZ(double direction)
{
  if (direction > 0) flashButton(m_btnZPlus);
  else flashButton(m_btnZMinus);
  sendJogCommand("Z", direction);
}

void JogPanelWindow::jogU(double direction)
{
  if (direction > 0) flashButton(m_btnUPlus);
  else flashButton(m_btnUMinus);
  sendJogCommand("U", direction);
}

void JogPanelWindow::jogV(double direction)
{
  if (direction > 0) flashButton(m_btnVPlus);
  else flashButton(m_btnVMinus);
  sendJogCommand("V", direction);
}

void JogPanelWindow::jogW(double direction)
{
  if (direction > 0) flashButton(m_btnWPlus);
  else flashButton(m_btnWMinus);
  sendJogCommand("W", direction);
}

void JogPanelWindow::cycleStepSizeUp()
{
  auto it = std::find(m_stepSizes.begin(), m_stepSizes.end(), m_currentStepSize);
  if (it != m_stepSizes.end()) {
    ++it;
    if (it == m_stepSizes.end()) {
      it = m_stepSizes.begin();
    }

    size_t index = std::distance(m_stepSizes.begin(), it);
    m_currentStepSize = *it;

    auto button = m_stepSizeButtonGroup->button(index);
    if (button) {
      button->setChecked(true);
    }

    updateStepSizeDisplay();

    if (m_context && m_context->logManager) {
      m_context->logManager->Info(
        "Step size: " + QString::number(m_currentStepSize).toStdString() + "µm",
        "JogPanel"
      );
    }
  }
}

void JogPanelWindow::cycleStepSizeDown()
{
  auto it = std::find(m_stepSizes.begin(), m_stepSizes.end(), m_currentStepSize);
  if (it != m_stepSizes.end()) {
    if (it == m_stepSizes.begin()) {
      it = m_stepSizes.end();
    }
    --it;

    size_t index = std::distance(m_stepSizes.begin(), it);
    m_currentStepSize = *it;

    auto button = m_stepSizeButtonGroup->button(index);
    if (button) {
      button->setChecked(true);
    }

    updateStepSizeDisplay();

    if (m_context && m_context->logManager) {
      m_context->logManager->Info(
        "Step size: " + QString::number(m_currentStepSize).toStdString() + "µm",
        "JogPanel"
      );
    }
  }
}