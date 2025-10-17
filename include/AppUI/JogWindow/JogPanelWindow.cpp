// JogPanelWindow.cpp
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
#include <QRadioButton>
#include <QCloseEvent>
#include <algorithm>

JogPanelWindow::JogPanelWindow(
  ApplicationContext* context,
  TransformationMatrixLoader* matrixLoader,
  QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_matrixLoader(matrixLoader)
  , m_deviceButtonGroup(nullptr)
  , m_stepSizeButtonGroup(nullptr)
  , m_currentStepLabel(nullptr)
  , m_currentStepSize(10.0)  // Default 10um
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
  mainLayout->setContentsMargins(10, 10, 10, 10);
  mainLayout->setSpacing(10);

  // Title
  QLabel* title = new QLabel("Global Jog Control");
  title->setStyleSheet("QLabel { font-size: 16px; font-weight: bold; color: white; }");
  title->setAlignment(Qt::AlignCenter);
  mainLayout->addWidget(title);

  // Three panels in horizontal layout
  QHBoxLayout* panelsLayout = new QHBoxLayout();

  // Panel 1: Device Selection
  QWidget* devicePanel = new QWidget();
  createDevicePanel(devicePanel);
  panelsLayout->addWidget(devicePanel);

  // Panel 2: Step Size Selection
  QWidget* stepPanel = new QWidget();
  createStepSizePanel(stepPanel);
  panelsLayout->addWidget(stepPanel);

  // Panel 3: Movement Controls
  QWidget* movePanel = new QWidget();
  createMovementPanel(movePanel);
  panelsLayout->addWidget(movePanel);

  mainLayout->addLayout(panelsLayout);

  // Styling
  setStyleSheet(R"(
    QWidget {
      background-color: #2b2b2b;
      color: white;
    }
    QPushButton {
      background-color: #3a3a3a;
      color: white;
      border: 1px solid #555;
      padding: 8px;
      font-size: 12px;
      min-width: 60px;
    }
    QPushButton:hover {
      background-color: #4a4a4a;
    }
    QPushButton:pressed {
      background-color: #2a2a2a;
    }
    QPushButton:checked {
      background-color: #0d7377;
      border: 2px solid #14ffec;
    }
    QRadioButton {
      color: white;
      spacing: 5px;
    }
    QRadioButton::indicator {
      width: 15px;
      height: 15px;
    }
    QGroupBox {
      border: 1px solid #555;
      margin-top: 10px;
      padding-top: 10px;
      font-weight: bold;
    }
    QGroupBox::title {
      color: white;
      subcontrol-origin: margin;
      left: 10px;
      padding: 0 5px;
    }
  )");

  setMinimumSize(700, 400);
}

void JogPanelWindow::createDevicePanel(QWidget* parent)
{
  QVBoxLayout* layout = new QVBoxLayout(parent);

  QGroupBox* groupBox = new QGroupBox("Device Selection");
  QVBoxLayout* groupLayout = new QVBoxLayout(groupBox);

  m_deviceButtonGroup = new QButtonGroup(this);
  m_deviceButtonGroup->setExclusive(true);

  // Get device list from matrix loader
  if (m_matrixLoader) {
    auto deviceIds = m_matrixLoader->GetDeviceIds();

    for (const auto& deviceId : deviceIds) {
      QRadioButton* radio = new QRadioButton(QString::fromStdString(deviceId));
      m_deviceButtonGroup->addButton(radio);
      groupLayout->addWidget(radio);

      connect(radio, &QRadioButton::toggled, this, &JogPanelWindow::onDeviceSelected);
    }

    // Select first device by default
    if (!deviceIds.empty()) {
      auto firstButton = qobject_cast<QRadioButton*>(m_deviceButtonGroup->buttons().first());
      if (firstButton) {
        firstButton->setChecked(true);
        m_selectedDevice = deviceIds[0];
      }
    }
  }

  groupLayout->addStretch();
  layout->addWidget(groupBox);
}

void JogPanelWindow::createStepSizePanel(QWidget* parent)
{
  QVBoxLayout* layout = new QVBoxLayout(parent);

  QGroupBox* groupBox = new QGroupBox("Step Size");
  QVBoxLayout* groupLayout = new QVBoxLayout(groupBox);

  // Current step display
  m_currentStepLabel = new QLabel();
  m_currentStepLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #14ffec; }");
  m_currentStepLabel->setAlignment(Qt::AlignCenter);
  updateStepSizeDisplay();
  groupLayout->addWidget(m_currentStepLabel);

  // Step size buttons
  m_stepSizeButtonGroup = new QButtonGroup(this);
  m_stepSizeButtonGroup->setExclusive(true);

  QGridLayout* gridLayout = new QGridLayout();
  gridLayout->setSpacing(5);

  const std::vector<QString> labels = {
    "1um", "2um", "5um", "10um", "20um", "50um",
    "100um", "200um", "500um", "1mm", "2mm"
  };

  int row = 0, col = 0;
  for (size_t i = 0; i < m_stepSizes.size(); ++i) {
    QPushButton* btn = new QPushButton(labels[i]);
    btn->setCheckable(true);
    btn->setProperty("stepSize", m_stepSizes[i]);

    m_stepSizeButtonGroup->addButton(btn, i);
    gridLayout->addWidget(btn, row, col);

    connect(btn, &QPushButton::clicked, this, &JogPanelWindow::onStepSizeSelected);

    // Default to 10um
    if (m_stepSizes[i] == 10.0) {
      btn->setChecked(true);
    }

    col++;
    if (col >= 3) {
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

  QGroupBox* groupBox = new QGroupBox("Movement Controls");
  QVBoxLayout* groupLayout = new QVBoxLayout(groupBox);

  // XYZ Controls - Grid layout
  QLabel* xyzLabel = new QLabel("XYZ Axes");
  xyzLabel->setStyleSheet("QLabel { font-weight: bold; color: #14ffec; }");
  groupLayout->addWidget(xyzLabel);

  QGridLayout* xyzGrid = new QGridLayout();
  xyzGrid->setSpacing(10);

  // Y+ (top)
  m_btnYPlus = new QPushButton("Y+");
  m_btnYPlus->setMinimumSize(80, 40);
  xyzGrid->addWidget(m_btnYPlus, 0, 1);
  connect(m_btnYPlus, &QPushButton::clicked, this, [this]() {
    sendJogCommand("Y", 1.0);
  });

  // X- (left), X+ (right)
  m_btnXMinus = new QPushButton("X-");
  m_btnXMinus->setMinimumSize(80, 40);
  xyzGrid->addWidget(m_btnXMinus, 1, 0);
  connect(m_btnXMinus, &QPushButton::clicked, this, [this]() {
    sendJogCommand("X", -1.0);
  });

  m_btnXPlus = new QPushButton("X+");
  m_btnXPlus->setMinimumSize(80, 40);
  xyzGrid->addWidget(m_btnXPlus, 1, 2);
  connect(m_btnXPlus, &QPushButton::clicked, this, [this]() {
    sendJogCommand("X", 1.0);
  });

  // Y- (bottom)
  m_btnYMinus = new QPushButton("Y-");
  m_btnYMinus->setMinimumSize(80, 40);
  xyzGrid->addWidget(m_btnYMinus, 2, 1);
  connect(m_btnYMinus, &QPushButton::clicked, this, [this]() {
    sendJogCommand("Y", -1.0);
  });

  groupLayout->addLayout(xyzGrid);

  // Z controls (separate row)
  QHBoxLayout* zLayout = new QHBoxLayout();
  zLayout->setSpacing(10);

  m_btnZMinus = new QPushButton("Z-");
  m_btnZMinus->setMinimumSize(80, 40);
  connect(m_btnZMinus, &QPushButton::clicked, this, [this]() {
    sendJogCommand("Z", -1.0);
  });

  m_btnZPlus = new QPushButton("Z+");
  m_btnZPlus->setMinimumSize(80, 40);
  connect(m_btnZPlus, &QPushButton::clicked, this, [this]() {
    sendJogCommand("Z", 1.0);
  });

  zLayout->addWidget(m_btnZMinus);
  zLayout->addWidget(m_btnZPlus);
  groupLayout->addLayout(zLayout);

  // Separator
  groupLayout->addSpacing(20);

  // UVW Controls
  QLabel* uvwLabel = new QLabel("UVW Axes");
  uvwLabel->setStyleSheet("QLabel { font-weight: bold; color: #14ffec; }");
  groupLayout->addWidget(uvwLabel);

  QGridLayout* uvwGrid = new QGridLayout();
  uvwGrid->setSpacing(10);

  // V+ (top)
  m_btnVPlus = new QPushButton("V+");
  m_btnVPlus->setMinimumSize(80, 40);
  uvwGrid->addWidget(m_btnVPlus, 0, 1);
  connect(m_btnVPlus, &QPushButton::clicked, this, [this]() {
    sendJogCommand("V", 1.0);
  });

  // U- (left), U+ (right)
  m_btnUMinus = new QPushButton("U-");
  m_btnUMinus->setMinimumSize(80, 40);
  uvwGrid->addWidget(m_btnUMinus, 1, 0);
  connect(m_btnUMinus, &QPushButton::clicked, this, [this]() {
    sendJogCommand("U", -1.0);
  });

  m_btnUPlus = new QPushButton("U+");
  m_btnUPlus->setMinimumSize(80, 40);
  uvwGrid->addWidget(m_btnUPlus, 1, 2);
  connect(m_btnUPlus, &QPushButton::clicked, this, [this]() {
    sendJogCommand("U", 1.0);
  });

  // V- (bottom)
  m_btnVMinus = new QPushButton("V-");
  m_btnVMinus->setMinimumSize(80, 40);
  uvwGrid->addWidget(m_btnVMinus, 2, 1);
  connect(m_btnVMinus, &QPushButton::clicked, this, [this]() {
    sendJogCommand("V", -1.0);
  });

  groupLayout->addLayout(uvwGrid);

  // W controls (separate row)
  QHBoxLayout* wLayout = new QHBoxLayout();
  wLayout->setSpacing(10);

  m_btnWMinus = new QPushButton("W-");
  m_btnWMinus->setMinimumSize(80, 40);
  connect(m_btnWMinus, &QPushButton::clicked, this, [this]() {
    sendJogCommand("W", -1.0);
  });

  m_btnWPlus = new QPushButton("W+");
  m_btnWPlus->setMinimumSize(80, 40);
  connect(m_btnWPlus, &QPushButton::clicked, this, [this]() {
    sendJogCommand("W", 1.0);
  });

  wLayout->addWidget(m_btnWMinus);
  wLayout->addWidget(m_btnWPlus);
  groupLayout->addLayout(wLayout);

  groupLayout->addStretch();
  layout->addWidget(groupBox);
}

void JogPanelWindow::onDeviceSelected()
{
  QRadioButton* radio = qobject_cast<QRadioButton*>(sender());
  if (radio && radio->isChecked()) {
    m_selectedDevice = radio->text().toStdString();

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
    text = QString::number(m_currentStepSize, 'f', 0) + " um";
  }

  m_currentStepLabel->setText("Current: " + text);
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

  // Transform to local coordinates (XYZ transformed, UVW pass through)
  auto localMove = CoordinateTransformer::TransformGlobalToLocal(globalMove, *matrix);

  // Convert microns to millimeters for motion manager
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
        "Jog " + axis + dirStr + " " + std::to_string(m_currentStepSize) + "um -> Device local: [" +
        std::to_string(xMM) + ", " + std::to_string(yMM) + ", " + std::to_string(zMM) + ", " +
        std::to_string(uMM) + ", " + std::to_string(vMM) + ", " + std::to_string(wMM) + "] mm",
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
  sendJogCommand("X", direction);
}

void JogPanelWindow::jogY(double direction)
{
  sendJogCommand("Y", direction);
}

void JogPanelWindow::jogZ(double direction)
{
  sendJogCommand("Z", direction);
}

void JogPanelWindow::jogU(double direction)
{
  sendJogCommand("U", direction);
}

void JogPanelWindow::jogV(double direction)
{
  sendJogCommand("V", direction);
}

void JogPanelWindow::jogW(double direction)
{
  sendJogCommand("W", direction);
}

void JogPanelWindow::cycleStepSizeUp()
{
  // Find current step size index
  auto it = std::find(m_stepSizes.begin(), m_stepSizes.end(), m_currentStepSize);
  if (it != m_stepSizes.end()) {
    // Move to next step size (wrap around)
    ++it;
    if (it == m_stepSizes.end()) {
      it = m_stepSizes.begin();
    }

    size_t index = std::distance(m_stepSizes.begin(), it);
    m_currentStepSize = *it;

    // Update button selection
    auto button = m_stepSizeButtonGroup->button(index);
    if (button) {
      button->setChecked(true);
    }

    updateStepSizeDisplay();

    if (m_context && m_context->logManager) {
      m_context->logManager->Info(
        "Step size: " + QString::number(m_currentStepSize).toStdString() + "um",
        "JogPanel"
      );
    }
  }
}

void JogPanelWindow::cycleStepSizeDown()
{
  // Find current step size index
  auto it = std::find(m_stepSizes.begin(), m_stepSizes.end(), m_currentStepSize);
  if (it != m_stepSizes.end()) {
    // Move to previous step size (wrap around)
    if (it == m_stepSizes.begin()) {
      it = m_stepSizes.end();
    }
    --it;

    size_t index = std::distance(m_stepSizes.begin(), it);
    m_currentStepSize = *it;

    // Update button selection
    auto button = m_stepSizeButtonGroup->button(index);
    if (button) {
      button->setChecked(true);
    }

    updateStepSizeDisplay();

    if (m_context && m_context->logManager) {
      m_context->logManager->Info(
        "Step size: " + QString::number(m_currentStepSize).toStdString() + "um",
        "JogPanel"
      );
    }
  }
}