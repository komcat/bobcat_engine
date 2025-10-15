// unified_motion_manager.h (Refactored with Interface)
#pragma once

#include "IMotionControllerManager.h"
#include "acs_controller_manager.h"
#include "pi_controller_manager.h"
#include "MotionTypes.h"

#include <string>
#include <map>
#include <vector>
#include <memory>

/**
 * @brief Unified Motion Manager - Manages multiple controller brands
 *
 * Automatically routes devices to appropriate controller manager based on device type.
 * Supports easy addition of new controller brands through IMotionControllerManager interface.
 */
class UnifiedMotionManager {
public:
  UnifiedMotionManager();
  ~UnifiedMotionManager();

  // Device management - automatically routes to correct manager based on Type
  void AddDevice(const MotionDevice& device);
  void RemoveDevice(const std::string& deviceName);
  bool HasDevice(const std::string& deviceName) const;

  // Get device type
  std::string GetDeviceType(const std::string& deviceName) const;

  // Connection management
  bool ConnectAll();
  void DisconnectAll();
  bool ConnectDevice(const std::string& deviceName);
  bool DisconnectDevice(const std::string& deviceName);
  bool ReconnectDevice(const std::string& deviceName);
  int ConnectEnabledDevices();

  // ============================================================================
  // MOTION CONTROL METHODS
  // ============================================================================

  // Move to absolute position (multi-axis)
  bool MoveToAbs(const std::string& deviceName,
    double x, double y, double z,
    double velocity = 100.0,
    bool blocking = true);

  // Move single axis to position
  bool MoveToPosition(const std::string& deviceName,
    const std::string& axis,
    double position,
    bool blocking = true);

  // Move to named position
  bool MoveToNamedPosition(const std::string& deviceName,
    const std::string& positionName);

  // Move relative (single axis)
  bool MoveRelative(const std::string& deviceName,
    const std::string& axis,
    double distance,
    bool blocking = true);

  // Emergency stop methods
  bool EmergencyStop(const std::string& deviceName);
  bool EmergencyStopAll();

  // Additional motion control
  bool StopAxis(const std::string& deviceName, const std::string& axis);
  bool HomeAxis(const std::string& deviceName, const std::string& axis);
  bool IsMoving(const std::string& deviceName, const std::string& axis);
  bool IsDeviceInMotion(const std::string& deviceName);

  // Position queries
  bool GetPosition(const std::string& deviceName,
    const std::string& axis,
    double& position);
  PositionStruct GetCurrentPosition(const std::string& deviceName);

  // Velocity control
  bool SetVelocity(const std::string& deviceName,
    const std::string& axis,
    double velocity);
  bool GetVelocity(const std::string& deviceName,
    const std::string& axis,
    double& velocity);

  // Servo control
  bool EnableServo(const std::string& deviceName,
    const std::string& axis,
    bool enable);

  // ============================================================================
  // CONTROLLER ACCESS (Type-safe methods)
  // ============================================================================

  // Type-safe controller access
  ACSController* GetACSController(const std::string& deviceName);
  PIController* GetPIController(const std::string& deviceName);

  // Generic controller access - check type first
  void* GetController(const std::string& deviceName, std::string& outType);

  // ============================================================================
  // DEVICE QUERIES
  // ============================================================================

  std::vector<std::string> GetAllDeviceNames() const;
  std::vector<std::string> GetDeviceNamesByType(const std::string& type) const;
  std::vector<std::string> GetConnectedDeviceNames() const;
  std::map<std::string, bool> GetConnectionStatus() const;

  // Statistics
  size_t GetTotalDeviceCount() const;
  size_t GetDeviceCountByType(const std::string& type) const;
  size_t GetConnectedDeviceCount() const;

  // Get list of supported controller types
  std::vector<std::string> GetSupportedControllerTypes() const;

  // ============================================================================
  // MANAGER ACCESS (for advanced usage)
  // ============================================================================

  // Get manager by type
  IMotionControllerManager* GetManagerByType(const std::string& type);

  // Direct manager access (legacy compatibility)
  ACSControllerManager* GetACSManager();
  PIControllerManager* GetPIManager();

private:
  // Map of controller type to manager
  std::map<std::string, std::unique_ptr<IMotionControllerManager>> m_managers;

  // Track device types for routing
  std::map<std::string, std::string> m_deviceTypes;  // deviceName -> type

  // Helper method to get manager for a device
  IMotionControllerManager* GetManagerForDevice(const std::string& deviceName);

  // Helper to normalize type strings (case-insensitive)
  std::string NormalizeType(const std::string& type) const;

  // Register a new controller manager
  void RegisterManager(const std::string& type, std::unique_ptr<IMotionControllerManager> manager);
};