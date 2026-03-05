// ProcessConfigWidget.cpp
#include "ProcessConfigWidget.h"
#include "PropertyTableWidget.h"
#include "DeviceSelectorWidgets.h"
#include "ApplicationContext.h"
#include "PneumaticSlideSelectorDialog.h"
#include "DataStore.h"
#include "TECManager.h"
#include "EziIOManager.h"  // For IO pin selector
#include "PowerSupplyManager.h"  // For PSU channel selector
#include "RecipeManager.h"
#include "ProcessPluginRegistry.h"
#include "IProcessPlugin.h"
#include "products/product_position_manager.h"
#include <QLabel>
#include <QFrame>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDialog>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#include <filesystem>

// ============================================================================
// BASE CLASS: ProcessConfigWidget
// ============================================================================

ProcessConfigWidget::ProcessConfigWidget(ApplicationContext* context, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_enabledCheckBox(nullptr)
  , m_nicknameEdit(nullptr)
  , m_channelRefreshTimer(nullptr)  // ✅ ADDED: Initialize timer to nullptr
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
  m_dataChannelCombos.clear();  // ✅ ADDED: Clear data channel combos
  m_visionProfileCombos.clear();  // ✅ ADDED: Clear vision profile combos

  // Add enabled checkbox
  m_enabledCheckBox = new QCheckBox("Step Enabled");
  m_enabledCheckBox->setChecked(instance->enabled);
  m_enabledCheckBox->setStyleSheet("font-weight: bold;");
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
  paramLabel->setStyleSheet("font-weight: bold; color: #d4d4d4;");
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

  // Connect BEFORE setting the selected device so auto-select triggers parameterChanged
  connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
    this, &ProcessConfigWidget::parameterChanged);

  m_formLayout->addRow(QString::fromStdString(label) + ":", combo);
  m_deviceCombos[key] = combo;

  // Set selected device AFTER connection and storage
  combo->setSelectedDevice(value);
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

void ProcessConfigWidget::addGraphSelectorForDevice(const std::string& key,
  const std::string& label,
  const std::string& value,
  const std::string& fixedDeviceName)
{
  QComboBox* combo = new QComboBox();
  combo->setStyleSheet(
    "padding: 5px; "
    "background-color: #3c3c3c; "
    "color: #d4d4d4; "
    "border: 1px solid #555555; "
    "border-radius: 3px;"
  );

  // Populate with graphs for the fixed device
  if (m_context && m_context->productPositionManager) {
    std::vector<std::string> graphNames =
      m_context->productPositionManager->GetGraphNamesForDevice(fixedDeviceName);

    for (const std::string& graphName : graphNames) {
      combo->addItem(QString::fromStdString(graphName));
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
}

void ProcessConfigWidget::addGraphNodeSelectorForDevice(const std::string& key,
  const std::string& label,
  const std::string& value,
  const std::string& fixedDeviceName,
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

  // Populate with nodes for the fixed device + selected graph
  auto params = getParameters();
  if (params.find(graphParamKey) != params.end()) {
    std::string graphName = params.at(graphParamKey);

    if (m_context && m_context->productPositionManager) {
      std::vector<std::string> nodeNames =
        m_context->productPositionManager->GetNodeNamesForGraph(fixedDeviceName, graphName);

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

  // Watch graph selector for changes and repopulate node list
  if (m_parameterCombos.find(graphParamKey) != m_parameterCombos.end()) {
    QComboBox* graphCombo = m_parameterCombos[graphParamKey];

    connect(graphCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
      [this, combo, fixedDeviceName, graphParamKey]() {
        auto params = getParameters();
        std::string graphName = params.at(graphParamKey);

        // Save current selection
        QString currentSelection = combo->currentText();

        // Repopulate
        combo->clear();
        if (m_context && m_context->productPositionManager) {
          std::vector<std::string> nodeNames =
            m_context->productPositionManager->GetNodeNamesForGraph(fixedDeviceName, graphName);

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
}

// ✅ NEW: Add DataStore channel selector with auto-refresh
void ProcessConfigWidget::addDataChannelSelector(const std::string& key,
  const std::string& label,
  const std::string& value)
{
  QComboBox* combo = new QComboBox();
  combo->setStyleSheet(
    "padding: 5px; "
    "background-color: #3c3c3c; "
    "color: #d4d4d4; "
    "border: 1px solid #555555; "
    "border-radius: 3px;"
  );
  combo->setEditable(true);  // Allow typing custom channels if needed

  // ✅ Populate with DataStore channels
  if (m_context && m_context->dataStore) {
    auto channelNames = m_context->dataStore->GetAllChannelNames();
    for (const auto& channel : channelNames) {
      combo->addItem(QString::fromStdString(channel));
    }
  }

  // Set current value
  if (!value.empty()) {
    int index = combo->findText(QString::fromStdString(value));
    if (index >= 0) {
      combo->setCurrentIndex(index);
    } else {
      // If not in list, set as custom text
      combo->setEditText(QString::fromStdString(value));
    }
  }

  // Connect to parameter change signal
  connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
    this, &ProcessConfigWidget::parameterChanged);
  connect(combo, &QComboBox::editTextChanged,
    this, &ProcessConfigWidget::parameterChanged);

  m_formLayout->addRow(QString::fromStdString(label) + ":", combo);
  m_dataChannelCombos[key] = combo;

  // ✅ Set up auto-refresh timer (only once, on first call)
  if (!m_channelRefreshTimer) {
    m_channelRefreshTimer = new QTimer(this);
    connect(m_channelRefreshTimer, &QTimer::timeout,
            this, &ProcessConfigWidget::refreshDataChannels);
    m_channelRefreshTimer->start(3000);  // Refresh every 3 seconds
    std::cout << "[ProcessConfigWidget] Data channel auto-refresh started" << std::endl;
  }
}

// ✅ NEW: Refresh DataStore channels periodically
void ProcessConfigWidget::refreshDataChannels()
{
  if (!m_context || !m_context->dataStore) return;

  // Get current channels from DataStore
  auto channelNames = m_context->dataStore->GetAllChannelNames();
  QStringList newChannels;
  for (const auto& channel : channelNames) {
    newChannels << QString::fromStdString(channel);
  }

  // Update all data channel selectors
  for (auto& [key, combo] : m_dataChannelCombos) {
    // Save current selection
    QString currentSelection = combo->currentText();

    // Check if list has changed
    QStringList currentItems;
    for (int i = 0; i < combo->count(); ++i) {
      currentItems << combo->itemText(i);
    }

    if (newChannels != currentItems) {
      // Update combo box
      combo->clear();
      combo->addItems(newChannels);

      // Restore selection if it exists
      int index = combo->findText(currentSelection);
      if (index >= 0) {
        combo->setCurrentIndex(index);
      } else if (!currentSelection.isEmpty()) {
        // Restore as custom text if not in list
        combo->setEditText(currentSelection);
      }
    }
  }
}

// ✅ NEW: Add vision profile selector (dropdown of .json files from product vision folder)
void ProcessConfigWidget::addVisionProfileSelector(const std::string& key,
  const std::string& label,
  const std::string& value)
{
  VisionProfileComboBox* combo = new VisionProfileComboBox(m_context);

  // Connect BEFORE setting profile so auto-select triggers parameterChanged
  connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
    this, &ProcessConfigWidget::parameterChanged);

  // Create horizontal layout with combobox + refresh button
  QWidget* container = new QWidget();
  QHBoxLayout* hLayout = new QHBoxLayout(container);
  hLayout->setContentsMargins(0, 0, 0, 0);
  hLayout->setSpacing(4);
  hLayout->addWidget(combo, 1);

  QPushButton* refreshBtn = new QPushButton("Refresh");
  refreshBtn->setFixedWidth(60);
  refreshBtn->setToolTip("Refresh the vision profile list");
  connect(refreshBtn, &QPushButton::clicked, this, [combo]() {
    QString currentSelection = combo->currentText();
    combo->blockSignals(true);
    combo->refreshProfiles();
    // Restore previous selection if still available
    int idx = combo->findText(currentSelection);
    if (idx >= 0) {
      combo->setCurrentIndex(idx);
    }
    combo->blockSignals(false);
    std::cout << "[ProcessConfigWidget] Vision profile list refreshed" << std::endl;
  });
  hLayout->addWidget(refreshBtn);

  m_formLayout->addRow(QString::fromStdString(label) + ":", container);

  // Store in vision profile combos map (special handling for full path)
  m_visionProfileCombos[key] = combo;

  // Set selected profile AFTER connection and storage
  combo->setSelectedProfile(value);

  std::cout << "[ProcessConfigWidget] Added vision profile selector for key: " << key << std::endl;
}

// ✅ NEW: Add IO pin name selector (dropdown with pin names from IO device)
void ProcessConfigWidget::addIOPinSelector(const std::string& key,
  const std::string& label,
  const std::string& value,
  const std::string& ioDeviceParamKey,
  const std::string& pinType)
{
  QComboBox* combo = new QComboBox();
  combo->setStyleSheet(
    "padding: 5px; "
    "background-color: #3c3c3c; "
    "color: #d4d4d4; "
    "border: 1px solid #555555; "
    "border-radius: 3px;"
  );
  combo->setEditable(true);  // Allow typing pin number if needed

  // Lambda to populate pin names from IO device
  auto populatePinNames = [this, combo, ioDeviceParamKey, pinType, value]() {
    combo->clear();

    // Get current IO device name from parameters
    auto params = getParameters();
    if (params.find(ioDeviceParamKey) == params.end()) {
      combo->addItem("(No IO device selected)");
      return;
    }

    std::string deviceName = params.at(ioDeviceParamKey);
    if (deviceName.empty()) {
      combo->addItem("(No IO device selected)");
      return;
    }

    // Get IO device from manager
    if (!m_context || !m_context->ioManager) {
      combo->addItem("(IO Manager not available)");
      return;
    }

    auto* ioDevice = m_context->ioManager->GetDevice(deviceName);
    if (!ioDevice) {
      combo->addItem("(Device not found)");
      return;
    }

    // Get pin names based on type
    std::vector<std::string> pinNames;
    if (pinType == "input") {
      pinNames = ioDevice->GetAllInputPinNames();
    } else {  // default to output
      pinNames = ioDevice->GetAllOutputPinNames();
    }

    if (pinNames.empty()) {
      combo->addItem("(No pins configured)");
      return;
    }

    // Add pin names to combo box
    for (const auto& pinName : pinNames) {
      combo->addItem(QString::fromStdString(pinName));
    }

    // Set current value (try to match pin name first)
    if (!value.empty()) {
      int index = combo->findText(QString::fromStdString(value));
      if (index >= 0) {
        combo->setCurrentIndex(index);
      } else {
        // If value is a number, try to find the pin name for that number
        try {
          int pinNumber = std::stoi(value);
          std::string pinName = (pinType == "input") ?
            ioDevice->GetInputPinName(pinNumber) :
            ioDevice->GetOutputPinName(pinNumber);

          index = combo->findText(QString::fromStdString(pinName));
          if (index >= 0) {
            combo->setCurrentIndex(index);
            // ✅ Trigger parameter changed to update stored value from "10" to "Vacuum_Base"
            emit parameterChanged();
          } else {
            // Set as custom text (pin number)
            combo->setEditText(QString::fromStdString(value));
          }
        } catch (...) {
          // Not a number, just set as custom text
          combo->setEditText(QString::fromStdString(value));
        }
      }
    }
  };

  // Initial population
  populatePinNames();

  // Connect to parameter change signal
  connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
    this, &ProcessConfigWidget::parameterChanged);
  connect(combo, &QComboBox::editTextChanged,
    this, &ProcessConfigWidget::parameterChanged);

  // Watch IO device selector for changes and repopulate
  if (m_deviceCombos.find(ioDeviceParamKey) != m_deviceCombos.end()) {
    DeviceComboBox* deviceCombo = m_deviceCombos[ioDeviceParamKey];
    connect(deviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
      [populatePinNames]() {
        populatePinNames();
      });
  }

  m_formLayout->addRow(QString::fromStdString(label) + ":", combo);
  m_parameterCombos[key] = combo;
}

// ✅ NEW: Add simple dropdown parameter with fixed options
void ProcessConfigWidget::addDropdownParameter(const std::string& key,
  const std::string& label,
  const std::string& value,
  const std::vector<std::string>& options,
  bool editable)
{
  QComboBox* combo = new QComboBox();
  combo->setStyleSheet(
    "padding: 5px; "
    "background-color: #3c3c3c; "
    "color: #d4d4d4; "
    "border: 1px solid #555555; "
    "border-radius: 3px;"
  );

  // Allow manual text input when editable (e.g., custom regex patterns)
  if (editable) {
    combo->setEditable(true);
    combo->lineEdit()->setStyleSheet(
      "background-color: #3c3c3c; "
      "color: #d4d4d4; "
      "border: none; "
      "padding: 2px;"
    );
  }

  // Add all options to combo box
  for (const auto& option : options) {
    combo->addItem(QString::fromStdString(option));
  }

  // Set current value
  int index = combo->findText(QString::fromStdString(value));
  if (index >= 0) {
    combo->setCurrentIndex(index);
  } else if (editable) {
    // Custom value not in preset list - set it as typed text
    combo->setCurrentText(QString::fromStdString(value));
  } else if (!options.empty()) {
    combo->setCurrentIndex(0);  // Default to first option if value not found
  }

  // Connect to parameter change signal
  connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
    this, &ProcessConfigWidget::parameterChanged);
  if (editable) {
    connect(combo, &QComboBox::editTextChanged,
      this, &ProcessConfigWidget::parameterChanged);
  }

  m_formLayout->addRow(QString::fromStdString(label) + ":", combo);
  m_parameterCombos[key] = combo;
}

// ✅ NEW: Add PSU channel selector (dynamic based on PSU device config)
void ProcessConfigWidget::addPSUChannelSelector(const std::string& key,
  const std::string& label,
  const std::string& value,
  const std::string& psuDeviceParamKey)
{
  QComboBox* combo = new QComboBox();
  combo->setStyleSheet(
    "padding: 5px; "
    "background-color: #3c3c3c; "
    "color: #d4d4d4; "
    "border: 1px solid #555555; "
    "border-radius: 3px;"
  );

  // Lambda to populate channels based on PSU device
  auto populateChannels = [this, combo, psuDeviceParamKey, value]() {
    combo->clear();

    // Get current PSU device name from parameters
    auto params = getParameters();
    if (params.find(psuDeviceParamKey) == params.end()) {
      combo->addItem("1");  // Default to single channel
      return;
    }

    std::string deviceName = params.at(psuDeviceParamKey);
    if (deviceName.empty()) {
      combo->addItem("1");  // Default to single channel
      return;
    }

    // Get PSU device from manager
    if (!m_context || !m_context->powerSupplyManager) {
      combo->addItem("1");  // Default to single channel
      return;
    }

    // Get channel count for the device
    int channelCount = m_context->powerSupplyManager->getDeviceChannelCount(deviceName);
    if (channelCount < 1) {
      channelCount = 1;  // Safety: at least 1 channel
    }

    // Add channel options (1, 2, 3, 4, etc.)
    for (int i = 1; i <= channelCount; i++) {
      combo->addItem(QString::number(i));
    }

    // Set current value
    if (!value.empty()) {
      int index = combo->findText(QString::fromStdString(value));
      if (index >= 0) {
        combo->setCurrentIndex(index);
      } else {
        combo->setCurrentIndex(0);  // Default to channel 1
      }
    } else {
      combo->setCurrentIndex(0);  // Default to channel 1
    }
  };

  // Initial population
  populateChannels();

  // Connect to parameter change signal
  connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
    this, &ProcessConfigWidget::parameterChanged);

  // Watch PSU device selector for changes and repopulate
  if (m_deviceCombos.find(psuDeviceParamKey) != m_deviceCombos.end()) {
    DeviceComboBox* deviceCombo = m_deviceCombos[psuDeviceParamKey];
    connect(deviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
      [populateChannels]() {
        populateChannels();
      });
  }

  m_formLayout->addRow(QString::fromStdString(label) + ":", combo);
  m_parameterCombos[key] = combo;
}

// Helper to add taught coordinate system selector (with browse button)
void ProcessConfigWidget::addTaughtCoordinateSystemSelector(const std::string& key,
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

  QPushButton* browseBtn = new QPushButton("Select...");
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
    // Create selection dialog
    QDialog dialog(this->window());
    dialog.setWindowTitle("Select Taught Coordinate System");
    dialog.setMinimumSize(400, 350);
    dialog.setStyleSheet(
      "QDialog { background-color: #2d2d30; }"
      "QLabel { color: #d4d4d4; }"
    );

    QVBoxLayout* dialogLayout = new QVBoxLayout(&dialog);

    // Header
    QLabel* headerLabel = new QLabel("Available Taught Coordinate Systems:");
    headerLabel->setStyleSheet("font-weight: bold; font-size: 12px; color: #d4d4d4;");
    dialogLayout->addWidget(headerLabel);

    // List widget
    QListWidget* listWidget = new QListWidget();
    listWidget->setStyleSheet(
      "QListWidget { "
      "  background-color: #1e1e1e; "
      "  color: #d4d4d4; "
      "  border: 1px solid #3f3f46; "
      "  border-radius: 3px; "
      "  font-size: 12px; "
      "} "
      "QListWidget::item { "
      "  padding: 8px 10px; "
      "  border-bottom: 1px solid #2d2d30; "
      "} "
      "QListWidget::item:selected { "
      "  background-color: #0e639c; "
      "  color: #ffffff; "
      "} "
      "QListWidget::item:hover { "
      "  background-color: #2a2d2e; "
      "}"
    );

    // Load taught systems from disk
    std::filesystem::path taughtDir = "config/coordalign/taught_systems";
    if (std::filesystem::exists(taughtDir)) {
      for (const auto& entry : std::filesystem::directory_iterator(taughtDir)) {
        if (entry.path().extension() == ".json") {
          std::string uid = entry.path().stem().string();

          // Try to read description from JSON
          QString displayText = QString::fromStdString(uid);
          try {
            QFile file(QString::fromStdString(entry.path().string()));
            if (file.open(QIODevice::ReadOnly)) {
              QByteArray data = file.readAll();
              QJsonDocument doc = QJsonDocument::fromJson(data);
              if (doc.isObject()) {
                QJsonObject obj = doc.object();
                QString desc = obj.value("description").toString();
                if (!desc.isEmpty()) {
                  displayText = QString("%1  (%2)").arg(QString::fromStdString(uid), desc);
                }
              }
              file.close();
            }
          } catch (...) {
            // Ignore parsing errors
          }

          QListWidgetItem* item = new QListWidgetItem(displayText);
          item->setData(Qt::UserRole, QString::fromStdString(uid));  // Store UID
          listWidget->addItem(item);
        }
      }
    }

    if (listWidget->count() == 0) {
      QListWidgetItem* emptyItem = new QListWidgetItem("(No taught systems found)");
      emptyItem->setFlags(emptyItem->flags() & ~Qt::ItemIsSelectable);
      emptyItem->setForeground(QBrush(QColor("#808080")));
      listWidget->addItem(emptyItem);
    }

    // Select current value if it exists
    QString currentValue = edit->text();
    for (int i = 0; i < listWidget->count(); ++i) {
      QListWidgetItem* item = listWidget->item(i);
      if (item->data(Qt::UserRole).toString() == currentValue) {
        listWidget->setCurrentItem(item);
        break;
      }
    }

    dialogLayout->addWidget(listWidget);

    // Info label
    QLabel* infoLabel = new QLabel("Taught systems are stored in config/coordalign/taught_systems/");
    infoLabel->setStyleSheet("font-size: 10px; color: #808080; font-style: italic;");
    dialogLayout->addWidget(infoLabel);

    // Buttons
    QDialogButtonBox* buttonBox = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttonBox->setStyleSheet(
      "QPushButton { "
      "  background-color: #3c3c3c; "
      "  color: #d4d4d4; "
      "  border: 1px solid #555555; "
      "  border-radius: 3px; "
      "  padding: 6px 16px; "
      "  min-width: 80px; "
      "} "
      "QPushButton:hover { background-color: #4a4a4a; } "
      "QPushButton:default { background-color: #0e639c; border-color: #0e639c; }"
    );
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialogLayout->addWidget(buttonBox);

    // Double-click to accept
    connect(listWidget, &QListWidget::itemDoubleClicked, [&dialog](QListWidgetItem* item) {
      if (item && (item->flags() & Qt::ItemIsSelectable)) {
        dialog.accept();
      }
    });

    if (dialog.exec() == QDialog::Accepted) {
      QListWidgetItem* selectedItem = listWidget->currentItem();
      if (selectedItem && (selectedItem->flags() & Qt::ItemIsSelectable)) {
        QString uid = selectedItem->data(Qt::UserRole).toString();
        edit->setText(uid);
      }
    }
  });

  layout->addWidget(browseBtn);

  m_formLayout->addRow(QString::fromStdString(label) + ":", container);
  m_parameterEdits[key] = edit;
}

// Helper to add local point selector (with browse button)
void ProcessConfigWidget::addLocalPointSelector(const std::string& key,
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

  QPushButton* browseBtn = new QPushButton("Select...");
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
    // Create selection dialog
    QDialog dialog(this->window());
    dialog.setWindowTitle("Select Local Point");
    dialog.setMinimumSize(500, 400);
    dialog.setStyleSheet(
      "QDialog { background-color: #2d2d30; }"
      "QLabel { color: #d4d4d4; }"
    );

    QVBoxLayout* dialogLayout = new QVBoxLayout(&dialog);

    // Header
    QLabel* headerLabel = new QLabel("Available Taught Local Points:");
    headerLabel->setStyleSheet("font-weight: bold; font-size: 12px; color: #d4d4d4;");
    dialogLayout->addWidget(headerLabel);

    // List widget
    QListWidget* listWidget = new QListWidget();
    listWidget->setStyleSheet(
      "QListWidget { "
      "  background-color: #1e1e1e; "
      "  color: #d4d4d4; "
      "  border: 1px solid #3f3f46; "
      "  border-radius: 3px; "
      "  font-size: 12px; "
      "} "
      "QListWidget::item { "
      "  padding: 8px 10px; "
      "  border-bottom: 1px solid #2d2d30; "
      "} "
      "QListWidget::item:selected { "
      "  background-color: #0e639c; "
      "  color: #ffffff; "
      "} "
      "QListWidget::item:hover { "
      "  background-color: #2a2d2e; "
      "}"
    );

    // Load local points from disk
    std::filesystem::path localPointsDir = "config/coordalign/local_points";
    if (std::filesystem::exists(localPointsDir)) {
      for (const auto& entry : std::filesystem::directory_iterator(localPointsDir)) {
        if (entry.path().extension() == ".json") {
          std::string uid = entry.path().stem().string();

          // Try to read details from JSON
          QString displayText = QString::fromStdString(uid);
          QString tooltip;
          try {
            QFile file(QString::fromStdString(entry.path().string()));
            if (file.open(QIODevice::ReadOnly)) {
              QByteArray data = file.readAll();
              QJsonDocument doc = QJsonDocument::fromJson(data);
              if (doc.isObject()) {
                QJsonObject obj = doc.object();
                QString coordSys = obj.value("coord_system_uid").toString();
                QString desc = obj.value("description").toString();
                double localX = obj.value("local_x_mm").toDouble();
                double localY = obj.value("local_y_mm").toDouble();

                if (!coordSys.isEmpty()) {
                  displayText = QString("%1  [%2]").arg(QString::fromStdString(uid), coordSys);
                }
                tooltip = QString("Coord System: %1\nLocal: (%.3f, %.3f)")
                  .arg(coordSys)
                  .arg(localX)
                  .arg(localY);
                if (!desc.isEmpty()) {
                  tooltip += QString("\nDescription: %1").arg(desc);
                }
              }
              file.close();
            }
          } catch (...) {
            // Ignore parsing errors
          }

          QListWidgetItem* item = new QListWidgetItem(displayText);
          item->setData(Qt::UserRole, QString::fromStdString(uid));  // Store UID
          if (!tooltip.isEmpty()) {
            item->setToolTip(tooltip);
          }
          listWidget->addItem(item);
        }
      }
    }

    if (listWidget->count() == 0) {
      QListWidgetItem* emptyItem = new QListWidgetItem("(No local points found)");
      emptyItem->setFlags(emptyItem->flags() & ~Qt::ItemIsSelectable);
      emptyItem->setForeground(QBrush(QColor("#808080")));
      listWidget->addItem(emptyItem);
    }

    // Select current value if it exists
    QString currentValue = edit->text();
    for (int i = 0; i < listWidget->count(); ++i) {
      QListWidgetItem* item = listWidget->item(i);
      if (item->data(Qt::UserRole).toString() == currentValue) {
        listWidget->setCurrentItem(item);
        break;
      }
    }

    dialogLayout->addWidget(listWidget);

    // Info label
    QLabel* infoLabel = new QLabel("Local points are stored in config/coordalign/local_points/");
    infoLabel->setStyleSheet("font-size: 10px; color: #808080; font-style: italic;");
    dialogLayout->addWidget(infoLabel);

    // Buttons
    QDialogButtonBox* buttonBox = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttonBox->setStyleSheet(
      "QPushButton { "
      "  background-color: #3c3c3c; "
      "  color: #d4d4d4; "
      "  border: 1px solid #555555; "
      "  border-radius: 3px; "
      "  padding: 6px 16px; "
      "  min-width: 80px; "
      "} "
      "QPushButton:hover { background-color: #4a4a4a; } "
      "QPushButton:default { background-color: #0e639c; border-color: #0e639c; }"
    );
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialogLayout->addWidget(buttonBox);

    // Double-click to accept
    connect(listWidget, &QListWidget::itemDoubleClicked, [&dialog](QListWidgetItem* item) {
      if (item && (item->flags() & Qt::ItemIsSelectable)) {
        dialog.accept();
      }
    });

    if (dialog.exec() == QDialog::Accepted) {
      QListWidgetItem* selectedItem = listWidget->currentItem();
      if (selectedItem && (selectedItem->flags() & Qt::ItemIsSelectable)) {
        QString uid = selectedItem->data(Qt::UserRole).toString();
        edit->setText(uid);
      }
    }
  });

  layout->addWidget(browseBtn);

  m_formLayout->addRow(QString::fromStdString(label) + ":", container);
  m_parameterEdits[key] = edit;
}

void ProcessConfigWidget::addFileSelector(const std::string& key,
  const std::string& label,
  const std::string& value,
  const std::string& fileFilter)
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

  connect(browseBtn, &QPushButton::clicked, [this, edit, fileFilter]() {
    QString currentPath = edit->text();
    QString startDir = currentPath.isEmpty() ? QString() : QFileInfo(currentPath).absolutePath();
    QString selected = QFileDialog::getOpenFileName(
      this->window(), "Select File", startDir, QString::fromStdString(fileFilter));
    if (!selected.isEmpty()) {
      edit->setText(selected);
    }
  });

  layout->addWidget(browseBtn);

  m_formLayout->addRow(QString::fromStdString(label) + ":", container);
  m_parameterEdits[key] = edit;
}

void ProcessConfigWidget::addFolderSelector(const std::string& key,
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
    QString currentPath = edit->text();
    QString startDir = currentPath.isEmpty() ? QString() : currentPath;
    QString selected = QFileDialog::getExistingDirectory(
      this->window(), "Select Folder", startDir);
    if (!selected.isEmpty()) {
      edit->setText(selected);
    }
  });

  layout->addWidget(browseBtn);

  m_formLayout->addRow(QString::fromStdString(label) + ":", container);
  m_parameterEdits[key] = edit;
}

void ProcessConfigWidget::addTrainingSetSelector(const std::string& key,
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
    std::string currentSet = edit->text().toStdString();
    std::string selected = TrainingSetSelectorDialog::SelectTrainingSet(
      m_context, currentSet, this
    );
    if (!selected.empty()) {
      edit->setText(QString::fromStdString(selected));
    }
  });

  layout->addWidget(browseBtn);

  m_formLayout->addRow(QString::fromStdString(label) + ":", container);
  m_parameterEdits[key] = edit;
}

void ProcessConfigWidget::addCameraOffsetSetSelector(const std::string& key,
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
    std::string currentSet = edit->text().toStdString();
    std::string selected = CameraOffsetSetSelectorDialog::SelectCalibrationSet(
      m_context, currentSet, this
    );
    if (!selected.empty()) {
      edit->setText(QString::fromStdString(selected));
    }
  });

  layout->addWidget(browseBtn);

  m_formLayout->addRow(QString::fromStdString(label) + ":", container);
  m_parameterEdits[key] = edit;
}

void ProcessConfigWidget::addPlanarAdjustmentSetSelector(const std::string& key,
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
    std::string currentSet = edit->text().toStdString();
    std::string selected = PlanarAdjustmentSetSelectorDialog::SelectPlanarAdjustmentSet(
      m_context, currentSet, this
    );
    if (!selected.empty()) {
      edit->setText(QString::fromStdString(selected));
    }
  });

  layout->addWidget(browseBtn);

  m_formLayout->addRow(QString::fromStdString(label) + ":", container);
  m_parameterEdits[key] = edit;
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

  // ✅ ADDED: Include data channel selector values
  for (const auto& [key, combo] : m_dataChannelCombos) {
    params[key] = combo->currentText().toStdString();
  }

  // ✅ ADDED: Include vision profile selector values (full path)
  for (const auto& [key, combo] : m_visionProfileCombos) {
    params[key] = combo->getSelectedProfile();
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
// TABLE-BASED CONFIG WIDGET
// ============================================================================

TableBasedConfigWidget::TableBasedConfigWidget(ApplicationContext* context, const std::string& processType, QWidget* parent)
  : ProcessConfigWidget(context, parent)
  , m_processType(processType)
  , m_propertyTable(nullptr)
{
}

void TableBasedConfigWidget::createParameterFields(const ProcessInstance* instance)
{
  // Create the property table widget
  m_propertyTable = new PropertyTableWidget(m_context, this);
  m_formLayout->addRow(m_propertyTable);

  // Connect property changes to our parameterChanged signal
  // Use the no-argument overload of propertyChanged
  connect(m_propertyTable, QOverload<>::of(&PropertyTableWidget::propertyChanged),
    this, &ProcessConfigWidget::parameterChanged);

  // Get the plugin and have it create the table UI
  auto& registry = ProcessPluginRegistry::Instance();
  IProcessPlugin* plugin = registry.GetPlugin(m_processType);

  if (plugin) {
    plugin->CreateTableUI(m_propertyTable, instance);
  }
  else {
    std::cerr << "[TableBasedConfigWidget] No plugin found for process type: " << m_processType << std::endl;
  }
}

std::map<std::string, std::string> TableBasedConfigWidget::getParameters() const
{
  if (m_propertyTable) {
    return m_propertyTable->getParameters();
  }
  // Fall back to base class implementation
  return ProcessConfigWidget::getParameters();
}

// ============================================================================
// All old process-specific widget implementations have been removed.
// The plugin architecture (PluginBasedConfigWidget + IProcessPlugin) now
// handles all process configuration UIs dynamically.
//
// See plugin files in include/recipe/processes/ for process implementations.
// ============================================================================
