// PowerSupplyManager.h - Manages multiple power supply devices
#pragma once

#include "IPowerSupply.h"
#include <QObject>
#include <map>
#include <string>
#include <vector>
#include <memory>

/**
 * @brief Device metadata for display purposes
 */
struct PowerSupplyDeviceInfo
{
  std::string type;
  std::string ip;
  int port = 5025;
  int channels = 1;
};

/**
 * @brief Manager for multiple power supply devices
 * Handles device lifecycle, selection, and command routing
 */
class PowerSupplyManager : public QObject
{
  Q_OBJECT

public:
  explicit PowerSupplyManager(QObject* parent = nullptr);
  ~PowerSupplyManager() = default;

  // Device management
  void addPowerSupply(const std::string& name, PowerSupplyPtr psu);
  void removePowerSupply(const std::string& name);
  void clearAllDevices();

  // Device access
  PowerSupplyPtr getPowerSupply(const std::string& name) const;
  std::vector<std::string> getDeviceNames() const;
  int getDeviceCount() const { return static_cast<int>(m_devices.size()); }
  bool hasDevice(const std::string& name) const;

  // Channel count management
  void setDeviceChannelCount(const std::string& name, int channels);
  int getDeviceChannelCount(const std::string& name) const;

  // Device info management
  void setDeviceInfo(const std::string& name, const PowerSupplyDeviceInfo& info);
  PowerSupplyDeviceInfo getDeviceInfo(const std::string& name) const;

  // Batch operations
  bool connectAll();
  void disconnectAll();

  /**
   * @brief Disable all PSU outputs without disconnecting (E-stop safety action)
   * Turns off output on all channels of all connected devices.
   */
  void disableAllOutputs();

signals:
  void deviceAdded(const QString& name);
  void deviceRemoved(const QString& name);
  void deviceConnectionChanged(const QString& name, bool connected);

private:
  std::map<std::string, PowerSupplyPtr> m_devices;
  std::map<std::string, int> m_deviceChannels; // Track channel count per device
  std::map<std::string, PowerSupplyDeviceInfo> m_deviceInfo; // Device metadata
};