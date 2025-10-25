// TECDevicesFormWidget.cpp
#include "TECDevicesFormWidget.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QJsonObject>
#include <QJsonArray>

TECDevicesFormWidget::TECDevicesFormWidget(QWidget* parent)
  : QWidget(parent)
  , m_scrollArea(nullptr)
  , m_formContent(nullptr)
{
  setupUI();
}

void TECDevicesFormWidget::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);

  // Title
  QLabel* title = new QLabel("TEC Devices Configuration");
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

void TECDevicesFormWidget::clear()
{
  m_deviceFields.clear();

  if (m_formContent->layout()) {
    QLayoutItem* item;
    while ((item = m_formContent->layout()->takeAt(0)) != nullptr) {
      delete item->widget();
      delete item;
    }
    delete m_formContent->layout();
  }
}

void TECDevicesFormWidget::addDeviceGroup(const QJsonObject& device)
{
  QVBoxLayout* formLayout = qobject_cast<QVBoxLayout*>(m_formContent->layout());
  if (!formLayout) return;

  int index = static_cast<int>(m_deviceFields.size());

  QString formStyle =
    "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { background-color: #3a3a3a; color: white; border: 1px solid #555555; "
    "padding: 5px; font-size: 14px; } "
    "QCheckBox { color: white; font-size: 14px; }";

  QGroupBox* groupBox = new QGroupBox(QString("TEC Device #%1").arg(index + 1));
  groupBox->setStyleSheet(
    "QGroupBox { color: white; font-size: 16px; font-weight: bold; "
    "border: 2px solid #555555; border-radius: 5px; margin-top: 10px; padding-top: 10px; } "
    "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }");

  QVBoxLayout* groupLayout = new QVBoxLayout();

  QFormLayout* deviceForm = new QFormLayout();
  deviceForm->setSpacing(10);
  deviceForm->setLabelAlignment(Qt::AlignRight);

  TECDeviceFields fields;

  // Name
  fields.name = new QLineEdit(device["name"].toString());
  fields.name->setStyleSheet(formStyle);
  deviceForm->addRow(new QLabel("<span style='color: white;'>Name:</span>"), fields.name);

  // Controller Type
  fields.controllerType = new QComboBox();
  fields.controllerType->addItems({"CLD101x", "custom"});
  fields.controllerType->setCurrentText(device.isEmpty() ? "CLD101x" : device["controller_type"].toString());
  fields.controllerType->setStyleSheet(formStyle);
  deviceForm->addRow(new QLabel("<span style='color: white;'>Controller Type:</span>"), fields.controllerType);

  // IP Address
  fields.ipAddress = new QLineEdit(device.isEmpty() ? "127.0.0.1" : device["ip_address"].toString());
  fields.ipAddress->setStyleSheet(formStyle);
  deviceForm->addRow(new QLabel("<span style='color: white;'>IP Address:</span>"), fields.ipAddress);

  // Port
  fields.port = new QSpinBox();
  fields.port->setRange(0, 65535);
  fields.port->setValue(device.isEmpty() ? 65432 : device["port"].toInt());
  fields.port->setStyleSheet(formStyle);
  deviceForm->addRow(new QLabel("<span style='color: white;'>Port:</span>"), fields.port);

  // Enabled
  fields.enabled = new QCheckBox("Enabled");
  fields.enabled->setChecked(device.isEmpty() ? true : device["enabled"].toBool());
  fields.enabled->setStyleSheet("QCheckBox { color: white; }");
  deviceForm->addRow("", fields.enabled);

  // Default Temperature
  fields.defaultTemperature = new QDoubleSpinBox();
  fields.defaultTemperature->setRange(-50.0, 150.0);
  fields.defaultTemperature->setDecimals(1);
  fields.defaultTemperature->setSuffix(" \u00B0C");
  fields.defaultTemperature->setValue(device.isEmpty() ? 25.0 : device["default_temperature"].toDouble());
  fields.defaultTemperature->setStyleSheet(formStyle);
  deviceForm->addRow(new QLabel("<span style='color: white;'>Default Temperature:</span>"), fields.defaultTemperature);

  // Default Laser Current
  fields.defaultLaserCurrent = new QDoubleSpinBox();
  fields.defaultLaserCurrent->setRange(0.0, 500.0);
  fields.defaultLaserCurrent->setDecimals(1);
  fields.defaultLaserCurrent->setSuffix(" mA");
  fields.defaultLaserCurrent->setValue(device.isEmpty() ? 50.0 : device["default_laser_current"].toDouble());
  fields.defaultLaserCurrent->setStyleSheet(formStyle);
  deviceForm->addRow(new QLabel("<span style='color: white;'>Default Laser Current:</span>"), fields.defaultLaserCurrent);

  groupLayout->addLayout(deviceForm);

  // Remove button
  QPushButton* removeBtn = new QPushButton("Remove Device");
  removeBtn->setStyleSheet(
    "QPushButton { background-color: #cc0000; color: white; border: none; "
    "padding: 8px; font-size: 12px; } "
    "QPushButton:hover { background-color: #ee0000; }");
  connect(removeBtn, &QPushButton::clicked, this, [this, index]() {
    onRemoveDeviceClicked(index);
  });
  groupLayout->addWidget(removeBtn);

  groupBox->setLayout(groupLayout);

  m_deviceFields.push_back(fields);

  // Insert before the stretch and add button
  int insertPos = formLayout->count() - 2;  // Before stretch and add button
  formLayout->insertWidget(insertPos, groupBox);
}

void TECDevicesFormWidget::rebuildForm()
{
  // Collect current data
  QJsonDocument currentData = collectToJson();

  // Reload form
  loadFromJson(currentData);
}

void TECDevicesFormWidget::loadFromJson(const QJsonDocument& config)
{
  clear();

  QVBoxLayout* formLayout = new QVBoxLayout(m_formContent);
  formLayout->setSpacing(20);

  QJsonArray devices = config.object()["tec_devices"].toArray();

  // Add each device
  for (int i = 0; i < devices.size(); ++i) {
    addDeviceGroup(devices[i].toObject());
  }

  // Add Device button
  QPushButton* addBtn = new QPushButton("Add TEC Device");
  addBtn->setStyleSheet(
    "QPushButton { background-color: #0066cc; color: white; border: none; "
    "padding: 10px; font-size: 14px; font-weight: bold; } "
    "QPushButton:hover { background-color: #0077ee; }");
  connect(addBtn, &QPushButton::clicked, this, &TECDevicesFormWidget::onAddDeviceClicked);
  formLayout->addWidget(addBtn);

  formLayout->addStretch();
}

void TECDevicesFormWidget::onAddDeviceClicked()
{
  QJsonObject newDevice;
  newDevice["name"] = QString("tec-device-%1").arg(m_deviceFields.size() + 1);
  newDevice["controller_type"] = "CLD101x";
  newDevice["ip_address"] = "127.0.0.1";
  newDevice["port"] = 65432;
  newDevice["enabled"] = true;
  newDevice["default_temperature"] = 25.0;
  newDevice["default_laser_current"] = 50.0;

  addDeviceGroup(newDevice);
}

void TECDevicesFormWidget::onRemoveDeviceClicked(int index)
{
  if (index < 0 || index >= static_cast<int>(m_deviceFields.size())) return;

  // Allow removing all devices
  // Remove from fields
  m_deviceFields.erase(m_deviceFields.begin() + index);

  // Rebuild the form to update indices
  rebuildForm();
}

QJsonDocument TECDevicesFormWidget::collectToJson() const
{
  QJsonArray devicesArray;

  for (const auto& fields : m_deviceFields) {
    QJsonObject device;
    device["name"] = fields.name->text();
    device["controller_type"] = fields.controllerType->currentText();
    device["ip_address"] = fields.ipAddress->text();
    device["port"] = fields.port->value();
    device["enabled"] = fields.enabled->isChecked();
    device["default_temperature"] = fields.defaultTemperature->value();
    device["default_laser_current"] = fields.defaultLaserCurrent->value();
    devicesArray.append(device);
  }

  QJsonObject root;
  root["tec_devices"] = devicesArray;
  return QJsonDocument(root);
}
