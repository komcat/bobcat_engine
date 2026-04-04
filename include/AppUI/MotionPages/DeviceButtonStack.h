// DeviceButtonStack.h
#pragma once
#include <QWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QButtonGroup>
#include <QStringList>
#include <QMap>
#include <QLabel>
#include <vector>
#include <string>

// Forward declarations
class UnifiedMotionManager;

/**
 * @brief Vertical stack of device selection buttons with velocity controls
 * Shows device buttons at top, velocity presets below
 */
class DeviceButtonStack : public QWidget
{
  Q_OBJECT

public:
  explicit DeviceButtonStack(QWidget* parent = nullptr);
  ~DeviceButtonStack() = default;

  // Set motion manager for connection status checking
  void setMotionManager(UnifiedMotionManager* motionManager);

  // Set available devices
  void setDevices(const QStringList& devices);

  // Refresh device list showing only connected devices, sorted alphabetically
  void refreshDevices();

  // Get currently selected device
  QString getCurrentDevice() const;

  // Set selected device programmatically
  void setCurrentDevice(const QString& deviceName);

  // Velocity presets
  void updateVelocityPresets(const QString& deviceType);

signals:
  void deviceChanged(const QString& deviceName);
  void velocityPresetClicked(double velocity);

private slots:
  void onButtonClicked(int id);

private:
  void setupUI();
  void clearButtons();

  QVBoxLayout* m_mainLayout;
  QButtonGroup* m_buttonGroup;
  QMap<int, QString> m_deviceMap;  // Button ID -> Device name
  QString m_currentDevice;

  // Container for dynamically added device buttons
  QWidget* m_deviceButtonsContainer;
  QVBoxLayout* m_deviceButtonsLayout;

  // Motion manager for connection status
  UnifiedMotionManager* m_motionManager;

  // Velocity presets
  QWidget* m_velocityPresetsWidget;
  QList<QPushButton*> m_velocityButtons;
};
