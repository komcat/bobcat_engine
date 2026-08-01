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
  , m_rateLabel(nullptr)
  , m_leftCoarseButton(nullptr)
  , m_leftFineButton(nullptr)
  , m_leftUltraFineButton(nullptr)
  , m_rightCoarseButton(nullptr)
  , m_rightFineButton(nullptr)
  , m_rightUltraFineButton(nullptr)
  , m_stopButton(nullptr)
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
  m_channelCombo->setFixedWidth(320);
  channelLayout->addWidget(channelLabel);
  channelLayout->addWidget(m_channelCombo);
  channelLayout->addStretch();
  groupLayout->addLayout(channelLayout);

  // === VALUE DISPLAY (moved below dropdown) ===
  m_valueLabel = new QLabel("-- --");
  m_valueLabel->setStyleSheet(
    "QLabel { "
    "  background-color: #1e1e1e; "
    "  border: 2px solid #3f3f46; "
    "  border-radius: 4px; "
    "  padding: 8px 16px; "
    "  font-size: 24pt; "
    "  font-weight: bold; "
    "  color: #ffffff; "
    "  min-height: 50px; "
    "}");
  m_valueLabel->setAlignment(Qt::AlignCenter);
  m_valueLabel->setFixedWidth(500);  // keep aligned across fixed-width button columns
  groupLayout->addWidget(m_valueLabel);

  // === DATA RATE ===
  // Publish rate of the selected channel, measured from DataStore's monotonic
  // publish counter - NOT from this widget's repaint rate. This is the number
  // the scan's fresh-sample gate depends on: if it falls off, the gate starts
  // hitting its timeout ceiling and the scan silently reverts to using stale
  // samples. Keep an eye on it before blaming the scan for being slow.
  m_rateLabel = new QLabel("-- Hz");
  m_rateLabel->setStyleSheet(
    "QLabel { color: #9a9a9a; font-size: 9pt; padding: 0px 4px 4px 4px; }");
  m_rateLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  m_rateLabel->setFixedWidth(500);  // match the value label so it right-aligns under it
  m_rateLabel->setToolTip(
    "Publish rate of the selected data channel.\n"
    "Measured from the DataStore publish counter, not the UI refresh rate.\n"
    "The scan's fresh-sample gate waits for new publishes, so this rate\n"
    "directly sets how fast a scan can step.");
  groupLayout->addWidget(m_rateLabel);

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
  m_leftCoarseButton->setFixedSize(140, 26);
  m_leftCoarseButton->setToolTip("Step sizes: 10, 5, 2, 1 µm");
  leftColumn->addWidget(m_leftCoarseButton);

  m_leftFineButton = new QPushButton("Fine");
  m_leftFineButton->setFixedSize(140, 26);
  m_leftFineButton->setToolTip("Step sizes: 2, 1, 0.5 µm");
  leftColumn->addWidget(m_leftFineButton);

  m_leftUltraFineButton = new QPushButton("Ultra Fine");
  m_leftUltraFineButton->setFixedSize(140, 26);
  m_leftUltraFineButton->setToolTip("Step sizes: 0.5, 0.2, 0.1 µm");
  leftColumn->addWidget(m_leftUltraFineButton);

  buttonsLayout->addLayout(leftColumn);

  // RIGHT COLUMN - Right Hexapod
  QVBoxLayout* rightColumn = new QVBoxLayout();
  rightColumn->setSpacing(4);

  QLabel* rightLabel = new QLabel("Right Hex:");
  rightLabel->setStyleSheet("font-weight: bold;");
  rightColumn->addWidget(rightLabel);

  m_rightCoarseButton = new QPushButton("Coarse");
  m_rightCoarseButton->setFixedSize(140, 26);
  m_rightCoarseButton->setToolTip("Step sizes: 10, 5, 2, 1 µm");
  rightColumn->addWidget(m_rightCoarseButton);

  m_rightFineButton = new QPushButton("Fine");
  m_rightFineButton->setFixedSize(140, 26);
  m_rightFineButton->setToolTip("Step sizes: 2, 1, 0.5 µm");
  rightColumn->addWidget(m_rightFineButton);

  m_rightUltraFineButton = new QPushButton("Ultra Fine");
  m_rightUltraFineButton->setFixedSize(140, 26);
  m_rightUltraFineButton->setToolTip("Step sizes: 0.5, 0.2, 0.1 µm");
  rightColumn->addWidget(m_rightUltraFineButton);

  buttonsLayout->addLayout(rightColumn);

  // RIGHT COLUMN - Stop Button
  QVBoxLayout* stopColumn = new QVBoxLayout();
  stopColumn->setSpacing(4);

  QLabel* stopLabel = new QLabel("Control:");
  stopLabel->setStyleSheet("font-weight: bold;");
  stopColumn->addWidget(stopLabel);

  // === STOP BUTTON ===
  m_stopButton = new QPushButton("■ STOP\nScanning");
  m_stopButton->setFixedSize(140, 90);
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

  // Anchor scanGroup top-left with trailing horizontal + vertical stretches
  // so the widget stays compact and doesn't stretch to fill the tab.
  scanGroup->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  QHBoxLayout* anchorLayout = new QHBoxLayout();
  anchorLayout->setContentsMargins(0, 0, 0, 0);
  anchorLayout->addWidget(scanGroup);
  anchorLayout->addStretch();
  mainLayout->addLayout(anchorLayout);
  mainLayout->addStretch();

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
  m_leftUltraFineButton->setStyleSheet(buttonStyle);
  m_rightCoarseButton->setStyleSheet(buttonStyle);
  m_rightFineButton->setStyleSheet(buttonStyle);
  m_rightUltraFineButton->setStyleSheet(buttonStyle);

  // === CONNECT SIGNALS ===
  connect(m_leftCoarseButton, &QPushButton::clicked, this, &ScanControlWidget::onLeftCoarseClicked);
  connect(m_leftFineButton, &QPushButton::clicked, this, &ScanControlWidget::onLeftFineClicked);
  connect(m_leftUltraFineButton, &QPushButton::clicked, this, &ScanControlWidget::onLeftUltraFineClicked);
  connect(m_rightCoarseButton, &QPushButton::clicked, this, &ScanControlWidget::onRightCoarseClicked);
  connect(m_rightFineButton, &QPushButton::clicked, this, &ScanControlWidget::onRightFineClicked);
  connect(m_rightUltraFineButton, &QPushButton::clicked, this, &ScanControlWidget::onRightUltraFineClicked);
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

void ScanControlWidget::setDataRate(double hz)
{
  if (hz < 0.0) {
    // Unknown - no channel selected, or not enough samples to measure yet.
    m_rateLabel->setText("-- Hz");
    m_rateLabel->setStyleSheet(
      "QLabel { color: #9a9a9a; font-size: 9pt; padding: 0px 4px 4px 4px; }");
    return;
  }

  // Colour is a health cue, not decoration: a stalled stream stops the scan
  // dead (the fresh-sample gate falls back to stale values), so make zero
  // impossible to miss.
  const char* colour = "#9a9a9a";
  if (hz <= 0.05) {
    colour = "#e05561";        // stalled
  }
  else if (hz < 5.0) {
    colour = "#d19a66";        // very slow - scan will crawl
  }

  m_rateLabel->setText(QString("%1 Hz").arg(hz, 0, 'f', 1));
  m_rateLabel->setStyleSheet(
    QString("QLabel { color: %1; font-size: 9pt; padding: 0px 4px 4px 4px; }").arg(colour));
}

void ScanControlWidget::setEnabled(bool enabled)
{
  m_channelCombo->setEnabled(enabled);
  m_leftCoarseButton->setEnabled(enabled);
  m_leftFineButton->setEnabled(enabled);
  m_leftUltraFineButton->setEnabled(enabled);
  m_rightCoarseButton->setEnabled(enabled);
  m_rightFineButton->setEnabled(enabled);
  m_rightUltraFineButton->setEnabled(enabled);
  m_stopButton->setEnabled(!enabled);  // Stop button opposite of scan buttons
}

void ScanControlWidget::setScanButtonsEnabled(bool enabled)
{
  m_leftCoarseButton->setEnabled(enabled);
  m_leftFineButton->setEnabled(enabled);
  m_leftUltraFineButton->setEnabled(enabled);
  m_rightCoarseButton->setEnabled(enabled);
  m_rightFineButton->setEnabled(enabled);
  m_rightUltraFineButton->setEnabled(enabled);
}

void ScanControlWidget::setStopButtonEnabled(bool enabled)
{
  m_stopButton->setEnabled(enabled);
}

void ScanControlWidget::onLeftCoarseClicked()
{
  emitScanRequest("hex-left", ScanMode::Coarse);
}

void ScanControlWidget::onLeftFineClicked()
{
  emitScanRequest("hex-left", ScanMode::Fine);
}

void ScanControlWidget::onLeftUltraFineClicked()
{
  emitScanRequest("hex-left", ScanMode::UltraFine);
}

void ScanControlWidget::onRightCoarseClicked()
{
  emitScanRequest("hex-right", ScanMode::Coarse);
}

void ScanControlWidget::onRightFineClicked()
{
  emitScanRequest("hex-right", ScanMode::Fine);
}

void ScanControlWidget::onRightUltraFineClicked()
{
  emitScanRequest("hex-right", ScanMode::UltraFine);
}

void ScanControlWidget::onStopClicked()
{
  emit stopRequested();
}

void ScanControlWidget::emitScanRequest(const QString& deviceName, ScanMode mode)
{
  QString channel = currentChannel();

  if (channel.isEmpty()) {
    // Widget can optionally show a warning, or let parent handle validation
    return;
  }

  emit scanRequested(deviceName, mode, channel);
}

