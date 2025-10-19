// DeviceSelectorWidgets.cpp
#include "DeviceSelectorWidgets.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "EziIOManager.h"
#include "PneumaticManager.h"
#include "CameraManager.h"
#include "product_position_manager.h"  // ✅ ADD THIS
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
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

  switch (m_deviceType) {
  case DeviceType::Motion:
    if (m_context->motionManager) {
      devices = m_context->motionManager->GetAllDeviceNames();
    }
    break;

  case DeviceType::Camera:
    if (m_context->cameraManager) {
      // For now, just use "camera" as single entry
      // You can extend CameraManager to return multiple camera names
      devices.push_back("gantry-main");
      devices.push_back("hex-left");
      devices.push_back("hex-right");
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
  }

  if (devices.empty()) {
    addItem("No devices found");
  }
  else {
    for (const auto& device : devices) {
      addItem(QString::fromStdString(device));
    }
  }
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
  return currentText().toStdString();
}

void DeviceComboBox::setSelectedDevice(const std::string& deviceName)
{
  int index = findText(QString::fromStdString(deviceName));
  if (index >= 0) {
    setCurrentIndex(index);
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

  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  // Info label
  QLabel* infoLabel = new QLabel(QString("Select a node for device: %1")
    .arg(QString::fromStdString(m_deviceName)));
  infoLabel->setStyleSheet("font-weight: bold; padding: 5px;");
  mainLayout->addWidget(infoLabel);

  // Search box
  m_searchEdit = new QLineEdit();
  m_searchEdit->setPlaceholderText("Search nodes...");
  m_searchEdit->setStyleSheet("padding: 8px; font-size: 12px;");
  connect(m_searchEdit, &QLineEdit::textChanged,
    this, &NodeSelectorDialog::onSearchTextChanged);
  mainLayout->addWidget(m_searchEdit);

  // Node list
  m_nodeList = new QListWidget();
  m_nodeList->setStyleSheet(
    "QListWidget { "
    "  background-color: white; "
    "  border: 1px solid #c0c0c0; "
    "  border-radius: 4px; "
    "  font-size: 12px; "
    "  padding: 5px; "
    "} "
    "QListWidget::item { "
    "  padding: 8px; "
    "  border-bottom: 1px solid #e0e0e0; "
    "} "
    "QListWidget::item:selected { "
    "  background-color: #5a8fd6; "
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
  connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
  buttonLayout->addWidget(m_cancelButton);

  m_okButton = new QPushButton("Select");
  m_okButton->setMinimumWidth(100);
  m_okButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #4CAF50; "
    "  color: white; "
    "  font-weight: bold; "
    "  padding: 8px; "
    "} "
    "QPushButton:hover { background-color: #45a049; }"
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