// RollCorrectionWidget.cpp
#include "RollCorrectionWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDoubleValidator>
#include <cmath>

RollCorrectionWidget::RollCorrectionWidget(QWidget* parent)
  : QWidget(parent)
  , m_spacingInput(nullptr)
{
  setupUI();
}

void RollCorrectionWidget::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(8);

  // === SPACING INPUT ===
  QHBoxLayout* spacingLayout = new QHBoxLayout();
  QLabel* spacingLabel = new QLabel("Fiber Spacing (um):");
  spacingLabel->setStyleSheet("font-weight: bold;");
  m_spacingInput = new QLineEdit("2500");
  m_spacingInput->setValidator(new QDoubleValidator(0.001, 100000.0, 3, this));
  m_spacingInput->setFixedWidth(100);
  m_spacingInput->setToolTip("Physical spacing between CH1 and CH2 fibers in micrometers");
  spacingLayout->addWidget(spacingLabel);
  spacingLayout->addWidget(m_spacingInput);
  spacingLayout->addStretch();
  mainLayout->addLayout(spacingLayout);

  // === DEVICE GROUPS SIDE BY SIDE ===
  QHBoxLayout* devicesLayout = new QHBoxLayout();
  devicesLayout->setSpacing(10);

  QGroupBox* leftGroup = createDeviceGroup("hex-left", "Hex Left");
  QGroupBox* rightGroup = createDeviceGroup("hex-right", "Hex Right");

  devicesLayout->addWidget(leftGroup);
  devicesLayout->addWidget(rightGroup);

  mainLayout->addLayout(devicesLayout);
  mainLayout->addStretch();
}

QGroupBox* RollCorrectionWidget::createDeviceGroup(const QString& deviceName, const QString& displayName)
{
  DeviceUI& dev = getDeviceUI(deviceName);

  dev.group = new QGroupBox(displayName);
  dev.group->setStyleSheet(
    "QGroupBox { "
    "  font-weight: bold; "
    "  color: #FFA500; "
    "  border: 1px solid #3f3f46; "
    "  border-radius: 4px; "
    "  margin-top: 10px; "
    "  padding-top: 10px; "
    "} "
    "QGroupBox::title { "
    "  subcontrol-origin: margin; "
    "  left: 10px; "
    "  padding: 0 5px; "
    "}"
  );

  QVBoxLayout* layout = new QVBoxLayout(dev.group);
  layout->setSpacing(6);
  layout->setContentsMargins(8, 20, 8, 8);

  // 3D beveled button style (blue for CH1)
  QString ch1ButtonStyle =
    "QPushButton { "
    "  background-color: #2196F3; "
    "  color: white; "
    "  font-weight: bold; "
    "  padding: 8px; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.4); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.4); "
    "} "
    "QPushButton:hover { "
    "  background-color: #1976D2; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #0D47A1; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "}";

  // Green/teal for CH2
  QString ch2ButtonStyle =
    "QPushButton { "
    "  background-color: #FF9800; "
    "  color: white; "
    "  font-weight: bold; "
    "  padding: 8px; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.4); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.4); "
    "} "
    "QPushButton:hover { "
    "  background-color: #F57C00; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #E65100; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "}";

  // Small move button style
  QString moveButtonStyle =
    "QPushButton { background-color: #37474F; color: #B0BEC5; padding: 4px 8px; "
    "  font-size: 8pt; border: 1px solid #546E7A; } "
    "QPushButton:hover { background-color: #546E7A; color: white; } "
    "QPushButton:disabled { background-color: #263238; color: #455A64; }";

  // Correct W button style (green)
  QString correctWStyle =
    "QPushButton { "
    "  background-color: #4CAF50; "
    "  color: white; "
    "  font-weight: bold; "
    "  padding: 8px; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.4); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.4); "
    "} "
    "QPushButton:hover { background-color: #388E3C; } "
    "QPushButton:pressed { "
    "  background-color: #1B5E20; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "} "
    "QPushButton:disabled { background-color: #424242; color: #757575; }";

  // === CH1 SECTION ===
  QHBoxLayout* ch1Row = new QHBoxLayout();
  dev.registerCH1Button = new QPushButton("Register CH1 Peak");
  dev.registerCH1Button->setMinimumHeight(32);
  dev.registerCH1Button->setStyleSheet(ch1ButtonStyle);
  dev.registerCH1Button->setToolTip("Capture current hexapod position as CH1 (Blue) peak");
  dev.moveToCH1Button = new QPushButton("Move");
  dev.moveToCH1Button->setFixedWidth(50);
  dev.moveToCH1Button->setMinimumHeight(32);
  dev.moveToCH1Button->setStyleSheet(moveButtonStyle);
  dev.moveToCH1Button->setToolTip("Move hexapod to registered CH1 peak position");
  dev.moveToCH1Button->setEnabled(false);
  ch1Row->addWidget(dev.registerCH1Button, 1);
  ch1Row->addWidget(dev.moveToCH1Button);
  layout->addLayout(ch1Row);

  dev.ch1PositionLabel = new QLabel("CH1 X: --");
  dev.ch1PositionLabel->setStyleSheet("color: #64B5F6; font-weight: bold;");
  layout->addWidget(dev.ch1PositionLabel);

  dev.ch1FullPosLabel = new QLabel("XYZUVW: --");
  dev.ch1FullPosLabel->setStyleSheet("color: #808080; font-size: 8pt;");
  dev.ch1FullPosLabel->setWordWrap(true);
  layout->addWidget(dev.ch1FullPosLabel);

  // === CH2 SECTION ===
  QHBoxLayout* ch2Row = new QHBoxLayout();
  dev.registerCH2Button = new QPushButton("Register CH2 Peak");
  dev.registerCH2Button->setMinimumHeight(32);
  dev.registerCH2Button->setStyleSheet(ch2ButtonStyle);
  dev.registerCH2Button->setToolTip("Capture current hexapod position as CH2 (Yellow) peak");
  dev.moveToCH2Button = new QPushButton("Move");
  dev.moveToCH2Button->setFixedWidth(50);
  dev.moveToCH2Button->setMinimumHeight(32);
  dev.moveToCH2Button->setStyleSheet(moveButtonStyle);
  dev.moveToCH2Button->setToolTip("Move hexapod to registered CH2 peak position");
  dev.moveToCH2Button->setEnabled(false);
  ch2Row->addWidget(dev.registerCH2Button, 1);
  ch2Row->addWidget(dev.moveToCH2Button);
  layout->addLayout(ch2Row);

  dev.ch2PositionLabel = new QLabel("CH2 X: --");
  dev.ch2PositionLabel->setStyleSheet("color: #FFD54F; font-weight: bold;");
  layout->addWidget(dev.ch2PositionLabel);

  dev.ch2FullPosLabel = new QLabel("XYZUVW: --");
  dev.ch2FullPosLabel->setStyleSheet("color: #808080; font-size: 8pt;");
  dev.ch2FullPosLabel->setWordWrap(true);
  layout->addWidget(dev.ch2FullPosLabel);

  // === ACTION BUTTONS ROW (Clear + Correct W) ===
  QHBoxLayout* actionRow = new QHBoxLayout();
  dev.clearButton = new QPushButton("Clear");
  dev.clearButton->setFixedWidth(80);
  dev.clearButton->setStyleSheet(
    "QPushButton { background-color: #424242; color: #cccccc; padding: 4px 8px; "
    "  border: 1px solid #616161; } "
    "QPushButton:hover { background-color: #616161; }"
  );
  dev.correctWButton = new QPushButton("Correct W");
  dev.correctWButton->setMinimumHeight(32);
  dev.correctWButton->setStyleSheet(correctWStyle);
  dev.correctWButton->setToolTip("Apply computed W rotation correction to this hexapod");
  dev.correctWButton->setEnabled(false);
  actionRow->addWidget(dev.clearButton);
  actionRow->addWidget(dev.correctWButton, 1);
  layout->addLayout(actionRow);

  // === RESULT DISPLAY ===
  dev.resultDisplay = new QTextEdit();
  dev.resultDisplay->setReadOnly(true);
  dev.resultDisplay->setMinimumHeight(140);
  dev.resultDisplay->setStyleSheet(
    "QTextEdit { "
    "  background-color: #1e1e1e; "
    "  border: 1px solid #3f3f46; "
    "  color: #00E676; "
    "  font-family: 'Consolas', 'Courier New', monospace; "
    "  font-size: 9pt; "
    "  padding: 6px; "
    "}"
  );
  dev.resultDisplay->setPlaceholderText("Register both CH1 and CH2 peaks to see results...");
  layout->addWidget(dev.resultDisplay);

  // === CONNECT SIGNALS ===
  connect(dev.registerCH1Button, &QPushButton::clicked, this, [this, deviceName]() {
    emit registerCH1Requested(deviceName);
  });
  connect(dev.registerCH2Button, &QPushButton::clicked, this, [this, deviceName]() {
    emit registerCH2Requested(deviceName);
  });
  connect(dev.moveToCH1Button, &QPushButton::clicked, this, [this, deviceName]() {
    emit moveToCH1Requested(deviceName);
  });
  connect(dev.moveToCH2Button, &QPushButton::clicked, this, [this, deviceName]() {
    emit moveToCH2Requested(deviceName);
  });
  connect(dev.correctWButton, &QPushButton::clicked, this, [this, deviceName]() {
    DeviceUI& d = getDeviceUI(deviceName);
    emit correctWRequested(deviceName, d.correctionAngleDeg, d.correctionPositiveW, d.correctionHalfwayXmm);
  });
  connect(dev.clearButton, &QPushButton::clicked, this, [this, deviceName]() {
    emit clearRequested(deviceName);
  });

  return dev.group;
}

void RollCorrectionWidget::setCH1Position(const QString& deviceName,
  double x, double y, double z, double u, double v, double w)
{
  DeviceUI& dev = getDeviceUI(deviceName);
  dev.ch1Registered = true;
  dev.ch1X = x; dev.ch1Y = y; dev.ch1Z = z;
  dev.ch1U = u; dev.ch1V = v; dev.ch1W = w;

  dev.ch1PositionLabel->setText(QString("CH1 X: %1 mm (%2 um)").arg(x, 0, 'f', 4).arg(x * 1000.0, 0, 'f', 1));
  dev.ch1FullPosLabel->setText(QString("X:%1  Y:%2  Z:%3  U:%4  V:%5  W:%6")
    .arg(x, 0, 'f', 4).arg(y, 0, 'f', 4).arg(z, 0, 'f', 4)
    .arg(u, 0, 'f', 4).arg(v, 0, 'f', 4).arg(w, 0, 'f', 4));
  dev.moveToCH1Button->setEnabled(true);

  computeAndDisplayResults(deviceName);
}

void RollCorrectionWidget::setCH2Position(const QString& deviceName,
  double x, double y, double z, double u, double v, double w)
{
  DeviceUI& dev = getDeviceUI(deviceName);
  dev.ch2Registered = true;
  dev.ch2X = x; dev.ch2Y = y; dev.ch2Z = z;
  dev.ch2U = u; dev.ch2V = v; dev.ch2W = w;

  dev.ch2PositionLabel->setText(QString("CH2 X: %1 mm (%2 um)").arg(x, 0, 'f', 4).arg(x * 1000.0, 0, 'f', 1));
  dev.ch2FullPosLabel->setText(QString("X:%1  Y:%2  Z:%3  U:%4  V:%5  W:%6")
    .arg(x, 0, 'f', 4).arg(y, 0, 'f', 4).arg(z, 0, 'f', 4)
    .arg(u, 0, 'f', 4).arg(v, 0, 'f', 4).arg(w, 0, 'f', 4));
  dev.moveToCH2Button->setEnabled(true);

  computeAndDisplayResults(deviceName);
}

void RollCorrectionWidget::clearPositions(const QString& deviceName)
{
  DeviceUI& dev = getDeviceUI(deviceName);
  dev.ch1Registered = false;
  dev.ch2Registered = false;
  dev.ch1X = 0.0; dev.ch1Y = 0.0; dev.ch1Z = 0.0;
  dev.ch1U = 0.0; dev.ch1V = 0.0; dev.ch1W = 0.0;
  dev.ch2X = 0.0; dev.ch2Y = 0.0; dev.ch2Z = 0.0;
  dev.ch2U = 0.0; dev.ch2V = 0.0; dev.ch2W = 0.0;

  dev.ch1PositionLabel->setText("CH1 X: --");
  dev.ch1FullPosLabel->setText("XYZUVW: --");
  dev.ch2PositionLabel->setText("CH2 X: --");
  dev.ch2FullPosLabel->setText("XYZUVW: --");
  dev.moveToCH1Button->setEnabled(false);
  dev.moveToCH2Button->setEnabled(false);
  dev.correctWButton->setEnabled(false);
  dev.resultDisplay->clear();
}

double RollCorrectionWidget::spacing() const
{
  return m_spacingInput->text().toDouble();
}

void RollCorrectionWidget::computeAndDisplayResults(const QString& deviceName)
{
  DeviceUI& dev = getDeviceUI(deviceName);
  if (!dev.ch1Registered || !dev.ch2Registered) return;

  double spacingUm = m_spacingInput->text().toDouble();
  if (spacingUm <= 0.0) {
    dev.resultDisplay->setText("Error: Spacing must be > 0");
    return;
  }

  double deltaXmm = dev.ch2X - dev.ch1X;            // mm (raw from hexapod)
  double deltaXum = deltaXmm * 1000.0;               // convert to um for comparison with spacing
  double angleRad = std::atan2(std::abs(deltaXum), spacingUm);
  double angleDeg = angleRad * 180.0 / 3.14159265358979323846;
  double halfwayOffsetMm = deltaXmm / 2.0;           // halfway in mm (hexapod units)
  double halfwayOffsetUm = deltaXum / 2.0;           // halfway in um

  // Direction table lookup
  bool isLeft = (deviceName == "hex-left");
  bool ch2Below = (deltaXum < 0.0);  // CH2 X < CH1 X = "DOWN"

  QString caseStr;
  QString moveDirection;
  QString wCorrection;
  QString lensType;

  if (isLeft && ch2Below) {
    // Case 1: hex-left, peak, DOWN -> Move UP halfway, W-
    caseStr = "Case 1";
    moveDirection = "Move X UP (positive) by halfway";
    wCorrection = "W- (negative rotation)";
    lensType = "Collimate MLA";
  } else if (isLeft && !ch2Below) {
    // Case 2: hex-left, peak, UP -> Move DOWN halfway, W+
    caseStr = "Case 2";
    moveDirection = "Move X DOWN (negative) by halfway";
    wCorrection = "W+ (positive rotation)";
    lensType = "Collimate MLA";
  } else if (!isLeft && ch2Below) {
    // Case 3: hex-right, peak, DOWN -> Move UP halfway, W+
    caseStr = "Case 3";
    moveDirection = "Move X UP (positive) by halfway";
    wCorrection = "W+ (positive rotation)";
    lensType = "Focus MLA";
  } else {
    // Case 4: hex-right, peak, UP -> Move DOWN halfway, W-
    caseStr = "Case 4";
    moveDirection = "Move X DOWN (negative) by halfway";
    wCorrection = "W- (negative rotation)";
    lensType = "Focus MLA";
  }

  QString summary;
  summary += QString("--- %1 Roll Correction ---\n").arg(deviceName);
  summary += QString("CH1 Peak X: %1 mm\n").arg(dev.ch1X, 0, 'f', 4);
  summary += QString("CH2 Peak X: %1 mm\n").arg(dev.ch2X, 0, 'f', 4);
  summary += QString("Delta X:    %1 um  (%2)\n").arg(deltaXum, 0, 'f', 1)
    .arg(ch2Below ? "DOWN" : "UP");
  summary += QString("Spacing:    %1 um\n").arg(spacingUm, 0, 'f', 1);
  summary += QString("Roll Angle: %1 deg\n\n").arg(angleDeg, 0, 'f', 4);
  summary += QString("[%1] %2\n").arg(caseStr, lensType);
  summary += QString("Halfway Offset: %1 mm (%2 um)\n").arg(halfwayOffsetMm, 0, 'f', 4).arg(halfwayOffsetUm, 0, 'f', 1);
  summary += QString("X Action: %1\n").arg(moveDirection);
  summary += QString("W Correction: %1\n").arg(wCorrection);

  dev.resultDisplay->setText(summary);

  // Store computed values for Correct W button
  dev.correctionAngleDeg = angleDeg;
  dev.correctionPositiveW = !ch2Below ? (isLeft) : (!isLeft);
  dev.correctionHalfwayXmm = halfwayOffsetMm;
  // Case 1: left+down -> W- (false), Case 2: left+up -> W+ (true)
  // Case 3: right+down -> W+ (true), Case 4: right+up -> W- (false)
  dev.correctWButton->setEnabled(true);
  dev.correctWButton->setText(QString("Correct W (%1%2 deg)")
    .arg(dev.correctionPositiveW ? "+" : "-")
    .arg(angleDeg, 0, 'f', 4));
}

RollCorrectionWidget::DeviceUI& RollCorrectionWidget::getDeviceUI(const QString& deviceName)
{
  if (deviceName == "hex-left") {
    return m_leftDevice;
  }
  return m_rightDevice;
}

const RollCorrectionWidget::DeviceUI& RollCorrectionWidget::getDeviceUI(const QString& deviceName) const
{
  if (deviceName == "hex-left") {
    return m_leftDevice;
  }
  return m_rightDevice;
}

RollCorrectionWidget::StoredPosition RollCorrectionWidget::getCH1Position(const QString& deviceName) const
{
  const DeviceUI& dev = getDeviceUI(deviceName);
  return { dev.ch1Registered, dev.ch1X, dev.ch1Y, dev.ch1Z, dev.ch1U, dev.ch1V, dev.ch1W };
}

RollCorrectionWidget::StoredPosition RollCorrectionWidget::getCH2Position(const QString& deviceName) const
{
  const DeviceUI& dev = getDeviceUI(deviceName);
  return { dev.ch2Registered, dev.ch2X, dev.ch2Y, dev.ch2Z, dev.ch2U, dev.ch2V, dev.ch2W };
}
