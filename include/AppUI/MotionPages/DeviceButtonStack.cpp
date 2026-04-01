// DeviceButtonStack.cpp
#include "DeviceButtonStack.h"
#include "unified_motion_manager.h"
#include <QFrame>
#include <algorithm>

DeviceButtonStack::DeviceButtonStack(QWidget* parent)
  : QWidget(parent)
  , m_motionManager(nullptr)
  , m_posNameLabel(nullptr)
  , m_posXLabel(nullptr)
  , m_posYLabel(nullptr)
  , m_posZLabel(nullptr)
  , m_posULabel(nullptr)
  , m_posVLabel(nullptr)
  , m_posWLabel(nullptr)
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

  // Setup selected position info section
  setupPositionInfoSection();

  // Set fixed width for the button stack (increased to accommodate position info and larger fonts)
  setFixedWidth(240);

  // Style for the container - Dark Theme
  setStyleSheet(
    "DeviceButtonStack {"
    "  background-color: #1e1e1e;"
    "  border-right: 2px solid #3f3f46;"
    "}"
  );
}

void DeviceButtonStack::setupPositionInfoSection()
{
  // Add separator before device buttons section
  QFrame* topLine = new QFrame();
  topLine->setFrameShape(QFrame::HLine);
  topLine->setStyleSheet("QFrame { background-color: #3f3f46; margin: 3px 0; }");
  m_mainLayout->addWidget(topLine);

  // === Selected Position Section ===
  QLabel* posLabel = new QLabel("Selected Position");
  posLabel->setStyleSheet("QLabel { color: #ffffff; font-size: 14px; font-weight: bold; background-color: transparent; }");
  m_mainLayout->addWidget(posLabel);

  // Compact info widget
  QWidget* posWidget = new QWidget();
  posWidget->setStyleSheet("QWidget { background-color: #1e1e1e; border: 1px solid #3f3f46; border-radius: 4px; }");
  QVBoxLayout* posLayout = new QVBoxLayout(posWidget);
  posLayout->setContentsMargins(6, 6, 6, 6);
  posLayout->setSpacing(3);

  m_posNameLabel = new QLabel("-");
  m_posXLabel = new QLabel("X: -");
  m_posYLabel = new QLabel("Y: -");
  m_posZLabel = new QLabel("Z: -");
  m_posULabel = new QLabel("U: -");
  m_posVLabel = new QLabel("V: -");
  m_posWLabel = new QLabel("W: -");

  // Position name label style
  m_posNameLabel->setStyleSheet("QLabel { color: #d4d4d4; font-size: 14px; }");
  posLayout->addWidget(m_posNameLabel);

  // XYZUVW labels with larger font
  QList<QLabel*> posLabels = { m_posXLabel, m_posYLabel, m_posZLabel,
                               m_posULabel, m_posVLabel, m_posWLabel };
  for (auto* label : posLabels) {
    label->setStyleSheet("QLabel { color: #d4d4d4; font-size: 16px; }");
    posLayout->addWidget(label);
  }

  m_mainLayout->addWidget(posWidget);

  // Add separator after position info
  QFrame* bottomLine = new QFrame();
  bottomLine->setFrameShape(QFrame::HLine);
  bottomLine->setStyleSheet("QFrame { background-color: #3f3f46; margin: 3px 0; }");
  m_mainLayout->addWidget(bottomLine);

  // Add label for device buttons section
  QLabel* deviceLabel = new QLabel("Devices");
  deviceLabel->setStyleSheet("QLabel { color: #ffffff; font-size: 14px; font-weight: bold; background-color: transparent; }");
  m_mainLayout->addWidget(deviceLabel);
}

void DeviceButtonStack::setDevices(const QStringList& devices)
{
  clearButtons();

  int buttonId = 0;
  for (const QString& device : devices) {
    QPushButton* btn = new QPushButton(device);
    btn->setCheckable(true);
    btn->setMinimumHeight(50);
    btn->setMaximumHeight(80);
    btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    // Style for device buttons - 3D Windows Style
    btn->setStyleSheet(
      "QPushButton { "
      "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #4a4a4a, stop:1 #3a3a3a); "
      "  color: white; "
      "  border-radius: 6px; "
      "  font-size: 13px; "
      "  font-weight: bold; "
      "  padding: 6px; "
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
      "  padding-top: 8px; "
      "  padding-left: 8px; "
      "} "
      "QPushButton:checked { "
      "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #1e73ac, stop:1 #0e639c); "
      "  color: white; "
      "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
      "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
      "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
      "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
      "} "
      "QPushButton:checked:hover { "
      "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #2e83bc, stop:1 #1e73ac); "
      "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
      "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
      "}"
    );

    m_mainLayout->addWidget(btn);
    m_buttonGroup->addButton(btn, buttonId);
    m_deviceMap[buttonId] = device;

    buttonId++;
  }

  // Add stretch to push buttons to top
  m_mainLayout->addStretch();

  // ✅ FIX: Do NOT auto-select first device on startup
  // User must explicitly click a device button to select it
  // m_currentDevice remains empty until user clicks
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

void DeviceButtonStack::setSelectedPosition(const QString& name, double x, double y, double z,
  double u, double v, double w,
  const std::vector<std::string>& waypoints)
{
  Q_UNUSED(waypoints);  // Waypoints field removed from UI
  m_selectedPositionName = name;

  m_posNameLabel->setText(QString("<b>%1</b>").arg(name));
  m_posXLabel->setText(QString("X: %1").arg(x, 0, 'f', 3));
  m_posYLabel->setText(QString("Y: %1").arg(y, 0, 'f', 3));
  m_posZLabel->setText(QString("Z: %1").arg(z, 0, 'f', 3));
  m_posULabel->setText(QString("U: %1").arg(u, 0, 'f', 3));
  m_posVLabel->setText(QString("V: %1").arg(v, 0, 'f', 3));
  m_posWLabel->setText(QString("W: %1").arg(w, 0, 'f', 3));
}

void DeviceButtonStack::clearSelectedPosition()
{
  m_selectedPositionName.clear();

  m_posNameLabel->setText("-");
  m_posXLabel->setText("X: -");
  m_posYLabel->setText("Y: -");
  m_posZLabel->setText("Z: -");
  m_posULabel->setText("U: -");
  m_posVLabel->setText("V: -");
  m_posWLabel->setText("W: -");
}

void DeviceButtonStack::clearButtons()
{
  // Remove all buttons from layout and button group
  QList<QAbstractButton*> buttons = m_buttonGroup->buttons();
  for (QAbstractButton* btn : buttons) {
    m_buttonGroup->removeButton(btn);
    m_mainLayout->removeWidget(btn);
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
