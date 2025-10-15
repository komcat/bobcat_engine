// EziIODevice.h - Concrete EziIO device implementation
#pragma once

#include "IEziIODevice.h"
#include "EziIO_Types.h"
#include "FAS_EziMOTIONPlusE.h"
#include <string>
#include <map>
#include <mutex>
#include <cstdint>

/**
 * @brief Concrete implementation of EziIO device
 *
 * Handles communication with physical EziIO hardware via TCP/IP.
 * Supports both 8-pin and 16-pin configurations.
 */
class EziIODevice : public IEziIODevice {
public:
  EziIODevice(const EziIODeviceConfig& config);
  virtual ~EziIODevice();

  // ============================================================================
  // CONNECTION MANAGEMENT
  // ============================================================================
  bool Connect() override;
  bool Disconnect() override;
  bool IsConnected() const override { return m_isConnected; }

  // ============================================================================
  // INPUT OPERATIONS
  // ============================================================================
  EziIOError ReadInputs(uint32_t& inputs, uint32_t& latch) override;
  EziIOError GetLastInputStatus(uint32_t& inputs, uint32_t& latch) override;
  EziIOError ClearLatch(uint32_t latchMask) override;

  // ============================================================================
  // OUTPUT OPERATIONS
  // ============================================================================
  EziIOError ReadOutputs(uint32_t& outputs, uint32_t& status) override;
  EziIOError GetLastOutputStatus(uint32_t& outputs, uint32_t& status) override;
  EziIOError SetOutputs(uint32_t setMask, uint32_t clearMask) override;
  EziIOError SetOutput(int pin, bool state) override;

  // ============================================================================
  // DEVICE INFO
  // ============================================================================
  int GetDeviceId() const override { return m_config.DeviceId; }
  std::string GetName() const override { return m_config.Name; }
  std::string GetIpAddress() const override { return m_config.IpAddress; }
  int GetInputCount() const override { return m_config.InputCount; }
  int GetOutputCount() const override { return m_config.OutputCount; }

  // ============================================================================
  // PIN NAMING
  // ============================================================================
  void MapInputPinName(const std::string& pinName, int pinNumber) override;
  void MapOutputPinName(const std::string& pinName, int pinNumber) override;
  int GetInputPinByName(const std::string& pinName) const override;
  int GetOutputPinByName(const std::string& pinName) const override;

private:
  // Configuration
  EziIODeviceConfig m_config;

  // Connection state
  bool m_isConnected;
  uint8_t m_ipBytes[4];

  // Pin masks for output operations
  static const uint32_t OUTPUT_PIN_MASKS_16[16];
  static const uint32_t OUTPUT_PIN_MASKS_8[8];
  const uint32_t* m_currentOutputMasks;

  // Status tracking
  std::mutex m_statusMutex;
  IOStatus m_lastStatus;

  // Pin name mapping
  std::map<std::string, int> m_inputPinNames;
  std::map<std::string, int> m_outputPinNames;

  // ============================================================================
  // PRIVATE HELPERS
  // ============================================================================
  void ParseIpAddress();
  uint32_t GetOutputPinMask(int pin) const;
};