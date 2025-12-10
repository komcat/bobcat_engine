// EziIOManager.h - Clean, maintainable manager
#pragma once

#include "IEziIODevice.h"
#include "EziIODevice.h"
#include "WagoIODevice.h"
#include "EziIO_Types.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <thread>
#include <atomic>

/**
 * @brief Manages multiple IO devices (EziIO and WAGO)
 *
 * Provides centralized control and monitoring of all IO devices.
 * Supports automatic polling, observer notifications, and JSON configuration.
 */
class EziIOManager {
public:
  EziIOManager();
  ~EziIOManager();

  // ============================================================================
  // INITIALIZATION
  // ============================================================================
  bool Initialize();
  void Shutdown();
  bool IsInitialized() const { return m_initialized; }

  // ============================================================================
  // DEVICE MANAGEMENT
  // ============================================================================
  bool AddDevice(const EziIODeviceConfig& config);
  bool AddWagoDevice(const EziIODeviceConfig& config);
  bool RegisterDevice(std::shared_ptr<IEziIODevice> device);
  bool RemoveDevice(const std::string& deviceName);
  bool HasDevice(const std::string& deviceName) const;

  // Get device by name or ID
  IEziIODevice* GetDevice(const std::string& deviceName);
  IEziIODevice* GetDeviceById(int deviceId);

  // ============================================================================
  // CONNECTION MANAGEMENT
  // ============================================================================
  bool ConnectAll();
  void DisconnectAll();
  bool ConnectDevice(const std::string& deviceName);
  bool DisconnectDevice(const std::string& deviceName);
  int ConnectEnabledDevices();

  // ============================================================================
  // INPUT OPERATIONS
  // ============================================================================
  EziIOError ReadInputs(const std::string& deviceName, uint32_t& inputs, uint32_t& latch);
  EziIOError GetLastInputStatus(const std::string& deviceName, uint32_t& inputs, uint32_t& latch);
  EziIOError ClearLatch(const std::string& deviceName, uint32_t latchMask);

  // ============================================================================
  // OUTPUT OPERATIONS
  // ============================================================================
  EziIOError ReadOutputs(const std::string& deviceName, uint32_t& outputs, uint32_t& status);
  EziIOError GetLastOutputStatus(const std::string& deviceName, uint32_t& outputs, uint32_t& status);
  EziIOError SetOutputs(const std::string& deviceName, uint32_t setMask, uint32_t clearMask);
  EziIOError SetOutput(const std::string& deviceName, int pin, bool state);

  // Pin name convenience methods
  EziIOError SetOutputByName(const std::string& deviceName, const std::string& pinName, bool state);
  bool GetInputByName(const std::string& deviceName, const std::string& pinName, bool& state);

  // ============================================================================
  // POLLING THREAD
  // ============================================================================
  void StartPolling(unsigned int intervalMs = 100);
  void StopPolling();
  void SetPollingInterval(unsigned int intervalMs);
  bool IsPolling() const;

  // ============================================================================
  // DEVICE QUERIES
  // ============================================================================
  std::vector<std::string> GetAllDeviceNames() const;
  std::vector<std::string> GetConnectedDeviceNames() const;
  std::map<std::string, bool> GetConnectionStatus() const;
  size_t GetTotalDeviceCount() const { return m_devices.size(); }
  size_t GetConnectedDeviceCount() const { return m_connectedDeviceCount; }

  // ============================================================================
  // CONFIGURATION
  // ============================================================================
  void SetLogging(bool enable) { m_enableLogging = enable; }
  bool IsLoggingEnabled() const { return m_enableLogging; }

private:
  // Initialization state
  bool m_initialized;
  bool m_enableLogging;

  // Device storage
  std::vector<std::shared_ptr<IEziIODevice>> m_devices;
  std::map<std::string, std::shared_ptr<IEziIODevice>> m_deviceNameMap;
  std::map<int, std::shared_ptr<IEziIODevice>> m_deviceIdMap;
  std::atomic<int> m_connectedDeviceCount;

  // Polling thread
  std::thread* m_pollingThread;
  std::atomic<bool> m_stopPolling;
  unsigned int m_pollingInterval;

  // Private methods
  void PollingThreadFunc();
  EziIOError ValidateDevice(const std::string& deviceName, IEziIODevice** device,
    const std::string& operation);
};
