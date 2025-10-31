// ProcessConfigWidget.cpp
#include "ProcessConfigWidget.h"
#include "DeviceSelectorWidgets.h"
#include "ApplicationContext.h"
#include "PneumaticSlideSelectorDialog.h"
#include "DataStore.h"
#include "TECManager.h"
#include "RecipeManager.h"
#include "ProcessPluginRegistry.h"
#include "products/product_position_manager.h"
#include <QLabel>
#include <QFrame>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <iostream>

// ============================================================================
// BASE CLASS: ProcessConfigWidget
// ============================================================================

ProcessConfigWidget::ProcessConfigWidget(ApplicationContext* context, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_enabledCheckBox(nullptr)
  , m_nicknameEdit(nullptr)
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(10, 10, 10, 10);

  m_formLayout = new QFormLayout();
  m_formLayout->setSpacing(10);

  mainLayout->addLayout(m_formLayout);
  mainLayout->addStretch();
}

void ProcessConfigWidget::loadFromInstance(const ProcessInstance* instance)
{
  if (!instance) return;

  // Clear existing fields
  while (m_formLayout->count() > 0) {
    QLayoutItem* item = m_formLayout->takeAt(0);
    if (item->widget()) item->widget()->deleteLater();
    delete item;
  }
  m_parameterEdits.clear();
  m_deviceCombos.clear();
  m_parameterCheckboxes.clear();
  m_parameterSpinBoxes.clear();
  m_parameterCombos.clear();

  // Add enabled checkbox
  m_enabledCheckBox = new QCheckBox("Step Enabled");
  m_enabledCheckBox->setChecked(instance->enabled);
  m_enabledCheckBox->setStyleSheet("font-weight: bold; font-size: 13px;");
  connect(m_enabledCheckBox, &QCheckBox::toggled,
    this, &ProcessConfigWidget::enabledChanged);
  m_formLayout->addRow(m_enabledCheckBox);

  // Divider
  QFrame* line1 = new QFrame();
  line1->setFrameShape(QFrame::HLine);
  line1->setStyleSheet("color: #ccc;");
  m_formLayout->addRow(line1);

  // Nickname field
  m_nicknameEdit = new QLineEdit(QString::fromStdString(instance->userNickname));
  m_nicknameEdit->setStyleSheet(
    "font-weight: bold; "
    "background-color: #3c3c3c; "
    "color: #d4d4d4; "
    "border: 1px solid #555555; "
    "border-radius: 3px; "
    "padding: 5px;"
  );

  disconnect(m_nicknameEdit, &QLineEdit::textChanged, this, nullptr);
  connect(m_nicknameEdit, &QLineEdit::textChanged,
    this, &ProcessConfigWidget::nicknameChanged, Qt::QueuedConnection);

  m_formLayout->addRow("Process Name:", m_nicknameEdit);

  // Read-only fields
  QLineEdit* idEdit = new QLineEdit(QString::fromStdString(instance->uniqueId));
  idEdit->setReadOnly(true);
  idEdit->setStyleSheet("background-color: #2d2d30; color: #888888; border: 1px solid #3f3f46;");
  m_formLayout->addRow("Unique ID:", idEdit);

  QLineEdit* typeEdit = new QLineEdit(QString::fromStdString(instance->processType));
  typeEdit->setReadOnly(true);
  typeEdit->setStyleSheet("background-color: #2d2d30; color: #888888; border: 1px solid #3f3f46;");
  m_formLayout->addRow("Process Type:", typeEdit);

  // Divider
  QFrame* line2 = new QFrame();
  line2->setFrameShape(QFrame::HLine);
  line2->setStyleSheet("color: #ccc;");
  m_formLayout->addRow(line2);

  // Add title for parameters section
  QLabel* paramLabel = new QLabel("Parameters:");
  paramLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #d4d4d4;");
  m_formLayout->addRow(paramLabel);

  // Let derived class create specific parameter fields
  createParameterFields(instance);
}

void ProcessConfigWidget::addParameterField(const std::string& key,
  const std::string& label,
  const std::string& value)
{
  QLineEdit* edit = new QLineEdit(QString::fromStdString(value));
  edit->setStyleSheet(
    "padding: 5px; "
    "background-color: #3c3c3c; "
    "color: #d4d4d4; "
    "border: 1px solid #555555; "
    "border-radius: 3px;"
  );
  connect(edit, &QLineEdit::textChanged,
    this, &ProcessConfigWidget::parameterChanged);

  m_formLayout->addRow(QString::fromStdString(label) + ":", edit);
  m_parameterEdits[key] = edit;
}

void ProcessConfigWidget::addDeviceSelector(const std::string& key,
  const std::string& label,
  const std::string& value,
  int deviceType)
{
  DeviceComboBox* combo = new DeviceComboBox(
    m_context,
    static_cast<DeviceComboBox::DeviceType>(deviceType)
  );
  combo->setSelectedDevice(value);
  connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
    this, &ProcessConfigWidget::parameterChanged);

  m_formLayout->addRow(QString::fromStdString(label) + ":", combo);
  m_deviceCombos[key] = combo;
}

void ProcessConfigWidget::addNodeSelector(const std::string& key,
  const std::string& label,
  const std::string& value,
  const std::string& deviceParamKey)
{
  QWidget* container = new QWidget();
  QHBoxLayout* layout = new QHBoxLayout(container);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(5);

  QLineEdit* edit = new QLineEdit(QString::fromStdString(value));
  edit->setStyleSheet(
    "padding: 5px; "
    "background-color: #3c3c3c; "
    "color: #d4d4d4; "
    "border: 1px solid #555555; "
    "border-radius: 3px;"
  );
  connect(edit, &QLineEdit::textChanged,
    this, &ProcessConfigWidget::parameterChanged);
  layout->addWidget(edit);

  QPushButton* browseBtn = new QPushButton("Browse...");
  browseBtn->setFixedWidth(80);
  browseBtn->setStyleSheet(
    "QPushButton { "
    "  background-color: #5a8fd6; "
    "  color: white; "
    "  border-radius: 3px; "
    "  padding: 5px; "
    "} "
    "QPushButton:hover { background-color: #4a7fc6; }"
  );

  // ✅ Dynamically get device name from device combo when browse is clicked
  connect(browseBtn, &QPushButton::clicked, [this, edit, deviceParamKey]() {
    // Get current device value from the parameters (not the original one)
    std::string deviceName = deviceParamKey;  // Default to parameter key if no device combo

    // Try to get from device combo box
    auto params = getParameters();
    if (params.find(deviceParamKey) != params.end()) {
      deviceName = params.at(deviceParamKey);
    }

    std::string currentNode = edit->text().toStdString();
    std::string selected = NodeSelectorDialog::SelectNode(
      m_context, deviceName, currentNode, this
    );
    edit->setText(QString::fromStdString(selected));
  });

  layout->addWidget(browseBtn);

  m_formLayout->addRow(QString::fromStdString(label) + ":", container);
  m_parameterEdits[key] = edit;
}

void ProcessConfigWidget::addSlideSelector(const std::string& key,
  const std::string& label,
  const std::string& value)
{
  QWidget* container = new QWidget();
  QHBoxLayout* layout = new QHBoxLayout(container);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(5);

  QLineEdit* edit = new QLineEdit(QString::fromStdString(value));
  edit->setStyleSheet(
    "padding: 5px; "
    "background-color: #3c3c3c; "
    "color: #d4d4d4; "
    "border: 1px solid #555555; "
    "border-radius: 3px;"
  );
  connect(edit, &QLineEdit::textChanged,
    this, &ProcessConfigWidget::parameterChanged);
  layout->addWidget(edit);

  QPushButton* browseBtn = new QPushButton("Browse...");
  browseBtn->setFixedWidth(80);
  browseBtn->setStyleSheet(
    "QPushButton { "
    "  background-color: #5a8fd6; "
    "  color: white; "
    "  border-radius: 3px; "
    "  padding: 5px; "
    "} "
    "QPushButton:hover { background-color: #4a7fc6; }"
  );

  connect(browseBtn, &QPushButton::clicked, [this, edit]() {
    std::string currentSlide = edit->text().toStdString();
    std::string selected = PneumaticSlideSelectorDialog::SelectSlide(
      m_context, currentSlide, this
    );
    if (!selected.empty()) {
      edit->setText(QString::fromStdString(selected));
    }
  });

  layout->addWidget(browseBtn);

  m_formLayout->addRow(QString::fromStdString(label) + ":", container);
  m_parameterEdits[key] = edit;
}

void ProcessConfigWidget::addCheckboxParameter(const std::string& key,
  const std::string& label,
  const std::string& value,
  const std::string& tooltip)
{
  QCheckBox* checkbox = new QCheckBox(QString::fromStdString(label));
  checkbox->setChecked(value == "true" || value == "1");
  checkbox->setFocusPolicy(Qt::StrongFocus);  // Ensure checkbox can receive focus

  // ✅ Add visual styling to make checked state clear
  checkbox->setStyleSheet(
    "QCheckBox { "
    "  spacing: 8px; "
    "  font-size: 12px; "
    "} "
    "QCheckBox::indicator { "
    "  width: 20px; "
    "  height: 20px; "
    "  border: 2px solid #555555; "
    "  border-radius: 3px; "
    "  background-color: #2d2d30; "
    "} "
    "QCheckBox::indicator:hover { "
    "  border-color: #0e639c; "
    "} "
    "QCheckBox::indicator:checked { "
    "  background-color: #27ae60; "
    "  border-color: #27ae60; "
    "} "
    "QCheckBox::indicator:checked:hover { "
    "  background-color: #2ecc71; "
    "  border-color: #2ecc71; "
    "} "
    "QCheckBox::indicator:unchecked { "
    "  background-color: #2d2d30; "
    "  border-color: #555555; "
    "}"
  );

  if (!tooltip.empty()) {
    checkbox->setToolTip(QString::fromStdString(tooltip));
  }

  connect(checkbox, &QCheckBox::toggled, this, [this]() {
    emit parameterChanged();
  });

  // ✅ Add checkbox spanning both columns to avoid layout blocking
  m_formLayout->addRow(checkbox);
  m_parameterCheckboxes[key] = checkbox;
}

void ProcessConfigWidget::addNumericParameter(const std::string& key,
  const std::string& label,
  const std::string& value,
  double minValue,
  double maxValue,
  double step,
  int decimals,
  const std::string& suffix)
{
  QDoubleSpinBox* spinBox = new QDoubleSpinBox();
  spinBox->setRange(minValue, maxValue);
  spinBox->setSingleStep(step);
  spinBox->setDecimals(decimals);
  spinBox->setValue(std::stod(value));
  spinBox->setStyleSheet(
    "padding: 5px; "
    "background-color: #3c3c3c; "
    "color: #d4d4d4; "
    "border: 1px solid #555555; "
    "border-radius: 3px;"
  );

  if (!suffix.empty()) {
    spinBox->setSuffix(QString::fromStdString(" " + suffix));
  }

  connect(spinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
    this, &ProcessConfigWidget::parameterChanged);

  m_formLayout->addRow(QString::fromStdString(label) + ":", spinBox);
  m_parameterSpinBoxes[key] = spinBox;
}

void ProcessConfigWidget::addGraphSelector(const std::string& key,
  const std::string& label,
  const std::string& value,
  const std::string& deviceParamKey)
{
  QComboBox* combo = new QComboBox();
  combo->setStyleSheet(
    "padding: 5px; "
    "background-color: #3c3c3c; "
    "color: #d4d4d4; "
    "border: 1px solid #555555; "
    "border-radius: 3px;"
  );

  // Populate with graphs for the current device
  auto params = getParameters();
  if (params.find(deviceParamKey) != params.end()) {
    std::string deviceName = params.at(deviceParamKey);

    if (m_context && m_context->productPositionManager) {
      std::vector<std::string> graphNames =
        m_context->productPositionManager->GetGraphNamesForDevice(deviceName);

      for (const std::string& graphName : graphNames) {
        combo->addItem(QString::fromStdString(graphName));
      }
    }
  }

  // Set current value
  int index = combo->findText(QString::fromStdString(value));
  if (index >= 0) {
    combo->setCurrentIndex(index);
  }

  // Connect to parameter change signal
  connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
    this, &ProcessConfigWidget::parameterChanged);

  m_formLayout->addRow(QString::fromStdString(label) + ":", combo);
  m_parameterCombos[key] = combo;

  // ✅ Watch device selector for changes and repopulate graph list
  if (m_deviceCombos.find(deviceParamKey) != m_deviceCombos.end()) {
    DeviceComboBox* deviceCombo = m_deviceCombos[deviceParamKey];

    connect(deviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
      [this, combo, deviceParamKey]() {
        std::string deviceName = getParameters().at(deviceParamKey);

        // Save current selection
        QString currentSelection = combo->currentText();

        // Repopulate
        combo->clear();
        if (m_context && m_context->productPositionManager) {
          std::vector<std::string> graphNames =
            m_context->productPositionManager->GetGraphNamesForDevice(deviceName);

          for (const std::string& graphName : graphNames) {
            combo->addItem(QString::fromStdString(graphName));
          }
        }

        // Try to restore selection
        int index = combo->findText(currentSelection);
        if (index >= 0) {
          combo->setCurrentIndex(index);
        }
      });
  }
}

void ProcessConfigWidget::addGraphNodeSelector(const std::string& key,
  const std::string& label,
  const std::string& value,
  const std::string& deviceParamKey,
  const std::string& graphParamKey)
{
  QComboBox* combo = new QComboBox();
  combo->setStyleSheet(
    "padding: 5px; "
    "background-color: #3c3c3c; "
    "color: #d4d4d4; "
    "border: 1px solid #555555; "
    "border-radius: 3px;"
  );

  // Populate with nodes for the current device + graph
  auto params = getParameters();
  if (params.find(deviceParamKey) != params.end() &&
      params.find(graphParamKey) != params.end()) {
    std::string deviceName = params.at(deviceParamKey);
    std::string graphName = params.at(graphParamKey);

    if (m_context && m_context->productPositionManager) {
      std::vector<std::string> nodeNames =
        m_context->productPositionManager->GetNodeNamesForGraph(deviceName, graphName);

      for (const std::string& nodeName : nodeNames) {
        combo->addItem(QString::fromStdString(nodeName));
      }
    }
  }

  // Set current value
  int index = combo->findText(QString::fromStdString(value));
  if (index >= 0) {
    combo->setCurrentIndex(index);
  }

  // Connect to parameter change signal
  connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
    this, &ProcessConfigWidget::parameterChanged);

  m_formLayout->addRow(QString::fromStdString(label) + ":", combo);
  m_parameterCombos[key] = combo;

  // ✅ Watch graph selector for changes and repopulate node list
  if (m_parameterCombos.find(graphParamKey) != m_parameterCombos.end()) {
    QComboBox* graphCombo = m_parameterCombos[graphParamKey];

    connect(graphCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
      [this, combo, deviceParamKey, graphParamKey]() {
        auto params = getParameters();
        std::string deviceName = params.at(deviceParamKey);
        std::string graphName = params.at(graphParamKey);

        // Save current selection
        QString currentSelection = combo->currentText();

        // Repopulate
        combo->clear();
        if (m_context && m_context->productPositionManager) {
          std::vector<std::string> nodeNames =
            m_context->productPositionManager->GetNodeNamesForGraph(deviceName, graphName);

          for (const std::string& nodeName : nodeNames) {
            combo->addItem(QString::fromStdString(nodeName));
          }
        }

        // Try to restore selection
        int index = combo->findText(currentSelection);
        if (index >= 0) {
          combo->setCurrentIndex(index);
        }
      });
  }

  // ✅ Also watch device selector for changes
  if (m_deviceCombos.find(deviceParamKey) != m_deviceCombos.end()) {
    DeviceComboBox* deviceCombo = m_deviceCombos[deviceParamKey];

    connect(deviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
      [this, combo, deviceParamKey, graphParamKey]() {
        auto params = getParameters();
        if (params.find(graphParamKey) == params.end()) return;

        std::string deviceName = params.at(deviceParamKey);
        std::string graphName = params.at(graphParamKey);

        // Repopulate
        combo->clear();
        if (m_context && m_context->productPositionManager) {
          std::vector<std::string> nodeNames =
            m_context->productPositionManager->GetNodeNamesForGraph(deviceName, graphName);

          for (const std::string& nodeName : nodeNames) {
            combo->addItem(QString::fromStdString(nodeName));
          }
        }
      });
  }
}

std::map<std::string, std::string> ProcessConfigWidget::getParameters() const
{
  std::map<std::string, std::string> params;

  for (const auto& [key, edit] : m_parameterEdits) {
    params[key] = edit->text().toStdString();
  }

  for (const auto& [key, combo] : m_deviceCombos) {
    params[key] = combo->getSelectedDevice();
  }

  for (const auto& [key, checkbox] : m_parameterCheckboxes) {
    params[key] = checkbox->isChecked() ? "true" : "false";
  }

  for (const auto& [key, spinBox] : m_parameterSpinBoxes) {
    params[key] = std::to_string(spinBox->value());
  }

  for (const auto& [key, combo] : m_parameterCombos) {
    params[key] = combo->currentText().toStdString();
  }

  return params;
}

std::string ProcessConfigWidget::getNickname() const
{
  return m_nicknameEdit ? m_nicknameEdit->text().toStdString() : "";
}

bool ProcessConfigWidget::isEnabled() const
{
  return m_enabledCheckBox ? m_enabledCheckBox->isChecked() : true;
}

// ============================================================================
// PLUGIN-BASED CONFIG WIDGET
// ============================================================================

PluginBasedConfigWidget::PluginBasedConfigWidget(ApplicationContext* context, const std::string& processType, QWidget* parent)
  : ProcessConfigWidget(context, parent)
  , m_processType(processType)
{
}

void PluginBasedConfigWidget::createParameterFields(const ProcessInstance* instance)
{
  auto& registry = ProcessPluginRegistry::Instance();
  IProcessPlugin* plugin = registry.GetPlugin(m_processType);

  if (plugin) {
    plugin->CreateConfigUI(this, instance);
  }
  else {
    std::cerr << "[PluginBasedConfigWidget] No plugin found for process type: " << m_processType << std::endl;
  }
}

// ============================================================================
// All old process-specific widget implementations have been removed.
// The plugin architecture (PluginBasedConfigWidget + IProcessPlugin) now
// handles all process configuration UIs dynamically.
//
// See plugin files in include/recipe/processes/ for process implementations.
// ============================================================================
