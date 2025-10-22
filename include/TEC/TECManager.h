// TECManager.h
#pragma once
#include "ITECDevice.h"
#include "TECTypes.h"
#include <string>
#include <map>
#include <vector>
#include <memory>

/**
 * @brief TEC Manager - Manages multiple TEC devices
 *
 * Follows the same pattern as UnifiedMotionManager.
 * Routes devices to appropriate hardware adapters based on device type.
 */
class TECManager {
public:
  TECManager();
  ~TECManager();

  // ============================================================================
  // DEVICE MANAGEMENT
  // ============================================================================

  /**
   * @brief Add a TEC device
   * Device is automatically routed to correct adapter based on TypeController
   */
  void AddDevice(const TECDevice& device);

  /**
   * @brief Remove a device
   */
  void RemoveDevice(const std::string& deviceName);

  /**
   * @brief Check if device exists
   */
  bool HasDevice(const std::string& deviceName) const;

  /**
   * @brief Get device configuration
   */
  bool GetDeviceConfig(const std::string& deviceName, TECDevice& outDevice) const;

  // ============================================================================
  // CONNECTION MANAGEMENT
  // ============================================================================

  /**
   * @brief Connect to all devices
   * @return Number of devices successfully connected
   */
  int ConnectAll();

  /**
   * @brief Disconnect all devices
   */
  void DisconnectAll();

  /**
   * @brief Connect to specific device
   */
  bool ConnectDevice(const std::string& deviceName);

  /**
   * @brief Disconnect specific device
   */
  bool DisconnectDevice(const std::string& deviceName);

  /**
   * @brief Reconnect specific device
   */
  bool ReconnectDevice(const std::string& deviceName);

  /**
   * @brief Connect only enabled devices
   * @return Number of devices successfully connected
   */
  int ConnectEnabledDevices();

  // ============================================================================
  // TEC CONTROL
  // ============================================================================

  /**
   * @brief Set TEC temperature setpoint
   */
  TECCommandResult SetTemperature(const std::string& deviceName, double temperature);

  /**
   * @brief Turn TEC on
   */
  TECCommandResult TEC_On(const std::string& deviceName);

  /**
   * @brief Turn TEC off
   */
  TECCommandResult TEC_Off(const std::string& deviceName);

  /**
   * @brief Read current temperature
   */
  TECCommandResult ReadTemperature(const std::string& deviceName);

  // ============================================================================
  // LASER CONTROL
  // ============================================================================

  /**
   * @brief Set laser current (in mA)
   */
  TECCommandResult SetLaserCurrent(const std::string& deviceName, double current);

  /**
   * @brief Turn laser on
   */
  TECCommandResult Laser_On(const std::string& deviceName);

  /**
   * @brief Turn laser off
   */
  TECCommandResult Laser_Off(const std::string& deviceName);

  /**
   * @brief Read current laser current (in mA)
   */
  TECCommandResult ReadLaserCurrent(const std::string& deviceName);

  // ============================================================================
  // STATUS QUERIES
  // ============================================================================

  /**
   * @brief Get device status
   */
  TECStatus GetDeviceStatus(const std::string& deviceName) const;

  /**
   * @brief Get all device names
   */
  std::vector<std::string> GetAllDeviceNames() const;

  /**
   * @brief Get connected device names
   */
  std::vector<std::string> GetConnectedDeviceNames() const;

  /**
   * @brief Get connection status map
   */
  std::map<std::string, bool> GetConnectionStatus() const;

  /**
   * @brief Get total device count
   */
  size_t GetTotalDeviceCount() const;

  /**
   * @brief Get connected device count
   */
  size_t GetConnectedDeviceCount() const;

  // ============================================================================
  // DIRECT DEVICE ACCESS (for advanced usage)
  // ============================================================================

  /**
   * @brief Get raw device pointer
   * @return Device pointer or nullptr if not found
   */
  ITECDevice* GetDevice(const std::string& deviceName);

private:
  // Device storage: deviceName -> device adapter
  std::map<std::string, std::unique_ptr<ITECDevice>> m_devices;

  // Device configurations: deviceName -> TECDevice config
  std::map<std::string, TECDevice> m_deviceConfigs;

  /**
   * @brief Create device adapter based on controller type
   * @param config Device configuration
   * @return Unique pointer to device adapter (nullptr if type unknown)
   */
  std::unique_ptr<ITECDevice> CreateDeviceAdapter(const TECDevice& config);

  /**
   * @brief Normalize controller type string (uppercase, trim)
   */
  std::string NormalizeType(const std::string& type) const;
};