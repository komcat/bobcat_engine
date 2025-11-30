// MotionDeviceSelectorDialog.cpp
#include "MotionDeviceSelectorDialog.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCheckBox>
#include <iostream>

MotionDeviceSelectorDialog::MotionDeviceSelectorDialog(ApplicationContext* context,
  const std::string& currentDevice,
  bool showOnlyConnected,
  const std::string& requiredAxes,
  QWidget* parent)
  : QDialog(parent)
  , m_context(context)
  , m_selectedDevice(currentDevice)
  , m_showOnlyConnected(showOnlyConnected)
  , m_requiredAxes(requiredAxes)
{
  setupUI();
  loadDevices();

  // Select current device if exists
  for (int i = 0; i < m_deviceList->count(); ++i) {
    QListWidgetItem* item = m_deviceList->item(i);
    QString itemText = item->text();

    // Extract device name (before the status indicator)
    QString deviceName = itemText.split(" [").first().trimmed();

    // Remove emoji prefix if present
    if (deviceName.startsWith("🟢") || deviceName.startsWith("🔴") ||
      deviceName.startsWith("⚪")) {
      deviceName = deviceName.mid(2).trimmed();
    }

    if (deviceName.toStdString() == currentDevice) {
      m_deviceList->setCurrentRow(i);
      break;
    }
  }
}

void MotionDeviceSelectorDialog::setupUI()
{
  // Update window title based on filter
  if (!m_requiredAxes.empty()) {
    setWindowTitle(QString("Select Motion Device (requires %1 axes)")
      .arg(QString::fromStdString(m_requiredAxes)));
  } else {
    setWindowTitle("Select Motion Device");
  }
  setMinimumSize(450, 500);

  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(15, 15, 15, 15);
  mainLayout->setSpacing(10);

  // Info label
  QString infoText = "Select a motion device:";
  if (!m_requiredAxes.empty()) {
    infoText = QString("Select a motion device with %1 axes:")
      .arg(QString::fromStdString(m_requiredAxes));
  }
  QLabel* infoLabel = new QLabel(infoText);
  infoLabel->setStyleSheet("font-weight: bold; padding: 5px;");
  mainLayout->addWidget(infoLabel);

  // Search box
  m_searchEdit = new QLineEdit();
  m_searchEdit->setPlaceholderText("Search devices...");
  m_searchEdit->setStyleSheet("padding: 8px;");
  connect(m_searchEdit, &QLineEdit::textChanged,
    this, &MotionDeviceSelectorDialog::onSearchTextChanged);
  mainLayout->addWidget(m_searchEdit);

  // Show all checkbox
  m_showAllCheckbox = new QCheckBox("Show disconnected devices");
  m_showAllCheckbox->setChecked(!m_showOnlyConnected);
  connect(m_showAllCheckbox, &QCheckBox::toggled,
    this, &MotionDeviceSelectorDialog::onShowAllToggled);
  mainLayout->addWidget(m_showAllCheckbox);

  // Device list
  m_deviceList = new QListWidget();
  m_deviceList->setStyleSheet("padding: 5px;");
  connect(m_deviceList, &QListWidget::itemDoubleClicked,
    this, &MotionDeviceSelectorDialog::onDeviceDoubleClicked);
  mainLayout->addWidget(m_deviceList);

  // Status legend
  QLabel* legendLabel = new QLabel(
    "Status: 🟢 Connected  |  🔴 Disconnected"
  );
  legendLabel->setStyleSheet("font-size: 11px; color: #a0a0a0; padding: 5px;");
  mainLayout->addWidget(legendLabel);

  // Buttons
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(10);

  m_refreshButton = new QPushButton("Refresh");
  m_refreshButton->setObjectName("refreshButton");
  m_refreshButton->setMinimumWidth(100);
  m_refreshButton->setStyleSheet(
    "QPushButton#refreshButton { background-color: #555555; padding: 8px; }"
    "QPushButton#refreshButton:hover { background-color: #666666; }"
  );
  connect(m_refreshButton, &QPushButton::clicked, this, &MotionDeviceSelectorDialog::refreshDeviceStatus);
  buttonLayout->addWidget(m_refreshButton);

  buttonLayout->addStretch();

  m_cancelButton = new QPushButton("Cancel");
  m_cancelButton->setObjectName("cancelButton");
  m_cancelButton->setMinimumWidth(100);
  m_cancelButton->setStyleSheet(
    "QPushButton#cancelButton { background-color: #555555; padding: 8px; }"
    "QPushButton#cancelButton:hover { background-color: #666666; }"
  );
  connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
  buttonLayout->addWidget(m_cancelButton);

  m_okButton = new QPushButton("Select");
  m_okButton->setObjectName("selectButton");
  m_okButton->setMinimumWidth(100);
  m_okButton->setStyleSheet(
    "QPushButton#selectButton { background-color: #27ae60; padding: 8px; }"
    "QPushButton#selectButton:hover { background-color: #2ecc71; }"
  );

  connect(m_okButton, &QPushButton::clicked, [this]() {
    if (m_deviceList->currentItem()) {
      QString itemText = m_deviceList->currentItem()->text();

      // Remove emoji prefix
      if (itemText.startsWith("🟢") || itemText.startsWith("🔴") ||
        itemText.startsWith("⚪")) {
        itemText = itemText.mid(2).trimmed();
      }

      // Extract device name (before the status bracket)
      QString deviceName = itemText.split(" [").first().trimmed();

      m_selectedDevice = deviceName.toStdString();
      accept();
    }
  });
  buttonLayout->addWidget(m_okButton);

  mainLayout->addLayout(buttonLayout);
}

bool MotionDeviceSelectorDialog::deviceHasRequiredAxes(const std::string& deviceName) const
{
  if (m_requiredAxes.empty()) {
    return true;  // No filter, all devices match
  }

  if (!m_context || !m_context->motionManager) {
    return false;
  }

  std::string installedAxes = m_context->motionManager->GetInstalledAxes(deviceName);

  // Check if all required axes are present
  for (char axis : m_requiredAxes) {
    if (installedAxes.find(axis) == std::string::npos) {
      return false;  // Missing required axis
    }
  }

  return true;
}

void MotionDeviceSelectorDialog::loadDevices()
{
  m_deviceList->clear();
  m_allDevices.clear();

  if (!m_context || !m_context->motionManager) {
    m_deviceList->addItem("Motion manager not available");
    return;
  }

  // Get all device names and connection status
  std::vector<std::string> allDevices = m_context->motionManager->GetAllDeviceNames();
  std::map<std::string, bool> connectionStatus = m_context->motionManager->GetConnectionStatus();

  if (allDevices.empty()) {
    m_deviceList->addItem("No motion devices configured");
    return;
  }

  std::cout << "[MotionDeviceSelectorDialog] Loaded " << allDevices.size()
    << " devices, filter: " << (m_requiredAxes.empty() ? "none" : m_requiredAxes) << std::endl;

  // Add devices with status
  for (const auto& deviceName : allDevices) {
    // Check axis filter first
    if (!deviceHasRequiredAxes(deviceName)) {
      continue;  // Skip devices without required axes
    }

    bool isConnected = false;
    auto it = connectionStatus.find(deviceName);
    if (it != connectionStatus.end()) {
      isConnected = it->second;
    }

    // Skip disconnected devices if showing only connected
    if (m_showOnlyConnected && !isConnected) {
      continue;
    }

    m_allDevices.push_back(deviceName);

    // Get device type and axes
    std::string deviceType = m_context->motionManager->GetDeviceType(deviceName);
    std::string installedAxes = m_context->motionManager->GetInstalledAxes(deviceName);

    // Create display text with emoji status
    QString displayText = QString::fromStdString(deviceName);
    QString statusEmoji;
    QString statusColor;

    if (isConnected) {
      statusEmoji = "🟢";
      statusColor = "#27ae60";
      displayText += " [Connected]";
    }
    else {
      statusEmoji = "🔴";
      statusColor = "#e74c3c";
      displayText += " [Disconnected]";
    }

    // Add device type and axes info
    QString typeInfo;
    if (!deviceType.empty()) {
      typeInfo = QString::fromStdString(deviceType);
    }
    if (!installedAxes.empty()) {
      if (!typeInfo.isEmpty()) {
        typeInfo += ", ";
      }
      typeInfo += QString::fromStdString(installedAxes);
    }
    if (!typeInfo.isEmpty()) {
      displayText += QString(" (%1)").arg(typeInfo);
    }

    QListWidgetItem* item = new QListWidgetItem(statusEmoji + " " + displayText);
    item->setForeground(QColor(statusColor));

    // Make disconnected items slightly dimmed if shown
    if (!isConnected) {
      item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
    }

    m_deviceList->addItem(item);
  }

  if (m_deviceList->count() == 0) {
    if (!m_requiredAxes.empty()) {
      m_deviceList->addItem(QString("No devices with %1 axes found")
        .arg(QString::fromStdString(m_requiredAxes)));
    } else {
      m_deviceList->addItem("No connected devices found");
    }
  }
}

void MotionDeviceSelectorDialog::refreshDeviceStatus()
{
  std::cout << "[MotionDeviceSelectorDialog] Refreshing device status..." << std::endl;

  // Remember current selection
  int currentRow = m_deviceList->currentRow();

  // Reload devices with updated status
  loadDevices();

  // Restore selection
  if (currentRow >= 0 && currentRow < m_deviceList->count()) {
    m_deviceList->setCurrentRow(currentRow);
  }
}

void MotionDeviceSelectorDialog::onSearchTextChanged(const QString& text)
{
  // Filter list based on search text
  std::string search = text.toLower().toStdString();

  for (int i = 0; i < m_deviceList->count(); ++i) {
    QListWidgetItem* item = m_deviceList->item(i);
    QString itemText = item->text().toLower();

    bool matches = search.empty() || itemText.contains(QString::fromStdString(search));
    item->setHidden(!matches);
  }
}

void MotionDeviceSelectorDialog::onShowAllToggled(bool checked)
{
  m_showOnlyConnected = !checked;
  loadDevices();
}

void MotionDeviceSelectorDialog::onDeviceDoubleClicked(QListWidgetItem* item)
{
  // Don't allow selecting disabled (disconnected) items
  if (!(item->flags() & Qt::ItemIsEnabled)) {
    return;
  }

  QString itemText = item->text();

  // Remove emoji prefix
  if (itemText.startsWith("🟢") || itemText.startsWith("🔴") ||
    itemText.startsWith("⚪")) {
    itemText = itemText.mid(2).trimmed();
  }

  // Extract device name (before the status bracket)
  QString deviceName = itemText.split(" [").first().trimmed();

  m_selectedDevice = deviceName.toStdString();
  accept();
}

std::string MotionDeviceSelectorDialog::getSelectedDevice() const
{
  return m_selectedDevice;
}

std::string MotionDeviceSelectorDialog::SelectDevice(ApplicationContext* context,
  const std::string& currentDevice,
  bool showOnlyConnected,
  QWidget* parent)
{
  MotionDeviceSelectorDialog dialog(context, currentDevice, showOnlyConnected, "", parent);

  if (dialog.exec() == QDialog::Accepted) {
    return dialog.getSelectedDevice();
  }

  return currentDevice; // Return original if cancelled
}

std::string MotionDeviceSelectorDialog::SelectDeviceWithAxes(ApplicationContext* context,
  const std::string& currentDevice,
  const std::string& requiredAxes,
  bool showOnlyConnected,
  QWidget* parent)
{
  MotionDeviceSelectorDialog dialog(context, currentDevice, showOnlyConnected, requiredAxes, parent);

  if (dialog.exec() == QDialog::Accepted) {
    return dialog.getSelectedDevice();
  }

  return currentDevice; // Return original if cancelled
}
