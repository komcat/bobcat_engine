// PropertyTableWidget.cpp
#include "PropertyTableWidget.h"
#include "DeviceSelectorWidgets.h"  // Contains NodeSelectorDialog
#include "ApplicationContext.h"
#include "EziIOManager.h"
#include "IEziIODevice.h"
#include "PneumaticSlideSelectorDialog.h"
#include "MotionDeviceSelectorDialog.h"
#include "products/product_position_manager.h"
#include "DataStore.h"
#include <QLabel>
#include <QPushButton>
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

  // Disable table's own scrollbars - use parent scroll area instead
  m_table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  // Configure table appearance
  applyTableStyle();

  layout->addWidget(m_table);

  // Set size policy to expand based on content
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
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

  // Update table height to fit content
  updateTableHeight();

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

  // Update table height to fit content
  updateTableHeight();
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

void PropertyTableWidget::addMotionDeviceProperty(const std::string& key, const std::string& label,
                                                  const std::string& value, const std::string& tooltip)
{
  // Create container with line edit + browse button
  QWidget* container = new QWidget();
  QHBoxLayout* layout = new QHBoxLayout(container);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(4);

  QLineEdit* lineEdit = new QLineEdit();
  lineEdit->setText(QString::fromStdString(value));
  lineEdit->setReadOnly(true);  // Only allow selection via dialog
  if (!tooltip.empty()) {
    lineEdit->setToolTip(QString::fromStdString(tooltip));
  }
  layout->addWidget(lineEdit, 1);

  QPushButton* browseBtn = new QPushButton("...");
  browseBtn->setFixedWidth(30);
  browseBtn->setToolTip("Browse connected motion devices");
  layout->addWidget(browseBtn);

  // Connect browse button
  connect(browseBtn, &QPushButton::clicked, this, [this, key, lineEdit]() {
    std::string currentValue = lineEdit->text().toStdString();

    std::string selected = MotionDeviceSelectorDialog::SelectDevice(
      m_context,
      currentValue,
      true,  // Show only connected devices by default
      this
    );

    if (selected != currentValue) {
      lineEdit->setText(QString::fromStdString(selected));
      emit propertyChanged(QString::fromStdString(key), QString::fromStdString(selected));
      emit propertyChanged();
    }
  });

  // Store line edit as the value widget
  m_valueWidgets[key] = lineEdit;

  // Add row manually since we're using a container
  int row = m_table->rowCount();
  m_table->insertRow(row);

  QWidget* labelWidget = createPropertyLabel(label, tooltip);
  m_table->setCellWidget(row, 0, labelWidget);

  QWidget* valueContainer = new QWidget();
  QHBoxLayout* valueLayout = new QHBoxLayout(valueContainer);
  valueLayout->setContentsMargins(6, 4, 6, 4);
  valueLayout->setSpacing(0);
  valueLayout->addWidget(container);
  m_table->setCellWidget(row, 1, valueContainer);

  m_keyToRow[key] = row;
  updateTableHeight();
}

void PropertyTableWidget::addMotionDevicePropertyWithAxes(const std::string& key, const std::string& label,
                                                          const std::string& value, const std::string& requiredAxes,
                                                          const std::string& tooltip)
{
  // Create container with line edit + browse button
  QWidget* container = new QWidget();
  QHBoxLayout* layout = new QHBoxLayout(container);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(4);

  QLineEdit* lineEdit = new QLineEdit();
  lineEdit->setText(QString::fromStdString(value));
  lineEdit->setReadOnly(true);  // Only allow selection via dialog
  std::string tooltipWithAxes = tooltip;
  if (!requiredAxes.empty()) {
    if (!tooltipWithAxes.empty()) tooltipWithAxes += " ";
    tooltipWithAxes += "(requires " + requiredAxes + " axes)";
  }
  if (!tooltipWithAxes.empty()) {
    lineEdit->setToolTip(QString::fromStdString(tooltipWithAxes));
  }
  layout->addWidget(lineEdit, 1);

  QPushButton* browseBtn = new QPushButton("...");
  browseBtn->setFixedWidth(30);
  browseBtn->setToolTip(QString("Browse devices with %1 axes").arg(QString::fromStdString(requiredAxes)));
  layout->addWidget(browseBtn);

  // Connect browse button with axis filter
  connect(browseBtn, &QPushButton::clicked, this, [this, key, lineEdit, requiredAxes]() {
    std::string currentValue = lineEdit->text().toStdString();

    std::string selected = MotionDeviceSelectorDialog::SelectDeviceWithAxes(
      m_context,
      currentValue,
      requiredAxes,
      true,  // Show only connected devices by default
      this
    );

    if (selected != currentValue) {
      lineEdit->setText(QString::fromStdString(selected));
      emit propertyChanged(QString::fromStdString(key), QString::fromStdString(selected));
      emit propertyChanged();
    }
  });

  // Store line edit as the value widget
  m_valueWidgets[key] = lineEdit;

  // Add row manually since we're using a container
  int row = m_table->rowCount();
  m_table->insertRow(row);

  QWidget* labelWidget = createPropertyLabel(label, tooltipWithAxes);
  m_table->setCellWidget(row, 0, labelWidget);

  QWidget* valueContainer = new QWidget();
  QHBoxLayout* valueLayout = new QHBoxLayout(valueContainer);
  valueLayout->setContentsMargins(6, 4, 6, 4);
  valueLayout->setSpacing(0);
  valueLayout->addWidget(container);
  m_table->setCellWidget(row, 1, valueContainer);

  m_keyToRow[key] = row;
  updateTableHeight();
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

void PropertyTableWidget::addSlideProperty(const std::string& key, const std::string& label,
                                           const std::string& value, const std::string& tooltip)
{
  // Create container with line edit and browse button
  QWidget* container = new QWidget();
  QHBoxLayout* layout = new QHBoxLayout(container);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(5);

  QLineEdit* edit = new QLineEdit(QString::fromStdString(value));
  edit->setStyleSheet(
    "QLineEdit { "
    "  background-color: #3c3c3c; "
    "  color: #d4d4d4; "
    "  border: 1px solid #555555; "
    "  border-radius: 3px; "
    "  padding: 4px 8px; "
    "  min-height: 24px; "
    "}"
  );
  if (!tooltip.empty()) {
    edit->setToolTip(QString::fromStdString(tooltip));
  }

  connect(edit, &QLineEdit::textChanged, this, [this, key](const QString& text) {
    emit propertyChanged(QString::fromStdString(key), text);
    emit propertyChanged();
  });
  layout->addWidget(edit, 1);  // Stretch factor 1

  QPushButton* browseBtn = new QPushButton("...");
  browseBtn->setFixedSize(28, 28);
  browseBtn->setStyleSheet(
    "QPushButton { "
    "  background-color: #5a8fd6; "
    "  color: white; "
    "  border-radius: 3px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { background-color: #4a7fc6; }"
  );
  browseBtn->setToolTip("Browse for pneumatic slide...");

  connect(browseBtn, &QPushButton::clicked, this, [this, edit]() {
    std::string currentSlide = edit->text().toStdString();
    std::string selected = PneumaticSlideSelectorDialog::SelectSlide(
      m_context, currentSlide, this
    );
    if (!selected.empty()) {
      edit->setText(QString::fromStdString(selected));
    }
  });

  layout->addWidget(browseBtn);

  // Store the line edit for value retrieval
  m_valueWidgets[key] = edit;

  // Add row manually (don't use addPropertyRow since we have custom container)
  int row = m_table->rowCount();
  m_table->insertRow(row);
  m_table->setCellWidget(row, 0, createPropertyLabel(label, tooltip));

  // Wrap container in another container with margins
  QWidget* outerContainer = new QWidget();
  QHBoxLayout* outerLayout = new QHBoxLayout(outerContainer);
  outerLayout->setContentsMargins(6, 4, 6, 4);
  outerLayout->setSpacing(0);
  outerLayout->addWidget(container);
  m_table->setCellWidget(row, 1, outerContainer);

  m_keyToRow[key] = row;

  // Update table height to fit content
  updateTableHeight();
}

void PropertyTableWidget::addNodeProperty(const std::string& key, const std::string& label,
                                          const std::string& value, const std::string& deviceKey,
                                          const std::string& tooltip)
{
  // Create container with line edit and browse button
  QWidget* container = new QWidget();
  QHBoxLayout* layout = new QHBoxLayout(container);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(5);

  QLineEdit* edit = new QLineEdit(QString::fromStdString(value));
  edit->setStyleSheet(
    "QLineEdit { "
    "  background-color: #3c3c3c; "
    "  color: #d4d4d4; "
    "  border: 1px solid #555555; "
    "  border-radius: 3px; "
    "  padding: 4px 8px; "
    "  min-height: 24px; "
    "}"
  );
  if (!tooltip.empty()) {
    edit->setToolTip(QString::fromStdString(tooltip));
  }

  connect(edit, &QLineEdit::textChanged, this, [this, key](const QString& text) {
    emit propertyChanged(QString::fromStdString(key), text);
    emit propertyChanged();
  });
  layout->addWidget(edit, 1);

  QPushButton* browseBtn = new QPushButton("...");
  browseBtn->setFixedSize(28, 28);
  browseBtn->setStyleSheet(
    "QPushButton { "
    "  background-color: #5a8fd6; "
    "  color: white; "
    "  border-radius: 3px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { background-color: #4a7fc6; }"
  );
  browseBtn->setToolTip("Browse for position...");

  connect(browseBtn, &QPushButton::clicked, this, [this, edit, deviceKey]() {
    // Get current device from parameters
    std::string deviceName = getParameter(deviceKey);
    if (deviceName.empty()) {
      deviceName = deviceKey;  // Fallback to key as device name
    }

    std::string currentNode = edit->text().toStdString();
    std::string selected = NodeSelectorDialog::SelectNode(
      m_context, deviceName, currentNode, this
    );
    if (!selected.empty()) {
      edit->setText(QString::fromStdString(selected));
    }
  });

  layout->addWidget(browseBtn);

  // Store the line edit for value retrieval
  m_valueWidgets[key] = edit;

  // Add row manually
  int row = m_table->rowCount();
  m_table->insertRow(row);
  m_table->setCellWidget(row, 0, createPropertyLabel(label, tooltip));

  // Wrap container
  QWidget* outerContainer = new QWidget();
  QHBoxLayout* outerLayout = new QHBoxLayout(outerContainer);
  outerLayout->setContentsMargins(6, 4, 6, 4);
  outerLayout->setSpacing(0);
  outerLayout->addWidget(container);
  m_table->setCellWidget(row, 1, outerContainer);

  m_keyToRow[key] = row;
  updateTableHeight();
}

void PropertyTableWidget::addGraphProperty(const std::string& key, const std::string& label,
                                           const std::string& value, const std::string& deviceKey,
                                           const std::string& tooltip)
{
  QComboBox* combo = new QComboBox();
  combo->setEditable(true);
  if (!tooltip.empty()) {
    combo->setToolTip(QString::fromStdString(tooltip));
  }

  // Lambda to populate graph names
  auto populateGraphs = [this, combo, deviceKey, value]() {
    QString currentText = combo->currentText();
    combo->clear();

    std::string deviceName = getParameter(deviceKey);
    if (deviceName.empty()) {
      combo->addItem("(Select device first)");
      return;
    }

    if (m_context && m_context->productPositionManager) {
      std::vector<std::string> graphNames =
        m_context->productPositionManager->GetGraphNamesForDevice(deviceName);

      for (const std::string& graphName : graphNames) {
        combo->addItem(QString::fromStdString(graphName));
      }
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
  populateGraphs();

  // Watch for device changes
  if (m_valueWidgets.find(deviceKey) != m_valueWidgets.end()) {
    QComboBox* deviceCombo = qobject_cast<QComboBox*>(m_valueWidgets[deviceKey]);
    if (deviceCombo) {
      connect(deviceCombo, &QComboBox::currentTextChanged, this, [populateGraphs]() {
        populateGraphs();
      });
    }
  }

  connect(combo, &QComboBox::currentTextChanged, this, [this, key](const QString& text) {
    emit propertyChanged(QString::fromStdString(key), text);
    emit propertyChanged();
  });

  addPropertyRow(key, label, combo, tooltip);
}

void PropertyTableWidget::addGraphNodeProperty(const std::string& key, const std::string& label,
                                               const std::string& value, const std::string& deviceKey,
                                               const std::string& graphKey, const std::string& tooltip)
{
  QComboBox* combo = new QComboBox();
  combo->setEditable(true);
  if (!tooltip.empty()) {
    combo->setToolTip(QString::fromStdString(tooltip));
  }

  // Lambda to populate node names
  auto populateNodes = [this, combo, deviceKey, graphKey, value]() {
    QString currentText = combo->currentText();
    combo->clear();

    std::string deviceName = getParameter(deviceKey);
    std::string graphName = getParameter(graphKey);

    if (deviceName.empty() || graphName.empty()) {
      combo->addItem("(Select device and graph first)");
      return;
    }

    if (m_context && m_context->productPositionManager) {
      std::vector<std::string> nodeNames =
        m_context->productPositionManager->GetNodeNamesForGraph(deviceName, graphName);

      for (const std::string& nodeName : nodeNames) {
        combo->addItem(QString::fromStdString(nodeName));
      }
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
  populateNodes();

  // Watch for device changes
  if (m_valueWidgets.find(deviceKey) != m_valueWidgets.end()) {
    QComboBox* deviceCombo = qobject_cast<QComboBox*>(m_valueWidgets[deviceKey]);
    if (deviceCombo) {
      connect(deviceCombo, &QComboBox::currentTextChanged, this, [populateNodes]() {
        populateNodes();
      });
    }
  }

  // Watch for graph changes
  if (m_valueWidgets.find(graphKey) != m_valueWidgets.end()) {
    QComboBox* graphCombo = qobject_cast<QComboBox*>(m_valueWidgets[graphKey]);
    if (graphCombo) {
      connect(graphCombo, &QComboBox::currentTextChanged, this, [populateNodes]() {
        populateNodes();
      });
    }
  }

  connect(combo, &QComboBox::currentTextChanged, this, [this, key](const QString& text) {
    emit propertyChanged(QString::fromStdString(key), text);
    emit propertyChanged();
  });

  addPropertyRow(key, label, combo, tooltip);
}

void PropertyTableWidget::addDataChannelProperty(const std::string& key, const std::string& label,
                                                 const std::string& value, const std::string& tooltip)
{
  QComboBox* combo = new QComboBox();
  combo->setEditable(true);
  if (!tooltip.empty()) {
    combo->setToolTip(QString::fromStdString(tooltip));
  }

  // Populate with DataStore channels
  if (m_context && m_context->dataStore) {
    auto channelNames = m_context->dataStore->GetAllChannelNames();
    for (const auto& channel : channelNames) {
      combo->addItem(QString::fromStdString(channel));
    }
  }

  // Set current value
  if (!value.empty()) {
    int idx = combo->findText(QString::fromStdString(value));
    if (idx >= 0) {
      combo->setCurrentIndex(idx);
    } else {
      combo->setEditText(QString::fromStdString(value));
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

  // Update table height to fit content
  updateTableHeight();
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

  // Update table height to fit content
  updateTableHeight();
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
  updateTableHeight();
}

void PropertyTableWidget::updateTableHeight()
{
  // Calculate total height needed: header + all rows
  int totalHeight = m_table->horizontalHeader()->height();
  for (int i = 0; i < m_table->rowCount(); ++i) {
    totalHeight += m_table->rowHeight(i);
  }
  // Add small margin for border
  totalHeight += 4;

  m_table->setFixedHeight(totalHeight);
  setMinimumHeight(totalHeight);
}
