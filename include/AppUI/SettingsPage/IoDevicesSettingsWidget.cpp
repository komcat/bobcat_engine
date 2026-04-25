// IoDevicesSettingsWidget.cpp
#include "IoDevicesSettingsWidget.h"
#include "context/ApplicationContext.h"
#include "logger/LogManager.h"

#include <QListWidget>
#include <QListWidgetItem>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QFileInfo>
#include <QDir>

#include <fstream>
#include <iomanip>

using json = nlohmann::json;

const std::string IoDevicesSettingsWidget::kLocalConfigPath = "config/io_devices.local.json";

namespace {
constexpr int kPinColNumber = 0;
constexpr int kPinColName   = 1;
}  // namespace

IoDevicesSettingsWidget::IoDevicesSettingsWidget(ApplicationContext* context, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_currentIndex(-1)
  , m_pinOverrides(json::object())
  , m_deviceList(nullptr)
  , m_addEziIOButton(nullptr)
  , m_addWagoButton(nullptr)
  , m_removeButton(nullptr)
  , m_detailPanel(nullptr)
  , m_typeLabel(nullptr)
  , m_nameEdit(nullptr)
  , m_ipEdit(nullptr)
  , m_idSpin(nullptr)
  , m_enabledCheck(nullptr)
  , m_inputCountSpin(nullptr)
  , m_outputCountSpin(nullptr)
  , m_inputsTable(nullptr)
  , m_outputsTable(nullptr)
  , m_addInputButton(nullptr)
  , m_addOutputButton(nullptr)
  , m_removeInputButton(nullptr)
  , m_removeOutputButton(nullptr)
  , m_saveButton(nullptr)
  , m_reloadButton(nullptr)
  , m_statusLabel(nullptr)
{
  setupUI();
  loadFromDisk();
}

void IoDevicesSettingsWidget::setupUI()
{
  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->setContentsMargins(10, 10, 10, 10);
  layout->setSpacing(8);

  layout->addWidget(new QLabel("<h2>IO Devices</h2>"));

  QLabel* pathLabel = new QLabel(QString("Config: %1").arg(QString::fromStdString(kLocalConfigPath)));
  pathLabel->setStyleSheet("color: #888; font-style: italic;");
  layout->addWidget(pathLabel);

  QLabel* warning = new QLabel("⚠ Changes require an application restart to take effect.");
  warning->setStyleSheet("color: #d80; font-weight: bold;");
  layout->addWidget(warning);

  QSplitter* split = new QSplitter(Qt::Horizontal, this);

  // Left side: device list + add/remove buttons
  QWidget* leftWrap = new QWidget();
  QVBoxLayout* leftLay = new QVBoxLayout(leftWrap);
  leftLay->setContentsMargins(0, 0, 0, 0);

  leftLay->addWidget(new QLabel("<b>Devices</b>"));
  m_deviceList = new QListWidget();
  leftLay->addWidget(m_deviceList);

  QHBoxLayout* leftButtons = new QHBoxLayout();
  m_addEziIOButton = new QPushButton("+ EziIO");
  m_addWagoButton = new QPushButton("+ WAGO");
  m_removeButton = new QPushButton("Remove");
  m_removeButton->setStyleSheet("QPushButton { color: #c33; font-weight: bold; }");
  leftButtons->addWidget(m_addEziIOButton);
  leftButtons->addWidget(m_addWagoButton);
  leftButtons->addWidget(m_removeButton);
  leftLay->addLayout(leftButtons);

  split->addWidget(leftWrap);

  // Right side: detail panel
  m_detailPanel = new QWidget();
  QVBoxLayout* detailLay = new QVBoxLayout(m_detailPanel);
  detailLay->setContentsMargins(10, 0, 0, 0);

  // Form
  QGroupBox* formGroup = new QGroupBox("Device Properties");
  QFormLayout* form = new QFormLayout(formGroup);
  m_typeLabel = new QLabel("—");
  m_nameEdit = new QLineEdit();
  m_ipEdit = new QLineEdit();
  m_idSpin = new QSpinBox();
  m_idSpin->setRange(0, 1000);
  m_enabledCheck = new QCheckBox("Enabled");
  m_inputCountSpin = new QSpinBox();
  m_inputCountSpin->setRange(0, 256);
  m_outputCountSpin = new QSpinBox();
  m_outputCountSpin->setRange(0, 256);

  form->addRow("Type:", m_typeLabel);
  form->addRow("Name:", m_nameEdit);
  form->addRow("IP Address:", m_ipEdit);
  form->addRow("Device ID:", m_idSpin);
  form->addRow("", m_enabledCheck);
  form->addRow("Input Count:", m_inputCountSpin);
  form->addRow("Output Count:", m_outputCountSpin);
  detailLay->addWidget(formGroup);

  // Inputs / Outputs side-by-side
  QHBoxLayout* pinsRow = new QHBoxLayout();

  {
    QGroupBox* inGroup = new QGroupBox("Inputs (pin → name)");
    QVBoxLayout* inLay = new QVBoxLayout(inGroup);
    m_inputsTable = new QTableWidget(0, 2);
    m_inputsTable->setHorizontalHeaderLabels(QStringList() << "Pin" << "Name");
    m_inputsTable->horizontalHeader()->setSectionResizeMode(kPinColNumber, QHeaderView::ResizeToContents);
    m_inputsTable->horizontalHeader()->setSectionResizeMode(kPinColName, QHeaderView::Stretch);
    m_inputsTable->verticalHeader()->setVisible(false);
    inLay->addWidget(m_inputsTable);
    QHBoxLayout* inBtns = new QHBoxLayout();
    m_addInputButton = new QPushButton("+ Add Input");
    m_removeInputButton = new QPushButton("− Remove Selected");
    inBtns->addWidget(m_addInputButton);
    inBtns->addWidget(m_removeInputButton);
    inBtns->addStretch();
    inLay->addLayout(inBtns);
    pinsRow->addWidget(inGroup, 1);
  }

  {
    QGroupBox* outGroup = new QGroupBox("Outputs (pin → name)");
    QVBoxLayout* outLay = new QVBoxLayout(outGroup);
    m_outputsTable = new QTableWidget(0, 2);
    m_outputsTable->setHorizontalHeaderLabels(QStringList() << "Pin" << "Name");
    m_outputsTable->horizontalHeader()->setSectionResizeMode(kPinColNumber, QHeaderView::ResizeToContents);
    m_outputsTable->horizontalHeader()->setSectionResizeMode(kPinColName, QHeaderView::Stretch);
    m_outputsTable->verticalHeader()->setVisible(false);
    outLay->addWidget(m_outputsTable);
    QHBoxLayout* outBtns = new QHBoxLayout();
    m_addOutputButton = new QPushButton("+ Add Output");
    m_removeOutputButton = new QPushButton("− Remove Selected");
    outBtns->addWidget(m_addOutputButton);
    outBtns->addWidget(m_removeOutputButton);
    outBtns->addStretch();
    outLay->addLayout(outBtns);
    pinsRow->addWidget(outGroup, 1);
  }

  detailLay->addLayout(pinsRow);

  split->addWidget(m_detailPanel);
  split->setStretchFactor(0, 1);
  split->setStretchFactor(1, 3);
  layout->addWidget(split, 1);

  // Bottom save row
  QHBoxLayout* bottom = new QHBoxLayout();
  m_reloadButton = new QPushButton("Reload from Disk");
  m_saveButton = new QPushButton("Save");
  bottom->addStretch();
  bottom->addWidget(m_reloadButton);
  bottom->addWidget(m_saveButton);
  layout->addLayout(bottom);

  m_statusLabel = new QLabel("");
  m_statusLabel->setWordWrap(true);
  layout->addWidget(m_statusLabel);

  clearPanel();

  connect(m_deviceList, &QListWidget::currentRowChanged, this, [this](int) { onDeviceSelected(); });
  connect(m_addEziIOButton, &QPushButton::clicked, this, &IoDevicesSettingsWidget::onAddEziIO);
  connect(m_addWagoButton, &QPushButton::clicked, this, &IoDevicesSettingsWidget::onAddWago);
  connect(m_removeButton, &QPushButton::clicked, this, &IoDevicesSettingsWidget::onRemoveDevice);
  connect(m_addInputButton, &QPushButton::clicked, this, &IoDevicesSettingsWidget::onAddInput);
  connect(m_addOutputButton, &QPushButton::clicked, this, &IoDevicesSettingsWidget::onAddOutput);
  connect(m_removeInputButton, &QPushButton::clicked, this, &IoDevicesSettingsWidget::onRemoveInputRow);
  connect(m_removeOutputButton, &QPushButton::clicked, this, &IoDevicesSettingsWidget::onRemoveOutputRow);
  connect(m_saveButton, &QPushButton::clicked, this, &IoDevicesSettingsWidget::onSave);
  connect(m_reloadButton, &QPushButton::clicked, this, &IoDevicesSettingsWidget::onReloadFromDisk);
}

void IoDevicesSettingsWidget::loadFromDisk()
{
  m_devices.clear();
  m_pinOverrides = json::object();
  m_currentIndex = -1;

  std::ifstream file(kLocalConfigPath);
  if (!file.is_open()) {
    refreshDeviceList();
    clearPanel();
    setStatus(QString("No %1 yet — edits will create it on Save.")
              .arg(QString::fromStdString(kLocalConfigPath)));
    return;
  }

  try {
    json cfg = json::parse(file);

    if (cfg.contains("eziio") && cfg["eziio"].is_array()) {
      for (const auto& d : cfg["eziio"]) {
        m_devices.push_back({DeviceType::EziIO, d});
      }
    }
    if (cfg.contains("wago") && cfg["wago"].is_array()) {
      for (const auto& d : cfg["wago"]) {
        m_devices.push_back({DeviceType::Wago, d});
      }
    }
    if (cfg.contains("pin_overrides") && cfg["pin_overrides"].is_object()) {
      m_pinOverrides = cfg["pin_overrides"];
    }

    refreshDeviceList();
    if (!m_devices.empty()) {
      m_deviceList->setCurrentRow(0);
    } else {
      clearPanel();
    }
    setStatus(QString("Loaded %1 device(s).").arg(m_devices.size()));
  } catch (const json::exception& e) {
    setStatus(QString("Parse error: %1").arg(e.what()), true);
  }
}

void IoDevicesSettingsWidget::refreshDeviceList()
{
  m_deviceList->blockSignals(true);
  m_deviceList->clear();
  for (const auto& e : m_devices) {
    m_deviceList->addItem(deviceDisplayName(e));
  }
  m_deviceList->blockSignals(false);
}

QString IoDevicesSettingsWidget::deviceDisplayName(const DeviceEntry& e) const
{
  const std::string name = e.data.value("name", std::string{"(unnamed)"});
  const QString prefix = (e.type == DeviceType::EziIO) ? "[EziIO] " : "[WAGO]  ";
  return prefix + QString::fromStdString(name);
}

void IoDevicesSettingsWidget::onDeviceSelected()
{
  // Persist current edits back to the model before switching.
  flushCurrentDeviceFromUI();

  const int row = m_deviceList->currentRow();
  if (row < 0 || row >= static_cast<int>(m_devices.size())) {
    m_currentIndex = -1;
    clearPanel();
    return;
  }

  m_currentIndex = row;
  showDeviceInPanel(row);
}

void IoDevicesSettingsWidget::showDeviceInPanel(int index)
{
  m_detailPanel->setEnabled(true);

  const DeviceEntry& e = m_devices[index];
  m_typeLabel->setText(e.type == DeviceType::EziIO ? "EziIO" : "WAGO");
  m_nameEdit->setText(QString::fromStdString(e.data.value("name", std::string{})));
  m_ipEdit->setText(QString::fromStdString(e.data.value("ip", std::string{})));
  m_idSpin->setValue(e.data.value("deviceId", 0));
  m_enabledCheck->setChecked(e.data.value("enabled", true));
  m_inputCountSpin->setValue(e.data.value("inputCount", 0));
  m_outputCountSpin->setValue(e.data.value("outputCount", 0));

  if (e.data.contains("ioConfig") && e.data["ioConfig"].is_object()) {
    const auto& io = e.data["ioConfig"];
    loadPinTable(m_inputsTable,  io.contains("inputs")  && io["inputs"].is_array()  ? io["inputs"]  : json::array());
    loadPinTable(m_outputsTable, io.contains("outputs") && io["outputs"].is_array() ? io["outputs"] : json::array());
  } else {
    loadPinTable(m_inputsTable, json::array());
    loadPinTable(m_outputsTable, json::array());
  }
}

void IoDevicesSettingsWidget::clearPanel()
{
  m_typeLabel->setText("—");
  m_nameEdit->clear();
  m_ipEdit->clear();
  m_idSpin->setValue(0);
  m_enabledCheck->setChecked(false);
  m_inputCountSpin->setValue(0);
  m_outputCountSpin->setValue(0);
  m_inputsTable->setRowCount(0);
  m_outputsTable->setRowCount(0);
  m_detailPanel->setEnabled(false);
}

void IoDevicesSettingsWidget::loadPinTable(QTableWidget* table, const json& pinsArray)
{
  table->setRowCount(0);
  for (const auto& p : pinsArray) {
    const int row = table->rowCount();
    table->insertRow(row);
    QSpinBox* pinSpin = new QSpinBox();
    pinSpin->setRange(0, 63);
    pinSpin->setValue(p.value("pin", 0));
    table->setCellWidget(row, kPinColNumber, pinSpin);
    table->setItem(row, kPinColName, new QTableWidgetItem(
      QString::fromStdString(p.value("name", std::string{}))));
  }
}

json IoDevicesSettingsWidget::pinTableToJson(QTableWidget* table) const
{
  json arr = json::array();
  for (int r = 0; r < table->rowCount(); ++r) {
    auto* pinSpin = qobject_cast<QSpinBox*>(table->cellWidget(r, kPinColNumber));
    QTableWidgetItem* nameItem = table->item(r, kPinColName);
    json p;
    p["pin"]  = pinSpin ? pinSpin->value() : 0;
    p["name"] = nameItem ? nameItem->text().toStdString() : std::string{};
    arr.push_back(p);
  }
  return arr;
}

void IoDevicesSettingsWidget::flushCurrentDeviceFromUI()
{
  if (m_currentIndex < 0 || m_currentIndex >= static_cast<int>(m_devices.size())) return;

  DeviceEntry& e = m_devices[m_currentIndex];
  e.data["name"]        = m_nameEdit->text().toStdString();
  e.data["ip"]          = m_ipEdit->text().toStdString();
  e.data["deviceId"]    = m_idSpin->value();
  e.data["enabled"]     = m_enabledCheck->isChecked();
  e.data["inputCount"]  = m_inputCountSpin->value();
  e.data["outputCount"] = m_outputCountSpin->value();
  e.data["ioConfig"] = {
    {"inputs",  pinTableToJson(m_inputsTable)},
    {"outputs", pinTableToJson(m_outputsTable)}
  };

  m_deviceList->blockSignals(true);
  if (auto* item = m_deviceList->item(m_currentIndex)) {
    item->setText(deviceDisplayName(e));
  }
  m_deviceList->blockSignals(false);
}

void IoDevicesSettingsWidget::onAddEziIO()
{
  flushCurrentDeviceFromUI();
  DeviceEntry e;
  e.type = DeviceType::EziIO;
  e.data = {
    {"deviceId", 0}, {"enabled", true},
    {"name", "NewEziIO"}, {"ip", "192.168.0.100"},
    {"inputCount", 16}, {"outputCount", 16},
    {"ioConfig", {{"inputs", json::array()}, {"outputs", json::array()}}}
  };
  m_devices.push_back(e);
  refreshDeviceList();
  m_deviceList->setCurrentRow(static_cast<int>(m_devices.size()) - 1);
}

void IoDevicesSettingsWidget::onAddWago()
{
  flushCurrentDeviceFromUI();
  DeviceEntry e;
  e.type = DeviceType::Wago;
  e.data = {
    {"deviceId", 10}, {"enabled", true},
    {"name", "NewWago"}, {"ip", "192.168.3.10"},
    {"inputCount", 8}, {"outputCount", 8},
    {"ioConfig", {{"inputs", json::array()}, {"outputs", json::array()}}}
  };
  m_devices.push_back(e);
  refreshDeviceList();
  m_deviceList->setCurrentRow(static_cast<int>(m_devices.size()) - 1);
}

void IoDevicesSettingsWidget::onRemoveDevice()
{
  const int row = m_deviceList->currentRow();
  if (row < 0 || row >= static_cast<int>(m_devices.size())) return;
  m_devices.erase(m_devices.begin() + row);
  m_currentIndex = -1;
  refreshDeviceList();
  if (!m_devices.empty()) {
    const int target = std::min(row, static_cast<int>(m_devices.size()) - 1);
    m_deviceList->setCurrentRow(target);
  } else {
    clearPanel();
  }
}

void IoDevicesSettingsWidget::onAddInput()
{
  const int row = m_inputsTable->rowCount();
  m_inputsTable->insertRow(row);
  QSpinBox* pinSpin = new QSpinBox();
  pinSpin->setRange(0, 63);
  pinSpin->setValue(row);
  m_inputsTable->setCellWidget(row, kPinColNumber, pinSpin);
  m_inputsTable->setItem(row, kPinColName, new QTableWidgetItem(QString("Input%1").arg(row)));
}

void IoDevicesSettingsWidget::onAddOutput()
{
  const int row = m_outputsTable->rowCount();
  m_outputsTable->insertRow(row);
  QSpinBox* pinSpin = new QSpinBox();
  pinSpin->setRange(0, 63);
  pinSpin->setValue(row);
  m_outputsTable->setCellWidget(row, kPinColNumber, pinSpin);
  m_outputsTable->setItem(row, kPinColName, new QTableWidgetItem(QString("Output%1").arg(row)));
}

void IoDevicesSettingsWidget::onRemoveInputRow()
{
  const int row = m_inputsTable->currentRow();
  if (row >= 0) m_inputsTable->removeRow(row);
}

void IoDevicesSettingsWidget::onRemoveOutputRow()
{
  const int row = m_outputsTable->currentRow();
  if (row >= 0) m_outputsTable->removeRow(row);
}

bool IoDevicesSettingsWidget::saveToLocalJson()
{
  flushCurrentDeviceFromUI();

  json cfg;
  cfg["eziio"] = json::array();
  cfg["wago"]  = json::array();

  for (const auto& e : m_devices) {
    if (e.type == DeviceType::EziIO) {
      cfg["eziio"].push_back(e.data);
    } else {
      cfg["wago"].push_back(e.data);
    }
  }

  if (!m_pinOverrides.empty()) {
    cfg["pin_overrides"] = m_pinOverrides;
  }

  QFileInfo fi(QString::fromStdString(kLocalConfigPath));
  QDir().mkpath(fi.absolutePath());

  std::ofstream out(kLocalConfigPath);
  if (!out.is_open()) {
    setStatus(QString("Failed to open %1 for writing")
              .arg(QString::fromStdString(kLocalConfigPath)), true);
    return false;
  }
  out << std::setw(2) << cfg << std::endl;
  return true;
}

void IoDevicesSettingsWidget::onSave()
{
  if (saveToLocalJson()) {
    setStatus(QString("Saved %1 device(s) to %2 — restart the application to apply.")
              .arg(m_devices.size())
              .arg(QString::fromStdString(kLocalConfigPath)));
  }
}

void IoDevicesSettingsWidget::onReloadFromDisk()
{
  loadFromDisk();
}

void IoDevicesSettingsWidget::setStatus(const QString& text, bool isError)
{
  m_statusLabel->setText(text);
  m_statusLabel->setStyleSheet(isError ? "color: #e55;" : "color: #5c5;");
  if (m_context && m_context->logManager) {
    if (isError) {
      m_context->logManager->Error(text.toStdString(), "IoDevicesSettings");
    } else {
      m_context->logManager->Info(text.toStdString(), "IoDevicesSettings");
    }
  }
}
