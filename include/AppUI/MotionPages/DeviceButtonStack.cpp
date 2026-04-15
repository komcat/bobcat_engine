// DeviceButtonStack.cpp
#include "DeviceButtonStack.h"
#include "unified_motion_manager.h"
#include <QFrame>
#include <algorithm>

namespace {
// Device color mapping - matches MotionGraphicsView overlay
QColor getDeviceColor(const QString& deviceName) {
    if (deviceName.contains("gantry", Qt::CaseInsensitive))     return QColor(52, 152, 219);   // blue
    if (deviceName.contains("hex-left", Qt::CaseInsensitive))   return QColor(46, 204, 113);   // green
    if (deviceName.contains("hex-right", Qt::CaseInsensitive))  return QColor(231, 76, 60);    // red
    if (deviceName.contains("hex-bottom", Qt::CaseInsensitive)) return QColor(155, 89, 182);   // purple
    if (deviceName.contains("stage", Qt::CaseInsensitive))      return QColor(241, 196, 15);   // yellow
    return QColor(149, 165, 166);  // gray
}
} // namespace

DeviceButtonStack::DeviceButtonStack(QWidget* parent)
  : QWidget(parent)
  , m_motionManager(nullptr)
  , m_deviceButtonsContainer(nullptr)
  , m_deviceButtonsLayout(nullptr)
  , m_velocityPresetsWidget(nullptr)
{
  setupUI();
}

void DeviceButtonStack::setupUI()
{
  m_mainLayout = new QVBoxLayout(this);
  m_mainLayout->setContentsMargins(5, 10, 5, 10);
  m_mainLayout->setSpacing(8);

  m_buttonGroup = new QButtonGroup(this);
  m_buttonGroup->setExclusive(true);

  // Connect button group signal
  connect(m_buttonGroup, QOverload<int>::of(&QButtonGroup::idClicked),
    this, &DeviceButtonStack::onButtonClicked);

  // === Devices section (TOP) ===
  QLabel* deviceLabel = new QLabel("Devices");
  deviceLabel->setStyleSheet("QLabel { color: #ffffff; font-size: 14px; font-weight: bold; background-color: transparent; }");
  m_mainLayout->addWidget(deviceLabel);

  // Container widget for dynamically added device buttons
  m_deviceButtonsContainer = new QWidget();
  m_deviceButtonsContainer->setStyleSheet("background-color: transparent;");
  m_deviceButtonsLayout = new QVBoxLayout(m_deviceButtonsContainer);
  m_deviceButtonsLayout->setContentsMargins(0, 0, 0, 0);
  m_deviceButtonsLayout->setSpacing(8);
  m_mainLayout->addWidget(m_deviceButtonsContainer);

  // === Velocity Presets Section ===
  QFrame* separatorLine = new QFrame();
  separatorLine->setFrameShape(QFrame::HLine);
  separatorLine->setStyleSheet("QFrame { background-color: #3f3f46; margin: 3px 0; }");
  m_mainLayout->addWidget(separatorLine);

  QLabel* velLabel = new QLabel("Velocity:");
  velLabel->setStyleSheet("QLabel { color: #d4d4d4; font-size: 12px; font-weight: bold; background-color: transparent; }");
  m_mainLayout->addWidget(velLabel);

  m_velocityPresetsWidget = new QWidget();
  m_velocityPresetsWidget->setStyleSheet("QWidget { background-color: transparent; }");
  QGridLayout* velGrid = new QGridLayout(m_velocityPresetsWidget);
  velGrid->setContentsMargins(0, 0, 0, 0);
  velGrid->setSpacing(3);
  m_mainLayout->addWidget(m_velocityPresetsWidget);

  // Push everything above to the top
  m_mainLayout->addStretch();

  // Set fixed width
  setFixedWidth(240);

  // Style for the container - Dark Theme
  setStyleSheet(
    "DeviceButtonStack {"
    "  background-color: #1e1e1e;"
    "  border-right: 2px solid #3f3f46;"
    "}"
  );
}

void DeviceButtonStack::setDevices(const QStringList& devices)
{
  clearButtons();

  int buttonId = 0;
  for (const QString& device : devices) {
    QPushButton* btn = new QPushButton(device);
    btn->setCheckable(true);
    btn->setMinimumHeight(100);
    btn->setMaximumHeight(160);
    btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    // Get device-specific color
    QColor dc = getDeviceColor(device);
    QString colorStr = dc.name();
    QColor dcDark = dc.darker(130);
    QString darkStr = dcDark.name();
    QColor dcLight = dc.lighter(120);
    QString lightStr = dcLight.name();

    // Style for device buttons - 3D style with device-specific checked color
    btn->setStyleSheet(QString(
      "QPushButton { "
      "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #4a4a4a, stop:1 #3a3a3a); "
      "  color: white; "
      "  border-radius: 6px; "
      "  font-size: 18px; "
      "  font-weight: bold; "
      "  padding: 8px; "
      "  text-align: center; "
      "  border-top: 2px solid rgba(255, 255, 255, 0.4); "
      "  border-left: 2px solid rgba(255, 255, 255, 0.4); "
      "  border-bottom: 2px solid rgba(0, 0, 0, 0.4); "
      "  border-right: 2px solid rgba(0, 0, 0, 0.4); "
      "} "
      "QPushButton:hover { "
      "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5a5a5a, stop:1 #4a4a4a); "
      "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
      "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
      "} "
      "QPushButton:pressed { "
      "  background-color: #2a2a2a; "
      "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
      "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
      "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
      "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
      "  padding-top: 10px; "
      "  padding-left: 10px; "
      "} "
      "QPushButton:checked { "
      "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %1, stop:1 %2); "
      "  color: white; "
      "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
      "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
      "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
      "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
      "} "
      "QPushButton:checked:hover { "
      "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %3, stop:1 %1); "
      "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
      "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
      "}"
    ).arg(colorStr, darkStr, lightStr));

    m_deviceButtonsLayout->addWidget(btn);
    m_buttonGroup->addButton(btn, buttonId);
    m_deviceMap[buttonId] = device;

    buttonId++;
  }
}

QString DeviceButtonStack::getCurrentDevice() const
{
  return m_currentDevice;
}

void DeviceButtonStack::setCurrentDevice(const QString& deviceName)
{
  // Find button with matching device name
  for (auto it = m_deviceMap.constBegin(); it != m_deviceMap.constEnd(); ++it) {
    if (it.value() == deviceName) {
      m_buttonGroup->button(it.key())->setChecked(true);
      m_currentDevice = deviceName;
      break;
    }
  }
}

void DeviceButtonStack::onButtonClicked(int id)
{
  if (m_deviceMap.contains(id)) {
    QString deviceName = m_deviceMap[id];
    m_currentDevice = deviceName;
    emit deviceChanged(deviceName);
  }
}

void DeviceButtonStack::updateVelocityPresets(const QString& deviceType)
{
  // Clear existing buttons
  for (auto* btn : m_velocityButtons) {
    btn->deleteLater();
  }
  m_velocityButtons.clear();

  QGridLayout* velGrid = qobject_cast<QGridLayout*>(m_velocityPresetsWidget->layout());
  if (!velGrid) return;

  // Choose presets based on device type
  QList<double> presets;
  if (deviceType == "PI") {
    presets = { 0.05, 0.1, 0.5, 1, 2, 5, 10, 12.5 };
  } else {
    // ACS or default
    presets = { 1, 2, 5, 10, 20, 30, 40, 50 };
  }

  int col = 0;
  int row = 0;
  int maxCols = 4;

  for (double vel : presets) {
    QString label;
    if (vel == static_cast<int>(vel)) {
      label = QString::number(static_cast<int>(vel));
    } else if (vel < 1.0) {
      label = QString::number(vel, 'f', 2);
    } else {
      label = QString::number(vel, 'f', 1);
    }

    QPushButton* btn = new QPushButton(label);
    btn->setMinimumHeight(26);
    btn->setStyleSheet(
      "QPushButton {"
      "  background-color: #3c3c3c;"
      "  color: #d4d4d4;"
      "  border: 1px solid #555555;"
      "  border-radius: 3px;"
      "  font-size: 11px;"
      "  font-weight: bold;"
      "  padding: 4px 2px;"
      "}"
      "QPushButton:hover {"
      "  background-color: #0e639c;"
      "  color: white;"
      "  border-color: #1177bb;"
      "}"
      "QPushButton:pressed {"
      "  background-color: #0c5280;"
      "}"
    );

    connect(btn, &QPushButton::clicked, this, [this, vel]() {
      emit velocityPresetClicked(vel);
    });

    velGrid->addWidget(btn, row, col);
    m_velocityButtons.append(btn);

    col++;
    if (col >= maxCols) {
      col = 0;
      row++;
    }
  }

  // Add "mm/s" label at the end
  QLabel* unitLabel = new QLabel("mm/s");
  unitLabel->setStyleSheet("QLabel { color: #888888; font-size: 10px; }");
  unitLabel->setAlignment(Qt::AlignCenter);
  velGrid->addWidget(unitLabel, row, col);
}

void DeviceButtonStack::clearButtons()
{
  // Remove all buttons from device container layout and button group
  QList<QAbstractButton*> buttons = m_buttonGroup->buttons();
  for (QAbstractButton* btn : buttons) {
    m_buttonGroup->removeButton(btn);
    m_deviceButtonsLayout->removeWidget(btn);
    btn->deleteLater();
  }

  m_deviceMap.clear();
  m_currentDevice.clear();
}

void DeviceButtonStack::setMotionManager(UnifiedMotionManager* motionManager)
{
  m_motionManager = motionManager;
}

void DeviceButtonStack::refreshDevices()
{
  if (!m_motionManager) {
    return;
  }

  // Save previously selected device so we can restore it after rebuild
  QString previousDevice = m_currentDevice;

  // Get all device names
  std::vector<std::string> allDevices = m_motionManager->GetAllDeviceNames();

  // Get connection status
  std::map<std::string, bool> connectionStatus = m_motionManager->GetConnectionStatus();

  // Filter to only connected devices
  std::vector<std::string> connectedDevices;
  for (const auto& deviceName : allDevices) {
    auto it = connectionStatus.find(deviceName);
    if (it != connectionStatus.end() && it->second) {
      connectedDevices.push_back(deviceName);
    }
  }

  // Sort alphabetically (ascending)
  std::sort(connectedDevices.begin(), connectedDevices.end());

  // Convert to QStringList
  QStringList deviceList;
  for (const auto& device : connectedDevices) {
    deviceList << QString::fromStdString(device);
  }

  // Update device buttons
  setDevices(deviceList);

  // Restore previous device selection if it still exists in the new list
  if (!previousDevice.isEmpty() && deviceList.contains(previousDevice)) {
    setCurrentDevice(previousDevice);
  }
}
