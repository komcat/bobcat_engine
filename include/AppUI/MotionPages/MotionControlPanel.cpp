// MotionControlPanel.cpp
#include "MotionControlPanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFrame>
#include <QScrollArea>
#include <QPalette>
#include <QColor>

MotionControlPanel::MotionControlPanel(QWidget* parent)
  : QWidget(parent)
  , m_deviceCombo(nullptr)
  , m_posNameLabel(nullptr)
  , m_moveToButton(nullptr)
  , m_editPosButton(nullptr)
  , m_updatePosButton(nullptr)
  , m_deletePosButton(nullptr)
  , m_createPosButton(nullptr)
  , m_autoFitButton(nullptr)  // ✅ NEW
  , m_homeButton(nullptr)
  , m_stopButton(nullptr)
{
  setupUI();
}

void MotionControlPanel::setupUI()
{
  // Set dark background for the entire widget
  setAutoFillBackground(true);
  QPalette pal = palette();
  pal.setColor(QPalette::Window, QColor(45, 45, 48));  // #2d2d30
  setPalette(pal);

  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(6, 6, 6, 6);
  mainLayout->setSpacing(6);

  // === Device Selection ===
  // ✅ HIDDEN - Device selection now handled by DeviceButtonStack on far left
  m_deviceCombo = new QComboBox();
  m_deviceCombo->setMinimumHeight(28);
  m_deviceCombo->setVisible(false);  // Hide combo box

  // ✅ REMOVED: Selected Position section - now shown in DeviceButtonStack (far left panel)
  // The position info widgets are still created but hidden, so existing code that updates them won't break

  m_posNameLabel = new QLabel("-");
  m_posXLabel = new QLabel("X: -");
  m_posYLabel = new QLabel("Y: -");
  m_posZLabel = new QLabel("Z: -");
  m_posULabel = new QLabel("U: -");
  m_posVLabel = new QLabel("V: -");
  m_posWLabel = new QLabel("W: -");
  m_waypointsLabel = new QLabel("Waypoints: -");

  // Hide all position labels (not added to layout)
  m_posNameLabel->hide();
  m_posXLabel->hide();
  m_posYLabel->hide();
  m_posZLabel->hide();
  m_posULabel->hide();
  m_posVLabel->hide();
  m_posWLabel->hide();
  m_waypointsLabel->hide();

  // === Tab Widget ===
  m_tabWidget = new QTabWidget();
  m_tabWidget->setStyleSheet(
    "QTabWidget::pane {"
    "  border: 1px solid #3f3f46;"
    "  background-color: #2d2d30;"
    "}"
    "QTabBar::tab {"
    "  background-color: #1e1e1e;"
    "  color: #888888;"
    "  padding: 8px 16px;"
    "  margin-right: 2px;"
    "  border: none;"
    "  font-size: 12px;"
    "  font-weight: bold;"
    "  border-top-left-radius: 3px;"
    "  border-top-right-radius: 3px;"
    "  border-bottom: 3px solid transparent;"
    "}"
    "QTabBar::tab:selected {"
    "  background-color: #0e639c;"
    "  color: #ffffff;"
    "  font-weight: bold;"
    "  border-bottom: 3px solid #0e639c;"
    "}"
    "QTabBar::tab:hover:!selected {"
    "  background-color: #2a2d2e;"
    "  color: #d4d4d4;"
    "}"
  );

  setupActionsTab();
  setupGraphTab();

  mainLayout->addWidget(m_tabWidget);

  // === Dark Theme Styling ===
  setStyleSheet(
    "MotionControlPanel {"
    "  background-color: #2d2d30;"
    "}"
    "QWidget {"
    "  background-color: #2d2d30;"
    "  color: #d4d4d4;"
    "}"
    "QComboBox {"
    "  background-color: #3c3c3c;"
    "  color: #d4d4d4;"
    "  border: 1px solid #555555;"
    "  border-radius: 3px;"
    "  padding: 4px;"
    "  font-size: 10px;"
    "}"
    "QComboBox:hover {"
    "  border-color: #0e639c;"
    "}"
    "QComboBox::drop-down {"
    "  border: none;"
    "  width: 18px;"
    "}"
    "QComboBox::down-arrow {"
    "  image: none;"
    "  border-left: 3px solid transparent;"
    "  border-right: 3px solid transparent;"
    "  border-top: 4px solid #d4d4d4;"
    "  margin-right: 5px;"
    "}"
    "QComboBox QAbstractItemView {"
    "  background-color: #1e1e1e;"
    "  color: #d4d4d4;"
    "  selection-background-color: #0e639c;"
    "  border: 1px solid #3f3f46;"
    "}"
    // Move to Position - Green
    "QPushButton#moveToButton {"
    "  background-color: #27ae60;"
    "  color: white;"
    "  border: 1px solid #229954;"
    "  border-radius: 3px;"
    "  font-size: 12px;"
    "  font-weight: bold;"
    "}"
    "QPushButton#moveToButton:hover {"
    "  background-color: #2ecc71;"
    "}"
    "QPushButton#moveToButton:disabled {"
    "  background-color: #3c3c3c;"
    "  color: #666666;"
    "  border-color: #555555;"
    "}"
    // Move with Offset - Orange
    "QPushButton#moveOffsetButton {"
    "  background-color: #f39c12;"
    "  color: white;"
    "  border: 1px solid #e67e22;"
    "  border-radius: 3px;"
    "  font-size: 12px;"
    "  font-weight: bold;"
    "}"
    "QPushButton#moveOffsetButton:hover {"
    "  background-color: #e67e22;"
    "}"
    "QPushButton#moveOffsetButton:disabled {"
    "  background-color: #3c3c3c;"
    "  color: #666666;"
    "  border-color: #555555;"
    "}"
    // Regular buttons - Blue
    "QPushButton {"
    "  background-color: #0e639c;"
    "  color: white;"
    "  border: 1px solid #0c5280;"
    "  border-radius: 3px;"
    "  font-size: 11px;"
    "  font-weight: 600;"
    "}"
    "QPushButton:hover {"
    "  background-color: #1177bb;"
    "}"
    "QPushButton:pressed {"
    "  background-color: #0c5280;"
    "}"
    "QPushButton:disabled {"
    "  background-color: #3c3c3c;"
    "  color: #666666;"
    "  border-color: #555555;"
    "}"
    // Delete button - Red
    "QPushButton#deletePosButton {"
    "  background-color: #e74c3c;"
    "  border-color: #c0392b;"
    "}"
    "QPushButton#deletePosButton:hover {"
    "  background-color: #c0392b;"
    "}"
    "QPushButton#deletePosButton:disabled {"
    "  background-color: #3c3c3c;"
    "  color: #666666;"
    "  border-color: #555555;"
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

  // === Connect Device Combo Signal ===
  connect(m_deviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
    this, [this]() {
    emit deviceChanged(m_deviceCombo->currentText());
  });
}

void MotionControlPanel::setDevices(const QStringList& devices)
{
  m_deviceCombo->clear();
  m_deviceCombo->addItems(devices);
}

void MotionControlPanel::setSelectedPosition(const QString& name, double x, double y, double z,
  double u, double v, double w,
  const std::vector<std::string>& waypoints)
{
  m_selectedPositionName = name;

  m_posNameLabel->setText(QString("<b>%1</b>").arg(name));
  m_posXLabel->setText(QString("X: %1").arg(x, 0, 'f', 3));
  m_posYLabel->setText(QString("Y: %1").arg(y, 0, 'f', 3));
  m_posZLabel->setText(QString("Z: %1").arg(z, 0, 'f', 3));
  m_posULabel->setText(QString("U: %1").arg(u, 0, 'f', 3));
  m_posVLabel->setText(QString("V: %1").arg(v, 0, 'f', 3));
  m_posWLabel->setText(QString("W: %1").arg(w, 0, 'f', 3));

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
  m_moveOffsetButton->setEnabled(true);
  m_editPosButton->setEnabled(true);  // NEW
  m_updatePosButton->setEnabled(true);
  m_editWaypointsBtn->setEnabled(true);
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
  m_waypointsLabel->setText("Waypoints: -");

  // Disable all position-specific buttons
  m_moveToButton->setEnabled(false);
  m_moveOffsetButton->setEnabled(false);
  m_editPosButton->setEnabled(false);  // NEW
  m_updatePosButton->setEnabled(false);
  m_editWaypointsBtn->setEnabled(false);
  m_deletePosButton->setEnabled(false);
}

void MotionControlPanel::setupActionsTab()
{
  QWidget* actionsTab = new QWidget();
  QVBoxLayout* actionsLayout = new QVBoxLayout(actionsTab);
  actionsLayout->setContentsMargins(6, 6, 6, 6);
  actionsLayout->setSpacing(6);

  // Move to Position
  m_moveToButton = new QPushButton("→ Move to Position");
  m_moveToButton->setObjectName("moveToButton");  // For green styling
  m_moveToButton->setMinimumHeight(30);
  m_moveToButton->setEnabled(false);
  actionsLayout->addWidget(m_moveToButton);

  // Move with Offset
  m_moveOffsetButton = new QPushButton("⇄ Move with Offset");
  m_moveOffsetButton->setObjectName("moveOffsetButton");  // For orange styling
  m_moveOffsetButton->setMinimumHeight(30);
  m_moveOffsetButton->setEnabled(false);
  actionsLayout->addWidget(m_moveOffsetButton);

  // Edit Position
  m_editPosButton = new QPushButton("✎ Edit Position");
  m_editPosButton->setMinimumHeight(28);
  m_editPosButton->setEnabled(false);
  actionsLayout->addWidget(m_editPosButton);

  // Update Position
  m_updatePosButton = new QPushButton("↻ Update Position");
  m_updatePosButton->setMinimumHeight(28);
  m_updatePosButton->setEnabled(false);
  actionsLayout->addWidget(m_updatePosButton);

  // Edit Waypoints
  m_editWaypointsBtn = new QPushButton("⚙ Edit Waypoints");
  m_editWaypointsBtn->setMinimumHeight(28);
  m_editWaypointsBtn->setEnabled(false);
  actionsLayout->addWidget(m_editWaypointsBtn);

  // Delete Position
  m_deletePosButton = new QPushButton("✕ Delete Position");
  m_deletePosButton->setObjectName("deletePosButton");  // For red styling
  m_deletePosButton->setMinimumHeight(28);
  m_deletePosButton->setEnabled(false);
  actionsLayout->addWidget(m_deletePosButton);

  // Create New Position
  m_createPosButton = new QPushButton("+ Create Position");
  m_createPosButton->setMinimumHeight(28);
  actionsLayout->addWidget(m_createPosButton);

  // Separator
  QFrame* line1 = new QFrame();
  line1->setFrameShape(QFrame::HLine);
  line1->setStyleSheet("QFrame { background-color: #3f3f46; margin: 3px 0; }");
  actionsLayout->addWidget(line1);

  // Auto Fit View Button
  m_autoFitButton = new QPushButton("⊡ Auto Fit View");
  m_autoFitButton->setMinimumHeight(28);
  actionsLayout->addWidget(m_autoFitButton);

  // Separator
  QFrame* line2 = new QFrame();
  line2->setFrameShape(QFrame::HLine);
  line2->setStyleSheet("QFrame { background-color: #3f3f46; margin: 3px 0; }");
  actionsLayout->addWidget(line2);

  // Home Button
  m_homeButton = new QPushButton("⌂ Home");
  m_homeButton->setMinimumHeight(28);
  actionsLayout->addWidget(m_homeButton);

  // Emergency Stop
  m_stopButton = new QPushButton("⚠ STOP");
  m_stopButton->setObjectName("stopButton");  // For dark red styling
  m_stopButton->setMinimumHeight(28);
  actionsLayout->addWidget(m_stopButton);

  // Push everything to top
  actionsLayout->addStretch();

  m_tabWidget->addTab(actionsTab, "Actions");

  // Connect signals
  connect(m_moveToButton, &QPushButton::clicked,
    this, &MotionControlPanel::moveToPositionClicked);

  connect(m_moveOffsetButton, &QPushButton::clicked,
    this, &MotionControlPanel::moveWithOffsetClicked);

  connect(m_editPosButton, &QPushButton::clicked,
    this, &MotionControlPanel::editPositionClicked);

  connect(m_updatePosButton, &QPushButton::clicked,
    this, &MotionControlPanel::updatePositionClicked);

  connect(m_editWaypointsBtn, &QPushButton::clicked,
    this, &MotionControlPanel::editWaypointsClicked);

  connect(m_deletePosButton, &QPushButton::clicked,
    this, &MotionControlPanel::deletePositionClicked);

  connect(m_createPosButton, &QPushButton::clicked,
    this, &MotionControlPanel::createPositionClicked);

  connect(m_autoFitButton, &QPushButton::clicked,
    this, &MotionControlPanel::autoFitClicked);

  connect(m_homeButton, &QPushButton::clicked,
    this, &MotionControlPanel::homeClicked);

  connect(m_stopButton, &QPushButton::clicked,
    this, &MotionControlPanel::stopClicked);
}

void MotionControlPanel::setupGraphTab()
{
  QWidget* graphTab = new QWidget();
  QVBoxLayout* graphLayout = new QVBoxLayout(graphTab);
  graphLayout->setContentsMargins(0, 0, 0, 0);
  graphLayout->setSpacing(0);

  // Sub-tabs for Nodes and Edges
  QTabWidget* graphSubTabs = new QTabWidget();
  graphSubTabs->setStyleSheet(
    "QTabWidget::pane {"
    "  border: 1px solid #3f3f46;"
    "  background-color: #2d2d30;"
    "}"
    "QTabBar::tab {"
    "  background-color: #1e1e1e;"
    "  color: #888888;"
    "  padding: 6px 12px;"
    "  margin-right: 2px;"
    "  border: none;"
    "  font-size: 11px;"
    "  font-weight: bold;"
    "  border-top-left-radius: 3px;"
    "  border-top-right-radius: 3px;"
    "}"
    "QTabBar::tab:selected {"
    "  background-color: #0e639c;"
    "  color: #ffffff;"
    "  font-weight: bold;"
    "}"
    "QTabBar::tab:hover:!selected {"
    "  background-color: #2a2d2e;"
    "  color: #d4d4d4;"
    "}"
  );

  // Nodes tab (placeholder)
  QWidget* nodesTab = new QWidget();
  QVBoxLayout* nodesLayout = new QVBoxLayout(nodesTab);
  nodesLayout->setContentsMargins(10, 10, 10, 10);
  QLabel* nodesPlaceholder = new QLabel("Nodes (Placeholder)");
  nodesPlaceholder->setStyleSheet("QLabel { color: #888888; font-size: 11px; }");
  nodesLayout->addWidget(nodesPlaceholder);
  nodesLayout->addStretch();

  // Edges tab (placeholder)
  QWidget* edgesTab = new QWidget();
  QVBoxLayout* edgesLayout = new QVBoxLayout(edgesTab);
  edgesLayout->setContentsMargins(10, 10, 10, 10);
  QLabel* edgesPlaceholder = new QLabel("Edges (Placeholder)");
  edgesPlaceholder->setStyleSheet("QLabel { color: #888888; font-size: 11px; }");
  edgesLayout->addWidget(edgesPlaceholder);
  edgesLayout->addStretch();

  graphSubTabs->addTab(nodesTab, "Nodes");
  graphSubTabs->addTab(edgesTab, "Edges");

  graphLayout->addWidget(graphSubTabs);

  m_tabWidget->addTab(graphTab, "Graph");
}

QString MotionControlPanel::getCurrentDevice() const
{
  return m_deviceCombo->currentText();
}