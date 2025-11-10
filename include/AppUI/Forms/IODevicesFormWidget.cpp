// IODevicesFormWidget.cpp
#include "IODevicesFormWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QHeaderView>
#include <QJsonArray>
#include <QMessageBox>

IODevicesFormWidget::IODevicesFormWidget(QWidget* parent)
  : QWidget(parent)
  , m_leftPanel(nullptr)
  , m_rightPanel(nullptr)
  , m_deviceList(nullptr)
  , m_deviceName(nullptr)
  , m_deviceId(nullptr)
  , m_ipAddress(nullptr)
  , m_inputCount(nullptr)
  , m_outputCount(nullptr)
  , m_enabled(nullptr)
  , m_inputPinsTable(nullptr)
  , m_outputPinsTable(nullptr)
{
  setupUI();
}

void IODevicesFormWidget::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Title
  QLabel* title = new QLabel("IO Devices Configuration");
  title->setStyleSheet("QLabel { color: white; font-size: 20px; font-weight: bold; padding: 10px; background-color: #1e1e1e; }");
  title->setFixedHeight(50);  // ✅ Fixed height to prevent expansion
  title->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);  // ✅ Don't expand vertically
  mainLayout->addWidget(title);

  // Splitter for left/right panels
  QSplitter* splitter = new QSplitter(Qt::Horizontal);
  splitter->setStyleSheet("QSplitter::handle { background-color: #3c3c3c; }");

  createLeftPanel();
  createRightPanel();

  splitter->addWidget(m_leftPanel);
  splitter->addWidget(m_rightPanel);
  splitter->setStretchFactor(0, 30);  // 30% left
  splitter->setStretchFactor(1, 70);  // 70% right

  mainLayout->addWidget(splitter);
}

void IODevicesFormWidget::createLeftPanel()
{
  m_leftPanel = new QWidget();
  m_leftPanel->setStyleSheet("QWidget { background-color: #2b2b2b; }");

  QVBoxLayout* layout = new QVBoxLayout(m_leftPanel);
  layout->setContentsMargins(10, 10, 10, 10);
  layout->setSpacing(10);

  // Title
  QLabel* titleLabel = new QLabel("IO Devices");
  titleLabel->setStyleSheet("QLabel { font-size: 16px; font-weight: bold; color: white; }");
  layout->addWidget(titleLabel);

  // Device list
  m_deviceList = new QListWidget();
  m_deviceList->setStyleSheet(
    "QListWidget { "
    "  background-color: #1e1e1e; "
    "  border: 1px solid #3c3c3c; "
    "  border-radius: 4px; "
    "  padding: 5px; "
    "  font-size: 14px; "
    "  color: white; "
    "} "
    "QListWidget::item { "
    "  padding: 10px; "
    "  border-bottom: 1px solid #3c3c3c; "
    "} "
    "QListWidget::item:selected { "
    "  background-color: #1177bb; "
    "  color: white; "
    "} "
    "QListWidget::item:hover { "
    "  background-color: #2d2d30; "
    "}");
  connect(m_deviceList, &QListWidget::itemSelectionChanged, this, &IODevicesFormWidget::onDeviceSelected);
  layout->addWidget(m_deviceList);

  // Add/Remove buttons
  QHBoxLayout* buttonLayout = new QHBoxLayout();

  m_addDeviceButton = new QPushButton("➕ Add Device");
  m_addDeviceButton->setStyleSheet(
    "QPushButton { background-color: #27ae60; color: white; border: none; "
    "border-radius: 4px; padding: 8px; font-size: 13px; font-weight: bold; } "
    "QPushButton:hover { background-color: #229954; }");
  connect(m_addDeviceButton, &QPushButton::clicked, this, &IODevicesFormWidget::onAddDeviceClicked);
  buttonLayout->addWidget(m_addDeviceButton);

  m_removeDeviceButton = new QPushButton("🗑 Remove");
  m_removeDeviceButton->setStyleSheet(
    "QPushButton { background-color: #e74c3c; color: white; border: none; "
    "border-radius: 4px; padding: 8px; font-size: 13px; font-weight: bold; } "
    "QPushButton:hover { background-color: #c0392b; }");
  connect(m_removeDeviceButton, &QPushButton::clicked, this, &IODevicesFormWidget::onRemoveDeviceClicked);
  buttonLayout->addWidget(m_removeDeviceButton);

  layout->addLayout(buttonLayout);
}

void IODevicesFormWidget::createRightPanel()
{
  m_rightPanel = new QWidget();
  m_rightPanel->setStyleSheet("QWidget { background-color: #2b2b2b; }");

  QHBoxLayout* layout = new QHBoxLayout(m_rightPanel);
  layout->setContentsMargins(10, 10, 10, 10);
  layout->setSpacing(10);

  QString groupBoxStyle =
    "QGroupBox { "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "  color: white; "
    "  background-color: #1e1e1e; "
    "  border: 2px solid #3c3c3c; "
    "  border-radius: 5px; "
    "  margin-top: 10px; "
    "  padding-top: 10px; "
    "} "
    "QGroupBox::title { "
    "  subcontrol-origin: margin; "
    "  left: 10px; "
    "  padding: 0 5px; "
    "}";

  QString formStyle =
    "QLineEdit, QSpinBox { background-color: #2d2d30; color: white; border: 1px solid #3c3c3c; "
    "padding: 5px; font-size: 14px; } "
    "QLineEdit:focus, QSpinBox:focus { border: 1px solid #1177bb; }";

  // ============================================================================
  // COLUMN 1: DEVICE INFORMATION
  // ============================================================================
  QGroupBox* infoGroup = new QGroupBox("Device Info");
  infoGroup->setStyleSheet(groupBoxStyle);

  QFormLayout* infoLayout = new QFormLayout(infoGroup);
  infoLayout->setSpacing(10);

  // Name
  m_deviceName = new QLineEdit();
  m_deviceName->setStyleSheet(formStyle);
  infoLayout->addRow(new QLabel("<span style='color: white; font-weight: bold;'>Name:</span>"), m_deviceName);

  // Device ID
  m_deviceId = new QSpinBox();
  m_deviceId->setRange(0, 255);
  m_deviceId->setStyleSheet(formStyle);
  infoLayout->addRow(new QLabel("<span style='color: white; font-weight: bold;'>ID:</span>"), m_deviceId);

  // IP Address
  m_ipAddress = new QLineEdit();
  m_ipAddress->setStyleSheet(formStyle);
  infoLayout->addRow(new QLabel("<span style='color: white; font-weight: bold;'>IP:</span>"), m_ipAddress);

  // Input Count
  m_inputCount = new QSpinBox();
  m_inputCount->setRange(0, 64);
  m_inputCount->setStyleSheet(formStyle);
  infoLayout->addRow(new QLabel("<span style='color: white; font-weight: bold;'>Inputs:</span>"), m_inputCount);

  // Output Count
  m_outputCount = new QSpinBox();
  m_outputCount->setRange(0, 64);
  m_outputCount->setStyleSheet(formStyle);
  infoLayout->addRow(new QLabel("<span style='color: white; font-weight: bold;'>Outputs:</span>"), m_outputCount);

  // Enabled
  m_enabled = new QCheckBox("Enabled");
  m_enabled->setStyleSheet("QCheckBox { color: white; font-weight: bold; }");
  infoLayout->addRow("", m_enabled);

  layout->addWidget(infoGroup, 1);  // 1 part width

  // ============================================================================
  // COLUMN 2: INPUT PINS
  // ============================================================================
  QGroupBox* inputGroup = new QGroupBox("Input Pins");
  inputGroup->setStyleSheet(groupBoxStyle);

  QVBoxLayout* inputLayout = new QVBoxLayout(inputGroup);

  m_inputPinsTable = new QTableWidget();
  m_inputPinsTable->setColumnCount(2);
  m_inputPinsTable->setHorizontalHeaderLabels({ "Name", "Pin" });
  m_inputPinsTable->horizontalHeader()->setStretchLastSection(false);
  m_inputPinsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_inputPinsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
  m_inputPinsTable->setColumnWidth(1, 50);
  m_inputPinsTable->setStyleSheet(
    "QTableWidget { "
    "  background-color: #2d2d30; "
    "  color: white; "
    "  border: 1px solid #3c3c3c; "
    "  gridline-color: #3c3c3c; "
    "} "
    "QTableWidget::item:selected { "
    "  background-color: #1177bb; "
    "} "
    "QHeaderView::section { "
    "  background-color: #1e1e1e; "
    "  color: white; "
    "  font-weight: bold; "
    "  padding: 5px; "
    "  border: 1px solid #3c3c3c; "
    "}");
  inputLayout->addWidget(m_inputPinsTable);

  QHBoxLayout* inputButtonLayout = new QHBoxLayout();
  m_addInputButton = new QPushButton("➕");
  m_addInputButton->setToolTip("Add Input Pin");
  m_addInputButton->setStyleSheet(
    "QPushButton { background-color: #3498db; color: white; border: none; border-radius: 4px; padding: 6px; font-size: 14px; font-weight: bold; } "
    "QPushButton:hover { background-color: #2980b9; }");
  connect(m_addInputButton, &QPushButton::clicked, this, &IODevicesFormWidget::onAddInputPinClicked);

  m_removeInputButton = new QPushButton("🗑");
  m_removeInputButton->setToolTip("Remove Selected Pin");
  m_removeInputButton->setStyleSheet(
    "QPushButton { background-color: #e74c3c; color: white; border: none; border-radius: 4px; padding: 6px; font-size: 14px; font-weight: bold; } "
    "QPushButton:hover { background-color: #c0392b; }");
  connect(m_removeInputButton, &QPushButton::clicked, this, &IODevicesFormWidget::onRemoveInputPinClicked);

  inputButtonLayout->addWidget(m_addInputButton);
  inputButtonLayout->addWidget(m_removeInputButton);
  inputLayout->addLayout(inputButtonLayout);

  layout->addWidget(inputGroup, 2);  // 2 parts width

  // ============================================================================
  // COLUMN 3: OUTPUT PINS
  // ============================================================================
  QGroupBox* outputGroup = new QGroupBox("Output Pins");
  outputGroup->setStyleSheet(groupBoxStyle);

  QVBoxLayout* outputLayout = new QVBoxLayout(outputGroup);

  m_outputPinsTable = new QTableWidget();
  m_outputPinsTable->setColumnCount(2);
  m_outputPinsTable->setHorizontalHeaderLabels({ "Name", "Pin" });
  m_outputPinsTable->horizontalHeader()->setStretchLastSection(false);
  m_outputPinsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_outputPinsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
  m_outputPinsTable->setColumnWidth(1, 50);
  m_outputPinsTable->setStyleSheet(m_inputPinsTable->styleSheet());
  outputLayout->addWidget(m_outputPinsTable);

  QHBoxLayout* outputButtonLayout = new QHBoxLayout();
  m_addOutputButton = new QPushButton("➕");
  m_addOutputButton->setToolTip("Add Output Pin");
  m_addOutputButton->setStyleSheet(m_addInputButton->styleSheet());
  connect(m_addOutputButton, &QPushButton::clicked, this, &IODevicesFormWidget::onAddOutputPinClicked);

  m_removeOutputButton = new QPushButton("🗑");
  m_removeOutputButton->setToolTip("Remove Selected Pin");
  m_removeOutputButton->setStyleSheet(m_removeInputButton->styleSheet());
  connect(m_removeOutputButton, &QPushButton::clicked, this, &IODevicesFormWidget::onRemoveOutputPinClicked);

  outputButtonLayout->addWidget(m_addOutputButton);
  outputButtonLayout->addWidget(m_removeOutputButton);
  outputLayout->addLayout(outputButtonLayout);

  layout->addWidget(outputGroup, 2);  // 2 parts width
}

void IODevicesFormWidget::clear()
{
  m_deviceList->clear();
  m_deviceName->clear();
  m_deviceId->setValue(0);
  m_ipAddress->clear();
  m_inputCount->setValue(0);
  m_outputCount->setValue(0);
  m_enabled->setChecked(false);
  m_inputPinsTable->setRowCount(0);
  m_outputPinsTable->setRowCount(0);
  m_selectedDevice = "";
}

void IODevicesFormWidget::loadFromJson(const QJsonDocument& config)
{
  clear();
  m_currentConfig = config;

  QJsonArray devices = config.object()["eziio"].toArray();

  for (const auto& deviceValue : devices) {
    QJsonObject device = deviceValue.toObject();
    QString name = device["name"].toString();
    m_deviceList->addItem(name);
  }

  // Auto-select first device
  if (m_deviceList->count() > 0) {
    m_deviceList->setCurrentRow(0);
  }
}

void IODevicesFormWidget::onDeviceSelected()
{
  if (m_deviceList->selectedItems().isEmpty()) {
    m_selectedDevice = "";
    return;
  }

  // Save previous device data
  saveCurrentDeviceData();

  m_selectedDevice = m_deviceList->currentItem()->text().toStdString();

  // Find device in config
  QJsonArray devices = m_currentConfig.object()["eziio"].toArray();
  for (const auto& deviceValue : devices) {
    QJsonObject device = deviceValue.toObject();
    if (device["name"].toString().toStdString() == m_selectedDevice) {
      loadDevicePinMappings(device);
      return;
    }
  }
}

void IODevicesFormWidget::loadDevicePinMappings(const QJsonObject& device)
{
  // Load device info
  m_deviceName->setText(device["name"].toString());
  m_deviceId->setValue(device["deviceId"].toInt());
  m_ipAddress->setText(device["ip"].toString());
  m_inputCount->setValue(device["inputCount"].toInt());
  m_outputCount->setValue(device["outputCount"].toInt());
  m_enabled->setChecked(device["enabled"].toBool());

  // Load input pins
  m_inputPinsTable->setRowCount(0);
  if (device.contains("ioConfig")) {
    QJsonObject ioConfig = device["ioConfig"].toObject();
    if (ioConfig.contains("inputs")) {
      QJsonArray inputs = ioConfig["inputs"].toArray();
      for (const auto& inputValue : inputs) {
        QJsonObject input = inputValue.toObject();
        int row = m_inputPinsTable->rowCount();
        m_inputPinsTable->insertRow(row);
        m_inputPinsTable->setItem(row, 0, new QTableWidgetItem(input["name"].toString()));
        m_inputPinsTable->setItem(row, 1, new QTableWidgetItem(QString::number(input["pin"].toInt())));
      }
    }

    // Load output pins
    m_outputPinsTable->setRowCount(0);
    if (ioConfig.contains("outputs")) {
      QJsonArray outputs = ioConfig["outputs"].toArray();
      for (const auto& outputValue : outputs) {
        QJsonObject output = outputValue.toObject();
        int row = m_outputPinsTable->rowCount();
        m_outputPinsTable->insertRow(row);
        m_outputPinsTable->setItem(row, 0, new QTableWidgetItem(output["name"].toString()));
        m_outputPinsTable->setItem(row, 1, new QTableWidgetItem(QString::number(output["pin"].toInt())));
      }
    }
  }
}

void IODevicesFormWidget::saveCurrentDeviceData()
{
  if (m_selectedDevice.empty()) return;

  // Update the config with current form data
  QJsonArray devices = m_currentConfig.object()["eziio"].toArray();

  for (int i = 0; i < devices.size(); ++i) {
    QJsonObject device = devices[i].toObject();
    if (device["name"].toString().toStdString() == m_selectedDevice) {
      device = getCurrentDeviceJson();
      devices[i] = device;
      break;
    }
  }

  QJsonObject root = m_currentConfig.object();
  root["eziio"] = devices;
  m_currentConfig.setObject(root);
}

QJsonObject IODevicesFormWidget::getCurrentDeviceJson() const
{
  QJsonObject device;
  device["name"] = m_deviceName->text();
  device["deviceId"] = m_deviceId->value();
  device["ip"] = m_ipAddress->text();
  device["inputCount"] = m_inputCount->value();
  device["outputCount"] = m_outputCount->value();
  device["enabled"] = m_enabled->isChecked();

  // Build ioConfig
  QJsonObject ioConfig;

  // Input pins
  QJsonArray inputs;
  for (int row = 0; row < m_inputPinsTable->rowCount(); ++row) {
    QJsonObject input;
    input["name"] = m_inputPinsTable->item(row, 0)->text();
    input["pin"] = m_inputPinsTable->item(row, 1)->text().toInt();
    inputs.append(input);
  }
  ioConfig["inputs"] = inputs;

  // Output pins
  QJsonArray outputs;
  for (int row = 0; row < m_outputPinsTable->rowCount(); ++row) {
    QJsonObject output;
    output["name"] = m_outputPinsTable->item(row, 0)->text();
    output["pin"] = m_outputPinsTable->item(row, 1)->text().toInt();
    outputs.append(output);
  }
  ioConfig["outputs"] = outputs;

  device["ioConfig"] = ioConfig;
  return device;
}

void IODevicesFormWidget::onAddDeviceClicked()
{
  // Save current device
  saveCurrentDeviceData();

  QString newName = QString("NewDevice_%1").arg(m_deviceList->count() + 1);

  // Add to list
  m_deviceList->addItem(newName);

  // Add to config
  QJsonObject newDevice;
  newDevice["name"] = newName;
  newDevice["deviceId"] = m_deviceList->count() - 1;
  newDevice["ip"] = "192.168.0.10";
  newDevice["inputCount"] = 8;
  newDevice["outputCount"] = 8;
  newDevice["enabled"] = true;

  QJsonObject ioConfig;
  ioConfig["inputs"] = QJsonArray();
  ioConfig["outputs"] = QJsonArray();
  newDevice["ioConfig"] = ioConfig;

  QJsonArray devices = m_currentConfig.object()["eziio"].toArray();
  devices.append(newDevice);

  QJsonObject root = m_currentConfig.object();
  root["eziio"] = devices;
  m_currentConfig.setObject(root);

  // Select the new device
  m_deviceList->setCurrentRow(m_deviceList->count() - 1);
}

void IODevicesFormWidget::onRemoveDeviceClicked()
{
  if (m_deviceList->selectedItems().isEmpty()) return;

  int currentRow = m_deviceList->currentRow();
  QString deviceName = m_deviceList->currentItem()->text();

  // Remove from config
  QJsonArray devices = m_currentConfig.object()["eziio"].toArray();
  for (int i = 0; i < devices.size(); ++i) {
    if (devices[i].toObject()["name"].toString() == deviceName) {
      devices.removeAt(i);
      break;
    }
  }

  QJsonObject root = m_currentConfig.object();
  root["eziio"] = devices;
  m_currentConfig.setObject(root);

  // Remove from list
  delete m_deviceList->takeItem(currentRow);
  m_selectedDevice = "";
}

void IODevicesFormWidget::onAddInputPinClicked()
{
  int row = m_inputPinsTable->rowCount();
  m_inputPinsTable->insertRow(row);
  m_inputPinsTable->setItem(row, 0, new QTableWidgetItem(QString("Input%1").arg(row)));
  m_inputPinsTable->setItem(row, 1, new QTableWidgetItem(QString::number(row)));
}

void IODevicesFormWidget::onRemoveInputPinClicked()
{
  int currentRow = m_inputPinsTable->currentRow();
  if (currentRow >= 0) {
    m_inputPinsTable->removeRow(currentRow);
  }
}

void IODevicesFormWidget::onAddOutputPinClicked()
{
  int row = m_outputPinsTable->rowCount();
  m_outputPinsTable->insertRow(row);
  m_outputPinsTable->setItem(row, 0, new QTableWidgetItem(QString("Output%1").arg(row)));
  m_outputPinsTable->setItem(row, 1, new QTableWidgetItem(QString::number(row)));
}

void IODevicesFormWidget::onRemoveOutputPinClicked()
{
  int currentRow = m_outputPinsTable->currentRow();
  if (currentRow >= 0) {
    m_outputPinsTable->removeRow(currentRow);
  }
}

QJsonDocument IODevicesFormWidget::collectToJson() const
{
  // Save current device data one more time
  const_cast<IODevicesFormWidget*>(this)->saveCurrentDeviceData();
  return m_currentConfig;
}