// SavedPositionsFormWidget.cpp
#include "SavedPositionsFormWidget.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QGroupBox>
#include <QButtonGroup>
#include <QInputDialog>
#include <QMessageBox>
#include <QJsonObject>
#include <QJsonArray>

SavedPositionsFormWidget::SavedPositionsFormWidget(QWidget* parent)
  : QWidget(parent)
  , m_devicePanel(nullptr)
  , m_deviceLayout(nullptr)
  , m_deviceButtonGroup(nullptr)
  , m_positionList(nullptr)
  , m_formPanel(nullptr)
{
  setupUI();
}

void SavedPositionsFormWidget::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Title - minimal and compact with fixed height
  QLabel* title = new QLabel("Saved Positions Configuration");
  title->setStyleSheet(
    "QLabel { "
    "  color: white; "
    "  font-size: 12px; "
    "  font-weight: bold; "
    "  padding: 3px 8px; "
    "  max-height: 25px; "
    "}");
  mainLayout->addWidget(title);

  // 3-column layout with splitters
  QSplitter* splitter = new QSplitter(Qt::Horizontal);
  splitter->setStyleSheet("QSplitter::handle { background-color: #555555; }");

  // Initialize button group for mutual exclusivity
  m_deviceButtonGroup = new QButtonGroup(this);
  m_deviceButtonGroup->setExclusive(true);

  // COLUMN 1: Device buttons
  m_devicePanel = new QWidget();
  m_devicePanel->setStyleSheet("QWidget { background-color: #2b2b2b; }");
  m_deviceLayout = new QVBoxLayout(m_devicePanel);
  m_deviceLayout->setContentsMargins(5, 5, 5, 5);
  m_deviceLayout->setSpacing(5);

  QLabel* deviceLabel = new QLabel("Devices");
  deviceLabel->setStyleSheet("QLabel { color: white; font-size: 13px; font-weight: bold; padding: 3px; }");
  m_deviceLayout->addWidget(deviceLabel);
  m_deviceLayout->addStretch();

  // COLUMN 2: Position list
  QWidget* listPanel = new QWidget();
  listPanel->setStyleSheet("QWidget { background-color: #2b2b2b; }");
  QVBoxLayout* listLayout = new QVBoxLayout(listPanel);
  listLayout->setContentsMargins(5, 5, 5, 5);
  listLayout->setSpacing(3);

  QLabel* posLabel = new QLabel("Positions");
  posLabel->setStyleSheet("QLabel { color: white; font-size: 13px; font-weight: bold; padding: 3px; }");
  listLayout->addWidget(posLabel);

  m_positionList = new QListWidget();
  m_positionList->setStyleSheet(
    "QListWidget { background-color: #3a3a3a; color: white; border: 1px solid #555555; "
    "font-size: 12px; } "
    "QListWidget::item { padding: 4px; } "
    "QListWidget::item:selected { background-color: #0066cc; }");
  connect(m_positionList, &QListWidget::itemClicked, this, &SavedPositionsFormWidget::onPositionSelected);
  listLayout->addWidget(m_positionList);

  // COLUMN 3: Position form
  m_formPanel = new QWidget();
  m_formPanel->setStyleSheet("QWidget { background-color: #2b2b2b; }");
  QVBoxLayout* formLayout = new QVBoxLayout(m_formPanel);
  formLayout->setContentsMargins(8, 5, 8, 5);
  formLayout->setSpacing(5);

  QLabel* formLabel = new QLabel("Position Editor");
  formLabel->setStyleSheet("QLabel { color: white; font-size: 13px; font-weight: bold; padding: 3px; }");
  formLayout->addWidget(formLabel);

  // Position name
  QFormLayout* coordForm = new QFormLayout();
  coordForm->setSpacing(6);

  QString formStyle =
    "QLineEdit, QDoubleSpinBox { background-color: #3a3a3a; color: white; border: 1px solid #555555; "
    "padding: 4px; font-size: 12px; }";

  m_positionNameEdit = new QLineEdit();
  m_positionNameEdit->setStyleSheet(formStyle);
  m_positionNameEdit->setPlaceholderText("Position name...");
  connect(m_positionNameEdit, &QLineEdit::textChanged, this, &SavedPositionsFormWidget::onPositionNameChanged);
  coordForm->addRow(new QLabel("<span style='color: white; font-size: 12px;'>Name:</span>"), m_positionNameEdit);

  // Coordinate spin boxes
  auto createSpinBox = [&formStyle]() {
    QDoubleSpinBox* spin = new QDoubleSpinBox();
    spin->setRange(-1000.0, 1000.0);
    spin->setDecimals(6);
    spin->setStyleSheet(formStyle);
    return spin;
  };

  m_xSpin = createSpinBox();
  coordForm->addRow(new QLabel("<span style='color: white; font-size: 12px;'>X:</span>"), m_xSpin);

  m_ySpin = createSpinBox();
  coordForm->addRow(new QLabel("<span style='color: white; font-size: 12px;'>Y:</span>"), m_ySpin);

  m_zSpin = createSpinBox();
  coordForm->addRow(new QLabel("<span style='color: white; font-size: 12px;'>Z:</span>"), m_zSpin);

  m_uSpin = createSpinBox();
  coordForm->addRow(new QLabel("<span style='color: white; font-size: 12px;'>U:</span>"), m_uSpin);

  m_vSpin = createSpinBox();
  coordForm->addRow(new QLabel("<span style='color: white; font-size: 12px;'>V:</span>"), m_vSpin);

  m_wSpin = createSpinBox();
  coordForm->addRow(new QLabel("<span style='color: white; font-size: 12px;'>W:</span>"), m_wSpin);

  formLayout->addLayout(coordForm);
  formLayout->addStretch();

  // Buttons - more compact
  m_addButton = new QPushButton("➕ Add Position");
  m_addButton->setStyleSheet(
    "QPushButton { background-color: #0066cc; color: white; border: none; "
    "padding: 8px; font-size: 12px; font-weight: bold; } "
    "QPushButton:hover { background-color: #0077ee; }");
  connect(m_addButton, &QPushButton::clicked, this, &SavedPositionsFormWidget::onAddPositionClicked);
  formLayout->addWidget(m_addButton);

  m_removeButton = new QPushButton("🗑 Remove Position");
  m_removeButton->setStyleSheet(
    "QPushButton { background-color: #cc0000; color: white; border: none; "
    "padding: 8px; font-size: 12px; } "
    "QPushButton:hover { background-color: #ee0000; }");
  m_removeButton->setEnabled(false);
  connect(m_removeButton, &QPushButton::clicked, this, &SavedPositionsFormWidget::onRemovePositionClicked);
  formLayout->addWidget(m_removeButton);

  // Add columns to splitter
  splitter->addWidget(m_devicePanel);
  splitter->addWidget(listPanel);
  splitter->addWidget(m_formPanel);
  splitter->setStretchFactor(0, 15);  // 15% for devices
  splitter->setStretchFactor(1, 30);  // 30% for position list
  splitter->setStretchFactor(2, 55);  // 55% for form

  mainLayout->addWidget(splitter);

  clearPositionForm();
}

void SavedPositionsFormWidget::clear()
{
  m_config = QJsonDocument();
  m_currentDevice.clear();
  m_currentPosition.clear();
  m_positionList->clear();
  clearPositionForm();

  // Clear button group
  if (m_deviceButtonGroup) {
    QList<QAbstractButton*> buttons = m_deviceButtonGroup->buttons();
    for (QAbstractButton* btn : buttons) {
      m_deviceButtonGroup->removeButton(btn);
    }
  }

  // Clear device buttons (keep label at position 0 and stretch at the end)
  while (m_deviceLayout->count() > 2) {
    QLayoutItem* item = m_deviceLayout->takeAt(1);
    if (item->widget()) {
      delete item->widget();
    }
    delete item;
  }
}

void SavedPositionsFormWidget::loadFromJson(const QJsonDocument& config)
{
  clear();
  m_config = config;
  loadDeviceButtons();
}

void SavedPositionsFormWidget::loadDeviceButtons()
{
  // Clear existing buttons (keep label)
  while (m_deviceLayout->count() > 1) {
    QLayoutItem* item = m_deviceLayout->takeAt(1);
    if (item->widget()) {
      delete item->widget();
    }
    delete item;
  }

  // Clear button group
  QList<QAbstractButton*> buttons = m_deviceButtonGroup->buttons();
  for (QAbstractButton* btn : buttons) {
    m_deviceButtonGroup->removeButton(btn);
  }

  QJsonObject positions = m_config.object()["positions"].toObject();

  // Create button for each device
  for (auto it = positions.begin(); it != positions.end(); ++it) {
    QString deviceId = it.key();

    QPushButton* btn = new QPushButton(deviceId);
    btn->setCheckable(true);
    btn->setStyleSheet(
      "QPushButton { background-color: #3a3a3a; color: white; border: 1px solid #555555; "
      "padding: 8px; font-size: 12px; text-align: left; } "
      "QPushButton:hover { background-color: #4a4a4a; } "
      "QPushButton:checked { background-color: #0066cc; border-color: #0066cc; }");

    // Add to button group for mutual exclusivity
    m_deviceButtonGroup->addButton(btn);

    connect(btn, &QPushButton::clicked, this, [this, deviceId]() {
      onDeviceButtonClicked(deviceId);
    });

    m_deviceLayout->insertWidget(m_deviceLayout->count() - 1, btn);
  }
}

void SavedPositionsFormWidget::onDeviceButtonClicked(const QString& deviceId)
{
  // Save current position if any
  if (!m_currentDevice.isEmpty() && !m_currentPosition.isEmpty()) {
    saveCurrentPosition();
  }

  // Button group handles mutual exclusivity automatically
  m_currentDevice = deviceId;
  m_currentPosition.clear();
  loadPositionsList(deviceId);
  clearPositionForm();
}

void SavedPositionsFormWidget::loadPositionsList(const QString& deviceId)
{
  m_positionList->clear();

  QJsonObject positions = m_config.object()["positions"].toObject();
  QJsonObject devicePositions = positions[deviceId].toObject();

  for (auto it = devicePositions.begin(); it != devicePositions.end(); ++it) {
    m_positionList->addItem(it.key());
  }
}

void SavedPositionsFormWidget::onPositionSelected(QListWidgetItem* item)
{
  if (!item) return;

  // Save previous position
  if (!m_currentDevice.isEmpty() && !m_currentPosition.isEmpty()) {
    saveCurrentPosition();
  }

  m_currentPosition = item->text();
  loadPositionForm(m_currentDevice, m_currentPosition);
}

void SavedPositionsFormWidget::loadPositionForm(const QString& deviceId, const QString& positionName)
{
  QJsonObject positions = m_config.object()["positions"].toObject();
  QJsonObject devicePositions = positions[deviceId].toObject();
  QJsonObject coords = devicePositions[positionName].toObject();

  m_positionNameEdit->setText(positionName);
  m_xSpin->setValue(coords["x"].toDouble());
  m_ySpin->setValue(coords["y"].toDouble());
  m_zSpin->setValue(coords["z"].toDouble());
  m_uSpin->setValue(coords["u"].toDouble());
  m_vSpin->setValue(coords["v"].toDouble());
  m_wSpin->setValue(coords["w"].toDouble());

  m_removeButton->setEnabled(true);
}

void SavedPositionsFormWidget::clearPositionForm()
{
  m_positionNameEdit->clear();
  m_xSpin->setValue(0.0);
  m_ySpin->setValue(0.0);
  m_zSpin->setValue(0.0);
  m_uSpin->setValue(0.0);
  m_vSpin->setValue(0.0);
  m_wSpin->setValue(0.0);
  m_removeButton->setEnabled(false);
}

void SavedPositionsFormWidget::saveCurrentPosition()
{
  if (m_currentDevice.isEmpty() || m_currentPosition.isEmpty()) return;

  QString newName = m_positionNameEdit->text().trimmed();
  if (newName.isEmpty()) return;

  QJsonObject root = m_config.object();
  QJsonObject positions = root["positions"].toObject();
  QJsonObject devicePositions = positions[m_currentDevice].toObject();

  // Remove old position if name changed
  if (newName != m_currentPosition) {
    devicePositions.remove(m_currentPosition);
  }

  // Save new position
  QJsonObject coords;
  coords["x"] = m_xSpin->value();
  coords["y"] = m_ySpin->value();
  coords["z"] = m_zSpin->value();
  coords["u"] = m_uSpin->value();
  coords["v"] = m_vSpin->value();
  coords["w"] = m_wSpin->value();

  devicePositions[newName] = coords;
  positions[m_currentDevice] = devicePositions;
  root["positions"] = positions;
  m_config = QJsonDocument(root);

  // Update list if name changed
  if (newName != m_currentPosition) {
    m_currentPosition = newName;
    loadPositionsList(m_currentDevice);

    // Reselect the renamed item
    QList<QListWidgetItem*> items = m_positionList->findItems(newName, Qt::MatchExactly);
    if (!items.isEmpty()) {
      m_positionList->setCurrentItem(items.first());
    }
  }
}

void SavedPositionsFormWidget::onPositionNameChanged()
{
  // Auto-save when editing existing position
  if (!m_currentDevice.isEmpty() && !m_currentPosition.isEmpty()) {
    saveCurrentPosition();
  }
}

void SavedPositionsFormWidget::onAddPositionClicked()
{
  if (m_currentDevice.isEmpty()) {
    QMessageBox::warning(this, "Error", "Please select a device first");
    return;
  }

  bool ok;
  QString positionName = QInputDialog::getText(this, "Add Position",
    "Position name:", QLineEdit::Normal, "", &ok);

  if (!ok || positionName.trimmed().isEmpty()) {
    return;
  }

  positionName = positionName.trimmed();

  // Check if position already exists
  QJsonObject positions = m_config.object()["positions"].toObject();
  QJsonObject devicePositions = positions[m_currentDevice].toObject();

  if (devicePositions.contains(positionName)) {
    QMessageBox::warning(this, "Error", "Position already exists");
    return;
  }

  // Create new position with default values
  QJsonObject coords;
  coords["x"] = 0.0;
  coords["y"] = 0.0;
  coords["z"] = 0.0;
  coords["u"] = 0.0;
  coords["v"] = 0.0;
  coords["w"] = 0.0;

  devicePositions[positionName] = coords;
  positions[m_currentDevice] = devicePositions;

  QJsonObject root = m_config.object();
  root["positions"] = positions;
  m_config = QJsonDocument(root);

  // Reload list and select new position
  loadPositionsList(m_currentDevice);

  QList<QListWidgetItem*> items = m_positionList->findItems(positionName, Qt::MatchExactly);
  if (!items.isEmpty()) {
    m_positionList->setCurrentItem(items.first());
    m_currentPosition = positionName;
    loadPositionForm(m_currentDevice, positionName);
  }
}

void SavedPositionsFormWidget::onRemovePositionClicked()
{
  if (m_currentDevice.isEmpty() || m_currentPosition.isEmpty()) {
    return;
  }

  QMessageBox::StandardButton reply = QMessageBox::question(this, "Confirm Delete",
    QString("Delete position '%1'?").arg(m_currentPosition),
    QMessageBox::Yes | QMessageBox::No);

  if (reply != QMessageBox::Yes) {
    return;
  }

  QJsonObject root = m_config.object();
  QJsonObject positions = root["positions"].toObject();
  QJsonObject devicePositions = positions[m_currentDevice].toObject();

  devicePositions.remove(m_currentPosition);
  positions[m_currentDevice] = devicePositions;
  root["positions"] = positions;
  m_config = QJsonDocument(root);

  m_currentPosition.clear();
  loadPositionsList(m_currentDevice);
  clearPositionForm();
}

QJsonDocument SavedPositionsFormWidget::collectToJson() const
{
  // Save current position before collecting
  const_cast<SavedPositionsFormWidget*>(this)->saveCurrentPosition();
  return m_config;
}