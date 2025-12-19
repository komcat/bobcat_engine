// ScanControlWidget.cpp
#include "ScanControlWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QGroupBox>

ScanControlWidget::ScanControlWidget(QWidget* parent)
  : QWidget(parent)
  , m_channelCombo(nullptr)
  , m_valueLabel(nullptr)
  , m_leftCoarseButton(nullptr)
  , m_leftFineButton(nullptr)
  , m_rightCoarseButton(nullptr)
  , m_rightFineButton(nullptr)
  , m_stopButton(nullptr)
  , m_rotationGroup(nullptr)
  , m_rotationContent(nullptr)
  , m_rotationDeviceCombo(nullptr)
  , m_rotationAxisCombo(nullptr)
  , m_rotationStepSpin(nullptr)
  , m_thresholdSpin(nullptr)
  , m_maxNoImprovementSpin(nullptr)
  , m_rotationScanButton(nullptr)
{
  setupUI();
}

void ScanControlWidget::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(8);

  // === GROUP BOX WRAPPER ===
  QGroupBox* scanGroup = new QGroupBox("Hexapod Scanning");
  QVBoxLayout* groupLayout = new QVBoxLayout(scanGroup);
  groupLayout->setSpacing(6);

  // === CHANNEL SELECTION ===
  QHBoxLayout* channelLayout = new QHBoxLayout();
  QLabel* channelLabel = new QLabel("Data Channel:");
  m_channelCombo = new QComboBox();
  m_channelCombo->setMinimumWidth(120);
  channelLayout->addWidget(channelLabel);
  channelLayout->addWidget(m_channelCombo, 1);
  groupLayout->addLayout(channelLayout);

  // === VALUE DISPLAY (moved below dropdown) ===
  m_valueLabel = new QLabel("-- --");
  m_valueLabel->setStyleSheet(
    "QLabel { "
    "  background-color: #E3F2FD; "
    "  border: 2px solid #2196F3; "
    "  border-radius: 4px; "
    "  padding: 8px 16px; "
    "  font-size: 24pt; "
    "  font-weight: bold; "
    "  color: #1565C0; "
    "  min-height: 50px; "
    "}");
  m_valueLabel->setAlignment(Qt::AlignCenter);
  groupLayout->addWidget(m_valueLabel);

  // === SCAN BUTTONS - TWO COLUMNS ===
  QHBoxLayout* buttonsLayout = new QHBoxLayout();
  buttonsLayout->setSpacing(8);

  // LEFT COLUMN - Left Hexapod
  QVBoxLayout* leftColumn = new QVBoxLayout();
  leftColumn->setSpacing(4);

  QLabel* leftLabel = new QLabel("Left Hex:");
  leftLabel->setStyleSheet("font-weight: bold;");
  leftColumn->addWidget(leftLabel);

  m_leftCoarseButton = new QPushButton("Coarse");
  m_leftCoarseButton->setMinimumHeight(26);
  m_leftCoarseButton->setToolTip("Step sizes: 10, 5, 2, 1 µm");
  leftColumn->addWidget(m_leftCoarseButton);

  m_leftFineButton = new QPushButton("Fine");
  m_leftFineButton->setMinimumHeight(26);
  m_leftFineButton->setToolTip("Step sizes: 2, 1, 0.5 µm");
  leftColumn->addWidget(m_leftFineButton);

  buttonsLayout->addLayout(leftColumn);

  // RIGHT COLUMN - Right Hexapod
  QVBoxLayout* rightColumn = new QVBoxLayout();
  rightColumn->setSpacing(4);

  QLabel* rightLabel = new QLabel("Right Hex:");
  rightLabel->setStyleSheet("font-weight: bold;");
  rightColumn->addWidget(rightLabel);

  m_rightCoarseButton = new QPushButton("Coarse");
  m_rightCoarseButton->setMinimumHeight(26);
  m_rightCoarseButton->setToolTip("Step sizes: 10, 5, 2, 1 µm");
  rightColumn->addWidget(m_rightCoarseButton);

  m_rightFineButton = new QPushButton("Fine");
  m_rightFineButton->setMinimumHeight(26);
  m_rightFineButton->setToolTip("Step sizes: 2, 1, 0.5 µm");
  rightColumn->addWidget(m_rightFineButton);

  buttonsLayout->addLayout(rightColumn);

  // RIGHT COLUMN - Stop Button
  QVBoxLayout* stopColumn = new QVBoxLayout();
  stopColumn->setSpacing(4);

  QLabel* stopLabel = new QLabel("Control:");
  stopLabel->setStyleSheet("font-weight: bold;");
  stopColumn->addWidget(stopLabel);

  // === STOP BUTTON ===
  m_stopButton = new QPushButton("■ STOP\nScanning");
  m_stopButton->setMinimumHeight(60);
  m_stopButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #F44336; "
    "  color: white; "
    "  font-weight: bold; "
    "  padding: 6px; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.4); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.4); "
    "} "
    "QPushButton:hover { "
    "  background-color: #D32F2F; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #B71C1C; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 8px; "
    "  padding-left: 8px; "
    "} "
    "QPushButton:disabled { "
    "  background-color: #BDBDBD; "
    "  color: #757575; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.2); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.2); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.2); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.2); "
    "}");
  m_stopButton->setEnabled(false);  // Initially disabled
  stopColumn->addWidget(m_stopButton);

  buttonsLayout->addLayout(stopColumn);

  groupLayout->addLayout(buttonsLayout);

  mainLayout->addWidget(scanGroup);

  // === ROTATION SCANNING SECTION ===
  setupRotationUI(mainLayout);

  // === STYLING FOR SCAN BUTTONS (3D Windows-style) ===
  QString buttonStyle =
    "QPushButton { "
    "  background-color: #2196F3; "
    "  color: white; "
    "  font-weight: bold; "
    "  padding: 6px; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.4); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.4); "
    "} "
    "QPushButton:hover { "
    "  background-color: #1976D2; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #0D47A1; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 8px; "
    "  padding-left: 8px; "
    "} "
    "QPushButton:disabled { "
    "  background-color: #BDBDBD; "
    "  color: #757575; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.2); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.2); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.2); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.2); "
    "}";

  m_leftCoarseButton->setStyleSheet(buttonStyle);
  m_leftFineButton->setStyleSheet(buttonStyle);
  m_rightCoarseButton->setStyleSheet(buttonStyle);
  m_rightFineButton->setStyleSheet(buttonStyle);

  // === CONNECT SIGNALS ===
  connect(m_leftCoarseButton, &QPushButton::clicked, this, &ScanControlWidget::onLeftCoarseClicked);
  connect(m_leftFineButton, &QPushButton::clicked, this, &ScanControlWidget::onLeftFineClicked);
  connect(m_rightCoarseButton, &QPushButton::clicked, this, &ScanControlWidget::onRightCoarseClicked);
  connect(m_rightFineButton, &QPushButton::clicked, this, &ScanControlWidget::onRightFineClicked);
  connect(m_stopButton, &QPushButton::clicked, this, &ScanControlWidget::onStopClicked);

  // Connect channel combo box to emit signal when selection changes
  connect(m_channelCombo, &QComboBox::currentTextChanged, this, &ScanControlWidget::channelChanged);
}

void ScanControlWidget::setAvailableChannels(const QStringList& channels)
{
  m_channelCombo->clear();
  m_channelCombo->addItems(channels);
}

QString ScanControlWidget::currentChannel() const
{
  return m_channelCombo->currentText();
}

void ScanControlWidget::setCurrentChannel(const QString& channel)
{
  int index = m_channelCombo->findText(channel);
  if (index >= 0) {
    m_channelCombo->setCurrentIndex(index);
  }
}

void ScanControlWidget::updateChannelValue(double value, const QString& unit)
{
  if (unit.isEmpty()) {
    // If no unit provided, value is a raw number to format
    m_valueLabel->setText(QString("%1").arg(value, 0, 'f', 3));
  }
  else {
    // Legacy format with unit
    m_valueLabel->setText(QString("%1 %2").arg(value, 0, 'f', 3).arg(unit));
  }
}

void ScanControlWidget::setDisplayText(const QString& text)
{
  m_valueLabel->setText(text);
}

void ScanControlWidget::setEnabled(bool enabled)
{
  m_channelCombo->setEnabled(enabled);
  m_leftCoarseButton->setEnabled(enabled);
  m_leftFineButton->setEnabled(enabled);
  m_rightCoarseButton->setEnabled(enabled);
  m_rightFineButton->setEnabled(enabled);
  m_stopButton->setEnabled(!enabled);  // Stop button opposite of scan buttons
}

void ScanControlWidget::setScanButtonsEnabled(bool enabled)
{
  m_leftCoarseButton->setEnabled(enabled);
  m_leftFineButton->setEnabled(enabled);
  m_rightCoarseButton->setEnabled(enabled);
  m_rightFineButton->setEnabled(enabled);
  if (m_rotationScanButton) {
    m_rotationScanButton->setEnabled(enabled);
  }
}

void ScanControlWidget::setStopButtonEnabled(bool enabled)
{
  m_stopButton->setEnabled(enabled);
}

void ScanControlWidget::onLeftCoarseClicked()
{
  emitScanRequest("hex-left", true);
}

void ScanControlWidget::onLeftFineClicked()
{
  emitScanRequest("hex-left", false);
}

void ScanControlWidget::onRightCoarseClicked()
{
  emitScanRequest("hex-right", true);
}

void ScanControlWidget::onRightFineClicked()
{
  emitScanRequest("hex-right", false);
}

void ScanControlWidget::onStopClicked()
{
  emit stopRequested();
}

void ScanControlWidget::emitScanRequest(const QString& deviceName, bool isCoarse)
{
  QString channel = currentChannel();

  if (channel.isEmpty()) {
    // Widget can optionally show a warning, or let parent handle validation
    return;
  }

  emit scanRequested(deviceName, isCoarse, channel);
}

void ScanControlWidget::setupRotationUI(QVBoxLayout* parentLayout)
{
  // Create collapsible group box
  m_rotationGroup = new QGroupBox("Rotation Optimization");
  m_rotationGroup->setCheckable(true);
  m_rotationGroup->setChecked(false);  // Start collapsed

  QVBoxLayout* groupLayout = new QVBoxLayout(m_rotationGroup);
  groupLayout->setSpacing(4);
  groupLayout->setContentsMargins(6, 6, 6, 6);

  // Create content widget to hold all controls (for easy show/hide)
  m_rotationContent = new QWidget();
  QVBoxLayout* contentLayout = new QVBoxLayout(m_rotationContent);
  contentLayout->setSpacing(4);
  contentLayout->setContentsMargins(0, 0, 0, 0);

  // === ROW 1: Device Selection ===
  QHBoxLayout* deviceLayout = new QHBoxLayout();
  QLabel* deviceLabel = new QLabel("Device:");
  deviceLabel->setFixedWidth(70);
  m_rotationDeviceCombo = new QComboBox();
  m_rotationDeviceCombo->addItem("hex-right");
  m_rotationDeviceCombo->addItem("hex-left");
  deviceLayout->addWidget(deviceLabel);
  deviceLayout->addWidget(m_rotationDeviceCombo, 1);
  contentLayout->addLayout(deviceLayout);

  // === ROW 2: Axis Selection ===
  QHBoxLayout* axisLayout = new QHBoxLayout();
  QLabel* axisLabel = new QLabel("Axis:");
  axisLabel->setFixedWidth(70);
  m_rotationAxisCombo = new QComboBox();
  m_rotationAxisCombo->addItem("W (Roll)", "W");
  m_rotationAxisCombo->addItem("V (Pitch)", "V");
  m_rotationAxisCombo->addItem("U (Yaw)", "U");
  m_rotationAxisCombo->setToolTip(
    "W (Roll): rotates around X, scans X-Y\n"
    "V (Pitch): rotates around Y, scans X-Z\n"
    "U (Yaw): rotates around Z, scans Y-Z");
  axisLayout->addWidget(axisLabel);
  axisLayout->addWidget(m_rotationAxisCombo, 1);
  contentLayout->addLayout(axisLayout);

  // === ROW 3: Step Size ===
  QHBoxLayout* stepLayout = new QHBoxLayout();
  QLabel* stepLabel = new QLabel("Step (°):");
  stepLabel->setFixedWidth(70);
  m_rotationStepSpin = new QDoubleSpinBox();
  m_rotationStepSpin->setRange(0.01, 1.0);
  m_rotationStepSpin->setValue(0.2);
  m_rotationStepSpin->setSingleStep(0.05);
  m_rotationStepSpin->setDecimals(2);
  m_rotationStepSpin->setToolTip("Rotation step size in degrees");
  stepLayout->addWidget(stepLabel);
  stepLayout->addWidget(m_rotationStepSpin, 1);
  contentLayout->addLayout(stepLayout);

  // === ROW 4: Threshold ===
  QHBoxLayout* thresholdLayout = new QHBoxLayout();
  QLabel* thresholdLabel = new QLabel("Threshold:");
  thresholdLabel->setFixedWidth(70);
  m_thresholdSpin = new QDoubleSpinBox();
  m_thresholdSpin->setRange(0.1, 10.0);
  m_thresholdSpin->setValue(1.0);
  m_thresholdSpin->setSingleStep(0.5);
  m_thresholdSpin->setDecimals(1);
  m_thresholdSpin->setSuffix(" %");
  m_thresholdSpin->setToolTip("Minimum improvement percentage to continue");
  thresholdLayout->addWidget(thresholdLabel);
  thresholdLayout->addWidget(m_thresholdSpin, 1);
  contentLayout->addLayout(thresholdLayout);

  // === ROW 5: Max No-Improvement (hidden - using default value of 1) ===
  m_maxNoImprovementSpin = new QSpinBox();
  m_maxNoImprovementSpin->setRange(1, 5);
  m_maxNoImprovementSpin->setValue(1);  // Default to 1
  m_maxNoImprovementSpin->setVisible(false);  // Hidden

  // === ROW 6: Start Button ===
  m_rotationScanButton = new QPushButton("Start Rotation Scan");
  m_rotationScanButton->setMinimumHeight(32);
  m_rotationScanButton->setToolTip(
    "Optimize rotation axis by:\n"
    "1. Rotate by step size\n"
    "2. Run translation scan (XY/XZ/YZ)\n"
    "3. If improved, continue rotating\n"
    "4. Return to best position found");

  // Style for rotation button (green/teal)
  QString rotationButtonStyle =
    "QPushButton { "
    "  background-color: #009688; "
    "  color: white; "
    "  font-weight: bold; "
    "  padding: 6px; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.4); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.4); "
    "} "
    "QPushButton:hover { "
    "  background-color: #00796B; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #004D40; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 8px; "
    "  padding-left: 8px; "
    "} "
    "QPushButton:disabled { "
    "  background-color: #BDBDBD; "
    "  color: #757575; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.2); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.2); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.2); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.2); "
    "}";

  m_rotationScanButton->setStyleSheet(rotationButtonStyle);
  contentLayout->addWidget(m_rotationScanButton);

  // Add content widget to group
  groupLayout->addWidget(m_rotationContent);

  // Start collapsed
  m_rotationContent->setVisible(false);

  parentLayout->addWidget(m_rotationGroup);

  // Connect signals
  connect(m_rotationScanButton, &QPushButton::clicked, this, &ScanControlWidget::onRotationScanClicked);
  connect(m_rotationGroup, &QGroupBox::toggled, this, &ScanControlWidget::onRotationGroupToggled);
}

void ScanControlWidget::onRotationScanClicked()
{
  emitRotationScanRequest();
}

void ScanControlWidget::emitRotationScanRequest()
{
  QString channel = currentChannel();

  if (channel.isEmpty()) {
    return;
  }

  QString deviceName = m_rotationDeviceCombo->currentText();
  QString rotationAxis = m_rotationAxisCombo->currentData().toString();
  double rotationStep = m_rotationStepSpin->value();
  double threshold = m_thresholdSpin->value() / 100.0;  // Convert from % to decimal
  int maxNoImprovement = m_maxNoImprovementSpin->value();

  emit rotationScanRequested(deviceName, rotationAxis, rotationStep, threshold, maxNoImprovement, channel);
}

void ScanControlWidget::onRotationGroupToggled(bool checked)
{
  // Show/hide the content based on checkbox state
  if (m_rotationContent) {
    m_rotationContent->setVisible(checked);
  }
}