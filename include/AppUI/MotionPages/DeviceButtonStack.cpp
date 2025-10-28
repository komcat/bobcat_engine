// DeviceButtonStack.cpp
#include "DeviceButtonStack.h"

DeviceButtonStack::DeviceButtonStack(QWidget* parent)
  : QWidget(parent)
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

  // Set fixed width for the button stack
  setFixedWidth(120);

  // Style for the container
  setStyleSheet(
    "DeviceButtonStack {"
    "  background-color: #ecf0f1;"
    "  border-right: 2px solid #bdc3c7;"
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
    btn->setMinimumHeight(50);
    btn->setMaximumHeight(80);
    btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    // Style for device buttons
    btn->setStyleSheet(
      "QPushButton {"
      "  background-color: white;"
      "  color: #2c3e50;"
      "  border: 2px solid #bdc3c7;"
      "  border-radius: 6px;"
      "  font-size: 12px;"
      "  font-weight: bold;"
      "  padding: 8px;"
      "  text-align: center;"
      "}"
      "QPushButton:hover {"
      "  background-color: #d5dbdb;"
      "  border-color: #95a5a6;"
      "}"
      "QPushButton:checked {"
      "  background-color: #3498db;"
      "  color: white;"
      "  border-color: #2980b9;"
      "}"
      "QPushButton:checked:hover {"
      "  background-color: #2980b9;"
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
