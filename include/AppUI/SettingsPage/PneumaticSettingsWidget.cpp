// PneumaticSettingsWidget.cpp
#include "PneumaticSettingsWidget.h"
#include "context/ApplicationContext.h"
#include "IO/EziIOManager.h"
#include "IO/IEziIODevice.h"
#include "logger/LogManager.h"

#include <QComboBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QSpinBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QDir>

#include <nlohmann/json.hpp>
#include <fstream>
#include <iomanip>

using json = nlohmann::json;

const std::string PneumaticSettingsWidget::kLocalConfigPath = "config/pneumatic_slides.local.json";

namespace {
constexpr int kColName      = 0;
constexpr int kColOutDevice = 1;
constexpr int kColOutPin    = 2;
constexpr int kColExtDevice = 3;
constexpr int kColExtPin    = 4;
constexpr int kColRetDevice = 5;
constexpr int kColRetPin    = 6;
constexpr int kColTimeout   = 7;
constexpr int kColRemove    = 8;
constexpr int kColumnCount  = 9;
}  // namespace

PneumaticSettingsWidget::PneumaticSettingsWidget(ApplicationContext* context, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_table(nullptr)
  , m_addButton(nullptr)
  , m_saveButton(nullptr)
  , m_reloadButton(nullptr)
  , m_statusLabel(nullptr)
{
  setupUI();
  loadFromDisk();
}

void PneumaticSettingsWidget::setupUI()
{
  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->setContentsMargins(10, 10, 10, 10);
  layout->setSpacing(8);

  layout->addWidget(new QLabel("<h2>Pneumatic Slides</h2>"));

  QLabel* pathLabel = new QLabel(QString("Config: %1").arg(QString::fromStdString(kLocalConfigPath)));
  pathLabel->setStyleSheet("color: #888; font-style: italic;");
  layout->addWidget(pathLabel);

  QLabel* warning = new QLabel("⚠ Changes require an application restart to take effect.");
  warning->setStyleSheet("color: #d80; font-weight: bold;");
  layout->addWidget(warning);

  m_table = new QTableWidget(0, kColumnCount, this);
  m_table->setHorizontalHeaderLabels(
    QStringList() << "Name"
                  << "Output Device" << "Output Pin"
                  << "Extended Input Device" << "Extended Input Pin"
                  << "Retracted Input Device" << "Retracted Input Pin"
                  << "Timeout (ms)" << "");
  m_table->horizontalHeader()->setStretchLastSection(false);
  m_table->horizontalHeader()->setSectionResizeMode(kColName, QHeaderView::Stretch);
  for (int c = kColOutDevice; c <= kColRetPin; ++c) {
    m_table->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
  }
  m_table->verticalHeader()->setVisible(false);
  layout->addWidget(m_table);

  QHBoxLayout* buttons = new QHBoxLayout();
  m_addButton = new QPushButton("+ Add Slide");
  m_saveButton = new QPushButton("Save");
  m_reloadButton = new QPushButton("Reload from Disk");
  buttons->addWidget(m_addButton);
  buttons->addStretch();
  buttons->addWidget(m_reloadButton);
  buttons->addWidget(m_saveButton);
  layout->addLayout(buttons);

  m_statusLabel = new QLabel("");
  m_statusLabel->setWordWrap(true);
  layout->addWidget(m_statusLabel);

  connect(m_addButton, &QPushButton::clicked, this, &PneumaticSettingsWidget::onAddSlide);
  connect(m_saveButton, &QPushButton::clicked, this, &PneumaticSettingsWidget::onSave);
  connect(m_reloadButton, &QPushButton::clicked, this, &PneumaticSettingsWidget::onReloadFromDisk);
}

void PneumaticSettingsWidget::loadFromDisk()
{
  m_table->setRowCount(0);

  std::ifstream file(kLocalConfigPath);
  if (!file.is_open()) {
    setStatus(QString("No %1 yet — edits will create it on Save.")
              .arg(QString::fromStdString(kLocalConfigPath)));
    return;
  }

  try {
    json cfg = json::parse(file);
    if (!cfg.contains("pneumaticSlides") || !cfg["pneumaticSlides"].is_array()) {
      setStatus("Config missing 'pneumaticSlides' array.", true);
      return;
    }

    for (const auto& s : cfg["pneumaticSlides"]) {
      const QString name    = QString::fromStdString(s.value("name", std::string{}));
      const QString outDev  = s.contains("output") && s["output"].is_object()
                                ? QString::fromStdString(s["output"].value("deviceName", std::string{}))
                                : QString{};
      const QString outPin  = s.contains("output") && s["output"].is_object()
                                ? QString::fromStdString(s["output"].value("pinName", std::string{}))
                                : QString{};
      const QString extDev  = s.contains("extendedInput") && s["extendedInput"].is_object()
                                ? QString::fromStdString(s["extendedInput"].value("deviceName", std::string{}))
                                : QString{};
      const QString extPin  = s.contains("extendedInput") && s["extendedInput"].is_object()
                                ? QString::fromStdString(s["extendedInput"].value("pinName", std::string{}))
                                : QString{};
      const QString retDev  = s.contains("retractedInput") && s["retractedInput"].is_object()
                                ? QString::fromStdString(s["retractedInput"].value("deviceName", std::string{}))
                                : QString{};
      const QString retPin  = s.contains("retractedInput") && s["retractedInput"].is_object()
                                ? QString::fromStdString(s["retractedInput"].value("pinName", std::string{}))
                                : QString{};
      const int timeout     = s.value("timeoutMs", 5000);

      const int row = m_table->rowCount();
      m_table->insertRow(row);
      populateRow(row, name, outDev, outPin, extDev, extPin, retDev, retPin, timeout);
    }

    setStatus(QString("Loaded %1 slide(s).").arg(m_table->rowCount()));
  } catch (const json::exception& e) {
    setStatus(QString("Parse error in %1: %2")
              .arg(QString::fromStdString(kLocalConfigPath))
              .arg(e.what()), true);
  }
}

void PneumaticSettingsWidget::populateRow(int row,
                                          const QString& name,
                                          const QString& outDevice, const QString& outPin,
                                          const QString& extDevice, const QString& extPin,
                                          const QString& retDevice, const QString& retPin,
                                          int timeoutMs)
{
  m_table->setItem(row, kColName, new QTableWidgetItem(name));

  QStringList deviceNames;
  if (m_context && m_context->ioManager) {
    for (const auto& n : m_context->ioManager->GetAllDeviceNames()) {
      deviceNames << QString::fromStdString(n);
    }
  }

  struct Group {
    int devCol;
    int pinCol;
    QString device;
    QString pin;
    bool isInput;
  };
  const Group groups[3] = {
    { kColOutDevice, kColOutPin, outDevice, outPin, false },
    { kColExtDevice, kColExtPin, extDevice, extPin, true  },
    { kColRetDevice, kColRetPin, retDevice, retPin, true  },
  };

  for (const auto& g : groups) {
    QComboBox* devCombo = new QComboBox();
    devCombo->setEditable(true);
    devCombo->addItems(deviceNames);
    if (!g.device.isEmpty() && devCombo->findText(g.device) < 0) {
      devCombo->addItem(g.device);
    }
    devCombo->setCurrentText(g.device);
    m_table->setCellWidget(row, g.devCol, devCombo);

    QComboBox* pinCombo = new QComboBox();
    pinCombo->setEditable(true);
    m_table->setCellWidget(row, g.pinCol, pinCombo);

    repopulatePinCombo(row, g.pinCol, g.isInput);
    if (!g.pin.isEmpty() && pinCombo->findText(g.pin) < 0) {
      pinCombo->addItem(g.pin);
    }
    pinCombo->setCurrentText(g.pin);

    const int pinCol = g.pinCol;
    const bool isIn  = g.isInput;
    connect(devCombo, &QComboBox::currentTextChanged,
            this, [this, row, pinCol, isIn]() { repopulatePinCombo(row, pinCol, isIn); });
  }

  QSpinBox* timeSpin = new QSpinBox();
  timeSpin->setRange(0, 600000);
  timeSpin->setSingleStep(500);
  timeSpin->setValue(timeoutMs);
  timeSpin->setSuffix(" ms");
  m_table->setCellWidget(row, kColTimeout, timeSpin);

  QPushButton* rm = new QPushButton("✕");
  rm->setFixedWidth(30);
  rm->setStyleSheet("QPushButton { color: #c33; font-weight: bold; }");
  m_table->setCellWidget(row, kColRemove, rm);
  connect(rm, &QPushButton::clicked, this, [this, rm]() {
    for (int r = 0; r < m_table->rowCount(); ++r) {
      if (m_table->cellWidget(r, kColRemove) == rm) {
        onRemoveSlideAtRow(r);
        return;
      }
    }
  });
}

void PneumaticSettingsWidget::repopulatePinCombo(int row, int pinColumn, bool isInput)
{
  int deviceColumn = kColOutDevice;
  switch (pinColumn) {
    case kColOutPin: deviceColumn = kColOutDevice; break;
    case kColExtPin: deviceColumn = kColExtDevice; break;
    case kColRetPin: deviceColumn = kColRetDevice; break;
    default: return;
  }

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

void PneumaticSettingsWidget::onAddSlide()
{
  const int row = m_table->rowCount();
  m_table->insertRow(row);
  populateRow(row, "NewSlide", "", "", "", "", "", "", 5000);
  m_table->scrollToBottom();
}

void PneumaticSettingsWidget::onRemoveSlideAtRow(int row)
{
  if (row < 0 || row >= m_table->rowCount()) return;
  m_table->removeRow(row);
}

bool PneumaticSettingsWidget::saveToLocalJson()
{
  json cfg;
  cfg["pneumaticSlides"] = json::array();

  for (int r = 0; r < m_table->rowCount(); ++r) {
    QTableWidgetItem* nameItem = m_table->item(r, kColName);
    auto* outDev = qobject_cast<QComboBox*>(m_table->cellWidget(r, kColOutDevice));
    auto* outPin = qobject_cast<QComboBox*>(m_table->cellWidget(r, kColOutPin));
    auto* extDev = qobject_cast<QComboBox*>(m_table->cellWidget(r, kColExtDevice));
    auto* extPin = qobject_cast<QComboBox*>(m_table->cellWidget(r, kColExtPin));
    auto* retDev = qobject_cast<QComboBox*>(m_table->cellWidget(r, kColRetDevice));
    auto* retPin = qobject_cast<QComboBox*>(m_table->cellWidget(r, kColRetPin));
    auto* timeSpin = qobject_cast<QSpinBox*>(m_table->cellWidget(r, kColTimeout));

    json s;
    s["name"] = nameItem ? nameItem->text().toStdString() : std::string{};
    s["output"] = {
      {"deviceName", outDev ? outDev->currentText().toStdString() : std::string{}},
      {"pinName",    outPin ? outPin->currentText().toStdString() : std::string{}}
    };
    s["extendedInput"] = {
      {"deviceName", extDev ? extDev->currentText().toStdString() : std::string{}},
      {"pinName",    extPin ? extPin->currentText().toStdString() : std::string{}}
    };
    s["retractedInput"] = {
      {"deviceName", retDev ? retDev->currentText().toStdString() : std::string{}},
      {"pinName",    retPin ? retPin->currentText().toStdString() : std::string{}}
    };
    s["timeoutMs"] = timeSpin ? timeSpin->value() : 5000;

    cfg["pneumaticSlides"].push_back(s);
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

void PneumaticSettingsWidget::onSave()
{
  if (saveToLocalJson()) {
    setStatus(QString("Saved %1 slide(s) to %2 — restart the application to apply.")
              .arg(m_table->rowCount())
              .arg(QString::fromStdString(kLocalConfigPath)));
  }
}

void PneumaticSettingsWidget::onReloadFromDisk()
{
  loadFromDisk();
}

void PneumaticSettingsWidget::setStatus(const QString& text, bool isError)
{
  m_statusLabel->setText(text);
  m_statusLabel->setStyleSheet(isError ? "color: #e55;" : "color: #5c5;");
  if (m_context && m_context->logManager) {
    if (isError) {
      m_context->logManager->Error(text.toStdString(), "PneumaticSettings");
    } else {
      m_context->logManager->Info(text.toStdString(), "PneumaticSettings");
    }
  }
}
