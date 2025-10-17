// WaypointEditorDialog.cpp
#include "WaypointEditorDialog.h"
#include <QGroupBox>
#include <QMessageBox>

WaypointEditorDialog::WaypointEditorDialog(const QString& positionName,
  const std::vector<std::string>& currentWaypoints,
  const std::vector<std::string>& availablePositions,
  QWidget* parent)
  : QDialog(parent)
  , m_positionName(positionName)
  , m_availablePositions(availablePositions)
{
  setupUI();
  populateAvailablePositions();

  // Load current waypoints
  for (const auto& waypoint : currentWaypoints) {
    m_waypointList->addItem(QString::fromStdString(waypoint));
  }

  updateButtonStates();
}

void WaypointEditorDialog::setupUI()
{
  setWindowTitle(QString("Edit Waypoints - %1").arg(m_positionName));
  setMinimumSize(500, 400);

  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  // Title
  QLabel* titleLabel = new QLabel(QString("Configure waypoint path for position: <b>%1</b>").arg(m_positionName));
  titleLabel->setStyleSheet("font-size: 13px; padding: 10px; background-color: #ecf0f1; border-radius: 4px;");
  mainLayout->addWidget(titleLabel);

  // Info label
  QLabel* infoLabel = new QLabel("Waypoints will be visited in order before reaching the target position.");
  infoLabel->setStyleSheet("color: #7f8c8d; font-size: 11px; padding: 5px;");
  mainLayout->addWidget(infoLabel);

  // === Waypoint List Group ===
  QGroupBox* listGroup = new QGroupBox("Waypoint Path");
  QVBoxLayout* listLayout = new QVBoxLayout(listGroup);

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
  listLayout->addWidget(m_waypointList);

  // Order control buttons
  QHBoxLayout* orderLayout = new QHBoxLayout();

  m_moveUpBtn = new QPushButton("↑ Move Up");
  m_moveUpBtn->setEnabled(false);
  orderLayout->addWidget(m_moveUpBtn);

  m_moveDownBtn = new QPushButton("↓ Move Down");
  m_moveDownBtn->setEnabled(false);
  orderLayout->addWidget(m_moveDownBtn);

  orderLayout->addStretch();
  listLayout->addLayout(orderLayout);

  mainLayout->addWidget(listGroup);

  // === Add Waypoint Group ===
  QGroupBox* addGroup = new QGroupBox("Add Waypoint");
  QHBoxLayout* addLayout = new QHBoxLayout(addGroup);

  QLabel* selectLabel = new QLabel("Select Position:");
  addLayout->addWidget(selectLabel);

  m_positionCombo = new QComboBox();
  m_positionCombo->setMinimumWidth(200);
  addLayout->addWidget(m_positionCombo, 1);

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
  addLayout->addWidget(m_addBtn);

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
  addLayout->addWidget(m_removeBtn);

  mainLayout->addWidget(addGroup);

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
  connect(m_addBtn, &QPushButton::clicked, this, &WaypointEditorDialog::onAddWaypoint);
  connect(m_removeBtn, &QPushButton::clicked, this, &WaypointEditorDialog::onRemoveWaypoint);
  connect(m_moveUpBtn, &QPushButton::clicked, this, &WaypointEditorDialog::onMoveUp);
  connect(m_moveDownBtn, &QPushButton::clicked, this, &WaypointEditorDialog::onMoveDown);
  connect(m_waypointList, &QListWidget::itemSelectionChanged,
    this, &WaypointEditorDialog::onWaypointSelectionChanged);
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

  for (const auto& position : m_availablePositions) {
    QString posName = QString::fromStdString(position);

    // Don't allow adding the target position as its own waypoint
    if (posName != m_positionName) {
      m_positionCombo->addItem(posName);
    }
  }
}

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
        QString("Position '%1' is already in the waypoint path.").arg(selectedPosition));
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

void WaypointEditorDialog::onMoveUp()
{
  int currentRow = m_waypointList->currentRow();
  if (currentRow > 0) {
    QListWidgetItem* item = m_waypointList->takeItem(currentRow);
    m_waypointList->insertItem(currentRow - 1, item);
    m_waypointList->setCurrentRow(currentRow - 1);
    updateButtonStates();
  }
}

void WaypointEditorDialog::onMoveDown()
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

std::vector<std::string> WaypointEditorDialog::getWaypoints() const
{
  std::vector<std::string> waypoints;

  for (int i = 0; i < m_waypointList->count(); ++i) {
    waypoints.push_back(m_waypointList->item(i)->text().toStdString());
  }

  return waypoints;
}