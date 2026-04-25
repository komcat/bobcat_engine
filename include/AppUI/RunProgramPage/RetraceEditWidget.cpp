// RetraceEditWidget.cpp
#include "RetraceEditWidget.h"
#include "unified_motion_manager.h"
#include "TransformationMatrixLoader.h"
#include "CoordinateTransformer.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QTimer>
#include <cmath>
#include <iostream>

RetraceEditWidget::RetraceEditWidget(
  UnifiedMotionManager* motionManager,
  TransformationMatrixLoader* matrixLoader,
  QWidget* parent)
  : QWidget(parent)
  , m_motionManager(motionManager)
  , m_matrixLoader(matrixLoader)
  , m_stepSizeUm(10.0)
  , m_originalX(0), m_originalY(0), m_originalZ(0)
  , m_originalU(0), m_originalV(0), m_originalW(0)
  , m_currentX(0), m_currentY(0), m_currentZ(0)
  , m_currentU(0), m_currentV(0), m_currentW(0)
  , m_positionModified(false)
{
  setupUI();

  // Create position update timer
  m_positionTimer = new QTimer(this);
  connect(m_positionTimer, &QTimer::timeout, this, &RetraceEditWidget::onUpdatePosition);
}

void RetraceEditWidget::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Create centered container
  QWidget* container = new QWidget();
  container->setMaximumWidth(700);
  container->setStyleSheet(
    "QWidget { "
    "  background-color: #1e1e1e; "
    "  border: 2px solid #d9534f; "  // Red border for retrace mode
    "}");

  QVBoxLayout* containerLayout = new QVBoxLayout(container);
  containerLayout->setContentsMargins(20, 15, 20, 15);
  containerLayout->setSpacing(8);

  // Header
  m_headerLabel = new QLabel("Retrace Edit Mode - Adjusting Positions");
  m_headerLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 16px; "
    "  font-weight: bold; "
    "  color: #d9534f; "  // Red accent
    "  background: transparent; "
    "  border: none; "
    "}");
  containerLayout->addWidget(m_headerLabel);

  // Device and position info row
  QHBoxLayout* infoLayout = new QHBoxLayout();

  m_deviceLabel = new QLabel("Device: -");
  m_deviceLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 12px; "
    "  font-weight: bold; "
    "  color: #1177bb; "
    "  background: transparent; "
    "  border: none; "
    "}");
  infoLayout->addWidget(m_deviceLabel);

  m_positionLabel = new QLabel("Position: -");
  m_positionLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 12px; "
    "  font-weight: bold; "
    "  color: #27ae60; "
    "  background: transparent; "
    "  border: none; "
    "}");
  infoLayout->addWidget(m_positionLabel);

  infoLayout->addStretch();

  m_progressLabel = new QLabel("");
  m_progressLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 12px; "
    "  color: #d4d4d4; "
    "  background: transparent; "
    "  border: none; "
    "}");
  infoLayout->addWidget(m_progressLabel);

  containerLayout->addLayout(infoLayout);

  // Position table (Original | Current | Delta)
  m_positionTable = new QTableWidget(6, 4);  // 6 rows (XYZUVW), 4 cols (Axis, Original, Current, Delta)
  m_positionTable->setHorizontalHeaderLabels({"Axis", "Original", "Current", "Delta"});
  m_positionTable->verticalHeader()->setVisible(false);
  m_positionTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_positionTable->setSelectionMode(QAbstractItemView::NoSelection);
  m_positionTable->setMaximumHeight(180);
  m_positionTable->setStyleSheet(
    "QTableWidget { "
    "  background-color: #252526; "
    "  color: #d4d4d4; "
    "  border: 1px solid #3c3c3c; "
    "  gridline-color: #3c3c3c; "
    "} "
    "QHeaderView::section { "
    "  background-color: #2d2d30; "
    "  color: #d4d4d4; "
    "  padding: 4px; "
    "  border: 1px solid #3c3c3c; "
    "  font-weight: bold; "
    "}");

  // Set column widths
  m_positionTable->setColumnWidth(0, 50);   // Axis
  m_positionTable->setColumnWidth(1, 120);  // Original
  m_positionTable->setColumnWidth(2, 120);  // Current
  m_positionTable->setColumnWidth(3, 100);  // Delta

  // Initialize axis labels
  QStringList axes = {"X", "Y", "Z", "U", "V", "W"};
  for (int i = 0; i < 6; ++i) {
    QTableWidgetItem* axisItem = new QTableWidgetItem(axes[i]);
    axisItem->setTextAlignment(Qt::AlignCenter);
    m_positionTable->setItem(i, 0, axisItem);

    for (int j = 1; j < 4; ++j) {
      QTableWidgetItem* item = new QTableWidgetItem("0.000");
      item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      m_positionTable->setItem(i, j, item);
    }
  }

  containerLayout->addWidget(m_positionTable);

  // Step size row
  QHBoxLayout* stepLayout = new QHBoxLayout();
  m_stepSizeLabel = new QLabel("Step: 10 um");
  m_stepSizeLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 12px; "
    "  font-weight: bold; "
    "  color: #e6a23c; "  // Orange
    "  background: transparent; "
    "  border: none; "
    "}");
  stepLayout->addWidget(m_stepSizeLabel);
  stepLayout->addStretch();
  containerLayout->addLayout(stepLayout);

  // Jog controls - Single row layout
  QHBoxLayout* jogLayout = new QHBoxLayout();
  jogLayout->setSpacing(5);

  QString buttonStyle =
    "QPushButton { "
    "  background-color: #0e639c; "
    "  color: white; "
    "  font-size: 11px; "
    "  border: 1px solid #0c5280; "
    "  padding: 5px 8px; "
    "  min-width: 55px; "
    "  min-height: 28px; "
    "} "
    "QPushButton:hover { background-color: #1177bb; } "
    "QPushButton:pressed { background-color: #005a9e; } "
    "QPushButton:disabled { background-color: #555555; color: #888888; }";

  m_btnXMinus = new QPushButton("X-");
  m_btnXMinus->setStyleSheet(buttonStyle);
  connect(m_btnXMinus, &QPushButton::clicked, this, &RetraceEditWidget::onJogXMinus);

  m_btnXPlus = new QPushButton("X+");
  m_btnXPlus->setStyleSheet(buttonStyle);
  connect(m_btnXPlus, &QPushButton::clicked, this, &RetraceEditWidget::onJogXPlus);

  m_btnYMinus = new QPushButton("Y-");
  m_btnYMinus->setStyleSheet(buttonStyle);
  connect(m_btnYMinus, &QPushButton::clicked, this, &RetraceEditWidget::onJogYMinus);

  m_btnYPlus = new QPushButton("Y+");
  m_btnYPlus->setStyleSheet(buttonStyle);
  connect(m_btnYPlus, &QPushButton::clicked, this, &RetraceEditWidget::onJogYPlus);

  m_btnZMinus = new QPushButton("Z-");
  m_btnZMinus->setStyleSheet(buttonStyle);
  connect(m_btnZMinus, &QPushButton::clicked, this, &RetraceEditWidget::onJogZMinus);

  m_btnZPlus = new QPushButton("Z+");
  m_btnZPlus->setStyleSheet(buttonStyle);
  connect(m_btnZPlus, &QPushButton::clicked, this, &RetraceEditWidget::onJogZPlus);

  jogLayout->addWidget(m_btnXMinus);
  jogLayout->addWidget(m_btnXPlus);
  jogLayout->addWidget(m_btnYMinus);
  jogLayout->addWidget(m_btnYPlus);
  jogLayout->addWidget(m_btnZMinus);
  jogLayout->addWidget(m_btnZPlus);
  jogLayout->addStretch();

  containerLayout->addLayout(jogLayout);

  containerLayout->addSpacing(10);

  // Action buttons row
  QHBoxLayout* actionLayout = new QHBoxLayout();
  actionLayout->setSpacing(10);

  m_saveAndContinueButton = new QPushButton("Save && Continue");
  m_saveAndContinueButton->setMinimumHeight(35);
  m_saveAndContinueButton->setMinimumWidth(130);
  m_saveAndContinueButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #27ae60; "
    "  color: white; "
    "  font-size: 12px; "
    "  font-weight: bold; "
    "  border: 1px solid #229954; "
    "  padding: 8px 16px; "
    "} "
    "QPushButton:hover { background-color: #2ecc71; } "
    "QPushButton:pressed { background-color: #1e8449; }");
  connect(m_saveAndContinueButton, &QPushButton::clicked, this, &RetraceEditWidget::onSaveAndContinue);

  m_skipButton = new QPushButton("Skip");
  m_skipButton->setMinimumHeight(35);
  m_skipButton->setMinimumWidth(80);
  m_skipButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #5bc0de; "
    "  color: white; "
    "  font-size: 12px; "
    "  border: 1px solid #46b8da; "
    "  padding: 8px 16px; "
    "} "
    "QPushButton:hover { background-color: #69d5f4; } "
    "QPushButton:pressed { background-color: #31b0d5; }");
  connect(m_skipButton, &QPushButton::clicked, this, &RetraceEditWidget::onSkip);

  m_stopButton = new QPushButton("Stop Retrace");
  m_stopButton->setMinimumHeight(35);
  m_stopButton->setMinimumWidth(110);
  m_stopButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #d9534f; "
    "  color: white; "
    "  font-size: 12px; "
    "  border: 1px solid #c9302c; "
    "  padding: 8px 16px; "
    "} "
    "QPushButton:hover { background-color: #ed6b66; } "
    "QPushButton:pressed { background-color: #c9302c; }");
  connect(m_stopButton, &QPushButton::clicked, this, &RetraceEditWidget::onStopRetrace);

  actionLayout->addStretch();
  actionLayout->addWidget(m_saveAndContinueButton);
  actionLayout->addWidget(m_skipButton);
  actionLayout->addWidget(m_stopButton);
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

  hide();
}

void RetraceEditWidget::showRetraceEdit(
  const QString& deviceName,
  const QString& positionName,
  double origX, double origY, double origZ,
  double origU, double origV, double origW,
  double currX, double currY, double currZ,
  double currU, double currV, double currW,
  int positionIndex,
  int totalPositions)
{
  m_currentDevice = deviceName.toStdString();
  m_currentPositionName = positionName.toStdString();

  m_deviceLabel->setText("Device: " + deviceName);
  m_positionLabel->setText("Position: " + positionName);
  m_stepSizeLabel->setText(QString("Step: %1 um").arg(m_stepSizeUm, 0, 'f', 1));

  if (totalPositions > 0) {
    m_progressLabel->setText(QString("Position %1 of %2").arg(positionIndex).arg(totalPositions));
  } else if (positionIndex > 0) {
    m_progressLabel->setText(QString("Position %1").arg(positionIndex));
  } else {
    m_progressLabel->setText("");
  }

  // Store original position
  m_originalX = origX; m_originalY = origY; m_originalZ = origZ;
  m_originalU = origU; m_originalV = origV; m_originalW = origW;

  // Store current position
  m_currentX = currX; m_currentY = currY; m_currentZ = currZ;
  m_currentU = currU; m_currentV = currV; m_currentW = currW;

  m_positionModified = false;

  // Update table
  updatePositionDisplay();

  // Start position update timer (100ms refresh)
  m_positionTimer->start(100);

  show();
  m_saveAndContinueButton->setFocus();

  std::cout << "[RetraceEditWidget] Showing for " << m_currentDevice << "/" << m_currentPositionName << std::endl;
}

void RetraceEditWidget::hideRetraceEdit()
{
  m_positionTimer->stop();
  hide();
}

void RetraceEditWidget::updatePositionDisplay()
{
  // Helper to format number (4 decimals — 0.1 µm visible)
  auto formatNum = [](double val) {
    return QString::number(val, 'f', 4);
  };

  // Helper to format delta with color
  auto formatDelta = [](double delta) -> QString {
    if (std::abs(delta) < 0.0001) {
      return "0.0000";
    }
    QString sign = delta > 0 ? "+" : "";
    return sign + QString::number(delta, 'f', 4);
  };

  // Update Original column
  m_positionTable->item(0, 1)->setText(formatNum(m_originalX));
  m_positionTable->item(1, 1)->setText(formatNum(m_originalY));
  m_positionTable->item(2, 1)->setText(formatNum(m_originalZ));
  m_positionTable->item(3, 1)->setText(formatNum(m_originalU));
  m_positionTable->item(4, 1)->setText(formatNum(m_originalV));
  m_positionTable->item(5, 1)->setText(formatNum(m_originalW));

  // Update Current column
  m_positionTable->item(0, 2)->setText(formatNum(m_currentX));
  m_positionTable->item(1, 2)->setText(formatNum(m_currentY));
  m_positionTable->item(2, 2)->setText(formatNum(m_currentZ));
  m_positionTable->item(3, 2)->setText(formatNum(m_currentU));
  m_positionTable->item(4, 2)->setText(formatNum(m_currentV));
  m_positionTable->item(5, 2)->setText(formatNum(m_currentW));

  // Update Delta column with colors
  auto updateDelta = [this, formatDelta](int row, double original, double current) {
    double delta = current - original;
    m_positionTable->item(row, 3)->setText(formatDelta(delta));

    // Color code the delta
    QColor color;
    if (std::abs(delta) < 0.001) {
      color = QColor("#d4d4d4");  // Gray - no change
    } else if (std::abs(delta) < 0.01) {
      color = QColor("#e6a23c");  // Orange - small change
    } else {
      color = QColor("#d9534f");  // Red - significant change
    }
    m_positionTable->item(row, 3)->setForeground(color);
  };

  updateDelta(0, m_originalX, m_currentX);
  updateDelta(1, m_originalY, m_currentY);
  updateDelta(2, m_originalZ, m_currentZ);
  updateDelta(3, m_originalU, m_currentU);
  updateDelta(4, m_originalV, m_currentV);
  updateDelta(5, m_originalW, m_currentW);
}

void RetraceEditWidget::onJogXMinus() { sendJogCommand("X", -1.0); }
void RetraceEditWidget::onJogXPlus() { sendJogCommand("X", 1.0); }
void RetraceEditWidget::onJogYMinus() { sendJogCommand("Y", -1.0); }
void RetraceEditWidget::onJogYPlus() { sendJogCommand("Y", 1.0); }
void RetraceEditWidget::onJogZMinus() { sendJogCommand("Z", -1.0); }
void RetraceEditWidget::onJogZPlus() { sendJogCommand("Z", 1.0); }

void RetraceEditWidget::onSaveAndContinue()
{
  emit responseReceived(true, true);  // Continue, save
  hideRetraceEdit();
}

void RetraceEditWidget::onSkip()
{
  emit responseReceived(true, false);  // Continue, don't save
  hideRetraceEdit();
}

void RetraceEditWidget::onStopRetrace()
{
  emit responseReceived(false, false);  // Stop, don't save
  hideRetraceEdit();
}

void RetraceEditWidget::onUpdatePosition()
{
  if (m_currentDevice.empty() || !m_motionManager) {
    return;
  }

  // Read current position from hardware
  double x = 0, y = 0, z = 0, u = 0, v = 0, w = 0;
  if (!m_motionManager->GetPosition(m_currentDevice, "X", x) ||
      !m_motionManager->GetPosition(m_currentDevice, "Y", y) ||
      !m_motionManager->GetPosition(m_currentDevice, "Z", z) ||
      !m_motionManager->GetPosition(m_currentDevice, "U", u) ||
      !m_motionManager->GetPosition(m_currentDevice, "V", v) ||
      !m_motionManager->GetPosition(m_currentDevice, "W", w)) {
    return; // Skip jog detection if position read fails
  }

  // Check if position changed (user jogged)
  double threshold = 0.001;  // 1 micron
  if (std::abs(x - m_currentX) > threshold ||
      std::abs(y - m_currentY) > threshold ||
      std::abs(z - m_currentZ) > threshold) {
    m_positionModified = true;
  }

  m_currentX = x; m_currentY = y; m_currentZ = z;
  m_currentU = u; m_currentV = v; m_currentW = w;

  updatePositionDisplay();
}

void RetraceEditWidget::sendJogCommand(const std::string& axis, double direction)
{
  if (m_currentDevice.empty() || !m_motionManager) {
    std::cerr << "[RetraceEditWidget] Invalid state for jog command" << std::endl;
    return;
  }

  if (!m_matrixLoader) {
    std::cerr << "[RetraceEditWidget] No matrix loader" << std::endl;
    return;
  }

  // Create global movement vector (in microns)
  CoordinateTransformer::Vector6D globalMove(0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
  double stepInMicrons = m_stepSizeUm * direction;

  if (axis == "X") {
    globalMove.x = stepInMicrons;
  } else if (axis == "Y") {
    globalMove.y = stepInMicrons;
  } else if (axis == "Z") {
    globalMove.z = stepInMicrons;
  }

  // Get device transformation matrix
  auto matrix = m_matrixLoader->GetMatrix(m_currentDevice);
  if (!matrix) {
    std::cerr << "[RetraceEditWidget] No transformation matrix for device: "
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

  if (success) {
    m_positionModified = true;
  } else {
    std::cerr << "[RetraceEditWidget] Failed to execute jog command" << std::endl;
  }
}
