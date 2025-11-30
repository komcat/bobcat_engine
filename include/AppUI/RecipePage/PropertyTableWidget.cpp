// PropertyTableWidget.cpp
#include "PropertyTableWidget.h"
#include "DeviceSelectorWidgets.h"
#include "ApplicationContext.h"
#include "EziIOManager.h"
#include "IEziIODevice.h"
#include <QLabel>
#include <iostream>

PropertyTableWidget::PropertyTableWidget(ApplicationContext* context, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
{
  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  m_table = new QTableWidget(this);
  m_table->setColumnCount(2);
  m_table->setHorizontalHeaderLabels({"Property", "Value"});

  // Configure table appearance
  applyTableStyle();

  layout->addWidget(m_table);
}

void PropertyTableWidget::applyTableStyle()
{
  m_table->setStyleSheet(
    "QTableWidget { "
    "  background-color: #1e1e1e; "
    "  border: 1px solid #3f3f46; "
    "  gridline-color: #3f3f46; "
    "  color: #d4d4d4; "
    "  font-size: 12px; "
    "} "
    "QTableWidget::item { "
    "  padding: 0px; "
    "  border-bottom: 1px solid #3f3f46; "
    "} "
    "QTableWidget::item:selected { "
    "  background-color: #264f78; "
    "} "
    "QHeaderView::section { "
    "  background-color: #2d2d30; "
    "  color: #d4d4d4; "
    "  padding: 6px 8px; "
    "  border: none; "
    "  border-bottom: 2px solid #007acc; "
    "  font-weight: bold; "
    "  font-size: 12px; "
    "} "
    "QComboBox { "
    "  background-color: #3c3c3c; "
    "  color: #d4d4d4; "
    "  border: 1px solid #555555; "
    "  border-radius: 3px; "
    "  padding: 4px 8px; "
    "  min-height: 24px; "
    "} "
    "QComboBox:hover { "
    "  border-color: #007acc; "
    "} "
    "QComboBox::drop-down { "
    "  border: none; "
    "  width: 20px; "
    "} "
    "QComboBox QAbstractItemView { "
    "  background-color: #2d2d30; "
    "  color: #d4d4d4; "
    "  selection-background-color: #007acc; "
    "} "
    "QLineEdit { "
    "  background-color: #3c3c3c; "
    "  color: #d4d4d4; "
    "  border: 1px solid #555555; "
    "  border-radius: 3px; "
    "  padding: 4px 8px; "
    "  min-height: 24px; "
    "} "
    "QLineEdit:focus { "
    "  border-color: #007acc; "
    "} "
    "QDoubleSpinBox, QSpinBox { "
    "  background-color: #3c3c3c; "
    "  color: #d4d4d4; "
    "  border: 1px solid #555555; "
    "  border-radius: 3px; "
    "  padding: 4px 8px; "
    "  min-height: 24px; "
    "} "
    "QDoubleSpinBox:focus, QSpinBox:focus { "
    "  border-color: #007acc; "
    "} "
    "QCheckBox { "
    "  color: #d4d4d4; "
    "  spacing: 8px; "
    "} "
    "QCheckBox::indicator { "
    "  width: 16px; "
    "  height: 16px; "
    "  border: 1px solid #555555; "
    "  border-radius: 2px; "
    "  background-color: #3c3c3c; "
    "} "
    "QCheckBox::indicator:checked { "
    "  background-color: #007acc; "
    "  border-color: #007acc; "
    "} "
  );

  // Configure columns
  m_table->horizontalHeader()->setStretchLastSection(true);
  m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  m_table->horizontalHeader()->setMinimumSectionSize(140);

  m_table->verticalHeader()->setVisible(false);
  m_table->verticalHeader()->setDefaultSectionSize(36);  // Taller rows for better widget fit

  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setSelectionMode(QAbstractItemView::SingleSelection);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);  // We use custom widgets

  m_table->setShowGrid(true);
  m_table->setAlternatingRowColors(false);
}

QWidget* PropertyTableWidget::createPropertyLabel(const std::string& label, const std::string& tooltip)
{
  QLabel* labelWidget = new QLabel(QString::fromStdString(label));
  labelWidget->setStyleSheet(
    "QLabel { "
    "  color: #d4d4d4; "
    "  background-color: transparent; "
    "  padding: 4px 8px; "
    "  font-weight: normal; "
    "}"
  );
  if (!tooltip.empty()) {
    labelWidget->setToolTip(QString::fromStdString(tooltip));
  }
  return labelWidget;
}

int PropertyTableWidget::addPropertyRow(const std::string& key, const std::string& label,
                                        QWidget* valueWidget, const std::string& tooltip)
{
  int row = m_table->rowCount();
  m_table->insertRow(row);

  // Property name column
  QWidget* labelWidget = createPropertyLabel(label, tooltip);
  m_table->setCellWidget(row, 0, labelWidget);

  // Value column - wrap in container with margins for proper spacing
  QWidget* valueContainer = new QWidget();
  QHBoxLayout* valueLayout = new QHBoxLayout(valueContainer);
  valueLayout->setContentsMargins(6, 4, 6, 4);  // Left, Top, Right, Bottom margins
  valueLayout->setSpacing(0);
  valueLayout->addWidget(valueWidget);
  m_table->setCellWidget(row, 1, valueContainer);

  // Store references
  m_valueWidgets[key] = valueWidget;
  m_keyToRow[key] = row;

  return row;
}

void PropertyTableWidget::addTextProperty(const std::string& key, const std::string& label,
                                          const std::string& value, const std::string& tooltip)
{
  QLineEdit* edit = new QLineEdit();
  edit->setText(QString::fromStdString(value));
  if (!tooltip.empty()) {
    edit->setToolTip(QString::fromStdString(tooltip));
  }

  connect(edit, &QLineEdit::textChanged, this, [this, key](const QString& text) {
    emit propertyChanged(QString::fromStdString(key), text);
    emit propertyChanged();
  });

  addPropertyRow(key, label, edit, tooltip);
}

void PropertyTableWidget::addDropdownProperty(const std::string& key, const std::string& label,
                                              const std::string& value,
                                              const std::vector<std::string>& options,
                                              const std::string& tooltip)
{
  QComboBox* combo = new QComboBox();
  for (const auto& opt : options) {
    combo->addItem(QString::fromStdString(opt));
  }

  // Set current value
  int index = combo->findText(QString::fromStdString(value));
  if (index >= 0) {
    combo->setCurrentIndex(index);
  }

  if (!tooltip.empty()) {
    combo->setToolTip(QString::fromStdString(tooltip));
  }

  connect(combo, &QComboBox::currentTextChanged, this, [this, key](const QString& text) {
    emit propertyChanged(QString::fromStdString(key), text);
    emit propertyChanged();
  });

  addPropertyRow(key, label, combo, tooltip);
}

void PropertyTableWidget::addCheckboxProperty(const std::string& key, const std::string& label,
                                              bool value, const std::string& tooltip)
{
  // Create a container widget with proper margins (matching other widgets)
  QWidget* container = new QWidget();
  QHBoxLayout* layout = new QHBoxLayout(container);
  layout->setContentsMargins(6, 4, 6, 4);  // Match other widget margins
  layout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

  QCheckBox* checkbox = new QCheckBox(value ? "Yes" : "No");
  checkbox->setChecked(value);
  if (!tooltip.empty()) {
    checkbox->setToolTip(QString::fromStdString(tooltip));
  }

  connect(checkbox, &QCheckBox::toggled, this, [this, key, checkbox](bool checked) {
    checkbox->setText(checked ? "Yes" : "No");
    emit propertyChanged(QString::fromStdString(key), checked ? "true" : "false");
    emit propertyChanged();
  });

  layout->addWidget(checkbox);
  m_valueWidgets[key] = checkbox;  // Store the checkbox, not container

  int row = m_table->rowCount();
  m_table->insertRow(row);
  m_table->setCellWidget(row, 0, createPropertyLabel(label, tooltip));
  m_table->setCellWidget(row, 1, container);
  m_keyToRow[key] = row;
}

void PropertyTableWidget::addNumericProperty(const std::string& key, const std::string& label,
                                             double value, double minValue, double maxValue,
                                             double step, int decimals,
                                             const std::string& suffix, const std::string& tooltip)
{
  QDoubleSpinBox* spinBox = new QDoubleSpinBox();
  spinBox->setRange(minValue, maxValue);
  spinBox->setSingleStep(step);
  spinBox->setDecimals(decimals);
  spinBox->setValue(value);
  if (!suffix.empty()) {
    spinBox->setSuffix(QString::fromStdString(" " + suffix));
  }
  if (!tooltip.empty()) {
    spinBox->setToolTip(QString::fromStdString(tooltip));
  }

  connect(spinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
    this, [this, key](double val) {
      emit propertyChanged(QString::fromStdString(key), QString::number(val));
      emit propertyChanged();
    });

  addPropertyRow(key, label, spinBox, tooltip);
}

void PropertyTableWidget::addDeviceProperty(const std::string& key, const std::string& label,
                                            const std::string& value, int deviceType,
                                            const std::string& tooltip)
{
  DeviceComboBox* combo = new DeviceComboBox(
    m_context,
    static_cast<DeviceComboBox::DeviceType>(deviceType)
  );
  combo->setSelectedDevice(value);
  if (!tooltip.empty()) {
    combo->setToolTip(QString::fromStdString(tooltip));
  }

  connect(combo, &QComboBox::currentTextChanged, this, [this, key](const QString& text) {
    emit propertyChanged(QString::fromStdString(key), text);
    emit propertyChanged();
  });

  addPropertyRow(key, label, combo, tooltip);
}

void PropertyTableWidget::addIOPinProperty(const std::string& key, const std::string& label,
                                           const std::string& value, const std::string& ioDeviceKey,
                                           const std::string& pinType, const std::string& tooltip)
{
  QComboBox* combo = new QComboBox();
  combo->setEditable(true);
  if (!tooltip.empty()) {
    combo->setToolTip(QString::fromStdString(tooltip));
  }

  // Lambda to populate pin names
  auto populatePinNames = [this, combo, ioDeviceKey, pinType, value]() {
    QString currentText = combo->currentText();
    combo->clear();

    // Get IO device name from parameters
    std::string deviceName = getParameter(ioDeviceKey);
    if (deviceName.empty()) {
      combo->addItem("(Select IO device first)");
      return;
    }

    if (!m_context || !m_context->ioManager) {
      combo->addItem("(IO Manager unavailable)");
      return;
    }

    auto* ioDevice = m_context->ioManager->GetDevice(deviceName);
    if (!ioDevice) {
      combo->addItem("(Device not found)");
      return;
    }

    // Get pin names
    std::vector<std::string> pinNames;
    if (pinType == "input") {
      pinNames = ioDevice->GetAllInputPinNames();
    } else {
      pinNames = ioDevice->GetAllOutputPinNames();
    }

    if (pinNames.empty()) {
      combo->addItem("(No pins configured)");
      return;
    }

    for (const auto& pinName : pinNames) {
      combo->addItem(QString::fromStdString(pinName));
    }

    // Restore selection
    if (!currentText.isEmpty()) {
      int idx = combo->findText(currentText);
      if (idx >= 0) {
        combo->setCurrentIndex(idx);
      } else {
        combo->setEditText(currentText);
      }
    } else if (!value.empty()) {
      int idx = combo->findText(QString::fromStdString(value));
      if (idx >= 0) {
        combo->setCurrentIndex(idx);
      } else {
        combo->setEditText(QString::fromStdString(value));
      }
    }
  };

  // Initial population
  populatePinNames();

  // Watch for IO device changes
  if (m_valueWidgets.find(ioDeviceKey) != m_valueWidgets.end()) {
    QComboBox* deviceCombo = qobject_cast<QComboBox*>(m_valueWidgets[ioDeviceKey]);
    if (deviceCombo) {
      connect(deviceCombo, &QComboBox::currentTextChanged, this, [populatePinNames]() {
        populatePinNames();
      });
    }
  }

  connect(combo, &QComboBox::currentTextChanged, this, [this, key](const QString& text) {
    emit propertyChanged(QString::fromStdString(key), text);
    emit propertyChanged();
  });

  addPropertyRow(key, label, combo, tooltip);
}

void PropertyTableWidget::addCategoryHeader(const std::string& categoryName)
{
  int row = m_table->rowCount();
  m_table->insertRow(row);

  // Create header label that spans both columns
  QLabel* headerLabel = new QLabel(QString::fromStdString(categoryName));
  headerLabel->setStyleSheet(
    "QLabel { "
    "  background-color: #2d2d30; "
    "  color: #dcdcaa; "
    "  font-weight: bold; "
    "  font-size: 11px; "
    "  padding: 6px 8px; "
    "  border-bottom: 1px solid #007acc; "
    "}"
  );

  m_table->setCellWidget(row, 0, headerLabel);
  m_table->setSpan(row, 0, 1, 2);  // Span both columns
}

void PropertyTableWidget::addSeparator()
{
  int row = m_table->rowCount();
  m_table->insertRow(row);

  QWidget* separator = new QWidget();
  separator->setFixedHeight(1);
  separator->setStyleSheet("background-color: #3f3f46;");

  m_table->setCellWidget(row, 0, separator);
  m_table->setSpan(row, 0, 1, 2);
  m_table->setRowHeight(row, 1);
}

std::map<std::string, std::string> PropertyTableWidget::getParameters() const
{
  std::map<std::string, std::string> params;
  for (const auto& [key, widget] : m_valueWidgets) {
    params[key] = getWidgetValue(key);
  }
  return params;
}

std::string PropertyTableWidget::getParameter(const std::string& key) const
{
  return getWidgetValue(key);
}

std::string PropertyTableWidget::getWidgetValue(const std::string& key) const
{
  auto it = m_valueWidgets.find(key);
  if (it == m_valueWidgets.end()) {
    return "";
  }

  QWidget* widget = it->second;

  // Try different widget types
  if (auto* lineEdit = qobject_cast<QLineEdit*>(widget)) {
    return lineEdit->text().toStdString();
  }
  if (auto* comboBox = qobject_cast<QComboBox*>(widget)) {
    return comboBox->currentText().toStdString();
  }
  if (auto* checkBox = qobject_cast<QCheckBox*>(widget)) {
    return checkBox->isChecked() ? "true" : "false";
  }
  if (auto* spinBox = qobject_cast<QDoubleSpinBox*>(widget)) {
    return std::to_string(spinBox->value());
  }

  return "";
}

void PropertyTableWidget::clear()
{
  m_table->setRowCount(0);
  m_valueWidgets.clear();
  m_keyToRow.clear();
}
