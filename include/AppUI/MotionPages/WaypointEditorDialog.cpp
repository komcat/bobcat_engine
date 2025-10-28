// WaypointEditorDialog.cpp
#include "WaypointEditorDialog.h"
#include <QGroupBox>
#include <QMessageBox>
#include <QTabWidget>

WaypointEditorDialog::WaypointEditorDialog(const QString& positionName,
  const std::vector<std::string>& currentWaypoints,
  const std::vector<std::string>& currentExitWaypoints,  // ✅ NEW
  const std::vector<std::string>& availablePositions,
  QWidget* parent)
  : QDialog(parent)
  , m_positionName(positionName)
  , m_availablePositions(availablePositions)
{
  setupUI();
  populateAvailablePositions();

  // Load current entry waypoints
  for (const auto& waypoint : currentWaypoints) {
    m_waypointList->addItem(QString::fromStdString(waypoint));
  }

  // ✅ NEW - Load current exit waypoints
  for (const auto& waypoint : currentExitWaypoints) {
    m_exitWaypointList->addItem(QString::fromStdString(waypoint));
  }

  updateButtonStates();
  updateExitButtonStates();
}

void WaypointEditorDialog::setupUI()
{
  setWindowTitle(QString("Edit Waypoints - %1").arg(m_positionName));
  setMinimumSize(600, 500);

  // Set light background for the entire dialog
  setStyleSheet("QDialog { background-color: white; }");

  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  // Title
  QLabel* titleLabel = new QLabel(QString("Configure waypoint paths for position: <b>%1</b>").arg(m_positionName));
  titleLabel->setStyleSheet("font-size: 13px; padding: 10px; background-color: #ecf0f1; border-radius: 4px;");
  mainLayout->addWidget(titleLabel);

  // Info label
  QLabel* infoLabel = new QLabel(
    "Entry waypoints: visited when moving TO this position<br>"
    "Exit waypoints: visited when moving FROM this position"
  );
  infoLabel->setStyleSheet("color: #7f8c8d; font-size: 11px; padding: 5px;");
  mainLayout->addWidget(infoLabel);

  // ✅ NEW - Tab widget for entry/exit waypoints
  QTabWidget* tabWidget = new QTabWidget();
  tabWidget->setStyleSheet(
    "QTabWidget::pane {"
    "  border: 1px solid #bdc3c7;"
    "  border-radius: 4px;"
    "}"
    "QTabBar::tab {"
    "  background-color: #ecf0f1;"
    "  padding: 8px 20px;"
    "  margin-right: 2px;"
    "  border-top-left-radius: 4px;"
    "  border-top-right-radius: 4px;"
    "}"
    "QTabBar::tab:selected {"
    "  background-color: #3498db;"
    "  color: white;"
    "  font-weight: bold;"
    "}"
  );

  // === ENTRY WAYPOINTS TAB ===
  QWidget* entryTab = new QWidget();
  QVBoxLayout* entryLayout = new QVBoxLayout(entryTab);

  QGroupBox* entryListGroup = new QGroupBox("Entry Waypoint Path");
  QVBoxLayout* entryListLayout = new QVBoxLayout(entryListGroup);

  m_waypointList = new QListWidget();
  m_waypointList->setSelectionMode(QAbstractItemView::SingleSelection);
  m_waypointList->setStyleSheet(
    "QListWidget {"
    "  background-color: white;"
    "  border: 1px solid #bdc3c7;"
    "  border-radius: 4px;"
    "  padding: 5px;"
    "  font-size: 12px;"
    "}"
    "QListWidget::item {"
    "  padding: 8px;"
    "  border-bottom: 1px solid #ecf0f1;"
    "}"
    "QListWidget::item:selected {"
    "  background-color: #3498db;"
    "  color: white;"
    "}"
  );
  entryListLayout->addWidget(m_waypointList);

  // Entry order control buttons
  QHBoxLayout* entryOrderLayout = new QHBoxLayout();
  m_moveUpBtn = new QPushButton("↑ Move Up");
  m_moveUpBtn->setEnabled(false);
  entryOrderLayout->addWidget(m_moveUpBtn);

  m_moveDownBtn = new QPushButton("↓ Move Down");
  m_moveDownBtn->setEnabled(false);
  entryOrderLayout->addWidget(m_moveDownBtn);

  entryOrderLayout->addStretch();
  entryListLayout->addLayout(entryOrderLayout);
  entryLayout->addWidget(entryListGroup);

  // Entry add waypoint group
  QGroupBox* entryAddGroup = new QGroupBox("Add Entry Waypoint");
  QHBoxLayout* entryAddLayout = new QHBoxLayout(entryAddGroup);

  QLabel* entrySelectLabel = new QLabel("Select Position:");
  entryAddLayout->addWidget(entrySelectLabel);

  m_positionCombo = new QComboBox();
  m_positionCombo->setMinimumWidth(200);
  entryAddLayout->addWidget(m_positionCombo, 1);

  m_addBtn = new QPushButton("+ Add");
  m_addBtn->setStyleSheet(
    "QPushButton {"
    "  background-color: #27ae60;"
    "  color: white;"
    "  border: none;"
    "  border-radius: 4px;"
    "  padding: 8px 20px;"
    "  font-weight: bold;"
    "}"
    "QPushButton:hover {"
    "  background-color: #229954;"
    "}"
  );
  entryAddLayout->addWidget(m_addBtn);

  m_removeBtn = new QPushButton("− Remove");
  m_removeBtn->setEnabled(false);
  m_removeBtn->setStyleSheet(
    "QPushButton {"
    "  background-color: #e74c3c;"
    "  color: white;"
    "  border: none;"
    "  border-radius: 4px;"
    "  padding: 8px 20px;"
    "  font-weight: bold;"
    "}"
    "QPushButton:hover {"
    "  background-color: #c0392b;"
    "}"
    "QPushButton:disabled {"
    "  background-color: #95a5a6;"
    "}"
  );
  entryAddLayout->addWidget(m_removeBtn);

  entryLayout->addWidget(entryAddGroup);

  // === EXIT WAYPOINTS TAB ===  // ✅ NEW
  QWidget* exitTab = new QWidget();
  QVBoxLayout* exitLayout = new QVBoxLayout(exitTab);

  QGroupBox* exitListGroup = new QGroupBox("Exit Waypoint Path");
  QVBoxLayout* exitListLayout = new QVBoxLayout(exitListGroup);

  m_exitWaypointList = new QListWidget();
  m_exitWaypointList->setSelectionMode(QAbstractItemView::SingleSelection);
  m_exitWaypointList->setStyleSheet(
    "QListWidget {"
    "  background-color: white;"
    "  border: 1px solid #bdc3c7;"
    "  border-radius: 4px;"
    "  padding: 5px;"
    "  font-size: 12px;"
    "}"
    "QListWidget::item {"
    "  padding: 8px;"
    "  border-bottom: 1px solid #ecf0f1;"
    "}"
    "QListWidget::item:selected {"
    "  background-color: #e67e22;"
    "  color: white;"
    "}"
  );
  exitListLayout->addWidget(m_exitWaypointList);

  // Exit order control buttons
  QHBoxLayout* exitOrderLayout = new QHBoxLayout();
  m_moveUpExitBtn = new QPushButton("↑ Move Up");
  m_moveUpExitBtn->setEnabled(false);
  exitOrderLayout->addWidget(m_moveUpExitBtn);

  m_moveDownExitBtn = new QPushButton("↓ Move Down");
  m_moveDownExitBtn->setEnabled(false);
  exitOrderLayout->addWidget(m_moveDownExitBtn);

  exitOrderLayout->addStretch();
  exitListLayout->addLayout(exitOrderLayout);
  exitLayout->addWidget(exitListGroup);

  // Exit add waypoint group
  QGroupBox* exitAddGroup = new QGroupBox("Add Exit Waypoint");
  QHBoxLayout* exitAddLayout = new QHBoxLayout(exitAddGroup);

  QLabel* exitSelectLabel = new QLabel("Select Position:");
  exitAddLayout->addWidget(exitSelectLabel);

  m_exitPositionCombo = new QComboBox();
  m_exitPositionCombo->setMinimumWidth(200);
  exitAddLayout->addWidget(m_exitPositionCombo, 1);

  m_addExitBtn = new QPushButton("+ Add");
  m_addExitBtn->setStyleSheet(
    "QPushButton {"
    "  background-color: #e67e22;"
    "  color: white;"
    "  border: none;"
    "  border-radius: 4px;"
    "  padding: 8px 20px;"
    "  font-weight: bold;"
    "}"
    "QPushButton:hover {"
    "  background-color: #d35400;"
    "}"
  );
  exitAddLayout->addWidget(m_addExitBtn);

  m_removeExitBtn = new QPushButton("− Remove");
  m_removeExitBtn->setEnabled(false);
  m_removeExitBtn->setStyleSheet(
    "QPushButton {"
    "  background-color: #e74c3c;"
    "  color: white;"
    "  border: none;"
    "  border-radius: 4px;"
    "  padding: 8px 20px;"
    "  font-weight: bold;"
    "}"
    "QPushButton:hover {"
    "  background-color: #c0392b;"
    "}"
    "QPushButton:disabled {"
    "  background-color: #95a5a6;"
    "}"
  );
  exitAddLayout->addWidget(m_removeExitBtn);

  exitLayout->addWidget(exitAddGroup);

  // Add tabs
  tabWidget->addTab(entryTab, "Entry Waypoints");
  tabWidget->addTab(exitTab, "Exit Waypoints");
  mainLayout->addWidget(tabWidget);

  // === Dialog Buttons ===
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->addStretch();

  m_cancelBtn = new QPushButton("Cancel");
  m_cancelBtn->setMinimumWidth(100);
  buttonLayout->addWidget(m_cancelBtn);

  m_okBtn = new QPushButton("OK");
  m_okBtn->setMinimumWidth(100);
  m_okBtn->setDefault(true);
  m_okBtn->setStyleSheet(
    "QPushButton {"
    "  background-color: #3498db;"
    "  color: white;"
    "  border: none;"
    "  border-radius: 4px;"
    "  padding: 8px 20px;"
    "  font-weight: bold;"
    "}"
    "QPushButton:hover {"
    "  background-color: #2980b9;"
    "}"
  );
  buttonLayout->addWidget(m_okBtn);

  mainLayout->addLayout(buttonLayout);

  // === Connect Signals ===
  // Entry waypoints
  connect(m_addBtn, &QPushButton::clicked, this, &WaypointEditorDialog::onAddWaypoint);
  connect(m_removeBtn, &QPushButton::clicked, this, &WaypointEditorDialog::onRemoveWaypoint);
  connect(m_moveUpBtn, &QPushButton::clicked, this, &WaypointEditorDialog::onMoveUpWaypoint);
  connect(m_moveDownBtn, &QPushButton::clicked, this, &WaypointEditorDialog::onMoveDownWaypoint);
  connect(m_waypointList, &QListWidget::itemSelectionChanged,
    this, &WaypointEditorDialog::onWaypointSelectionChanged);

  // Exit waypoints  // ✅ NEW
  connect(m_addExitBtn, &QPushButton::clicked, this, &WaypointEditorDialog::onAddExitWaypoint);
  connect(m_removeExitBtn, &QPushButton::clicked, this, &WaypointEditorDialog::onRemoveExitWaypoint);
  connect(m_moveUpExitBtn, &QPushButton::clicked, this, &WaypointEditorDialog::onMoveUpExitWaypoint);
  connect(m_moveDownExitBtn, &QPushButton::clicked, this, &WaypointEditorDialog::onMoveDownExitWaypoint);
  connect(m_exitWaypointList, &QListWidget::itemSelectionChanged,
    this, &WaypointEditorDialog::onExitWaypointSelectionChanged);

  connect(m_okBtn, &QPushButton::clicked, this, &QDialog::accept);
  connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

  setStyleSheet(
    "QGroupBox {"
    "  font-weight: bold;"
    "  border: 1px solid #bdc3c7;"
    "  border-radius: 4px;"
    "  margin-top: 10px;"
    "  padding-top: 10px;"
    "}"
    "QGroupBox::title {"
    "  subcontrol-origin: margin;"
    "  left: 10px;"
    "  padding: 0 5px;"
    "}"
  );
}

void WaypointEditorDialog::populateAvailablePositions()
{
  m_positionCombo->clear();
  m_exitPositionCombo->clear();

  for (const auto& position : m_availablePositions) {
    QString posName = QString::fromStdString(position);

    // Don't allow adding the target position as its own waypoint
    if (posName != m_positionName) {
      m_positionCombo->addItem(posName);
      m_exitPositionCombo->addItem(posName);  // ✅ NEW
    }
  }
}

// ============================================================================
// ENTRY WAYPOINT HANDLERS
// ============================================================================

void WaypointEditorDialog::onAddWaypoint()
{
  QString selectedPosition = m_positionCombo->currentText();

  if (selectedPosition.isEmpty()) {
    QMessageBox::warning(this, "No Position Selected",
      "Please select a position to add as a waypoint.");
    return;
  }

  // Check if waypoint already exists
  for (int i = 0; i < m_waypointList->count(); ++i) {
    if (m_waypointList->item(i)->text() == selectedPosition) {
      QMessageBox::warning(this, "Duplicate Waypoint",
        QString("Position '%1' is already in the entry waypoint path.").arg(selectedPosition));
      return;
    }
  }

  // Add to list
  m_waypointList->addItem(selectedPosition);
  updateButtonStates();
}

void WaypointEditorDialog::onRemoveWaypoint()
{
  QListWidgetItem* currentItem = m_waypointList->currentItem();
  if (currentItem) {
    delete m_waypointList->takeItem(m_waypointList->row(currentItem));
    updateButtonStates();
  }
}

void WaypointEditorDialog::onMoveUpWaypoint()
{
  int currentRow = m_waypointList->currentRow();
  if (currentRow > 0) {
    QListWidgetItem* item = m_waypointList->takeItem(currentRow);
    m_waypointList->insertItem(currentRow - 1, item);
    m_waypointList->setCurrentRow(currentRow - 1);
    updateButtonStates();
  }
}

void WaypointEditorDialog::onMoveDownWaypoint()
{
  int currentRow = m_waypointList->currentRow();
  if (currentRow < m_waypointList->count() - 1 && currentRow >= 0) {
    QListWidgetItem* item = m_waypointList->takeItem(currentRow);
    m_waypointList->insertItem(currentRow + 1, item);
    m_waypointList->setCurrentRow(currentRow + 1);
    updateButtonStates();
  }
}

void WaypointEditorDialog::onWaypointSelectionChanged()
{
  updateButtonStates();
}

void WaypointEditorDialog::updateButtonStates()
{
  int currentRow = m_waypointList->currentRow();
  int count = m_waypointList->count();

  m_removeBtn->setEnabled(currentRow >= 0);
  m_moveUpBtn->setEnabled(currentRow > 0);
  m_moveDownBtn->setEnabled(currentRow >= 0 && currentRow < count - 1);
}

// ============================================================================
// EXIT WAYPOINT HANDLERS  // ✅ NEW
// ============================================================================

void WaypointEditorDialog::onAddExitWaypoint()
{
  QString selectedPosition = m_exitPositionCombo->currentText();

  if (selectedPosition.isEmpty()) {
    QMessageBox::warning(this, "No Position Selected",
      "Please select a position to add as an exit waypoint.");
    return;
  }

  // Check if waypoint already exists
  for (int i = 0; i < m_exitWaypointList->count(); ++i) {
    if (m_exitWaypointList->item(i)->text() == selectedPosition) {
      QMessageBox::warning(this, "Duplicate Waypoint",
        QString("Position '%1' is already in the exit waypoint path.").arg(selectedPosition));
      return;
    }
  }

  // Add to list
  m_exitWaypointList->addItem(selectedPosition);
  updateExitButtonStates();
}

void WaypointEditorDialog::onRemoveExitWaypoint()
{
  QListWidgetItem* currentItem = m_exitWaypointList->currentItem();
  if (currentItem) {
    delete m_exitWaypointList->takeItem(m_exitWaypointList->row(currentItem));
    updateExitButtonStates();
  }
}

void WaypointEditorDialog::onMoveUpExitWaypoint()
{
  int currentRow = m_exitWaypointList->currentRow();
  if (currentRow > 0) {
    QListWidgetItem* item = m_exitWaypointList->takeItem(currentRow);
    m_exitWaypointList->insertItem(currentRow - 1, item);
    m_exitWaypointList->setCurrentRow(currentRow - 1);
    updateExitButtonStates();
  }
}

void WaypointEditorDialog::onMoveDownExitWaypoint()
{
  int currentRow = m_exitWaypointList->currentRow();
  if (currentRow < m_exitWaypointList->count() - 1 && currentRow >= 0) {
    QListWidgetItem* item = m_exitWaypointList->takeItem(currentRow);
    m_exitWaypointList->insertItem(currentRow + 1, item);
    m_exitWaypointList->setCurrentRow(currentRow + 1);
    updateExitButtonStates();
  }
}

void WaypointEditorDialog::onExitWaypointSelectionChanged()
{
  updateExitButtonStates();
}

void WaypointEditorDialog::updateExitButtonStates()
{
  int currentRow = m_exitWaypointList->currentRow();
  int count = m_exitWaypointList->count();

  m_removeExitBtn->setEnabled(currentRow >= 0);
  m_moveUpExitBtn->setEnabled(currentRow > 0);
  m_moveDownExitBtn->setEnabled(currentRow >= 0 && currentRow < count - 1);
}

// ============================================================================
// GETTERS
// ============================================================================

std::vector<std::string> WaypointEditorDialog::getWaypoints() const
{
  std::vector<std::string> waypoints;

  for (int i = 0; i < m_waypointList->count(); ++i) {
    waypoints.push_back(m_waypointList->item(i)->text().toStdString());
  }

  return waypoints;
}

std::vector<std::string> WaypointEditorDialog::getExitWaypoints() const  // ✅ NEW
{
  std::vector<std::string> exitWaypoints;

  for (int i = 0; i < m_exitWaypointList->count(); ++i) {
    exitWaypoints.push_back(m_exitWaypointList->item(i)->text().toStdString());
  }

  return exitWaypoints;
}