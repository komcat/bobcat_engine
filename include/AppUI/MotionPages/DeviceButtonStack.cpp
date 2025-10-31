// DeviceButtonStack.cpp
#include "DeviceButtonStack.h"
#include <QFrame>

DeviceButtonStack::DeviceButtonStack(QWidget* parent)
  : QWidget(parent)
  , m_posNameLabel(nullptr)
  , m_posXLabel(nullptr)
  , m_posYLabel(nullptr)
  , m_posZLabel(nullptr)
  , m_posULabel(nullptr)
  , m_posVLabel(nullptr)
  , m_posWLabel(nullptr)
  , m_waypointsLabel(nullptr)
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
  m_waypointsLabel = new QLabel("Waypoints: -");
  m_waypointsLabel->setWordWrap(true);
  m_waypointsLabel->setStyleSheet("QLabel { color: #0e639c; font-size: 11px; font-weight: bold; }");

  QList<QLabel*> labels = { m_posNameLabel, m_posXLabel, m_posYLabel,
                           m_posZLabel, m_posULabel, m_posVLabel, m_posWLabel,
                           m_waypointsLabel };
  for (auto* label : labels) {
    if (label != m_waypointsLabel) {
      label->setStyleSheet("QLabel { color: #d4d4d4; font-size: 12px; }");
    }
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

    // Style for device buttons - Dark Theme
    btn->setStyleSheet(
      "QPushButton {"
      "  background-color: #2d2d30;"
      "  color: #d4d4d4;"
      "  border: 2px solid #3f3f46;"
      "  border-radius: 6px;"
      "  font-size: 13px;"
      "  font-weight: bold;"
      "  padding: 6px;"
      "  text-align: center;"
      "}"
      "QPushButton:hover {"
      "  background-color: #3c3c3c;"
      "  border-color: #555555;"
      "}"
      "QPushButton:checked {"
      "  background-color: #0e639c;"
      "  color: white;"
      "  border-color: #0e639c;"
      "}"
      "QPushButton:checked:hover {"
      "  background-color: #1177bb;"
      "}"
    );

    m_mainLayout->addWidget(btn);
    m_buttonGroup->addButton(btn, buttonId);
    m_deviceMap[buttonId] = device;

    buttonId++;
  }

  // Add stretch to push buttons to top
  m_mainLayout->addStretch();

  // Select first device by default
  if (!devices.isEmpty()) {
    m_buttonGroup->button(0)->setChecked(true);
    m_currentDevice = devices.first();
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

void DeviceButtonStack::setSelectedPosition(const QString& name, double x, double y, double z,
  double u, double v, double w,
  const std::vector<std::string>& waypoints)
{
  m_selectedPositionName = name;

  m_posNameLabel->setText(QString("<b>%1</b>").arg(name));
  m_posXLabel->setText(QString("X: %1").arg(x, 0, 'f', 3));
  m_posYLabel->setText(QString("Y: %1").arg(y, 0, 'f', 3));
  m_posZLabel->setText(QString("Z: %1").arg(z, 0, 'f', 3));
  m_posULabel->setText(QString("U: %1").arg(u, 0, 'f', 3));
  m_posVLabel->setText(QString("V: %1").arg(v, 0, 'f', 3));
  m_posWLabel->setText(QString("W: %1").arg(w, 0, 'f', 3));

  if (waypoints.empty()) {
    m_waypointsLabel->setText("Waypoints: None");
  }
  else {
    QString waypointStr = "WP: ";
    for (size_t i = 0; i < waypoints.size(); ++i) {
      waypointStr += QString::fromStdString(waypoints[i]);
      if (i < waypoints.size() - 1) {
        waypointStr += " → ";
      }
    }
    m_waypointsLabel->setText(waypointStr);
  }
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
  m_waypointsLabel->setText("Waypoints: -");
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
