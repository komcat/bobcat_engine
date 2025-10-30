// DeviceButtonStack.h
#pragma once
#include <QWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QButtonGroup>
#include <QStringList>
#include <QMap>
#include <QLabel>
#include <vector>
#include <string>

/**
 * @brief Vertical stack of device selection buttons with selected position display
 * Replaces the device dropdown with large, easy-to-click buttons
 * Also shows information about the currently selected position
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

  // Update selected position info display
  void setSelectedPosition(const QString& name, double x, double y, double z,
    double u, double v, double w,
    const std::vector<std::string>& waypoints = {});
  void clearSelectedPosition();

signals:
  void deviceChanged(const QString& deviceName);

private slots:
  void onButtonClicked(int id);

private:
  void setupUI();
  void clearButtons();
  void setupPositionInfoSection();

  QVBoxLayout* m_mainLayout;
  QButtonGroup* m_buttonGroup;
  QMap<int, QString> m_deviceMap;  // Button ID -> Device name
  QString m_currentDevice;

  // Selected position info widgets
  QLabel* m_posNameLabel;
  QLabel* m_posXLabel;
  QLabel* m_posYLabel;
  QLabel* m_posZLabel;
  QLabel* m_posULabel;
  QLabel* m_posVLabel;
  QLabel* m_posWLabel;
  QLabel* m_waypointsLabel;
  QString m_selectedPositionName;
};
