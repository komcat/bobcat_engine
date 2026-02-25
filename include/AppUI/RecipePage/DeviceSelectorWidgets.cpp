// DeviceSelectorWidgets.cpp
#include "DeviceSelectorWidgets.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "EziIOManager.h"
#include "PneumaticManager.h"
#include "CameraManager.h"
#include "product_position_manager.h"
#include "TECManager.h"
#include "PowerSupplyManager.h"
#include "SMUManager.h"
#include "lighting/LightingManager.h"
#include "AccuracyTrainerStore.h"
#include "../CameraOffsetTrainer/CameraOffsetStore.h"
#include <filesystem>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <iostream>

// ============================================================================
// DEVICE COMBOBOX
// ============================================================================

DeviceComboBox::DeviceComboBox(ApplicationContext* context, DeviceType type, QWidget* parent)
  : QComboBox(parent)
  , m_context(context)
  , m_deviceType(type)
{
  setStyleSheet("QComboBox { padding: 5px; min-height: 25px; }");
  populateDevices();
}

void DeviceComboBox::populateDevices()
{
  clear();

  if (!m_context) {
    addItem("No context available");
    return;
  }

  std::vector<std::string> devices;

  std::cout << "[DeviceComboBox] populateDevices() called for type: " << static_cast<int>(m_deviceType) << std::endl;

  switch (m_deviceType) {
  case DeviceType::Motion:
    if (m_context->motionManager) {
      // ✅ FIX: Filter for only CONNECTED devices (not all configured devices)
      std::vector<std::string> allDevices = m_context->motionManager->GetAllDeviceNames();
      std::map<std::string, bool> connectionStatus = m_context->motionManager->GetConnectionStatus();

      // Only add connected devices
      for (const auto& deviceName : allDevices) {
        auto it = connectionStatus.find(deviceName);
        if (it != connectionStatus.end() && it->second) {
          devices.push_back(deviceName);
          std::cout << "[DeviceComboBox] Motion device connected: " << deviceName << std::endl;
        }
        else {
          std::cout << "[DeviceComboBox] Motion device NOT connected (skipped): " << deviceName << std::endl;
        }
      }
    }
    break;

  case DeviceType::Camera:
    if (m_context->cameraManager) {
      devices = m_context->cameraManager->GetCameraIds();
    }
    break;

  case DeviceType::IO:
    if (m_context->ioManager) {
      devices = m_context->ioManager->GetAllDeviceNames();
    }
    break;

  case DeviceType::Pneumatic:
    if (m_context->pneumaticManager) {
      devices = m_context->pneumaticManager->GetAllSlideNames();
    }
    break;

  case DeviceType::Lens:
    if (m_context->cameraManager) {
      // Get lens names from camera manager
      // Assuming you have a method to get lens list
      devices.push_back("lens");
      devices.push_back("wide_angle");
      devices.push_back("telephoto");
    }
    break;

  case DeviceType::TEC:
    if (m_context->tecManager) {
      devices = m_context->tecManager->GetAllDeviceNames();
    }
    break;

  case DeviceType::PSU:
    if (m_context->powerSupplyManager) {
      devices = m_context->powerSupplyManager->getDeviceNames();
    }
    break;

  case DeviceType::SMU:
    if (m_context->smuManager) {
      devices = m_context->smuManager->GetDeviceNames();
      std::cout << "[DeviceComboBox] SMU: Got " << devices.size() << " devices from smuManager" << std::endl;
    }
    else {
      std::cout << "[DeviceComboBox] SMU: smuManager is NULL!" << std::endl;
    }
    break;

  case DeviceType::Lighting:
    if (m_context->lightingManager) {
      // Get device names from lighting manager config
      auto deviceIds = m_context->lightingManager->GetControllerIds();
      for (const auto& deviceId : deviceIds) {
        const auto* config = m_context->lightingManager->GetControllerConfig(deviceId);
        if (config) {
          devices.push_back(config->deviceName);
          std::cout << "[DeviceComboBox] Lighting: Added device: " << config->deviceName << std::endl;
        }
      }
    }
    else {
      std::cout << "[DeviceComboBox] Lighting: lightingManager is NULL!" << std::endl;
    }
    break;
  }

  if (devices.empty()) {
    std::cout << "[DeviceComboBox] No devices found, adding placeholder" << std::endl;
    addItem("No devices found");
  }
  else {
    std::cout << "[DeviceComboBox] Adding " << devices.size() << " devices to combo" << std::endl;
    for (const auto& device : devices) {
      addItem(QString::fromStdString(device));
      std::cout << "[DeviceComboBox]   - Added: " << device << std::endl;
    }
  }

  std::cout << "[DeviceComboBox] populateDevices() complete. Total items: " << count() << std::endl;
}

void DeviceComboBox::refreshDevices()
{
  QString current = currentText();
  populateDevices();

  // Try to restore selection
  int index = findText(current);
  if (index >= 0) {
    setCurrentIndex(index);
  }
}

std::string DeviceComboBox::getSelectedDevice() const
{
  std::string result = currentText().toStdString();
  std::cout << "[DeviceComboBox] getSelectedDevice() returning: '" << result
            << "' (currentIndex=" << currentIndex() << ", count=" << count() << ")" << std::endl;
  return result;
}

void DeviceComboBox::setSelectedDevice(const std::string& deviceName)
{
  std::cout << "[DeviceComboBox] setSelectedDevice('" << deviceName << "')" << std::endl;
  int index = findText(QString::fromStdString(deviceName));
  std::cout << "[DeviceComboBox]   findText returned index: " << index << std::endl;

  if (index >= 0) {
    setCurrentIndex(index);
    std::cout << "[DeviceComboBox]   setCurrentIndex(" << index << ") - SUCCESS" << std::endl;
  }
  else {
    std::cout << "[DeviceComboBox]   Device '" << deviceName << "' not found in list! Current items:" << std::endl;
    for (int i = 0; i < count(); ++i) {
      std::cout << "[DeviceComboBox]     [" << i << "] = " << itemText(i).toStdString() << std::endl;
    }

    // ✅ AUTO-SELECT FIX: If device is not found and we have valid devices, auto-select the first one
    // This handles both empty deviceName AND obsolete/renamed device names (like "camera_1" -> "main_camera")
    if (count() > 0 && itemText(0) != "No devices found") {
      std::cout << "[DeviceComboBox]   Auto-selecting first device: " << itemText(0).toStdString()
                << " (replacing invalid device name: '" << deviceName << "')" << std::endl;
      setCurrentIndex(0);
      // Emit the change signal to trigger parameterChanged so it gets saved with the corrected value
      emit currentIndexChanged(0);
    }
  }
}

// ============================================================================
// NODE SELECTOR DIALOG
// ============================================================================

NodeSelectorDialog::NodeSelectorDialog(ApplicationContext* context,
  const std::string& deviceName,
  const std::string& currentNode,
  QWidget* parent)
  : QDialog(parent)
  , m_context(context)
  , m_deviceName(deviceName)
  , m_selectedNode(currentNode)
{
  setupUI();
  loadNodes();

  // Select current node if exists
  for (int i = 0; i < m_nodeList->count(); ++i) {
    if (m_nodeList->item(i)->text().toStdString() == currentNode) {
      m_nodeList->setCurrentRow(i);
      break;
    }
  }
}

void NodeSelectorDialog::setupUI()
{
  setWindowTitle("Select Node");
  setMinimumSize(400, 500);

  // Set dark background for the entire dialog
  setStyleSheet("QDialog { background-color: #2b2b2b; }");

  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  // Info label
  QLabel* infoLabel = new QLabel(QString("Select a node for device: %1")
    .arg(QString::fromStdString(m_deviceName)));
  infoLabel->setStyleSheet(
    "color: white; "
    "font-weight: bold; "
    "padding: 5px; "
    "background-color: #1e1e1e; "
    "border-radius: 4px;"
  );
  mainLayout->addWidget(infoLabel);

  // Search box
  m_searchEdit = new QLineEdit();
  m_searchEdit->setPlaceholderText("Search nodes...");
  m_searchEdit->setStyleSheet(
    "QLineEdit { "
    "  background-color: #1e1e1e; "
    "  color: white; "
    "  border: 1px solid #3c3c3c; "
    "  border-radius: 4px; "
    "  padding: 8px; "
    "} "
    "QLineEdit:focus { "
    "  border: 1px solid #1177bb; "
    "}"
  );
  connect(m_searchEdit, &QLineEdit::textChanged,
    this, &NodeSelectorDialog::onSearchTextChanged);
  mainLayout->addWidget(m_searchEdit);

  // Node list
  m_nodeList = new QListWidget();
  m_nodeList->setStyleSheet(
    "QListWidget { "
    "  background-color: #1e1e1e; "
    "  color: white; "
    "  border: 1px solid #3c3c3c; "
    "  border-radius: 4px; "
    "  padding: 5px; "
    "} "
    "QListWidget::item { "
    "  padding: 8px; "
    "  border-bottom: 1px solid #3c3c3c; "
    "} "
    "QListWidget::item:hover { "
    "  background-color: #2d2d30; "
    "} "
    "QListWidget::item:selected { "
    "  background-color: #1177bb; "
    "  color: white; "
    "}"
  );
  connect(m_nodeList, &QListWidget::itemDoubleClicked,
    this, &NodeSelectorDialog::onNodeDoubleClicked);
  mainLayout->addWidget(m_nodeList);

  // Buttons
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->addStretch();

  m_cancelButton = new QPushButton("Cancel");
  m_cancelButton->setMinimumWidth(100);
  m_cancelButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #3c3c3c; "
    "  color: white; "
    "  padding: 8px; "
    "  border-radius: 4px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #4c4c4c; "
    "}"
  );
  connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
  buttonLayout->addWidget(m_cancelButton);

  m_okButton = new QPushButton("Select");
  m_okButton->setMinimumWidth(100);
  m_okButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #27ae60; "
    "  color: white; "
    "  font-weight: bold; "
    "  padding: 8px; "
    "  border-radius: 4px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #2ecc71; "
    "}"
  );
  connect(m_okButton, &QPushButton::clicked, [this]() {
    if (m_nodeList->currentItem()) {
      m_selectedNode = m_nodeList->currentItem()->text().toStdString();
      accept();
    }
  });
  buttonLayout->addWidget(m_okButton);

  mainLayout->addLayout(buttonLayout);
}

void NodeSelectorDialog::loadNodes()
{
  m_nodeList->clear();
  m_allNodes.clear();

  if (!m_context || !m_context->motionManager) {
    m_nodeList->addItem("Motion manager not available");
    return;
  }

  // ✅ NEW: Get nodes from product position manager if available
  if (m_context->productPositionManager &&
    m_context->productPositionManager->HasLoadedProduct()) {

    std::cout << "[NodeSelectorDialog] Loading positions from product manager" << std::endl;

    // Get all positions for this device from product manager
    auto positions = m_context->productPositionManager->GetAllPositions(m_deviceName);

    for (const auto& [posName, pos] : positions) {
      m_allNodes.push_back(posName);
    }

    std::cout << "[NodeSelectorDialog] Loaded " << m_allNodes.size()
      << " positions from product for device: " << m_deviceName << std::endl;
  }
  else {
    // Fallback: Get from motion manager
    std::cout << "[NodeSelectorDialog] Loading positions from motion manager" << std::endl;
    m_allNodes = m_context->motionManager->GetAllNodeNames(m_deviceName);

    std::cout << "[NodeSelectorDialog] Loaded " << m_allNodes.size()
      << " nodes from motion manager for device: " << m_deviceName << std::endl;
  }

  if (m_allNodes.empty()) {
    m_nodeList->addItem(QString("No positions found for device: %1")
      .arg(QString::fromStdString(m_deviceName)));
    return;
  }

  // Add nodes to list
  for (const auto& node : m_allNodes) {
    QListWidgetItem* item = new QListWidgetItem(QString::fromStdString(node));
    m_nodeList->addItem(item);
  }
}

void NodeSelectorDialog::onSearchTextChanged(const QString& text)
{
  // Filter list based on search text
  std::string search = text.toLower().toStdString();

  for (int i = 0; i < m_nodeList->count(); ++i) {
    QListWidgetItem* item = m_nodeList->item(i);
    std::string nodeName = item->text().toLower().toStdString();

    bool matches = search.empty() || nodeName.find(search) != std::string::npos;
    item->setHidden(!matches);
  }
}

void NodeSelectorDialog::onNodeDoubleClicked(QListWidgetItem* item)
{
  m_selectedNode = item->text().toStdString();
  accept();
}

std::string NodeSelectorDialog::getSelectedNode() const
{
  return m_selectedNode;
}

std::string NodeSelectorDialog::SelectNode(ApplicationContext* context,
  const std::string& deviceName,
  const std::string& currentNode,
  QWidget* parent)
{
  NodeSelectorDialog dialog(context, deviceName, currentNode, parent);

  if (dialog.exec() == QDialog::Accepted) {
    return dialog.getSelectedNode();
  }

  return currentNode; // Return original if cancelled
}

// ============================================================================
// TRAINING SET SELECTOR DIALOG
// ============================================================================

TrainingSetSelectorDialog::TrainingSetSelectorDialog(ApplicationContext* context,
  const std::string& currentSet,
  QWidget* parent)
  : QDialog(parent)
  , m_context(context)
  , m_selectedSet(currentSet)
{
  setupUI();
  loadSets();

  // Select current set if exists
  for (int i = 0; i < m_setList->count(); ++i) {
    if (m_setList->item(i)->text().toStdString() == currentSet) {
      m_setList->setCurrentRow(i);
      break;
    }
  }
}

void TrainingSetSelectorDialog::setupUI()
{
  setWindowTitle("Select Training Set");
  setMinimumSize(400, 500);

  // Set dark background for the entire dialog
  setStyleSheet("QDialog { background-color: #2b2b2b; }");

  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  // Info label
  QLabel* infoLabel = new QLabel("Select an Accuracy Trainer training set:");
  infoLabel->setStyleSheet(
    "color: white; "
    "font-weight: bold; "
    "padding: 5px; "
    "background-color: #1e1e1e; "
    "border-radius: 4px;"
  );
  mainLayout->addWidget(infoLabel);

  // Search box
  m_searchEdit = new QLineEdit();
  m_searchEdit->setPlaceholderText("Search training sets...");
  m_searchEdit->setStyleSheet(
    "QLineEdit { "
    "  background-color: #1e1e1e; "
    "  color: white; "
    "  border: 1px solid #3c3c3c; "
    "  border-radius: 4px; "
    "  padding: 8px; "
    "} "
    "QLineEdit:focus { "
    "  border: 1px solid #1177bb; "
    "}"
  );
  connect(m_searchEdit, &QLineEdit::textChanged,
    this, &TrainingSetSelectorDialog::onSearchTextChanged);
  mainLayout->addWidget(m_searchEdit);

  // Set list
  m_setList = new QListWidget();
  m_setList->setStyleSheet(
    "QListWidget { "
    "  background-color: #1e1e1e; "
    "  color: white; "
    "  border: 1px solid #3c3c3c; "
    "  border-radius: 4px; "
    "  padding: 5px; "
    "} "
    "QListWidget::item { "
    "  padding: 8px; "
    "  border-bottom: 1px solid #3c3c3c; "
    "} "
    "QListWidget::item:hover { "
    "  background-color: #2d2d30; "
    "} "
    "QListWidget::item:selected { "
    "  background-color: #1177bb; "
    "  color: white; "
    "}"
  );
  connect(m_setList, &QListWidget::itemDoubleClicked,
    this, &TrainingSetSelectorDialog::onItemDoubleClicked);
  mainLayout->addWidget(m_setList);

  // Buttons
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->addStretch();

  m_cancelButton = new QPushButton("Cancel");
  m_cancelButton->setMinimumWidth(100);
  m_cancelButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #3c3c3c; "
    "  color: white; "
    "  padding: 8px; "
    "  border-radius: 4px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #4c4c4c; "
    "}"
  );
  connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
  buttonLayout->addWidget(m_cancelButton);

  m_okButton = new QPushButton("Select");
  m_okButton->setMinimumWidth(100);
  m_okButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #27ae60; "
    "  color: white; "
    "  font-weight: bold; "
    "  padding: 8px; "
    "  border-radius: 4px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #2ecc71; "
    "}"
  );
  connect(m_okButton, &QPushButton::clicked, [this]() {
    if (m_setList->currentItem()) {
      m_selectedSet = m_setList->currentItem()->text().toStdString();
      accept();
    }
  });
  buttonLayout->addWidget(m_okButton);

  mainLayout->addLayout(buttonLayout);
}

void TrainingSetSelectorDialog::loadSets()
{
  m_setList->clear();
  m_allSets.clear();

  if (!m_context) {
    m_setList->addItem("Application context not available");
    return;
  }

  AccuracyTrainerStore store;
  if (m_context->productPositionManager &&
      m_context->productPositionManager->HasLoadedProduct()) {
    std::filesystem::path productDir = std::filesystem::path(
      m_context->productPositionManager->GetCurrentProductPath()).parent_path();
    store.SetBasePath((productDir / "accuracy_trainer").string());
  }

  m_allSets = store.ListTrainingSets();

  std::cout << "[TrainingSetSelectorDialog] Loaded " << m_allSets.size()
    << " training sets" << std::endl;

  if (m_allSets.empty()) {
    m_setList->addItem("No training sets found");
    return;
  }

  for (const auto& name : m_allSets) {
    QListWidgetItem* item = new QListWidgetItem(QString::fromStdString(name));
    m_setList->addItem(item);
  }
}

void TrainingSetSelectorDialog::onSearchTextChanged(const QString& text)
{
  std::string search = text.toLower().toStdString();

  for (int i = 0; i < m_setList->count(); ++i) {
    QListWidgetItem* item = m_setList->item(i);
    std::string setName = item->text().toLower().toStdString();

    bool matches = search.empty() || setName.find(search) != std::string::npos;
    item->setHidden(!matches);
  }
}

void TrainingSetSelectorDialog::onItemDoubleClicked(QListWidgetItem* item)
{
  m_selectedSet = item->text().toStdString();
  accept();
}

std::string TrainingSetSelectorDialog::getSelectedSet() const
{
  return m_selectedSet;
}

std::string TrainingSetSelectorDialog::SelectTrainingSet(ApplicationContext* context,
  const std::string& currentSet,
  QWidget* parent)
{
  TrainingSetSelectorDialog dialog(context, currentSet, parent);

  if (dialog.exec() == QDialog::Accepted) {
    return dialog.getSelectedSet();
  }

  return currentSet; // Return original if cancelled
}

// ============================================================================
// CAMERA OFFSET SET SELECTOR DIALOG
// ============================================================================

CameraOffsetSetSelectorDialog::CameraOffsetSetSelectorDialog(ApplicationContext* context,
  const std::string& currentSet,
  QWidget* parent)
  : QDialog(parent)
  , m_context(context)
  , m_selectedSet(currentSet)
{
  setupUI();
  loadSets();

  // Select current set if exists
  for (int i = 0; i < m_setList->count(); ++i) {
    if (m_setList->item(i)->text().toStdString() == currentSet) {
      m_setList->setCurrentRow(i);
      break;
    }
  }
}

void CameraOffsetSetSelectorDialog::setupUI()
{
  setWindowTitle("Select Camera Offset Calibration Set");
  setMinimumSize(400, 500);

  setStyleSheet("QDialog { background-color: #2b2b2b; }");

  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  // Info label
  QLabel* infoLabel = new QLabel("Select a Camera Offset calibration set:");
  infoLabel->setStyleSheet(
    "color: white; "
    "font-weight: bold; "
    "padding: 5px; "
    "background-color: #1e1e1e; "
    "border-radius: 4px;"
  );
  mainLayout->addWidget(infoLabel);

  // Search box
  m_searchEdit = new QLineEdit();
  m_searchEdit->setPlaceholderText("Search calibration sets...");
  m_searchEdit->setStyleSheet(
    "QLineEdit { "
    "  background-color: #1e1e1e; "
    "  color: white; "
    "  border: 1px solid #3c3c3c; "
    "  border-radius: 4px; "
    "  padding: 8px; "
    "} "
    "QLineEdit:focus { "
    "  border: 1px solid #1177bb; "
    "}"
  );
  connect(m_searchEdit, &QLineEdit::textChanged,
    this, &CameraOffsetSetSelectorDialog::onSearchTextChanged);
  mainLayout->addWidget(m_searchEdit);

  // Set list
  m_setList = new QListWidget();
  m_setList->setStyleSheet(
    "QListWidget { "
    "  background-color: #1e1e1e; "
    "  color: white; "
    "  border: 1px solid #3c3c3c; "
    "  border-radius: 4px; "
    "  padding: 5px; "
    "} "
    "QListWidget::item { "
    "  padding: 8px; "
    "  border-bottom: 1px solid #3c3c3c; "
    "} "
    "QListWidget::item:hover { "
    "  background-color: #2d2d30; "
    "} "
    "QListWidget::item:selected { "
    "  background-color: #1177bb; "
    "  color: white; "
    "}"
  );
  connect(m_setList, &QListWidget::itemDoubleClicked,
    this, &CameraOffsetSetSelectorDialog::onItemDoubleClicked);
  mainLayout->addWidget(m_setList);

  // Buttons
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->addStretch();

  m_cancelButton = new QPushButton("Cancel");
  m_cancelButton->setMinimumWidth(100);
  m_cancelButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #3c3c3c; "
    "  color: white; "
    "  padding: 8px; "
    "  border-radius: 4px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #4c4c4c; "
    "}"
  );
  connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
  buttonLayout->addWidget(m_cancelButton);

  m_okButton = new QPushButton("Select");
  m_okButton->setMinimumWidth(100);
  m_okButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #27ae60; "
    "  color: white; "
    "  font-weight: bold; "
    "  padding: 8px; "
    "  border-radius: 4px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #2ecc71; "
    "}"
  );
  connect(m_okButton, &QPushButton::clicked, [this]() {
    if (m_setList->currentItem()) {
      m_selectedSet = m_setList->currentItem()->text().toStdString();
      accept();
    }
  });
  buttonLayout->addWidget(m_okButton);

  mainLayout->addLayout(buttonLayout);
}

void CameraOffsetSetSelectorDialog::loadSets()
{
  m_setList->clear();
  m_allSets.clear();

  if (!m_context) {
    m_setList->addItem("Application context not available");
    return;
  }

  CameraOffsetStore store;
  if (m_context->productPositionManager &&
      m_context->productPositionManager->HasLoadedProduct()) {
    std::filesystem::path productDir = std::filesystem::path(
      m_context->productPositionManager->GetCurrentProductPath()).parent_path();
    store.SetBasePath((productDir / "PR_Camera_Offset").string());
  }

  m_allSets = store.ListCalibrationSets();

  std::cout << "[CameraOffsetSetSelectorDialog] Loaded " << m_allSets.size()
    << " calibration sets from " << store.GetBasePath() << std::endl;

  if (m_allSets.empty()) {
    m_setList->addItem("No calibration sets found");
    return;
  }

  for (const auto& name : m_allSets) {
    QListWidgetItem* item = new QListWidgetItem(QString::fromStdString(name));
    m_setList->addItem(item);
  }
}

void CameraOffsetSetSelectorDialog::onSearchTextChanged(const QString& text)
{
  std::string search = text.toLower().toStdString();

  for (int i = 0; i < m_setList->count(); ++i) {
    QListWidgetItem* item = m_setList->item(i);
    std::string setName = item->text().toLower().toStdString();

    bool matches = search.empty() || setName.find(search) != std::string::npos;
    item->setHidden(!matches);
  }
}

void CameraOffsetSetSelectorDialog::onItemDoubleClicked(QListWidgetItem* item)
{
  m_selectedSet = item->text().toStdString();
  accept();
}

std::string CameraOffsetSetSelectorDialog::getSelectedSet() const
{
  return m_selectedSet;
}

std::string CameraOffsetSetSelectorDialog::SelectCalibrationSet(ApplicationContext* context,
  const std::string& currentSet,
  QWidget* parent)
{
  CameraOffsetSetSelectorDialog dialog(context, currentSet, parent);

  if (dialog.exec() == QDialog::Accepted) {
    return dialog.getSelectedSet();
  }

  return currentSet; // Return original if cancelled
}

// ============================================================================
// VISION PROFILE COMBOBOX
// ============================================================================

VisionProfileComboBox::VisionProfileComboBox(ApplicationContext* context, QWidget* parent)
  : QComboBox(parent)
  , m_context(context)
{
  setStyleSheet("QComboBox { padding: 5px; min-height: 25px; }");
  populateProfiles();
}

void VisionProfileComboBox::populateProfiles()
{
  clear();
  m_profilePaths.clear();

  if (!m_context) {
    addItem("No context available");
    return;
  }

  // Get product name from ProductPositionManager
  std::string productName = "";
  if (m_context->productPositionManager && m_context->productPositionManager->HasLoadedProduct()) {
    productName = m_context->productPositionManager->GetCurrentProductName();
  }

  if (productName.empty()) {
    addItem("No product loaded");
    return;
  }

  // Build vision config directory path
  QString appDir = QCoreApplication::applicationDirPath();
  m_visionConfigDir = (appDir + "/config/" + QString::fromStdString(productName) + "/vision").toStdString();

  QDir visionDir(QString::fromStdString(m_visionConfigDir));
  if (!visionDir.exists()) {
    addItem("Vision folder not found");
    std::cout << "[VisionProfileComboBox] Vision folder not found: " << m_visionConfigDir << std::endl;
    return;
  }

  // Scan for .json files
  QStringList filters;
  filters << "*.json";
  visionDir.setNameFilters(filters);
  visionDir.setSorting(QDir::Name);

  QFileInfoList files = visionDir.entryInfoList(QDir::Files);

  if (files.isEmpty()) {
    addItem("No profiles found");
    return;
  }

  for (const QFileInfo& fileInfo : files) {
    QString fileName = fileInfo.fileName();
    QString fullPath = fileInfo.absoluteFilePath();

    // Add to combo (display just filename)
    addItem(fileName);
    m_profilePaths.push_back(fullPath.toStdString());
  }

  std::cout << "[VisionProfileComboBox] Found " << m_profilePaths.size()
            << " profiles in " << m_visionConfigDir << std::endl;
}

std::string VisionProfileComboBox::getSelectedProfile() const
{
  int index = currentIndex();
  if (index >= 0 && index < static_cast<int>(m_profilePaths.size())) {
    return m_profilePaths[index];
  }
  return "";
}

void VisionProfileComboBox::setSelectedProfile(const std::string& profilePath)
{
  // Try to match by full path first
  for (int i = 0; i < static_cast<int>(m_profilePaths.size()); ++i) {
    if (m_profilePaths[i] == profilePath) {
      setCurrentIndex(i);
      return;
    }
  }

  // Try to match by filename only
  QFileInfo fileInfo(QString::fromStdString(profilePath));
  QString fileName = fileInfo.fileName();

  int index = findText(fileName);
  if (index >= 0) {
    setCurrentIndex(index);
  }
  else if (profilePath.empty() && !m_profilePaths.empty()) {
    // Auto-select first profile if path is empty and profiles are available
    setCurrentIndex(0);
    std::cout << "[VisionProfileComboBox] Auto-selected first profile: " << m_profilePaths[0] << std::endl;
    emit currentIndexChanged(0);  // Trigger parameter change
  }
}

void VisionProfileComboBox::refreshProfiles()
{
  populateProfiles();
}

std::string VisionProfileComboBox::getVisionConfigDir() const
{
  return m_visionConfigDir;
}