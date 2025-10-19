// MotionControlPanel.cpp
#include "MotionControlPanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFrame>
#include <QScrollArea>

MotionControlPanel::MotionControlPanel(QWidget* parent)
  : QWidget(parent)
  , m_deviceCombo(nullptr)
  , m_posNameLabel(nullptr)
  , m_moveToButton(nullptr)
  , m_updatePosButton(nullptr)
  , m_deletePosButton(nullptr)
  , m_createPosButton(nullptr)
  , m_homeButton(nullptr)
  , m_stopButton(nullptr)
{
  setupUI();
}

void MotionControlPanel::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(8, 8, 8, 8);
  mainLayout->setSpacing(8);

  // === Device Selection (Compact) ===
  QLabel* deviceLabel = new QLabel("Device");
  deviceLabel->setStyleSheet("QLabel { color: #2c3e50; font-size: 11px; font-weight: bold; }");
  mainLayout->addWidget(deviceLabel);

  m_deviceCombo = new QComboBox();
  m_deviceCombo->setMinimumHeight(32);
  mainLayout->addWidget(m_deviceCombo);

  // Separator
  QFrame* line1 = new QFrame();
  line1->setFrameShape(QFrame::HLine);
  line1->setStyleSheet("QFrame { background-color: #bdc3c7; margin: 4px 0; }");
  mainLayout->addWidget(line1);

  // === Selected Position (Compact Grid) ===
  QLabel* posLabel = new QLabel("Selected Position");
  posLabel->setStyleSheet("QLabel { color: #2c3e50; font-size: 11px; font-weight: bold; }");
  mainLayout->addWidget(posLabel);

  // Compact info widget
  QWidget* posWidget = new QWidget();
  posWidget->setStyleSheet("QWidget { background-color: #f8f9fa; border-radius: 4px; }");
  QVBoxLayout* posLayout = new QVBoxLayout(posWidget);
  posLayout->setContentsMargins(8, 8, 8, 8);
  posLayout->setSpacing(4);

  m_posNameLabel = new QLabel("-");
  m_posXLabel = new QLabel("X: -");
  m_posYLabel = new QLabel("Y: -");
  m_posZLabel = new QLabel("Z: -");
  m_posULabel = new QLabel("U: -");
  m_posVLabel = new QLabel("V: -");
  m_posWLabel = new QLabel("W: -");

  // ✅ NEW - Waypoints label
  m_waypointsLabel = new QLabel("Waypoints: -");
  m_waypointsLabel->setWordWrap(true);
  m_waypointsLabel->setStyleSheet("QLabel { color: #2980b9; font-size: 11px; font-weight: bold; }");

  // Compact label style
  QList<QLabel*> labels = { m_posNameLabel, m_posXLabel, m_posYLabel,
                           m_posZLabel, m_posULabel, m_posVLabel, m_posWLabel,
                           m_waypointsLabel };  // ✅ Added waypoints label
  for (auto* label : labels) {
    if (label != m_waypointsLabel) {  // Don't override waypoint label style
      label->setStyleSheet("QLabel { color: #2c3e50; font-size: 11px; }");
    }
    posLayout->addWidget(label);
  }

  mainLayout->addWidget(posWidget);

  // Separator
  QFrame* line2 = new QFrame();
  line2->setFrameShape(QFrame::HLine);
  line2->setStyleSheet("QFrame { background-color: #bdc3c7; margin: 4px 0; }");
  mainLayout->addWidget(line2);

  // === Actions (Compact) ===
  QLabel* actionsLabel = new QLabel("Actions");
  actionsLabel->setStyleSheet("QLabel { color: #2c3e50; font-size: 11px; font-weight: bold; }");
  mainLayout->addWidget(actionsLabel);

  // Move to Position (Primary action)
  m_moveToButton = new QPushButton("→ Move to Position");
  m_moveToButton->setMinimumHeight(38);
  m_moveToButton->setEnabled(false);
  mainLayout->addWidget(m_moveToButton);

  // ✅ NEW - Move with Offset button
  m_moveOffsetButton = new QPushButton("⇄ Move with Offset");
  m_moveOffsetButton->setMinimumHeight(38);
  m_moveOffsetButton->setEnabled(false);
  mainLayout->addWidget(m_moveOffsetButton);

  // Update Position
  m_updatePosButton = new QPushButton("↻ Update Position");
  m_updatePosButton->setMinimumHeight(34);
  m_updatePosButton->setEnabled(false);
  mainLayout->addWidget(m_updatePosButton);

  // ✅ NEW - Edit Waypoints button
  m_editWaypointsBtn = new QPushButton("⚙ Edit Waypoints");
  m_editWaypointsBtn->setMinimumHeight(34);
  m_editWaypointsBtn->setEnabled(false);
  mainLayout->addWidget(m_editWaypointsBtn);

  // Delete Position button
  m_deletePosButton = new QPushButton("✕ Delete Position");
  m_deletePosButton->setMinimumHeight(34);
  m_deletePosButton->setEnabled(false);
  mainLayout->addWidget(m_deletePosButton);

  // Create New Position
  m_createPosButton = new QPushButton("+ Create Position");
  m_createPosButton->setMinimumHeight(34);
  mainLayout->addWidget(m_createPosButton);

  // Thin separator
  QFrame* line3 = new QFrame();
  line3->setFrameShape(QFrame::HLine);
  line3->setStyleSheet("QFrame { background-color: #bdc3c7; margin: 4px 0; }");
  mainLayout->addWidget(line3);

  // Home Button
  m_homeButton = new QPushButton("⌂ Home");
  m_homeButton->setMinimumHeight(34);
  mainLayout->addWidget(m_homeButton);

  // Emergency Stop
  m_stopButton = new QPushButton("⚠ STOP");
  m_stopButton->setMinimumHeight(34);
  mainLayout->addWidget(m_stopButton);

  // Push everything to top
  mainLayout->addStretch();

  // === Compact Styling ===
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
    // Move to Position - Green primary
    "QPushButton#moveToButton {"
    "  background-color: #27ae60;"
    "  color: white;"
    "  border: 1px solid #229954;"
    "  border-radius: 4px;"
    "  font-size: 12px;"
    "  font-weight: bold;"
    "}"
    "QPushButton#moveToButton:hover {"
    "  background-color: #229954;"
    "}"

    // Move with Offset - Orange
    "QPushButton#moveOffsetButton {"
    "  background-color: #f39c12;"
    "  color: white;"
    "  border: 1px solid #e67e22;"
    "  border-radius: 4px;"
    "  font-size: 12px;"
    "  font-weight: bold;"
    "}"
    "QPushButton#moveOffsetButton:hover {"
    "  background-color: #e67e22;"
    "}"
    // Regular buttons - Blue
    "QPushButton {"
    "  background-color: #3498db;"
    "  color: white;"
    "  border: 1px solid #2980b9;"
    "  border-radius: 4px;"
    "  font-size: 11px;"
    "  font-weight: 600;"
    "}"
    "QPushButton:hover {"
    "  background-color: #2980b9;"
    "}"
    "QPushButton:pressed {"
    "  background-color: #21618c;"
    "}"
    "QPushButton:disabled {"
    "  background-color: #ecf0f1;"
    "  color: #95a5a6;"
    "  border-color: #bdc3c7;"
    "}"
    // Delete button - Red
    "QPushButton#deletePosButton {"
    "  background-color: #e74c3c;"
    "  border-color: #c0392b;"
    "}"
    "QPushButton#deletePosButton:hover {"
    "  background-color: #c0392b;"
    "}"
    // Stop button - Dark Red
    "QPushButton#stopButton {"
    "  background-color: #c0392b;"
    "  border-color: #a93226;"
    "}"
    "QPushButton#stopButton:hover {"
    "  background-color: #a93226;"
    "}"
  );

  // Set object names for specific styling
  m_moveToButton->setObjectName("moveToButton");
  m_moveOffsetButton->setObjectName("moveOffsetButton");  // ✅ NEW
  m_deletePosButton->setObjectName("deletePosButton");
  m_stopButton->setObjectName("stopButton");

  // === Connect Signals ===
  connect(m_deviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
    this, [this]() {
    emit deviceChanged(m_deviceCombo->currentText());
  });

  connect(m_moveToButton, &QPushButton::clicked,
    this, &MotionControlPanel::moveToPositionClicked);

  // ✅ NEW
  connect(m_moveOffsetButton, &QPushButton::clicked,
    this, &MotionControlPanel::moveWithOffsetClicked);

  connect(m_updatePosButton, &QPushButton::clicked,
    this, &MotionControlPanel::updatePositionClicked);

  // ✅ NEW - Connect edit waypoints button
  connect(m_editWaypointsBtn, &QPushButton::clicked,
    this, &MotionControlPanel::editWaypointsClicked);

  connect(m_deletePosButton, &QPushButton::clicked,
    this, &MotionControlPanel::deletePositionClicked);

  connect(m_createPosButton, &QPushButton::clicked,
    this, &MotionControlPanel::createPositionClicked);

  connect(m_homeButton, &QPushButton::clicked,
    this, &MotionControlPanel::homeClicked);

  connect(m_stopButton, &QPushButton::clicked,
    this, &MotionControlPanel::stopClicked);
}

void MotionControlPanel::setDevices(const QStringList& devices)
{
  m_deviceCombo->clear();
  m_deviceCombo->addItems(devices);
}

void MotionControlPanel::setSelectedPosition(const QString& name, double x, double y, double z,
  double u, double v, double w,
  const std::vector<std::string>& waypoints)  // ✅ UPDATED - added waypoints parameter
{
  m_selectedPositionName = name;

  // Compact format
  m_posNameLabel->setText(QString("<b>%1</b>").arg(name));
  m_posXLabel->setText(QString("X: %1").arg(x, 0, 'f', 3));
  m_posYLabel->setText(QString("Y: %1").arg(y, 0, 'f', 3));
  m_posZLabel->setText(QString("Z: %1").arg(z, 0, 'f', 3));
  m_posULabel->setText(QString("U: %1").arg(u, 0, 'f', 3));
  m_posVLabel->setText(QString("V: %1").arg(v, 0, 'f', 3));
  m_posWLabel->setText(QString("W: %1").arg(w, 0, 'f', 3));

  // ✅ NEW - Display waypoints
  if (waypoints.empty()) {
    m_waypointsLabel->setText("Waypoints: None");
  }
  else {
    QString waypointStr = "Waypoints: ";
    for (size_t i = 0; i < waypoints.size(); ++i) {
      waypointStr += QString::fromStdString(waypoints[i]);
      if (i < waypoints.size() - 1) {
        waypointStr += " → ";
      }
    }
    m_waypointsLabel->setText(waypointStr);
  }

  // Enable all position-specific buttons
  m_moveToButton->setEnabled(true);
  m_moveOffsetButton->setEnabled(true);  // ✅ NEW
  m_updatePosButton->setEnabled(true);
  m_editWaypointsBtn->setEnabled(true);  // ✅ NEW
  m_deletePosButton->setEnabled(true);
}

void MotionControlPanel::clearSelectedPosition()
{
  m_selectedPositionName.clear();

  m_posNameLabel->setText("-");
  m_posXLabel->setText("X: -");
  m_posYLabel->setText("Y: -");
  m_posZLabel->setText("Z: -");
  m_posULabel->setText("U: -");
  m_posVLabel->setText("V: -");
  m_posWLabel->setText("W: -");
  m_waypointsLabel->setText("Waypoints: -");  // ✅ NEW

  // Disable all position-specific buttons
  m_moveToButton->setEnabled(false);
  m_moveOffsetButton->setEnabled(false);  // ✅ NEW
  m_updatePosButton->setEnabled(false);
  m_editWaypointsBtn->setEnabled(false);  // ✅ NEW
  m_deletePosButton->setEnabled(false);
}

QString MotionControlPanel::getCurrentDevice() const
{
  return m_deviceCombo->currentText();
}