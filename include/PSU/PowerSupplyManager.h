// PowerSupplyManager.h - Manages multiple power supply devices
#pragma once

#include "IPowerSupply.h"
#include <QObject>
#include <map>
#include <string>
#include <vector>
#include <memory>

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

  // Batch operations
  bool connectAll();
  void disconnectAll();

signals:
  void deviceAdded(const QString& name);
  void deviceRemoved(const QString& name);
  void deviceConnectionChanged(const QString& name, bool connected);

private:
  std::map<std::string, PowerSupplyPtr> m_devices;
};