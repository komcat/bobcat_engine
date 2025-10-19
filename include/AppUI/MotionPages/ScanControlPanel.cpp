// ScanControlPanel.cpp
#include "ScanControlPanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFrame>

ScanControlPanel::ScanControlPanel(QWidget* parent)
  : QWidget(parent)
  , m_deviceCombo(nullptr)
  , m_presetCombo(nullptr)
  , m_dataChannelCombo(nullptr)
  , m_startScanButton(nullptr)
  , m_stopScanButton(nullptr)
  , m_statusLabel(nullptr)
  , m_progressLabel(nullptr)
{
  setupUI();
}

void ScanControlPanel::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(8, 8, 8, 8);
  mainLayout->setSpacing(8);

  // === Title ===
  QLabel* titleLabel = new QLabel("SCANNING CONTROL");
  titleLabel->setStyleSheet(
    "QLabel {"
    "  color: #2c3e50;"
    "  font-size: 13px;"
    "  font-weight: bold;"
    "  padding: 4px;"
    "}"
  );
  mainLayout->addWidget(titleLabel);

  // Separator
  QFrame* line1 = new QFrame();
  line1->setFrameShape(QFrame::HLine);
  line1->setStyleSheet("QFrame { background-color: #bdc3c7; margin: 4px 0; }");
  mainLayout->addWidget(line1);

  // === Device Selection ===
  QLabel* deviceLabel = new QLabel("Device");
  deviceLabel->setStyleSheet("QLabel { color: #2c3e50; font-size: 11px; font-weight: bold; }");
  mainLayout->addWidget(deviceLabel);

  m_deviceCombo = new QComboBox();
  m_deviceCombo->setMinimumHeight(32);
  mainLayout->addWidget(m_deviceCombo);

  // Separator
  QFrame* line2 = new QFrame();
  line2->setFrameShape(QFrame::HLine);
  line2->setStyleSheet("QFrame { background-color: #bdc3c7; margin: 4px 0; }");
  mainLayout->addWidget(line2);

  // === Scan Preset ===
  QLabel* presetLabel = new QLabel("Scan Preset");
  presetLabel->setStyleSheet("QLabel { color: #2c3e50; font-size: 11px; font-weight: bold; }");
  mainLayout->addWidget(presetLabel);

  m_presetCombo = new QComboBox();
  m_presetCombo->addItem("Coarse");
  m_presetCombo->addItem("Fine");
  m_presetCombo->setMinimumHeight(32);
  mainLayout->addWidget(m_presetCombo);

  // Separator
  QFrame* line3 = new QFrame();
  line3->setFrameShape(QFrame::HLine);
  line3->setStyleSheet("QFrame { background-color: #bdc3c7; margin: 4px 0; }");
  mainLayout->addWidget(line3);

  // === Data Channel Selection ===
  QLabel* channelLabel = new QLabel("Data Channel");
  channelLabel->setStyleSheet("QLabel { color: #2c3e50; font-size: 11px; font-weight: bold; }");
  mainLayout->addWidget(channelLabel);

  m_dataChannelCombo = new QComboBox();
  m_dataChannelCombo->setMinimumHeight(32);
  mainLayout->addWidget(m_dataChannelCombo);

  // Separator
  QFrame* line4 = new QFrame();
  line4->setFrameShape(QFrame::HLine);
  line4->setStyleSheet("QFrame { background-color: #bdc3c7; margin: 4px 0; }");
  mainLayout->addWidget(line4);

  // === Scan Control Buttons ===

  // Start Scan Button
  m_startScanButton = new QPushButton("▶ START SCAN");
  m_startScanButton->setMinimumHeight(44);
  mainLayout->addWidget(m_startScanButton);

  // Stop Scan Button
  m_stopScanButton = new QPushButton("⏹ STOP SCAN");
  m_stopScanButton->setMinimumHeight(38);
  m_stopScanButton->setEnabled(false);
  mainLayout->addWidget(m_stopScanButton);

  // Separator
  QFrame* line5 = new QFrame();
  line5->setFrameShape(QFrame::HLine);
  line5->setStyleSheet("QFrame { background-color: #bdc3c7; margin: 4px 0; }");
  mainLayout->addWidget(line5);

  // === Status Display ===
  QLabel* statusTitleLabel = new QLabel("Status");
  statusTitleLabel->setStyleSheet("QLabel { color: #2c3e50; font-size: 11px; font-weight: bold; }");
  mainLayout->addWidget(statusTitleLabel);

  // Status widget with background
  QWidget* statusWidget = new QWidget();
  statusWidget->setStyleSheet("QWidget { background-color: #f8f9fa; border-radius: 4px; }");
  QVBoxLayout* statusLayout = new QVBoxLayout(statusWidget);
  statusLayout->setContentsMargins(8, 8, 8, 8);
  statusLayout->setSpacing(4);

  m_statusLabel = new QLabel("Idle");
  m_statusLabel->setStyleSheet("QLabel { color: #7f8c8d; font-size: 11px; font-weight: bold; }");
  statusLayout->addWidget(m_statusLabel);

  m_progressLabel = new QLabel("Progress: -");
  m_progressLabel->setStyleSheet("QLabel { color: #2c3e50; font-size: 11px; }");
  statusLayout->addWidget(m_progressLabel);

  mainLayout->addWidget(statusWidget);

  // Push everything to top
  mainLayout->addStretch();

  // === Styling ===
  setStyleSheet(
    "QWidget {"
    "  background-color: white;"
    "}"
    "QComboBox {"
    "  background-color: white;"
    "  color: #2c3e50;"
    "  border: 1px solid #bdc3c7;"
    "  border-radius: 4px;"
    "  padding: 6px;"
    "  font-size: 12px;"
    "}"
    "QComboBox:hover {"
    "  border-color: #3498db;"
    "}"
    "QComboBox::drop-down {"
    "  border: none;"
    "  width: 20px;"
    "}"
    "QComboBox::down-arrow {"
    "  image: none;"
    "  border-left: 4px solid transparent;"
    "  border-right: 4px solid transparent;"
    "  border-top: 5px solid #2c3e50;"
    "  margin-right: 6px;"
    "}"
    "QComboBox QAbstractItemView {"
    "  background-color: white;"
    "  color: #2c3e50;"
    "  selection-background-color: #3498db;"
    "  border: 1px solid #bdc3c7;"
    "}"
    // Start Scan Button - Big Green
    "QPushButton#startScanButton {"
    "  background-color: #27ae60;"
    "  color: white;"
    "  border: 2px solid #229954;"
    "  border-radius: 6px;"
    "  font-size: 13px;"
    "  font-weight: bold;"
    "}"
    "QPushButton#startScanButton:hover {"
    "  background-color: #229954;"
    "}"
    "QPushButton#startScanButton:disabled {"
    "  background-color: #95a5a6;"
    "  border-color: #7f8c8d;"
    "}"
    // Stop Scan Button - Red
    "QPushButton#stopScanButton {"
    "  background-color: #e74c3c;"
    "  color: white;"
    "  border: 1px solid #c0392b;"
    "  border-radius: 4px;"
    "  font-size: 12px;"
    "  font-weight: bold;"
    "}"
    "QPushButton#stopScanButton:hover {"
    "  background-color: #c0392b;"
    "}"
    "QPushButton#stopScanButton:disabled {"
    "  background-color: #ecf0f1;"
    "  color: #95a5a6;"
    "  border-color: #bdc3c7;"
    "}"
  );

  // Set object names for styling
  m_startScanButton->setObjectName("startScanButton");
  m_stopScanButton->setObjectName("stopScanButton");

  // === Connect Signals ===
  connect(m_deviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
    this, [this]() {
    emit deviceChanged(m_deviceCombo->currentText());
  });

  connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
    this, [this]() {
    emit presetChanged(m_presetCombo->currentText());
  });

  connect(m_dataChannelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
    this, [this]() {
    emit dataChannelChanged(m_dataChannelCombo->currentText());
  });

  connect(m_startScanButton, &QPushButton::clicked,
    this, &ScanControlPanel::startScanClicked);

  connect(m_stopScanButton, &QPushButton::clicked,
    this, &ScanControlPanel::stopScanClicked);
}

void ScanControlPanel::setDevices(const QStringList& devices)
{
  m_deviceCombo->clear();
  m_deviceCombo->addItems(devices);
}

void ScanControlPanel::setDataChannels(const QStringList& channels)
{
  m_dataChannelCombo->clear();
  m_dataChannelCombo->addItems(channels);
}

QString ScanControlPanel::getCurrentDevice() const
{
  return m_deviceCombo->currentText();
}

QString ScanControlPanel::getCurrentPreset() const
{
  return m_presetCombo->currentText();
}

QString ScanControlPanel::getCurrentDataChannel() const
{
  return m_dataChannelCombo->currentText();
}

void ScanControlPanel::setStatus(const QString& status)
{
  m_statusLabel->setText(status);

  // Update color based on status
  if (status.contains("Idle", Qt::CaseInsensitive)) {
    m_statusLabel->setStyleSheet("QLabel { color: #7f8c8d; font-size: 11px; font-weight: bold; }");
  }
  else if (status.contains("Scanning", Qt::CaseInsensitive) ||
    status.contains("Running", Qt::CaseInsensitive)) {
    m_statusLabel->setStyleSheet("QLabel { color: #27ae60; font-size: 11px; font-weight: bold; }");
  }
  else if (status.contains("Error", Qt::CaseInsensitive) ||
    status.contains("Failed", Qt::CaseInsensitive)) {
    m_statusLabel->setStyleSheet("QLabel { color: #e74c3c; font-size: 11px; font-weight: bold; }");
  }
  else if (status.contains("Complete", Qt::CaseInsensitive)) {
    m_statusLabel->setStyleSheet("QLabel { color: #3498db; font-size: 11px; font-weight: bold; }");
  }
}

void ScanControlPanel::setProgress(double progress)
{
  if (progress < 0) {
    m_progressLabel->setText("Progress: -");
  }
  else {
    m_progressLabel->setText(QString("Progress: %1%").arg(
      static_cast<int>(progress * 100)));
  }
}

void ScanControlPanel::setScanActive(bool active)
{
  m_startScanButton->setEnabled(!active);
  m_stopScanButton->setEnabled(active);
  m_deviceCombo->setEnabled(!active);
  m_presetCombo->setEnabled(!active);
  m_dataChannelCombo->setEnabled(!active);
}