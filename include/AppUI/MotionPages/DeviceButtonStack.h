// DeviceButtonStack.h
#pragma once
#include <QWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QButtonGroup>
#include <QStringList>
#include <QMap>

/**
 * @brief Vertical stack of device selection buttons
 * Replaces the device dropdown with large, easy-to-click buttons
 */
class DeviceButtonStack : public QWidget
{
  Q_OBJECT

public:
  explicit DeviceButtonStack(QWidget* parent = nullptr);
  ~DeviceButtonStack() = default;

  // Set available devices
  void setDevices(const QStringList& devices);

  // Get currently selected device
  QString getCurrentDevice() const;

  // Set selected device programmatically
  void setCurrentDevice(const QString& deviceName);

signals:
  void deviceChanged(const QString& deviceName);

private slots:
  void onButtonClicked(int id);

private:
  void setupUI();
  void clearButtons();

  QVBoxLayout* m_mainLayout;
  QButtonGroup* m_buttonGroup;
  QMap<int, QString> m_deviceMap;  // Button ID -> Device name
  QString m_currentDevice;
};
