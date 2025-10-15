// MotorDeviceFormWidget.cpp
#include "MotorDeviceFormWidget.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QJsonObject>
#include <QJsonArray>

MotorDeviceFormWidget::MotorDeviceFormWidget(QWidget* parent)
  : QWidget(parent)
  , m_scrollArea(nullptr)
  , m_formContent(nullptr)
{
  setupUI();
}

void MotorDeviceFormWidget::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);

  // Title
  QLabel* title = new QLabel("Motor Devices Configuration");
  title->setStyleSheet("QLabel { color: white; font-size: 20px; font-weight: bold; padding: 10px; }");
  mainLayout->addWidget(title);

  // Scroll area
  m_scrollArea = new QScrollArea();
  m_scrollArea->setWidgetResizable(true);
  m_scrollArea->setStyleSheet("QScrollArea { border: none; background-color: #2b2b2b; }");

  m_formContent = new QWidget();
  m_scrollArea->setWidget(m_formContent);

  mainLayout->addWidget(m_scrollArea);
}

void MotorDeviceFormWidget::clear()
{
  m_fields.clear();

  if (m_formContent->layout()) {
    QLayoutItem* item;
    while ((item = m_formContent->layout()->takeAt(0)) != nullptr) {
      delete item->widget();
      delete item;
    }
    delete m_formContent->layout();
  }
}

void MotorDeviceFormWidget::addDeviceGroup(const QJsonObject& device)
{
  QVBoxLayout* formLayout = qobject_cast<QVBoxLayout*>(m_formContent->layout());
  if (!formLayout) return;

  int index = static_cast<int>(m_fields.size());

  QString formStyle =
    "QLineEdit, QSpinBox, QComboBox { background-color: #3a3a3a; color: white; border: 1px solid #555555; "
    "padding: 5px; font-size: 14px; } "
    "QCheckBox { color: white; font-size: 14px; }";

  QGroupBox* groupBox = new QGroupBox(QString("Motor Device #%1").arg(index + 1));
  groupBox->setStyleSheet(
    "QGroupBox { color: white; font-size: 16px; font-weight: bold; "
    "border: 2px solid #555555; border-radius: 5px; margin-top: 10px; padding-top: 10px; } "
    "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }");

  QVBoxLayout* groupLayout = new QVBoxLayout();

  QFormLayout* deviceForm = new QFormLayout();
  deviceForm->setSpacing(10);
  deviceForm->setLabelAlignment(Qt::AlignRight);

  MotorDeviceFields fields;

  // ID
  fields.id = new QLineEdit(device["id"].toString());
  fields.id->setStyleSheet(formStyle);
  deviceForm->addRow(new QLabel("<span style='color: white;'>ID:</span>"), fields.id);

  // Name
  fields.name = new QLineEdit(device["name"].toString());
  fields.name->setStyleSheet(formStyle);
  deviceForm->addRow(new QLabel("<span style='color: white;'>Name:</span>"), fields.name);

  // Device ID
  fields.deviceId = new QSpinBox();
  fields.deviceId->setRange(0, 255);
  fields.deviceId->setValue(device.isEmpty() ? 0 : device["device_id"].toInt());
  fields.deviceId->setStyleSheet(formStyle);
  deviceForm->addRow(new QLabel("<span style='color: white;'>Device ID:</span>"), fields.deviceId);

  // IP Address
  fields.ipAddress = new QLineEdit(device.isEmpty() ? "192.168.0.10" : device["ip_address"].toString());
  fields.ipAddress->setStyleSheet(formStyle);
  deviceForm->addRow(new QLabel("<span style='color: white;'>IP Address:</span>"), fields.ipAddress);

  // Port
  fields.port = new QSpinBox();
  fields.port->setRange(0, 65535);
  fields.port->setValue(device.isEmpty() ? 50000 : device["port"].toInt());
  fields.port->setStyleSheet(formStyle);
  deviceForm->addRow(new QLabel("<span style='color: white;'>Port:</span>"), fields.port);

  // Enabled
  fields.enabled = new QCheckBox("Enabled");
  fields.enabled->setChecked(device.isEmpty() ? false : device["enabled"].toBool());
  fields.enabled->setStyleSheet("QCheckBox { color: white; }");
  deviceForm->addRow("", fields.enabled);

  // Controller Type
  fields.controllerType = new QComboBox();
  fields.controllerType->addItems({ "ACS", "PI", "Generic" });
  fields.controllerType->setCurrentText(device.isEmpty() ? "PI" : device["controller_type"].toString());
  fields.controllerType->setStyleSheet(formStyle);
  deviceForm->addRow(new QLabel("<span style='color: white;'>Controller Type:</span>"), fields.controllerType);

  // Axes
  fields.axes = new QLineEdit(device.isEmpty() ? "X Y Z" : device["axes"].toString());
  fields.axes->setStyleSheet(formStyle);
  deviceForm->addRow(new QLabel("<span style='color: white;'>Axes:</span>"), fields.axes);

  groupLayout->addLayout(deviceForm);

  // Remove button
  QPushButton* removeBtn = new QPushButton("🗑 Remove Device");
  removeBtn->setStyleSheet(
    "QPushButton { background-color: #cc0000; color: white; border: none; "
    "padding: 8px; font-size: 12px; } "
    "QPushButton:hover { background-color: #ee0000; }");
  connect(removeBtn, &QPushButton::clicked, this, [this, index]() {
    onRemoveDeviceClicked(index);
  });
  groupLayout->addWidget(removeBtn);

  groupBox->setLayout(groupLayout);

  m_fields.push_back(fields);

  // Insert before the stretch and add button
  int insertPos = formLayout->count() - 2;  // Before stretch and add button
  formLayout->insertWidget(insertPos, groupBox);
}

void MotorDeviceFormWidget::rebuildForm()
{
  // Collect current data
  QJsonDocument currentData = collectToJson();

  // Reload form
  loadFromJson(currentData);
}

void MotorDeviceFormWidget::loadFromJson(const QJsonDocument& config)
{
  clear();

  QVBoxLayout* formLayout = new QVBoxLayout(m_formContent);
  formLayout->setSpacing(20);

  QJsonArray devices = config.object()["motion_devices"].toArray();

  // Add each device
  for (int i = 0; i < devices.size(); ++i) {
    addDeviceGroup(devices[i].toObject());
  }

  // Add Device button
  QPushButton* addBtn = new QPushButton("➕ Add Motor Device");
  addBtn->setStyleSheet(
    "QPushButton { background-color: #0066cc; color: white; border: none; "
    "padding: 10px; font-size: 14px; font-weight: bold; } "
    "QPushButton:hover { background-color: #0077ee; }");
  connect(addBtn, &QPushButton::clicked, this, &MotorDeviceFormWidget::onAddDeviceClicked);
  formLayout->addWidget(addBtn);

  formLayout->addStretch();
}

void MotorDeviceFormWidget::onAddDeviceClicked()
{
  QJsonObject newDevice;
  newDevice["id"] = QString("device_%1").arg(m_fields.size() + 1);
  newDevice["name"] = QString("Device %1").arg(m_fields.size() + 1);
  newDevice["device_id"] = static_cast<int>(m_fields.size());
  newDevice["ip_address"] = "192.168.0.10";
  newDevice["port"] = 50000;
  newDevice["enabled"] = false;
  newDevice["controller_type"] = "PI";
  newDevice["axes"] = "X Y Z";

  addDeviceGroup(newDevice);
}

void MotorDeviceFormWidget::onRemoveDeviceClicked(int index)
{
  if (index < 0 || index >= static_cast<int>(m_fields.size())) return;

  // Must keep at least one device
  if (m_fields.size() <= 1) {
    return;
  }

  // Remove from fields
  m_fields.erase(m_fields.begin() + index);

  // Rebuild the form to update indices
  rebuildForm();
}

QJsonDocument MotorDeviceFormWidget::collectToJson() const
{
  QJsonArray devicesArray;

  for (const auto& fields : m_fields) {
    QJsonObject device;
    device["id"] = fields.id->text();
    device["name"] = fields.name->text();
    device["device_id"] = fields.deviceId->value();
    device["ip_address"] = fields.ipAddress->text();
    device["port"] = fields.port->value();
    device["enabled"] = fields.enabled->isChecked();
    device["controller_type"] = fields.controllerType->currentText();
    device["axes"] = fields.axes->text();
    devicesArray.append(device);
  }

  QJsonObject root;
  root["motion_devices"] = devicesArray;
  return QJsonDocument(root);
}