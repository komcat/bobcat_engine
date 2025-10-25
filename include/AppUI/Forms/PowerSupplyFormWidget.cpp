// PowerSupplyFormWidget.cpp
#include "PowerSupplyFormWidget.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QJsonObject>
#include <QJsonArray>

PowerSupplyFormWidget::PowerSupplyFormWidget(QWidget* parent)
  : QWidget(parent)
  , m_scrollArea(nullptr)
  , m_formContent(nullptr)
  , m_serverHost(nullptr)
  , m_serverPort(nullptr)
{
  setupUI();
}

void PowerSupplyFormWidget::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);

  // Title
  QLabel* title = new QLabel("Power Supply Configuration");
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

void PowerSupplyFormWidget::clear()
{
  m_deviceFields.clear();
  m_serverHost = nullptr;
  m_serverPort = nullptr;

  if (m_formContent->layout()) {
    QLayoutItem* item;
    while ((item = m_formContent->layout()->takeAt(0)) != nullptr) {
      delete item->widget();
      delete item;
    }
    delete m_formContent->layout();
  }
}

void PowerSupplyFormWidget::addDeviceGroup(const QJsonObject& device)
{
  QVBoxLayout* formLayout = qobject_cast<QVBoxLayout*>(m_formContent->layout());
  if (!formLayout) return;

  int index = static_cast<int>(m_deviceFields.size());

  QString formStyle =
    "QLineEdit, QSpinBox, QComboBox { background-color: #3a3a3a; color: white; border: 1px solid #555555; "
    "padding: 5px; font-size: 14px; }";

  QGroupBox* groupBox = new QGroupBox(QString("Power Supply Device #%1").arg(index + 1));
  groupBox->setStyleSheet(
    "QGroupBox { color: white; font-size: 16px; font-weight: bold; "
    "border: 2px solid #555555; border-radius: 5px; margin-top: 10px; padding-top: 10px; } "
    "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }");

  QVBoxLayout* groupLayout = new QVBoxLayout();

  QFormLayout* deviceForm = new QFormLayout();
  deviceForm->setSpacing(10);
  deviceForm->setLabelAlignment(Qt::AlignRight);

  PowerSupplyDeviceFields fields;

  // Name
  fields.name = new QLineEdit(device["name"].toString());
  fields.name->setStyleSheet(formStyle);
  deviceForm->addRow(new QLabel("<span style='color: white;'>Name:</span>"), fields.name);

  // Type
  fields.type = new QComboBox();
  fields.type->addItems({"keysight_e36103b", "custom"});
  fields.type->setCurrentText(device.isEmpty() ? "keysight_e36103b" : device["type"].toString());
  fields.type->setStyleSheet(formStyle);
  deviceForm->addRow(new QLabel("<span style='color: white;'>Type:</span>"), fields.type);

  // IP
  fields.ip = new QLineEdit(device.isEmpty() ? "192.168.0.60" : device["ip"].toString());
  fields.ip->setStyleSheet(formStyle);
  deviceForm->addRow(new QLabel("<span style='color: white;'>IP Address:</span>"), fields.ip);

  // Port
  fields.port = new QSpinBox();
  fields.port->setRange(0, 65535);
  fields.port->setValue(device.isEmpty() ? 5025 : device["port"].toInt());
  fields.port->setStyleSheet(formStyle);
  deviceForm->addRow(new QLabel("<span style='color: white;'>Port:</span>"), fields.port);

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

void PowerSupplyFormWidget::rebuildForm()
{
  // Collect current data
  QJsonDocument currentData = collectToJson();

  // Reload form
  loadFromJson(currentData);
}

void PowerSupplyFormWidget::loadFromJson(const QJsonDocument& config)
{
  clear();

  QVBoxLayout* formLayout = new QVBoxLayout(m_formContent);
  formLayout->setSpacing(20);

  QString formStyle =
    "QLineEdit, QSpinBox { background-color: #3a3a3a; color: white; border: 1px solid #555555; "
    "padding: 5px; font-size: 14px; }";

  // Server Section
  QGroupBox* serverGroup = new QGroupBox("Server Configuration");
  serverGroup->setStyleSheet(
    "QGroupBox { color: white; font-size: 16px; font-weight: bold; "
    "border: 2px solid #555555; border-radius: 5px; margin-top: 10px; padding-top: 10px; } "
    "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }");

  QFormLayout* serverForm = new QFormLayout();
  serverForm->setSpacing(10);
  serverForm->setLabelAlignment(Qt::AlignRight);

  QJsonObject server = config.object()["server"].toObject();

  // Server Host
  m_serverHost = new QLineEdit(server.isEmpty() ? "127.0.0.60" : server["host"].toString());
  m_serverHost->setStyleSheet(formStyle);
  serverForm->addRow(new QLabel("<span style='color: white;'>Host:</span>"), m_serverHost);

  // Server Port
  m_serverPort = new QSpinBox();
  m_serverPort->setRange(0, 65535);
  m_serverPort->setValue(server.isEmpty() ? 5000 : server["port"].toInt());
  m_serverPort->setStyleSheet(formStyle);
  serverForm->addRow(new QLabel("<span style='color: white;'>Port:</span>"), m_serverPort);

  serverGroup->setLayout(serverForm);
  formLayout->addWidget(serverGroup);

  // Devices Section
  QJsonArray devices = config.object()["devices"].toArray();

  // Add each device
  for (int i = 0; i < devices.size(); ++i) {
    addDeviceGroup(devices[i].toObject());
  }

  // Add Device button
  QPushButton* addBtn = new QPushButton("Add Power Supply Device");
  addBtn->setStyleSheet(
    "QPushButton { background-color: #0066cc; color: white; border: none; "
    "padding: 10px; font-size: 14px; font-weight: bold; } "
    "QPushButton:hover { background-color: #0077ee; }");
  connect(addBtn, &QPushButton::clicked, this, &PowerSupplyFormWidget::onAddDeviceClicked);
  formLayout->addWidget(addBtn);

  formLayout->addStretch();
}

void PowerSupplyFormWidget::onAddDeviceClicked()
{
  QJsonObject newDevice;
  newDevice["name"] = QString("PSU%1").arg(m_deviceFields.size() + 1);
  newDevice["type"] = "keysight_e36103b";
  newDevice["ip"] = "192.168.0.60";
  newDevice["port"] = 5025;

  addDeviceGroup(newDevice);
}

void PowerSupplyFormWidget::onRemoveDeviceClicked(int index)
{
  if (index < 0 || index >= static_cast<int>(m_deviceFields.size())) return;

  // Allow removing all devices (unlike TCP clients which must keep at least one)
  // Remove from fields
  m_deviceFields.erase(m_deviceFields.begin() + index);

  // Rebuild the form to update indices
  rebuildForm();
}

QJsonDocument PowerSupplyFormWidget::collectToJson() const
{
  QJsonObject root;

  // Server section
  QJsonObject server;
  if (m_serverHost && m_serverPort) {
    server["host"] = m_serverHost->text();
    server["port"] = m_serverPort->value();
    root["server"] = server;
  }

  // Devices array
  QJsonArray devicesArray;
  for (const auto& fields : m_deviceFields) {
    QJsonObject device;
    device["name"] = fields.name->text();
    device["type"] = fields.type->currentText();
    device["ip"] = fields.ip->text();
    device["port"] = fields.port->value();
    devicesArray.append(device);
  }
  root["devices"] = devicesArray;

  return QJsonDocument(root);
}
