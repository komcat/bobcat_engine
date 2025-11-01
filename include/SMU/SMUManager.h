// SMUManager.h - Manages multiple SMU devices
#pragma once

#include "ISMUDevice.h"
#include <QObject>
#include <map>
#include <string>
#include <vector>
#include <memory>

/**
 * @brief Manager for multiple SMU (Source Measure Unit) devices
 *
 * Handles device lifecycle, selection, and command routing
 *
 * Usage:
 *   SMUManager* manager = new SMUManager();
 *   SMUJsonLoader::loadConfiguration("config/smu_devices.json", manager);
 *
 *   SMUDevicePtr smu = manager->GetDevice("SMU1");
 *   if (smu && smu->Connect()) {
 *     smu->SetOutputOn();
 *     double current = smu->ReadCurrent();
 *   }
 */
class SMUManager : public QObject
{
  Q_OBJECT

public:
  explicit SMUManager(QObject* parent = nullptr);
  ~SMUManager() = default;

  // ========== Device Management ==========
  void AddDevice(const std::string& name, SMUDevicePtr device);
  void RemoveDevice(const std::string& name);
  void ClearAllDevices();

  // ========== Device Access ==========
  SMUDevicePtr GetDevice(const std::string& name) const;
  std::vector<std::string> GetDeviceNames() const;
  int GetDeviceCount() const { return static_cast<int>(m_devices.size()); }
  bool HasDevice(const std::string& name) const;

  // ========== Batch Operations ==========
  /**
   * @brief Connect all devices
   * @return true if all devices connected successfully
   */
  bool ConnectAll();

  /**
   * @brief Disconnect all devices
   */
  void DisconnectAll();

  /**
   * @brief Turn off all device outputs (safety)
   */
  void OutputOffAll();

signals:
  void deviceAdded(const QString& name);
  void deviceRemoved(const QString& name);
  void deviceConnectionChanged(const QString& name, bool connected);
  void measurementReady(const QString& deviceName, double value);

private:
  std::map<std::string, SMUDevicePtr> m_devices;
};
