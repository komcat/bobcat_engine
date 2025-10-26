// ProcessConfigWidget.cpp
#include "ProcessConfigWidget.h"
#include "DeviceSelectorWidgets.h"
#include "ApplicationContext.h"
#include "PneumaticSlideSelectorDialog.h"  // Add this include at top
#include "DataStore.h"
#include "TECManager.h"
#include "RecipeManager.h"
#include <QLabel>
#include <QFrame>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QComboBox>

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
  m_parameterCheckboxes.clear();  // ← ADD THIS LINE

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
  m_nicknameEdit->setStyleSheet("font-weight: bold;");

  disconnect(m_nicknameEdit, &QLineEdit::textChanged, this, nullptr);
  connect(m_nicknameEdit, &QLineEdit::textChanged,
    this, &ProcessConfigWidget::nicknameChanged, Qt::QueuedConnection);

  m_formLayout->addRow("Process Name:", m_nicknameEdit);

  // Read-only fields
  QLineEdit* idEdit = new QLineEdit(QString::fromStdString(instance->uniqueId));
  idEdit->setReadOnly(true);
  idEdit->setStyleSheet("background-color: #f0f0f0; color: #666;");
  m_formLayout->addRow("Unique ID:", idEdit);

  QLineEdit* typeEdit = new QLineEdit(QString::fromStdString(instance->processType));
  typeEdit->setReadOnly(true);
  typeEdit->setStyleSheet("background-color: #f0f0f0; color: #666;");
  m_formLayout->addRow("Process Type:", typeEdit);

  // Divider
  QFrame* line2 = new QFrame();
  line2->setFrameShape(QFrame::HLine);
  line2->setStyleSheet("color: #ccc;");
  m_formLayout->addRow(line2);

  // Add title for parameters section
  QLabel* paramLabel = new QLabel("Parameters:");
  paramLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f;");
  m_formLayout->addRow(paramLabel);

  // Let derived class create specific parameter fields
  createParameterFields(instance);
}

void ProcessConfigWidget::addParameterField(const std::string& key,
  const std::string& label,
  const std::string& value)
{
  QLineEdit* edit = new QLineEdit(QString::fromStdString(value));
  edit->setStyleSheet("padding: 5px;");
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
  const std::string& deviceName)
{
  QWidget* container = new QWidget();
  QHBoxLayout* layout = new QHBoxLayout(container);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(5);

  QLineEdit* edit = new QLineEdit(QString::fromStdString(value));
  edit->setStyleSheet("padding: 5px;");
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

  connect(browseBtn, &QPushButton::clicked, [this, edit, deviceName]() {
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

std::map<std::string, std::string> ProcessConfigWidget::getParameters() const
{
  std::map<std::string, std::string> params;

  for (const auto& [key, edit] : m_parameterEdits) {
    params[key] = edit->text().toStdString();
  }

  for (const auto& [key, combo] : m_deviceCombos) {
    params[key] = combo->getSelectedDevice();
  }

  // ← ADD THIS BLOCK:
  for (const auto& [key, checkbox] : m_parameterCheckboxes) {
    params[key] = checkbox->isChecked() ? "true" : "false";
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
// PICK CONFIG WIDGET
// ============================================================================

PickConfigWidget::PickConfigWidget(ApplicationContext* context, QWidget* parent)
  : ProcessConfigWidget(context, parent)
{
}

void PickConfigWidget::createParameterFields(const ProcessInstance* instance)
{
  const auto& params = instance->parameters;

  QLabel* cameraLabel = new QLabel("Camera Configuration:");
  cameraLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(cameraLabel);

  addDeviceSelector("camera_device", "Camera Device",
    params.at("camera_device"),
    static_cast<int>(DeviceComboBox::DeviceType::Camera));

  addParameterField("lens_name", "Lens Name", params.at("lens_name"));

  addNodeSelector("view_node", "View Node",
    params.at("view_node"),
    params.at("camera_device"));

  m_formLayout->addRow(new QLabel(""));

  QLabel* gripperLabel = new QLabel("Gripper Configuration:");
  gripperLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(gripperLabel);

  addDeviceSelector("gripper_device", "Gripper Device",
    params.at("gripper_device"),
    static_cast<int>(DeviceComboBox::DeviceType::Motion));

  addNodeSelector("pick_node", "Pick Node",
    params.at("pick_node"),
    params.at("gripper_device"));

  m_formLayout->addRow(new QLabel(""));

  QLabel* ioLabel = new QLabel("IO Configuration:");
  ioLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(ioLabel);

  addDeviceSelector("io_device", "IO Device",
    params.at("io_device"),
    static_cast<int>(DeviceComboBox::DeviceType::IO));

  addParameterField("gripper_pin", "Gripper Pin", params.at("gripper_pin"));

  m_formLayout->addRow(new QLabel(""));

  QLabel* timingLabel = new QLabel("Timing:");
  timingLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(timingLabel);

  addParameterField("regrip_wait_ms", "Regrip Wait (ms)", params.at("regrip_wait_ms"));
  addParameterField("release_wait_ms", "Release Wait (ms)", params.at("release_wait_ms"));
}

// ============================================================================
// PLACE CONFIG WIDGET
// ============================================================================

PlaceConfigWidget::PlaceConfigWidget(ApplicationContext* context, QWidget* parent)
  : ProcessConfigWidget(context, parent)
{
}

void PlaceConfigWidget::createParameterFields(const ProcessInstance* instance)
{
  const auto& params = instance->parameters;

  QLabel* cameraLabel = new QLabel("Camera Configuration:");
  cameraLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(cameraLabel);

  addDeviceSelector("camera_device", "Camera Device",
    params.at("camera_device"),
    static_cast<int>(DeviceComboBox::DeviceType::Camera));

  addParameterField("lens_name", "Lens Name", params.at("lens_name"));

  addNodeSelector("view_node", "View Node",
    params.at("view_node"),
    params.at("camera_device"));

  m_formLayout->addRow(new QLabel(""));

  QLabel* gripperLabel = new QLabel("Gripper Configuration:");
  gripperLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(gripperLabel);

  addDeviceSelector("gripper_device", "Gripper Device",
    params.at("gripper_device"),
    static_cast<int>(DeviceComboBox::DeviceType::Motion));

  addNodeSelector("place_node", "Place Node",
    params.at("place_node"),
    params.at("gripper_device"));

  m_formLayout->addRow(new QLabel(""));

  QLabel* ioLabel = new QLabel("IO Configuration:");
  ioLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(ioLabel);

  addDeviceSelector("io_device", "IO Device",
    params.at("io_device"),
    static_cast<int>(DeviceComboBox::DeviceType::IO));

  addParameterField("gripper_pin", "Gripper Pin", params.at("gripper_pin"));
}

// ============================================================================
// PICK AND PLACE CONFIG WIDGET
// ============================================================================

PickAndPlaceConfigWidget::PickAndPlaceConfigWidget(ApplicationContext* context, QWidget* parent)
  : ProcessConfigWidget(context, parent)
{
}

void PickAndPlaceConfigWidget::createParameterFields(const ProcessInstance* instance)
{
  const auto& params = instance->parameters;

  QLabel* cameraLabel = new QLabel("Camera Configuration:");
  cameraLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(cameraLabel);

  addDeviceSelector("camera_device", "Camera Device",
    params.at("camera_device"),
    static_cast<int>(DeviceComboBox::DeviceType::Camera));

  addParameterField("lens_name", "Lens Name", params.at("lens_name"));

  addNodeSelector("pick_view_node", "Pick View Node",
    params.at("pick_view_node"),
    params.at("camera_device"));

  addNodeSelector("place_view_node", "Place View Node",
    params.at("place_view_node"),
    params.at("camera_device"));

  m_formLayout->addRow(new QLabel(""));

  QLabel* gripperLabel = new QLabel("Gripper Configuration:");
  gripperLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(gripperLabel);

  addDeviceSelector("gripper_device", "Gripper Device",
    params.at("gripper_device"),
    static_cast<int>(DeviceComboBox::DeviceType::Motion));

  addNodeSelector("pick_node", "Pick Node",
    params.at("pick_node"),
    params.at("gripper_device"));

  addNodeSelector("place_node", "Place Node",
    params.at("place_node"),
    params.at("gripper_device"));

  m_formLayout->addRow(new QLabel(""));

  QLabel* ioLabel = new QLabel("IO Configuration:");
  ioLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(ioLabel);

  addDeviceSelector("io_device", "IO Device",
    params.at("io_device"),
    static_cast<int>(DeviceComboBox::DeviceType::IO));

  addParameterField("gripper_pin", "Gripper Pin", params.at("gripper_pin"));

  m_formLayout->addRow(new QLabel(""));

  QLabel* timingLabel = new QLabel("Timing:");
  timingLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(timingLabel);

  addParameterField("regrip_wait_ms", "Regrip Wait (ms)", params.at("regrip_wait_ms"));
  addParameterField("release_wait_ms", "Release Wait (ms)", params.at("release_wait_ms"));
}

// ============================================================================
// MOVE TO POSITION CONFIG WIDGET
// ============================================================================

MoveToPositionConfigWidget::MoveToPositionConfigWidget(ApplicationContext* context, QWidget* parent)
  : ProcessConfigWidget(context, parent)
{
}

void MoveToPositionConfigWidget::createParameterFields(const ProcessInstance* instance)
{
  const auto& params = instance->parameters;

  QLabel* deviceLabel = new QLabel("Device Configuration:");
  deviceLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(deviceLabel);

  addDeviceSelector("device", "Motion Device",
    params.at("device"),
    static_cast<int>(DeviceComboBox::DeviceType::Motion));

  addNodeSelector("target_position", "Target Position",
    params.at("target_position"),
    params.at("device"));
}

// ============================================================================
// MOVE RELATIVE CONFIG WIDGET
// ============================================================================

MoveRelativeConfigWidget::MoveRelativeConfigWidget(ApplicationContext* context, QWidget* parent)
  : ProcessConfigWidget(context, parent)
{
}

void MoveRelativeConfigWidget::createParameterFields(const ProcessInstance* instance)
{
  const auto& params = instance->parameters;

  QLabel* deviceLabel = new QLabel("Device Configuration:");
  deviceLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(deviceLabel);

  addDeviceSelector("device", "Motion Device",
    params.at("device"),
    static_cast<int>(DeviceComboBox::DeviceType::Motion));

  m_formLayout->addRow(new QLabel(""));

  QLabel* moveLabel = new QLabel("Movement Parameters:");
  moveLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(moveLabel);

  addParameterField("axis", "Axis (X/Y/Z/U/V/W)", params.at("axis"));
  addParameterField("distance", "Distance (mm)", params.at("distance"));
}

// ============================================================================
// SET OUTPUT CONFIG WIDGET
// ============================================================================

SetOutputConfigWidget::SetOutputConfigWidget(ApplicationContext* context, QWidget* parent)
  : ProcessConfigWidget(context, parent)
{
}

void SetOutputConfigWidget::createParameterFields(const ProcessInstance* instance)
{
  const auto& params = instance->parameters;

  QLabel* ioLabel = new QLabel("IO Configuration:");
  ioLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(ioLabel);

  addDeviceSelector("io_device", "IO Device",
    params.at("io_device"),
    static_cast<int>(DeviceComboBox::DeviceType::IO));

  addParameterField("pin", "Pin Number", params.at("pin"));
  addParameterField("state", "State (true/false)", params.at("state"));
}

// ============================================================================
// WAIT CONFIG WIDGET
// ============================================================================

WaitConfigWidget::WaitConfigWidget(ApplicationContext* context, QWidget* parent)
  : ProcessConfigWidget(context, parent)
{
}

void WaitConfigWidget::createParameterFields(const ProcessInstance* instance)
{
  const auto& params = instance->parameters;

  QLabel* timingLabel = new QLabel("Timing:");
  timingLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(timingLabel);

  addParameterField("duration_ms", "Duration (milliseconds)", params.at("duration_ms"));
}



// ============================================================================
// AskUserConfigWidget
// ============================================================================
AskUserConfigWidget::AskUserConfigWidget(ApplicationContext* context, QWidget* parent)
  : ProcessConfigWidget(context, parent)
{
}

void AskUserConfigWidget::createParameterFields(const ProcessInstance* instance)
{
  addParameterField("title", "Dialog Title",
    instance->parameters.at("title"));

  addParameterField("message", "Message Text",
    instance->parameters.at("message"));

  // Add checkbox for blocking mode
  QCheckBox* blockingCheckbox = new QCheckBox("Wait for user response (blocking)");
  blockingCheckbox->setChecked(instance->parameters.at("blocking") == "true");
  blockingCheckbox->setToolTip("When checked: sequence pauses until user clicks Continue/Cancel.\n"
    "When unchecked: dialog shows but sequence continues immediately.");

  connect(blockingCheckbox, &QCheckBox::toggled, this, [this]() {
    emit parameterChanged();
  });

  m_formLayout->addRow("Blocking Mode:", blockingCheckbox);

  // ← ADD THIS LINE to store the checkbox:
  m_parameterCheckboxes["blocking"] = blockingCheckbox;
}

// ============================================================================
// LOG MESSAGE CONFIG WIDGET
// ============================================================================
LogMessageConfigWidget::LogMessageConfigWidget(ApplicationContext* context, QWidget* parent)
  : ProcessConfigWidget(context, parent)
{
}

void LogMessageConfigWidget::createParameterFields(const ProcessInstance* instance)
{
  const auto& params = instance->parameters;

  QLabel* messageLabel = new QLabel("Message:");
  messageLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(messageLabel);

  // Message text field
  std::string message = params.count("message") ? params.at("message") : "Log message here...";
  QLineEdit* messageEdit = new QLineEdit(QString::fromStdString(message));
  messageEdit->setPlaceholderText("Enter the message to log");
  messageEdit->setStyleSheet("padding: 5px;");
  connect(messageEdit, &QLineEdit::textChanged, this, &ProcessConfigWidget::parameterChanged);
  m_formLayout->addRow("Message Text:", messageEdit);
  m_parameterEdits["message"] = messageEdit;

  // Log level dropdown
  std::string logLevel = params.count("log_level") ? params.at("log_level") : "Info";
  QLineEdit* levelEdit = new QLineEdit(QString::fromStdString(logLevel));
  m_parameterEdits["log_level"] = levelEdit;

  QComboBox* levelCombo = new QComboBox();
  levelCombo->addItem("Info");
  levelCombo->addItem("Debug");
  levelCombo->addItem("Warning");
  levelCombo->addItem("Error");
  levelCombo->setCurrentText(QString::fromStdString(logLevel));

  connect(levelCombo, &QComboBox::currentTextChanged, [this, levelEdit](const QString& text) {
    levelEdit->setText(text);
    emit parameterChanged();
  });
  m_formLayout->addRow("Log Level:", levelCombo);

  // Info label
  QLabel* infoLabel = new QLabel(
    "<i>Prints a message to the log. Useful for debugging and tracking execution flow in conditionals.</i>"
  );
  infoLabel->setWordWrap(true);
  infoLabel->setStyleSheet("color: #666; font-size: 10px; margin-top: 8px;");
  m_formLayout->addRow("", infoLabel);
}

// ============================================================================
// GROUP START CONFIG WIDGET
// ============================================================================
GroupStartConfigWidget::GroupStartConfigWidget(ApplicationContext* context, QWidget* parent)
  : ProcessConfigWidget(context, parent)
{
}

void GroupStartConfigWidget::createParameterFields(const ProcessInstance* instance)
{
  const auto& params = instance->parameters;

  QLabel* groupLabel = new QLabel("Group Settings:");
  groupLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(groupLabel);

  // Group name field
  std::string groupName = params.count("group_name") ? params.at("group_name") : "Process Group";
  QLineEdit* nameEdit = new QLineEdit(QString::fromStdString(groupName));
  nameEdit->setPlaceholderText("Enter group name");
  nameEdit->setStyleSheet("padding: 5px;");
  connect(nameEdit, &QLineEdit::textChanged, this, &ProcessConfigWidget::parameterChanged);
  m_formLayout->addRow("Group Name:", nameEdit);
  m_parameterEdits["group_name"] = nameEdit;

  // Info label
  QLabel* infoLabel = new QLabel(
    "<i><b>Group Start</b><br>"
    "Begins a collapsible group. All steps until the matching 'Group End' will be indented.</i>"
  );
  infoLabel->setWordWrap(true);
  infoLabel->setStyleSheet("color: #2d3e5f; font-size: 11px; margin-top: 8px;");
  m_formLayout->addRow("", infoLabel);
}

// ============================================================================
// GROUP END CONFIG WIDGET
// ============================================================================
GroupEndConfigWidget::GroupEndConfigWidget(ApplicationContext* context, QWidget* parent)
  : ProcessConfigWidget(context, parent)
{
}

void GroupEndConfigWidget::createParameterFields(const ProcessInstance* instance)
{
  // No parameters needed - just visual indicator
  QLabel* infoLabel = new QLabel(
    "<i><b>Group End</b><br>"
    "Closes the current group. Steps after this will return to the previous indent level.</i>"
  );
  infoLabel->setWordWrap(true);
  infoLabel->setStyleSheet("color: #2d3e5f; font-size: 11px; margin-top: 8px;");
  m_formLayout->addRow("", infoLabel);
}

// ============================================================================
// MOVE RELATIVE MULTI-AXIS CONFIG WIDGET
// ============================================================================

MoveRelativeMultiAxisConfigWidget::MoveRelativeMultiAxisConfigWidget(
  ApplicationContext* context, QWidget* parent)
  : ProcessConfigWidget(context, parent)
{
}

void MoveRelativeMultiAxisConfigWidget::createParameterFields(const ProcessInstance* instance)
{
  const auto& params = instance->parameters;

  QLabel* deviceLabel = new QLabel("Device Configuration:");
  deviceLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(deviceLabel);

  addDeviceSelector("device", "Motion Device",
    params.at("device"),
    static_cast<int>(DeviceComboBox::DeviceType::Motion));

  m_formLayout->addRow(new QLabel(""));

  QLabel* moveLabel = new QLabel("Movement Parameters:");
  moveLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(moveLabel);

  addParameterField("distance_x", "X Distance (mm, 0=no move)", params.at("distance_x"));
  addParameterField("distance_y", "Y Distance (mm, 0=no move)", params.at("distance_y"));
  addParameterField("distance_z", "Z Distance (mm, 0=no move)", params.at("distance_z"));
}

// ============================================================================
// MOVE TO POSITION WITH OFFSET CONFIG WIDGET
// ============================================================================

MoveToPositionWithOffsetConfigWidget::MoveToPositionWithOffsetConfigWidget(
  ApplicationContext* context, QWidget* parent)
  : ProcessConfigWidget(context, parent)
{
}

void MoveToPositionWithOffsetConfigWidget::createParameterFields(const ProcessInstance* instance)
{
  const auto& params = instance->parameters;

  QLabel* deviceLabel = new QLabel("Device Configuration:");
  deviceLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(deviceLabel);

  addDeviceSelector("device", "Motion Device",
    params.at("device"),
    static_cast<int>(DeviceComboBox::DeviceType::Motion));

  m_formLayout->addRow(new QLabel(""));

  QLabel* positionLabel = new QLabel("Base Position:");
  positionLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(positionLabel);

  addNodeSelector("base_position", "Base Position",
    params.at("base_position"),
    params.at("device"));

  m_formLayout->addRow(new QLabel(""));

  QLabel* offsetLabel = new QLabel("Offset Parameters:");
  offsetLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(offsetLabel);

  addParameterField("offset_x", "X Offset (mm, 0=no offset)", params.at("offset_x"));
  addParameterField("offset_y", "Y Offset (mm, 0=no offset)", params.at("offset_y"));
  addParameterField("offset_z", "Z Offset (mm, 0=no offset)", params.at("offset_z"));
}

// ============================================================================
// SLIDE EXTEND CONFIG WIDGET
// ============================================================================

SlideExtendConfigWidget::SlideExtendConfigWidget(ApplicationContext* context, QWidget* parent)
  : ProcessConfigWidget(context, parent)
{
}


void SlideExtendConfigWidget::createParameterFields(const ProcessInstance* instance)
{
  const auto& params = instance->parameters;

  // ✅ ADD THIS SECTION:
  QLabel* deviceLabel = new QLabel("Device Configuration:");
  deviceLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(deviceLabel);

  addDeviceSelector("device", "Device",
    params.count("device") ? params.at("device") : "pneumatic",  // ← Handle missing key
    static_cast<int>(DeviceComboBox::DeviceType::Motion));

  m_formLayout->addRow(new QLabel(""));
  // ✅ END NEW SECTION

  QLabel* slideLabel = new QLabel("Pneumatic Slide:");
  slideLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(slideLabel);

  // ✅ Use new slide selector with browse button
  addSlideSelector("slide_name", "Slide Name", params.at("slide_name"));

  m_formLayout->addRow(new QLabel(""));

  QLabel* timingLabel = new QLabel("Timing Parameters:");
  timingLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(timingLabel);

  addParameterField("inbounce_delay_ms", "Inbounce Delay (ms, before action)", params.at("inbounce_delay_ms"));
  addParameterField("timeout_ms", "Timeout (ms)", params.at("timeout_ms"));
  addParameterField("debounce_delay_ms", "Debounce Delay (ms, after action)", params.at("debounce_delay_ms"));
}
// ============================================================================
// SLIDE RETRACT CONFIG WIDGET
// ============================================================================

SlideRetractConfigWidget::SlideRetractConfigWidget(ApplicationContext* context, QWidget* parent)
  : ProcessConfigWidget(context, parent)
{
}

void SlideRetractConfigWidget::createParameterFields(const ProcessInstance* instance)
{
  const auto& params = instance->parameters;

  // ✅ ADD DEVICE CONFIGURATION SECTION:
  QLabel* deviceLabel = new QLabel("Device Configuration:");
  deviceLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(deviceLabel);

  addDeviceSelector("device", "Device",
    params.count("device") ? params.at("device") : "pneumatic",
    static_cast<int>(DeviceComboBox::DeviceType::Motion));

  m_formLayout->addRow(new QLabel(""));
  // ✅ END NEW SECTION

  QLabel* slideLabel = new QLabel("Pneumatic Slide:");
  slideLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(slideLabel);

  // ✅ Use new slide selector with browse button
  addSlideSelector("slide_name", "Slide Name", params.at("slide_name"));

  m_formLayout->addRow(new QLabel(""));

  QLabel* timingLabel = new QLabel("Timing Parameters:");
  timingLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(timingLabel);

  addParameterField("inbounce_delay_ms", "Inbounce Delay (ms, before action)", params.at("inbounce_delay_ms"));
  addParameterField("timeout_ms", "Timeout (ms)", params.at("timeout_ms"));
  addParameterField("debounce_delay_ms", "Debounce Delay (ms, after action)", params.at("debounce_delay_ms"));
}

// Add this method implementation:
void ProcessConfigWidget::addSlideSelector(const std::string& key,
  const std::string& label,
  const std::string& value)
{
  QWidget* container = new QWidget();
  QHBoxLayout* layout = new QHBoxLayout(container);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(5);

  QLineEdit* edit = new QLineEdit(QString::fromStdString(value));
  edit->setStyleSheet("padding: 5px;");
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
    edit->setText(QString::fromStdString(selected));
  });

  layout->addWidget(browseBtn);

  m_formLayout->addRow(QString::fromStdString(label) + ":", container);
  m_parameterEdits[key] = edit;
}


// ============================================================================
// SET TEC TEMP CONFIG WIDGET
// ============================================================================

SetTECTempConfigWidget::SetTECTempConfigWidget(ApplicationContext* context, QWidget* parent)
  : ProcessConfigWidget(context, parent)
{
}

void SetTECTempConfigWidget::createParameterFields(const ProcessInstance* instance)
{
  const auto& params = instance->parameters;

  QLabel* tempLabel = new QLabel("Temperature Control:");
  tempLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(tempLabel);

  addParameterField("device", "Device Name", params.at("device"));
  addParameterField("temperature", "Set Temperature (°C)", params.at("temperature"));
  addParameterField("power_state", "Power State (on/off)", params.at("power_state"));
}

// ============================================================================
// SET LASER CURRENT CONFIG WIDGET
// ============================================================================

SetLaserCurrentConfigWidget::SetLaserCurrentConfigWidget(ApplicationContext* context, QWidget* parent)
  : ProcessConfigWidget(context, parent)
{
}

void SetLaserCurrentConfigWidget::createParameterFields(const ProcessInstance* instance)
{
  const auto& params = instance->parameters;

  QLabel* laserLabel = new QLabel("Laser Control:");
  laserLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(laserLabel);

  addParameterField("device", "Device Name", params.at("device"));
  addParameterField("current_ma", "Laser Current (mA)", params.at("current_ma"));
  addParameterField("power_state", "Power State (on/off)", params.at("power_state"));
}



//=============================================================================
// READ DATASTORE CONFIG WIDGET
//=============================================================================

ReadDataStoreConfigWidget::ReadDataStoreConfigWidget(ApplicationContext* context, QWidget* parent)
  : ProcessConfigWidget(context, parent)
{
}

void ReadDataStoreConfigWidget::createParameterFields(const ProcessInstance* instance)
{
  if (!instance) return;

  const auto& params = instance->parameters;

  // Channel Name - text field with available channels hint
  std::string channelName = params.count("channel_name") ? params.at("channel_name") : "Voltage";

  QLineEdit* channelEdit = new QLineEdit(QString::fromStdString(channelName));
  channelEdit->setPlaceholderText("e.g., Voltage, Current, Temperature");

  // Show available channels as tooltip if DataStore exists
  if (m_context && m_context->dataStore) {
    auto availableChannels = m_context->dataStore->GetAvailableChannels();
    if (!availableChannels.empty()) {
      QString tooltip = "Available channels:\n";
      for (const auto& ch : availableChannels) {
        tooltip += "  • " + QString::fromStdString(ch) + "\n";
      }
      channelEdit->setToolTip(tooltip);
    }
  }

  connect(channelEdit, &QLineEdit::textChanged, this, &ProcessConfigWidget::parameterChanged);
  m_formLayout->addRow("Channel Name:", channelEdit);
  m_parameterEdits["channel_name"] = channelEdit;

  // Log Level - dropdown
  std::string logLevel = params.count("log_level") ? params.at("log_level") : "Info";

  QComboBox* logLevelCombo = new QComboBox();
  logLevelCombo->addItem("Debug");
  logLevelCombo->addItem("Info");
  logLevelCombo->addItem("Warning");
  logLevelCombo->setCurrentText(QString::fromStdString(logLevel));

  connect(logLevelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
    this, &ProcessConfigWidget::parameterChanged);
  m_formLayout->addRow("Log Level:", logLevelCombo);

  // Store in a hidden line edit so getParameters() works
  QLineEdit* logLevelEdit = new QLineEdit(QString::fromStdString(logLevel));
  logLevelEdit->setVisible(false);
  connect(logLevelCombo, &QComboBox::currentTextChanged, [logLevelEdit](const QString& text) {
    logLevelEdit->setText(text);
  });
  m_parameterEdits["log_level"] = logLevelEdit;

  // Context capture field (optional)
  std::string captureAs = params.count("captureAs") ? params.at("captureAs") : "";
  QLineEdit* captureEdit = new QLineEdit(QString::fromStdString(captureAs));
  captureEdit->setPlaceholderText("Optional: Variable name to store DataStore value");
  captureEdit->setStyleSheet("padding: 5px;");
  connect(captureEdit, &QLineEdit::textChanged, this, &ProcessConfigWidget::parameterChanged);
  m_formLayout->addRow("Capture As:", captureEdit);
  m_parameterEdits["captureAs"] = captureEdit;

  // Add info label
  QLabel* infoLabel = new QLabel(
    "<i>Reads the specified channel from DataStore and logs its value. "
    "If 'Capture As' is set, the value will be stored in the execution context.</i>"
  );
  infoLabel->setWordWrap(true);
  infoLabel->setStyleSheet("color: #666; font-size: 10px; margin-top: 8px;");
  m_formLayout->addRow("", infoLabel);
}


//=============================================================================
// READ TEMPERATURE CONFIG WIDGET
//=============================================================================
ReadTemperatureConfigWidget::ReadTemperatureConfigWidget(ApplicationContext* context, QWidget* parent)
  : ProcessConfigWidget(context, parent)
{
}

void ReadTemperatureConfigWidget::createParameterFields(const ProcessInstance* instance)
{
  if (!instance) return;
  const auto& params = instance->parameters;

  // TEC Device - dropdown
  std::string tecDevice = params.count("tec_device") ? params.at("tec_device") : "TEC1";

  // Create the line edit first (this is what gets saved)
  QLineEdit* deviceEdit = new QLineEdit(QString::fromStdString(tecDevice));
  m_parameterEdits["tec_device"] = deviceEdit;

  // Create combo box
  QComboBox* deviceCombo = new QComboBox();

  // Populate with available TEC devices
  if (m_context && m_context->tecManager) {
    auto devices = m_context->tecManager->GetAllDeviceNames();
    for (const auto& device : devices) {
      deviceCombo->addItem(QString::fromStdString(device));
    }
    deviceCombo->setCurrentText(QString::fromStdString(tecDevice));
  }
  else {
    deviceCombo->addItem(QString::fromStdString(tecDevice));
  }

  // Connect combo to line edit AND signal parameter changed
  connect(deviceCombo, &QComboBox::currentTextChanged, [this, deviceEdit](const QString& text) {
    deviceEdit->setText(text);
    emit parameterChanged();  // ← THIS IS CRITICAL
  });

  m_formLayout->addRow("TEC Device:", deviceCombo);

  // Log Level - dropdown
  std::string logLevel = params.count("log_level") ? params.at("log_level") : "Info";

  // Create the line edit first
  QLineEdit* logLevelEdit = new QLineEdit(QString::fromStdString(logLevel));
  m_parameterEdits["log_level"] = logLevelEdit;

  QComboBox* logLevelCombo = new QComboBox();
  logLevelCombo->addItem("Debug");
  logLevelCombo->addItem("Info");
  logLevelCombo->addItem("Warning");
  logLevelCombo->setCurrentText(QString::fromStdString(logLevel));

  connect(logLevelCombo, &QComboBox::currentTextChanged, [this, logLevelEdit](const QString& text) {
    logLevelEdit->setText(text);
    emit parameterChanged();  // ← THIS IS CRITICAL
  });

  m_formLayout->addRow("Log Level:", logLevelCombo);

  // Context capture field (optional)
  std::string captureAs = params.count("captureAs") ? params.at("captureAs") : "";
  QLineEdit* captureEdit = new QLineEdit(QString::fromStdString(captureAs));
  captureEdit->setPlaceholderText("Optional: Variable name to store temperature");
  captureEdit->setStyleSheet("padding: 5px;");
  connect(captureEdit, &QLineEdit::textChanged, this, &ProcessConfigWidget::parameterChanged);
  m_formLayout->addRow("Capture As:", captureEdit);
  m_parameterEdits["captureAs"] = captureEdit;

  // Info label
  QLabel* infoLabel = new QLabel(
    "<i>Reads the current temperature from the selected TEC device and logs it. "
    "If 'Capture As' is set, the temperature will be stored in the execution context.</i>"
  );
  infoLabel->setWordWrap(true);
  infoLabel->setStyleSheet("color: #666; font-size: 10px; margin-top: 8px;");
  m_formLayout->addRow("", infoLabel);
}


//=============================================================================
// READ LASER CURRENT CONFIG WIDGET
//=============================================================================

ReadLaserCurrentConfigWidget::ReadLaserCurrentConfigWidget(ApplicationContext* context, QWidget* parent)
  : ProcessConfigWidget(context, parent)
{
}


void ReadLaserCurrentConfigWidget::createParameterFields(const ProcessInstance* instance)
{
  if (!instance) return;
  const auto& params = instance->parameters;

  // TEC Device - dropdown
  std::string tecDevice = params.count("tec_device") ? params.at("tec_device") : "TEC1";

  // Create the line edit first (this is what gets saved)
  QLineEdit* deviceEdit = new QLineEdit(QString::fromStdString(tecDevice));
  m_parameterEdits["tec_device"] = deviceEdit;

  // Create combo box
  QComboBox* deviceCombo = new QComboBox();

  // Populate with available TEC devices
  if (m_context && m_context->tecManager) {
    auto devices = m_context->tecManager->GetAllDeviceNames();
    for (const auto& device : devices) {
      deviceCombo->addItem(QString::fromStdString(device));
    }
    deviceCombo->setCurrentText(QString::fromStdString(tecDevice));
  }
  else {
    deviceCombo->addItem(QString::fromStdString(tecDevice));
  }

  // Connect combo to line edit AND signal parameter changed
  connect(deviceCombo, &QComboBox::currentTextChanged, [this, deviceEdit](const QString& text) {
    deviceEdit->setText(text);
    emit parameterChanged();  // ← THIS IS CRITICAL
  });

  m_formLayout->addRow("TEC Device:", deviceCombo);

  // Log Level - dropdown
  std::string logLevel = params.count("log_level") ? params.at("log_level") : "Info";

  // Create the line edit first
  QLineEdit* logLevelEdit = new QLineEdit(QString::fromStdString(logLevel));
  m_parameterEdits["log_level"] = logLevelEdit;

  QComboBox* logLevelCombo = new QComboBox();
  logLevelCombo->addItem("Debug");
  logLevelCombo->addItem("Info");
  logLevelCombo->addItem("Warning");
  logLevelCombo->setCurrentText(QString::fromStdString(logLevel));

  connect(logLevelCombo, &QComboBox::currentTextChanged, [this, logLevelEdit](const QString& text) {
    logLevelEdit->setText(text);
    emit parameterChanged();  // ← THIS IS CRITICAL
  });

  m_formLayout->addRow("Log Level:", logLevelCombo);

  // Context capture field (optional)
  std::string captureAs = params.count("captureAs") ? params.at("captureAs") : "";
  QLineEdit* captureEdit = new QLineEdit(QString::fromStdString(captureAs));
  captureEdit->setPlaceholderText("Optional: Variable name to store laser current");
  captureEdit->setStyleSheet("padding: 5px;");
  connect(captureEdit, &QLineEdit::textChanged, this, &ProcessConfigWidget::parameterChanged);
  m_formLayout->addRow("Capture As:", captureEdit);
  m_parameterEdits["captureAs"] = captureEdit;

  // Info label
  QLabel* infoLabel = new QLabel(
    "<i>Reads the current laser current from the selected TEC device and logs it. "
    "If 'Capture As' is set, the laser current will be stored in the execution context.</i>"
  );
  infoLabel->setWordWrap(true);
  infoLabel->setStyleSheet("color: #666; font-size: 10px; margin-top: 8px;");
  m_formLayout->addRow("", infoLabel);
}

// ============================================================================
// PSU OUTPUT ON/OFF CONFIG WIDGET
// ============================================================================

PSUOutputConfigWidget::PSUOutputConfigWidget(ApplicationContext* context, QWidget* parent)
  : ProcessConfigWidget(context, parent)
{
}

void PSUOutputConfigWidget::createParameterFields(const ProcessInstance* instance)
{
  const auto& params = instance->parameters;

  QLabel* psuLabel = new QLabel("PSU Control:");
  psuLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(psuLabel);

  addParameterField("psu_device", "PSU Device Name", params.at("psu_device"));

  // Output State - dropdown
  std::string outputState = params.at("output_state");
  QLineEdit* stateEdit = new QLineEdit(QString::fromStdString(outputState));
  m_parameterEdits["output_state"] = stateEdit;

  QComboBox* stateCombo = new QComboBox();
  stateCombo->addItem("on");
  stateCombo->addItem("off");
  stateCombo->setCurrentText(QString::fromStdString(outputState));

  connect(stateCombo, &QComboBox::currentTextChanged, [this, stateEdit](const QString& text) {
    stateEdit->setText(text);
    emit parameterChanged();
  });

  m_formLayout->addRow("Output State:", stateCombo);

  // Info label
  QLabel* infoLabel = new QLabel(
    "<i>Turns the PSU output ON or OFF.</i>"
  );
  infoLabel->setWordWrap(true);
  infoLabel->setStyleSheet("color: #666; font-size: 10px; margin-top: 8px;");
  m_formLayout->addRow("", infoLabel);
}

// ============================================================================
// PSU SET VOLTAGE CONFIG WIDGET
// ============================================================================

PSUSetVoltageConfigWidget::PSUSetVoltageConfigWidget(ApplicationContext* context, QWidget* parent)
  : ProcessConfigWidget(context, parent)
{
}

void PSUSetVoltageConfigWidget::createParameterFields(const ProcessInstance* instance)
{
  const auto& params = instance->parameters;

  QLabel* psuLabel = new QLabel("PSU Voltage Control (CV Mode):");
  psuLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(psuLabel);

  addParameterField("psu_device", "PSU Device Name", params.at("psu_device"));
  addParameterField("voltage", "Voltage (V)", params.at("voltage"));

  // Info label
  QLabel* infoLabel = new QLabel(
    "<i>Sets the PSU voltage in Constant Voltage (CV) mode. Default: 3.0V</i>"
  );
  infoLabel->setWordWrap(true);
  infoLabel->setStyleSheet("color: #666; font-size: 10px; margin-top: 8px;");
  m_formLayout->addRow("", infoLabel);
}

// ============================================================================
// PSU SET CURRENT CONFIG WIDGET
// ============================================================================

PSUSetCurrentConfigWidget::PSUSetCurrentConfigWidget(ApplicationContext* context, QWidget* parent)
  : ProcessConfigWidget(context, parent)
{
}

void PSUSetCurrentConfigWidget::createParameterFields(const ProcessInstance* instance)
{
  const auto& params = instance->parameters;

  QLabel* psuLabel = new QLabel("PSU Current Control (CC Mode):");
  psuLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(psuLabel);

  addParameterField("psu_device", "PSU Device Name", params.at("psu_device"));
  addParameterField("current", "Current (mA)", params.at("current"));

  // Info label
  QLabel* infoLabel = new QLabel(
    "<i>Sets the PSU current in Constant Current (CC) mode. Default: 150mA</i>"
  );
  infoLabel->setWordWrap(true);
  infoLabel->setStyleSheet("color: #666; font-size: 10px; margin-top: 8px;");
  m_formLayout->addRow("", infoLabel);
}

// ============================================================================
// PSU READ VOLTAGE CONFIG WIDGET
// ============================================================================

PSUReadVoltageConfigWidget::PSUReadVoltageConfigWidget(ApplicationContext* context, QWidget* parent)
  : ProcessConfigWidget(context, parent)
{
}

void PSUReadVoltageConfigWidget::createParameterFields(const ProcessInstance* instance)
{
  const auto& params = instance->parameters;

  QLabel* psuLabel = new QLabel("PSU Voltage Reading:");
  psuLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(psuLabel);

  addParameterField("psu_device", "PSU Device Name", params.at("psu_device"));

  QLabel* readingLabel = new QLabel("Reading Parameters:");
  readingLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 10px;");
  m_formLayout->addRow(readingLabel);

  addParameterField("read_count", "Number of Reads", params.at("read_count"));
  addParameterField("delay_ms", "Delay Between Reads (ms)", params.at("delay_ms"));

  // Context capture field (optional)
  QLabel* captureLabel = new QLabel("Context Capture:");
  captureLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 10px;");
  m_formLayout->addRow(captureLabel);

  std::string captureAs = params.count("captureAs") ? params.at("captureAs") : "";
  QLineEdit* captureEdit = new QLineEdit(QString::fromStdString(captureAs));
  captureEdit->setPlaceholderText("Optional: Variable name to store last voltage reading");
  captureEdit->setStyleSheet("padding: 5px;");
  connect(captureEdit, &QLineEdit::textChanged, this, &ProcessConfigWidget::parameterChanged);
  m_formLayout->addRow("Capture As:", captureEdit);
  m_parameterEdits["captureAs"] = captureEdit;

  // Info label
  QLabel* infoLabel = new QLabel(
    "<i>Reads the PSU voltage multiple times with a delay between each read. "
    "All readings are logged to LogManager. If 'Capture As' is set, the last reading "
    "will be stored in the execution context. Default: 3 reads with 100ms delay.</i>"
  );
  infoLabel->setWordWrap(true);
  infoLabel->setStyleSheet("color: #666; font-size: 10px; margin-top: 8px;");
  m_formLayout->addRow("", infoLabel);
}

// ============================================================================
// PSU READ CURRENT CONFIG WIDGET
// ============================================================================

PSUReadCurrentConfigWidget::PSUReadCurrentConfigWidget(ApplicationContext* context, QWidget* parent)
  : ProcessConfigWidget(context, parent)
{
}

void PSUReadCurrentConfigWidget::createParameterFields(const ProcessInstance* instance)
{
  const auto& params = instance->parameters;

  QLabel* psuLabel = new QLabel("PSU Current Reading:");
  psuLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(psuLabel);

  addParameterField("psu_device", "PSU Device Name", params.at("psu_device"));

  QLabel* readingLabel = new QLabel("Reading Parameters:");
  readingLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 10px;");
  m_formLayout->addRow(readingLabel);

  addParameterField("read_count", "Number of Reads", params.at("read_count"));
  addParameterField("delay_ms", "Delay Between Reads (ms)", params.at("delay_ms"));

  // Context capture field (optional)
  QLabel* captureLabel = new QLabel("Context Capture:");
  captureLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 10px;");
  m_formLayout->addRow(captureLabel);

  std::string captureAs = params.count("captureAs") ? params.at("captureAs") : "";
  QLineEdit* captureEdit = new QLineEdit(QString::fromStdString(captureAs));
  captureEdit->setPlaceholderText("Optional: Variable name to store last current reading");
  captureEdit->setStyleSheet("padding: 5px;");
  connect(captureEdit, &QLineEdit::textChanged, this, &ProcessConfigWidget::parameterChanged);
  m_formLayout->addRow("Capture As:", captureEdit);
  m_parameterEdits["captureAs"] = captureEdit;

  // Info label
  QLabel* infoLabel = new QLabel(
    "<i>Reads the PSU current multiple times with a delay between each read. "
    "All readings are logged to LogManager. If 'Capture As' is set, the last reading "
    "will be stored in the execution context. Default: 3 reads with 100ms delay.</i>"
  );
  infoLabel->setWordWrap(true);
  infoLabel->setStyleSheet("color: #666; font-size: 10px; margin-top: 8px;");
  m_formLayout->addRow("", infoLabel);
}

// ============================================================================
// CONDITIONAL IF CONFIG WIDGET
// ============================================================================

ConditionalIfConfigWidget::ConditionalIfConfigWidget(ApplicationContext* context, QWidget* parent)
  : ProcessConfigWidget(context, parent)
{
}

void ConditionalIfConfigWidget::createParameterFields(const ProcessInstance* instance)
{
  const auto& params = instance->parameters;

  QLabel* condLabel = new QLabel("Condition:");
  condLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2d3e5f; margin-top: 5px;");
  m_formLayout->addRow(condLabel);

  // Context key field
  std::string contextKey = params.count("context_key") ? params.at("context_key") : "MeasuredValue";
  QLineEdit* keyEdit = new QLineEdit(QString::fromStdString(contextKey));
  keyEdit->setPlaceholderText("Variable name from context (e.g., MeasuredVoltage)");
  keyEdit->setStyleSheet("padding: 5px;");
  connect(keyEdit, &QLineEdit::textChanged, this, &ProcessConfigWidget::parameterChanged);
  m_formLayout->addRow("Context Key:", keyEdit);
  m_parameterEdits["context_key"] = keyEdit;

  // Comparison dropdown
  std::string comparison = params.count("comparison") ? params.at("comparison") : ">";
  QLineEdit* compEdit = new QLineEdit(QString::fromStdString(comparison));
  m_parameterEdits["comparison"] = compEdit;

  QComboBox* compCombo = new QComboBox();
  compCombo->addItem(">");
  compCombo->addItem("<");
  compCombo->addItem("==");
  compCombo->addItem("!=");
  compCombo->addItem(">=");
  compCombo->addItem("<=");
  compCombo->addItem("IS_TRUE");
  compCombo->addItem("IS_FALSE");
  compCombo->addItem("EXISTS");
  compCombo->addItem("NOT_EXISTS");
  compCombo->setCurrentText(QString::fromStdString(comparison));

  connect(compCombo, &QComboBox::currentTextChanged, [this, compEdit](const QString& text) {
    compEdit->setText(text);
    emit parameterChanged();
  });
  m_formLayout->addRow("Comparison:", compCombo);

  // Threshold field
  std::string threshold = params.count("threshold") ? params.at("threshold") : "0";
  addParameterField("threshold", "Threshold Value", threshold);

  // Info label
  QLabel* infoLabel = new QLabel(
    "<i>Evaluates the condition. If TRUE, executes steps until 'Conditional Else' or 'Conditional End'. "
    "If FALSE, executes 'Conditional Else' branch if present.</i>"
  );
  infoLabel->setWordWrap(true);
  infoLabel->setStyleSheet("color: #666; font-size: 10px; margin-top: 8px;");
  m_formLayout->addRow("", infoLabel);
}

// ============================================================================
// CONDITIONAL ELSE CONFIG WIDGET
// ============================================================================

ConditionalElseConfigWidget::ConditionalElseConfigWidget(ApplicationContext* context, QWidget* parent)
  : ProcessConfigWidget(context, parent)
{
}

void ConditionalElseConfigWidget::createParameterFields(const ProcessInstance* instance)
{
  // No parameters needed - just visual indicator
  QLabel* infoLabel = new QLabel(
    "<i><b>ELSE Branch</b><br>"
    "Steps after this will execute if the condition is FALSE.</i>"
  );
  infoLabel->setWordWrap(true);
  infoLabel->setStyleSheet("color: #2d3e5f; font-size: 11px; margin-top: 8px;");
  m_formLayout->addRow("", infoLabel);
}

// ============================================================================
// CONDITIONAL END CONFIG WIDGET
// ============================================================================

ConditionalEndConfigWidget::ConditionalEndConfigWidget(ApplicationContext* context, QWidget* parent)
  : ProcessConfigWidget(context, parent)
{
}

void ConditionalEndConfigWidget::createParameterFields(const ProcessInstance* instance)
{
  // No parameters needed - just visual indicator
  QLabel* infoLabel = new QLabel(
    "<i><b>END Conditional</b><br>"
    "Marks the end of the conditional block.</i>"
  );
  infoLabel->setWordWrap(true);
  infoLabel->setStyleSheet("color: #2d3e5f; font-size: 11px; margin-top: 8px;");
  m_formLayout->addRow("", infoLabel);
}