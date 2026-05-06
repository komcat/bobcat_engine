// IoWatchSettingsWidget.cpp
#include "IoWatchSettingsWidget.h"
#include "context/ApplicationContext.h"
#include "IO/EziIOManager.h"
#include "IO/IEziIODevice.h"
#include "IO/IoWatchManager.h"
#include "logger/LogManager.h"

#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QSpinBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>

#include <nlohmann/json.hpp>
#include <fstream>
#include <iomanip>

using json = nlohmann::json;

const std::string IoWatchSettingsWidget::kLocalConfigPath = "config/io_watch.local.json";

namespace {
constexpr int kColEnabled      = 0;
constexpr int kColName         = 1;
constexpr int kColInputDevice  = 2;
constexpr int kColInputPin     = 3;
constexpr int kColOutputDevice = 4;
constexpr int kColOutputPin    = 5;
constexpr int kColTrigger      = 6;
constexpr int kColAction       = 7;
constexpr int kColDebounce     = 8;
constexpr int kColRemove       = 9;
constexpr int kColumnCount     = 10;
}  // namespace

IoWatchSettingsWidget::IoWatchSettingsWidget(ApplicationContext* context, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_enabledCheck(nullptr)
  , m_intervalSpin(nullptr)
  , m_table(nullptr)
  , m_addButton(nullptr)
  , m_saveButton(nullptr)
  , m_reloadButton(nullptr)
  , m_reloadFromDiskButton(nullptr)
  , m_statusLabel(nullptr)
  , m_pathLabel(nullptr)
{
  setupUI();
  loadFromDisk();
}

void IoWatchSettingsWidget::setupUI()
{
  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->setContentsMargins(10, 10, 10, 10);
  layout->setSpacing(8);

  QLabel* title = new QLabel("<h2>IO Watch Bindings</h2>");
  layout->addWidget(title);

  m_pathLabel = new QLabel(QString("Config: %1").arg(QString::fromStdString(kLocalConfigPath)));
  m_pathLabel->setStyleSheet("color: #888; font-style: italic;");
  layout->addWidget(m_pathLabel);

  // Global settings row
  QHBoxLayout* globalRow = new QHBoxLayout();
  m_enabledCheck = new QCheckBox("Enabled");
  globalRow->addWidget(m_enabledCheck);

  globalRow->addSpacing(20);
  globalRow->addWidget(new QLabel("Polling interval (ms):"));
  m_intervalSpin = new QSpinBox();
  m_intervalSpin->setRange(5, 5000);
  m_intervalSpin->setValue(50);
  m_intervalSpin->setSingleStep(5);
  globalRow->addWidget(m_intervalSpin);

  globalRow->addStretch();
  layout->addLayout(globalRow);

  // Bindings table
  m_table = new QTableWidget(0, kColumnCount, this);
  QStringList headers;
  headers << "Enabled" << "Name"
          << "Input Device" << "Input Pin"
          << "Output Device" << "Output Pin"
          << "Trigger" << "Action" << "Debounce (ms)" << "";
  m_table->setHorizontalHeaderLabels(headers);
  m_table->horizontalHeader()->setStretchLastSection(false);
  m_table->horizontalHeader()->setSectionResizeMode(kColName, QHeaderView::Stretch);
  m_table->horizontalHeader()->setSectionResizeMode(kColInputDevice, QHeaderView::ResizeToContents);
  m_table->horizontalHeader()->setSectionResizeMode(kColInputPin, QHeaderView::ResizeToContents);
  m_table->horizontalHeader()->setSectionResizeMode(kColOutputDevice, QHeaderView::ResizeToContents);
  m_table->horizontalHeader()->setSectionResizeMode(kColOutputPin, QHeaderView::ResizeToContents);
  m_table->verticalHeader()->setVisible(false);
  layout->addWidget(m_table);

  // Buttons
  QHBoxLayout* buttonRow = new QHBoxLayout();
  m_addButton = new QPushButton("+ Add Binding");
  m_saveButton = new QPushButton("Save");
  m_reloadFromDiskButton = new QPushButton("Reload from Disk");
  m_reloadButton = new QPushButton("Hot Reload Watcher");
  m_reloadButton->setStyleSheet("font-weight: bold; background-color: #2a7fff; color: white; padding: 6px 12px;");

  buttonRow->addWidget(m_addButton);
  buttonRow->addStretch();
  buttonRow->addWidget(m_reloadFromDiskButton);
  buttonRow->addWidget(m_saveButton);
  buttonRow->addWidget(m_reloadButton);
  layout->addLayout(buttonRow);

  m_statusLabel = new QLabel("");
  m_statusLabel->setWordWrap(true);
  layout->addWidget(m_statusLabel);

  connect(m_addButton, &QPushButton::clicked, this, &IoWatchSettingsWidget::onAddBinding);
  connect(m_saveButton, &QPushButton::clicked, this, &IoWatchSettingsWidget::onSave);
  connect(m_reloadButton, &QPushButton::clicked, this, &IoWatchSettingsWidget::onHotReload);
  connect(m_reloadFromDiskButton, &QPushButton::clicked, this, &IoWatchSettingsWidget::onReloadFromDisk);
}

void IoWatchSettingsWidget::loadFromDisk()
{
  m_table->setRowCount(0);

  std::ifstream file(kLocalConfigPath);
  if (!file.is_open()) {
    m_enabledCheck->setChecked(true);
    m_intervalSpin->setValue(50);
    setStatus(QString("No %1 found yet — edits will create it on Save.")
              .arg(QString::fromStdString(kLocalConfigPath)));
    return;
  }

  try {
    json cfg = json::parse(file);

    m_enabledCheck->setChecked(cfg.value("enabled", true));
    m_intervalSpin->setValue(cfg.value("polling_interval_ms", 50));

    if (cfg.contains("bindings") && cfg["bindings"].is_array()) {
      for (const auto& b : cfg["bindings"]) {
        const QString name    = QString::fromStdString(b.value("name", std::string{}));
        const QString inDev   = b.contains("input")  && b["input"].is_object()
                                  ? QString::fromStdString(b["input"].value("device", std::string{}))
                                  : QString{};
        const QString inPin   = b.contains("input")  && b["input"].is_object()
                                  ? QString::fromStdString(b["input"].value("pin_name", std::string{}))
                                  : QString{};
        const QString outDev  = b.contains("output") && b["output"].is_object()
                                  ? QString::fromStdString(b["output"].value("device", std::string{}))
                                  : QString{};
        const QString outPin  = b.contains("output") && b["output"].is_object()
                                  ? QString::fromStdString(b["output"].value("pin_name", std::string{}))
                                  : QString{};
        const QString trigger = QString::fromStdString(b.value("trigger", std::string{"rising"}));
        const QString action  = QString::fromStdString(b.value("action",  std::string{"toggle"}));
        const int debounce    = b.value("debounce_ms", 100);
        const bool enabled    = b.value("enabled", true);

        const int row = m_table->rowCount();
        m_table->insertRow(row);
        populateRow(row, enabled, name, inDev, inPin, outDev, outPin, trigger, action, debounce);
      }
    }

    setStatus(QString("Loaded %1 binding(s) from %2")
              .arg(m_table->rowCount())
              .arg(QString::fromStdString(kLocalConfigPath)));
  } catch (const json::exception& e) {
    setStatus(QString("Parse error in %1: %2")
              .arg(QString::fromStdString(kLocalConfigPath))
              .arg(e.what()), true);
  }
}

void IoWatchSettingsWidget::populateRow(int row,
                                        bool enabled,
                                        const QString& name,
                                        const QString& inputDevice,
                                        const QString& inputPin,
                                        const QString& outputDevice,
                                        const QString& outputPin,
                                        const QString& trigger,
                                        const QString& action,
                                        int debounceMs)
{
  // Enabled checkbox (centered via wrapper)
  {
    QWidget* wrap = new QWidget();
    QHBoxLayout* lay = new QHBoxLayout(wrap);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setAlignment(Qt::AlignCenter);
    QCheckBox* cb = new QCheckBox();
    cb->setChecked(enabled);
    lay->addWidget(cb);
    m_table->setCellWidget(row, kColEnabled, wrap);
  }

  // Name
  m_table->setItem(row, kColName, new QTableWidgetItem(name));

  // Device names from EziIOManager
  QStringList deviceNames;
  if (m_context && m_context->ioManager) {
    for (const auto& n : m_context->ioManager->GetAllDeviceNames()) {
      deviceNames << QString::fromStdString(n);
    }
  }

  // Input Device combo
  QComboBox* inDevCombo = new QComboBox();
  inDevCombo->setEditable(true);
  inDevCombo->addItems(deviceNames);
  if (!inputDevice.isEmpty() && inDevCombo->findText(inputDevice) < 0) {
    inDevCombo->addItem(inputDevice);
  }
  inDevCombo->setCurrentText(inputDevice);
  m_table->setCellWidget(row, kColInputDevice, inDevCombo);

  // Input Pin combo
  QComboBox* inPinCombo = new QComboBox();
  inPinCombo->setEditable(true);
  m_table->setCellWidget(row, kColInputPin, inPinCombo);
  repopulatePinCombo(row, kColInputPin, true);
  if (!inputPin.isEmpty() && inPinCombo->findText(inputPin) < 0) {
    inPinCombo->addItem(inputPin);
  }
  inPinCombo->setCurrentText(inputPin);

  connect(inDevCombo, &QComboBox::currentTextChanged,
          this, [this, row]() { repopulatePinCombo(row, kColInputPin, true); });

  // Output Device combo
  QComboBox* outDevCombo = new QComboBox();
  outDevCombo->setEditable(true);
  outDevCombo->addItems(deviceNames);
  if (!outputDevice.isEmpty() && outDevCombo->findText(outputDevice) < 0) {
    outDevCombo->addItem(outputDevice);
  }
  outDevCombo->setCurrentText(outputDevice);
  m_table->setCellWidget(row, kColOutputDevice, outDevCombo);

  // Output Pin combo
  QComboBox* outPinCombo = new QComboBox();
  outPinCombo->setEditable(true);
  m_table->setCellWidget(row, kColOutputPin, outPinCombo);
  repopulatePinCombo(row, kColOutputPin, false);
  if (!outputPin.isEmpty() && outPinCombo->findText(outputPin) < 0) {
    outPinCombo->addItem(outputPin);
  }
  outPinCombo->setCurrentText(outputPin);

  connect(outDevCombo, &QComboBox::currentTextChanged,
          this, [this, row]() { repopulatePinCombo(row, kColOutputPin, false); });

  // Trigger combo
  QComboBox* trigCombo = new QComboBox();
  trigCombo->addItems({"rising", "falling", "both"});
  const int idx = trigCombo->findText(trigger.toLower());
  trigCombo->setCurrentIndex(idx >= 0 ? idx : 0);
  m_table->setCellWidget(row, kColTrigger, trigCombo);

  // Action combo
  QComboBox* actionCombo = new QComboBox();
  actionCombo->addItems({"toggle", "follow"});
  actionCombo->setToolTip(
    "toggle: flip output on the configured edge.\n"
    "follow: output mirrors input level (rising=ON, falling=OFF). 'Trigger' is ignored.");
  const int actIdx = actionCombo->findText(action.toLower());
  actionCombo->setCurrentIndex(actIdx >= 0 ? actIdx : 0);
  m_table->setCellWidget(row, kColAction, actionCombo);

  // Debounce spin
  QSpinBox* debSpin = new QSpinBox();
  debSpin->setRange(0, 60000);
  debSpin->setValue(debounceMs);
  debSpin->setSuffix(" ms");
  m_table->setCellWidget(row, kColDebounce, debSpin);

  // Remove button
  QPushButton* rm = new QPushButton("✕");
  rm->setFixedWidth(30);
  rm->setStyleSheet("QPushButton { color: #c33; font-weight: bold; }");
  m_table->setCellWidget(row, kColRemove, rm);
  connect(rm, &QPushButton::clicked, this, [this, rm]() {
    for (int r = 0; r < m_table->rowCount(); ++r) {
      if (m_table->cellWidget(r, kColRemove) == rm) {
        onRemoveBindingAtRow(r);
        return;
      }
    }
  });
}

void IoWatchSettingsWidget::repopulatePinCombo(int row, int pinColumn, bool isInput)
{
  const int deviceColumn = isInput ? kColInputDevice : kColOutputDevice;
  auto* devCombo = qobject_cast<QComboBox*>(m_table->cellWidget(row, deviceColumn));
  auto* pinCombo = qobject_cast<QComboBox*>(m_table->cellWidget(row, pinColumn));
  if (!devCombo || !pinCombo) return;

  const QString currentPin = pinCombo->currentText();
  const QString deviceName = devCombo->currentText();

  pinCombo->blockSignals(true);
  pinCombo->clear();

  if (m_context && m_context->ioManager && !deviceName.isEmpty()) {
    IEziIODevice* dev = m_context->ioManager->GetDevice(deviceName.toStdString());
    if (dev) {
      const auto names = isInput ? dev->GetAllInputPinNames() : dev->GetAllOutputPinNames();
      for (const auto& n : names) {
        pinCombo->addItem(QString::fromStdString(n));
      }
    }
  }

  if (!currentPin.isEmpty()) {
    if (pinCombo->findText(currentPin) < 0) {
      pinCombo->addItem(currentPin);
    }
    pinCombo->setCurrentText(currentPin);
  }
  pinCombo->blockSignals(false);
}

void IoWatchSettingsWidget::onAddBinding()
{
  const int row = m_table->rowCount();
  m_table->insertRow(row);
  populateRow(row, true, "New Binding", "", "", "", "", "rising", "toggle", 100);
  m_table->scrollToBottom();
}

void IoWatchSettingsWidget::onRemoveBindingAtRow(int row)
{
  if (row < 0 || row >= m_table->rowCount()) return;
  m_table->removeRow(row);
}

bool IoWatchSettingsWidget::saveToLocalJson()
{
  json cfg;
  cfg["enabled"] = m_enabledCheck->isChecked();
  cfg["polling_interval_ms"] = m_intervalSpin->value();
  cfg["bindings"] = json::array();

  for (int r = 0; r < m_table->rowCount(); ++r) {
    QCheckBox* enCb = nullptr;
    QWidget* wrap = m_table->cellWidget(r, kColEnabled);
    if (wrap) {
      enCb = wrap->findChild<QCheckBox*>();
    }

    auto* inDev  = qobject_cast<QComboBox*>(m_table->cellWidget(r, kColInputDevice));
    auto* inPin  = qobject_cast<QComboBox*>(m_table->cellWidget(r, kColInputPin));
    auto* outDev = qobject_cast<QComboBox*>(m_table->cellWidget(r, kColOutputDevice));
    auto* outPin = qobject_cast<QComboBox*>(m_table->cellWidget(r, kColOutputPin));
    auto* trig   = qobject_cast<QComboBox*>(m_table->cellWidget(r, kColTrigger));
    auto* act    = qobject_cast<QComboBox*>(m_table->cellWidget(r, kColAction));
    auto* deb    = qobject_cast<QSpinBox*>(m_table->cellWidget(r, kColDebounce));
    QTableWidgetItem* nameItem = m_table->item(r, kColName);

    json b;
    b["name"] = nameItem ? nameItem->text().toStdString() : std::string{};
    b["input"] = {
      {"device",   inDev  ? inDev->currentText().toStdString()  : std::string{}},
      {"pin_name", inPin  ? inPin->currentText().toStdString()  : std::string{}}
    };
    b["output"] = {
      {"device",   outDev ? outDev->currentText().toStdString() : std::string{}},
      {"pin_name", outPin ? outPin->currentText().toStdString() : std::string{}}
    };
    b["trigger"]     = trig ? trig->currentText().toStdString() : std::string{"rising"};
    b["action"]      = act  ? act->currentText().toStdString()  : std::string{"toggle"};
    b["debounce_ms"] = deb ? deb->value() : 100;
    b["enabled"]     = enCb ? enCb->isChecked() : true;

    cfg["bindings"].push_back(b);
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

void IoWatchSettingsWidget::onSave()
{
  if (saveToLocalJson()) {
    setStatus(QString("Saved %1 — click 'Hot Reload Watcher' to apply.")
              .arg(QString::fromStdString(kLocalConfigPath)));
  }
}

void IoWatchSettingsWidget::onHotReload()
{
  if (!m_context || !m_context->ioWatchManager) {
    setStatus("IoWatchManager not available in ApplicationContext.", true);
    return;
  }

  if (!saveToLocalJson()) return;

  const bool ok = m_context->ioWatchManager->Reload(kLocalConfigPath);
  if (ok) {
    setStatus(QString("Reloaded %1 — watcher restarted.")
              .arg(QString::fromStdString(kLocalConfigPath)));
  } else {
    setStatus("Hot reload failed — see log for details.", true);
  }
}

void IoWatchSettingsWidget::onReloadFromDisk()
{
  loadFromDisk();
}

void IoWatchSettingsWidget::setStatus(const QString& text, bool isError)
{
  m_statusLabel->setText(text);
  m_statusLabel->setStyleSheet(isError ? "color: #e55;" : "color: #5c5;");
  if (m_context && m_context->logManager) {
    if (isError) {
      m_context->logManager->Error(text.toStdString(), "IoWatchSettings");
    } else {
      m_context->logManager->Info(text.toStdString(), "IoWatchSettings");
    }
  }
}
