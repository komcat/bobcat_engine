// pi_controller_manager.h
#pragma once

#include "IMotionControllerManager.h"
#include "pi_controller.h"
#include "MotionTypes.h"
#include <string>
#include <map>
#include <vector>
#include <memory>
#include <iostream>

/**
 * @brief PI Controller Manager
 *
 * Manages multiple PI motion controllers.
 * Implements IMotionControllerManager interface for use with UnifiedMotionManager.
 */
class PIControllerManager : public IMotionControllerManager {
public:
  PIControllerManager();
  ~PIControllerManager() override;

  // ============================================================================
  // IMotionControllerManager Interface Implementation
  // ============================================================================

  void AddDevice(const MotionDevice& device) override;
  void RemoveDevice(const std::string& deviceName) override;
  bool HasDevice(const std::string& deviceName) const override;

  bool ConnectAll() override;
  void DisconnectAll() override;
  bool ConnectDevice(const std::string& deviceName) override;
  bool DisconnectDevice(const std::string& deviceName) override;
  bool ReconnectDevice(const std::string& deviceName) override;
  int ConnectEnabledDevices() override;

  std::vector<std::string> GetDeviceNames() const override;
  std::vector<std::string> GetConnectedDeviceNames() const override;
  std::map<std::string, bool> GetConnectionStatus() const override;

  size_t GetControllerCount() const override;
  size_t GetConnectedCount() const override;

  std::string GetControllerType() const override { return "PI"; }

  void* GetController(const std::string& deviceName) override;

  // ============================================================================
  // PI-Specific Methods (Type-Safe Access)
  // ============================================================================

  /**
   * @brief Get PI controller by device name (type-safe)
   * @param deviceName Name of the device
   * @return Pointer to PIController or nullptr if not found
   */
  PIController* GetPIController(const std::string& deviceName);

  /**
   * @brief Get const PI controller by device name
   */
  const PIController* GetPIController(const std::string& deviceName) const;

  // ============================================================================
  // Additional Management Methods
  // ============================================================================

  /**
   * @brief Check if a device is currently connected
   */
  bool IsDeviceConnected(const std::string& deviceName) const;

  /**
   * @brief Get device configuration
   */
  MotionDevice GetDeviceConfig(const std::string& deviceName) const;

  /**
   * @brief Update device configuration
   */
  bool UpdateDeviceConfig(const MotionDevice& device);

  /**
   * @brief Get list of devices that failed to connect
   */
  std::vector<std::string> GetFailedDevices() const;

  /**
   * @brief Clear connection error for a device
   */
  void ClearConnectionError(const std::string& deviceName);

private:
  // Controller instances mapped by device name
  std::map<std::string, std::unique_ptr<PIController>> m_controllers;

  // Device configurations
  std::map<std::string, MotionDevice> m_deviceConfigs;

  // Connection status tracking
  std::map<std::string, bool> m_connectionStatus;

  // Track connection errors
  std::map<std::string, std::string> m_connectionErrors;

  // Helper methods
  PIController* CreateController(const MotionDevice& device);
  bool ValidateDeviceConfig(const MotionDevice& device) const;
  void LogDeviceInfo(const std::string& deviceName, const std::string& message) const;
};