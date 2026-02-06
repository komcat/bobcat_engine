// IMotionControllerManager.h
#pragma once

#include "MotionTypes.h"
#include <string>
#include <vector>
#include <map>

/**
 * @brief Interface for Motion Controller Managers
 *
 * All controller managers (ACS, PI, Newport, etc.) must implement this interface.
 * This allows UnifiedMotionManager to work with any controller brand without modification.
 */
class IMotionControllerManager {
public:
  virtual ~IMotionControllerManager() = default;

  // Device Management
  virtual void AddDevice(const MotionDevice& device) = 0;
  virtual void RemoveDevice(const std::string& deviceName) = 0;
  virtual bool HasDevice(const std::string& deviceName) const = 0;

  // Connection Management
  [[nodiscard]] virtual bool ConnectAll() = 0;
  virtual void DisconnectAll() = 0;
  [[nodiscard]] virtual bool ConnectDevice(const std::string& deviceName) = 0;
  [[nodiscard]] virtual bool DisconnectDevice(const std::string& deviceName) = 0;
  virtual bool ReconnectDevice(const std::string& deviceName) = 0;
  virtual int ConnectEnabledDevices() = 0;

  // Device Queries
  virtual std::vector<std::string> GetDeviceNames() const = 0;
  virtual std::vector<std::string> GetConnectedDeviceNames() const = 0;
  virtual std::map<std::string, bool> GetConnectionStatus() const = 0;

  // Statistics
  virtual size_t GetControllerCount() const = 0;
  virtual size_t GetConnectedCount() const = 0;

  // Controller Type Identification
  virtual std::string GetControllerType() const = 0; // Returns "ACS", "PI", etc.

  // Generic controller access - returns void* that must be cast to appropriate type
  // For type safety, use the specific GetACSController() or GetPIController() methods
  virtual void* GetController(const std::string& deviceName) = 0;
};