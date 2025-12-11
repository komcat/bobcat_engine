// IEziIODevice.h - Interface for EziIO devices
#pragma once

#include "EziIO_Types.h"
#include <string>
#include <vector>
#include <cstdint>

/**
 * @brief Interface for EziIO devices
 *
 * Defines the contract that all EziIO device implementations must follow.
 * Allows for easy testing and future device type extensions.
 */
class IEziIODevice {
public:
  virtual ~IEziIODevice() = default;

  // ============================================================================
  // CONNECTION MANAGEMENT
  // ============================================================================
  virtual bool Connect() = 0;
  virtual bool Disconnect() = 0;
  virtual bool IsConnected() const = 0;

  // ============================================================================
  // INPUT OPERATIONS (uint64_t supports up to 64 I/O pins)
  // ============================================================================
  virtual EziIOError ReadInputs(uint64_t& inputs, uint64_t& latch) = 0;
  virtual EziIOError GetLastInputStatus(uint64_t& inputs, uint64_t& latch) = 0;
  virtual EziIOError ClearLatch(uint64_t latchMask) = 0;

  // ============================================================================
  // OUTPUT OPERATIONS (uint64_t supports up to 64 I/O pins)
  // ============================================================================
  virtual EziIOError ReadOutputs(uint64_t& outputs, uint64_t& status) = 0;
  virtual EziIOError GetLastOutputStatus(uint64_t& outputs, uint64_t& status) = 0;
  virtual EziIOError SetOutputs(uint64_t setMask, uint64_t clearMask) = 0;
  virtual EziIOError SetOutput(int pin, bool state) = 0;

  // ============================================================================
  // DEVICE INFO
  // ============================================================================
  virtual int GetDeviceId() const = 0;
  virtual std::string GetName() const = 0;
  virtual std::string GetIpAddress() const = 0;
  virtual int GetInputCount() const = 0;
  virtual int GetOutputCount() const = 0;

  // ============================================================================
  // PIN NAMING (Optional)
  // ============================================================================
  virtual void MapInputPinName(const std::string& pinName, int pinNumber) = 0;
  virtual void MapOutputPinName(const std::string& pinName, int pinNumber) = 0;
  virtual int GetInputPinByName(const std::string& pinName) const = 0;
  virtual int GetOutputPinByName(const std::string& pinName) const = 0;

  // NEW: Reverse lookup - get name from pin number
  virtual std::string GetInputPinName(int pinNumber) const = 0;
  virtual std::string GetOutputPinName(int pinNumber) const = 0;

  // NEW: Get all configured pin names (for UI dropdowns)
  virtual std::vector<std::string> GetAllInputPinNames() const = 0;
  virtual std::vector<std::string> GetAllOutputPinNames() const = 0;
};