// GlobalMaximizeWidget.cpp
#include "GlobalMaximizeWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QVariant>

namespace {

// 3D Windows-style button look, matching ScanControlWidget for visual parity.
const char* kBlueButtonStyle =
  "QPushButton { "
  "  background-color: #2196F3; color: white; font-weight: bold; padding: 6px; "
  "  border-top: 2px solid rgba(255,255,255,0.4); border-left: 2px solid rgba(255,255,255,0.4); "
  "  border-bottom: 2px solid rgba(0,0,0,0.4); border-right: 2px solid rgba(0,0,0,0.4); } "
  "QPushButton:hover { background-color: #1976D2; } "
  "QPushButton:pressed { background-color: #0D47A1; padding-top: 8px; padding-left: 8px; } "
  "QPushButton:disabled { background-color: #BDBDBD; color: #757575; }";

const char* kRedButtonStyle =
  "QPushButton { "
  "  background-color: #F44336; color: white; font-weight: bold; padding: 6px; "
  "  border-top: 2px solid rgba(255,255,255,0.4); border-left: 2px solid rgba(255,255,255,0.4); "
  "  border-bottom: 2px solid rgba(0,0,0,0.4); border-right: 2px solid rgba(0,0,0,0.4); } "
  "QPushButton:hover { background-color: #D32F2F; } "
  "QPushButton:pressed { background-color: #B71C1C; padding-top: 8px; padding-left: 8px; } "
  "QPushButton:disabled { background-color: #BDBDBD; color: #757575; }";

}  // namespace

GlobalMaximizeWidget::GlobalMaximizeWidget(QWidget* parent)
  : QWidget(parent) {
  setupUI();
}

void GlobalMaximizeWidget::setupUI() {
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(8);

  QGroupBox* group = new QGroupBox("Dual-Hex Global Maximize");
  QVBoxLayout* groupLayout = new QVBoxLayout(group);
  groupLayout->setSpacing(6);

  // --- Channel + direction row ---
  QFormLayout* form = new QFormLayout();
  form->setSpacing(6);

  m_channelCombo = new QComboBox();
  m_channelCombo->setFixedWidth(320);
  form->addRow("Data Channel:", m_channelCombo);

  m_directionCombo = new QComboBox();
  m_directionCombo->addItem("Left → Right");   // index 0 (default)
  m_directionCombo->addItem("Right → Left");   // index 1
  m_directionCombo->setFixedWidth(320);
  form->addRow("Beam Direction:", m_directionCombo);

  // Z step size (um) — value stored as userData.
  m_zStepCombo = new QComboBox();
  m_zStepCombo->addItem("0.5 um", 0.5);
  m_zStepCombo->addItem("1 um",   1.0);
  m_zStepCombo->addItem("2 um",   2.0);
  m_zStepCombo->addItem("3 um",   3.0);
  m_zStepCombo->addItem("4 um",   4.0);
  m_zStepCombo->addItem("5 um",   5.0);
  m_zStepCombo->setCurrentIndex(2);  // 2 um default
  m_zStepCombo->setFixedWidth(320);
  form->addRow("Z Step Size:", m_zStepCombo);

  // XY grid step (um).
  m_gridStepCombo = new QComboBox();
  m_gridStepCombo->addItem("0.1 um", 0.1);
  m_gridStepCombo->addItem("0.2 um", 0.2);
  m_gridStepCombo->addItem("0.3 um", 0.3);
  m_gridStepCombo->addItem("0.5 um", 0.5);
  m_gridStepCombo->setCurrentIndex(1);  // 0.2 um default
  m_gridStepCombo->setFixedWidth(320);
  form->addRow("XY Grid Step:", m_gridStepCombo);

  // Grid size.
  m_gridSizeCombo = new QComboBox();
  m_gridSizeCombo->addItem("3 x 3", 3);
  m_gridSizeCombo->addItem("5 x 5", 5);
  m_gridSizeCombo->setCurrentIndex(0);  // 3x3 default
  m_gridSizeCombo->setFixedWidth(320);
  form->addRow("Grid Size:", m_gridSizeCombo);

  // Settle time (ms).
  m_settleCombo = new QComboBox();
  m_settleCombo->addItem("100 ms", 100);
  m_settleCombo->addItem("150 ms", 150);
  m_settleCombo->addItem("200 ms", 200);
  m_settleCombo->addItem("250 ms", 250);
  m_settleCombo->addItem("300 ms", 300);
  m_settleCombo->setCurrentIndex(2);  // 200 ms default
  m_settleCombo->setFixedWidth(320);
  form->addRow("Settle Time:", m_settleCombo);

  groupLayout->addLayout(form);

  // --- Value display ---
  m_valueLabel = new QLabel("-- --");
  m_valueLabel->setStyleSheet(
    "QLabel { background-color: #1e1e1e; border: 2px solid #3f3f46; border-radius: 4px; "
    "padding: 8px 16px; font-size: 24pt; font-weight: bold; color: #ffffff; min-height: 50px; }");
  m_valueLabel->setAlignment(Qt::AlignCenter);
  m_valueLabel->setFixedWidth(500);
  groupLayout->addWidget(m_valueLabel);

  // --- Start / Stop ---
  QHBoxLayout* buttonRow = new QHBoxLayout();
  buttonRow->setSpacing(8);

  m_startButton = new QPushButton("▶ Start Maximize");
  m_startButton->setFixedSize(180, 44);
  m_startButton->setStyleSheet(kBlueButtonStyle);

  m_stopButton = new QPushButton("■ STOP");
  m_stopButton->setFixedSize(140, 44);
  m_stopButton->setStyleSheet(kRedButtonStyle);
  m_stopButton->setEnabled(false);

  buttonRow->addWidget(m_startButton);
  buttonRow->addWidget(m_stopButton);
  buttonRow->addStretch();
  groupLayout->addLayout(buttonRow);

  // Anchor compact top-left like the scan widget.
  group->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  QHBoxLayout* anchor = new QHBoxLayout();
  anchor->setContentsMargins(0, 0, 0, 0);
  anchor->addWidget(group);
  anchor->addStretch();
  mainLayout->addLayout(anchor);

  connect(m_startButton, &QPushButton::clicked, this, &GlobalMaximizeWidget::startRequested);
  connect(m_stopButton,  &QPushButton::clicked, this, &GlobalMaximizeWidget::stopRequested);
}

void GlobalMaximizeWidget::setAvailableChannels(const QStringList& channels) {
  const QString current = m_channelCombo->currentText();
  m_channelCombo->clear();
  m_channelCombo->addItems(channels);
  if (!current.isEmpty()) {
    const int idx = m_channelCombo->findText(current);
    if (idx >= 0) {
      m_channelCombo->setCurrentIndex(idx);
    }
  }
}

QString GlobalMaximizeWidget::currentChannel() const {
  return m_channelCombo->currentText();
}

void GlobalMaximizeWidget::setCurrentChannel(const QString& channel) {
  const int idx = m_channelCombo->findText(channel);
  if (idx >= 0) {
    m_channelCombo->setCurrentIndex(idx);
  }
}

void GlobalMaximizeWidget::setDisplayText(const QString& text) {
  m_valueLabel->setText(text);
}

void GlobalMaximizeWidget::setRunningState(bool running) {
  m_channelCombo->setEnabled(!running);
  m_directionCombo->setEnabled(!running);
  m_zStepCombo->setEnabled(!running);
  m_gridStepCombo->setEnabled(!running);
  m_gridSizeCombo->setEnabled(!running);
  m_settleCombo->setEnabled(!running);
  m_startButton->setEnabled(!running);
  m_stopButton->setEnabled(running);
}

bool GlobalMaximizeWidget::isLeftToRight() const {
  return m_directionCombo->currentIndex() == 0;
}

QString GlobalMaximizeWidget::zStepDevice() const {
  // Beam left->right: the collimating (Z-stepping) side is hex-left.
  return isLeftToRight() ? QStringLiteral("hex-left") : QStringLiteral("hex-right");
}

QString GlobalMaximizeWidget::gridDevice() const {
  return isLeftToRight() ? QStringLiteral("hex-right") : QStringLiteral("hex-left");
}

double GlobalMaximizeWidget::zStepMm() const {
  return m_zStepCombo->currentData().toDouble() / 1000.0;  // um -> mm
}

double GlobalMaximizeWidget::gridStepMm() const {
  return m_gridStepCombo->currentData().toDouble() / 1000.0;  // um -> mm
}

int GlobalMaximizeWidget::gridSize() const {
  return m_gridSizeCombo->currentData().toInt();
}

int GlobalMaximizeWidget::settleMs() const {
  return m_settleCombo->currentData().toInt();
}
