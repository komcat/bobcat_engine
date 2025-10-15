// IEziIODevice.h - Interface for EziIO devices
#pragma once

#include "EziIO_Types.h"
#include <string>
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
  // INPUT OPERATIONS
  // ============================================================================
  virtual EziIOError ReadInputs(uint32_t& inputs, uint32_t& latch) = 0;
  virtual EziIOError GetLastInputStatus(uint32_t& inputs, uint32_t& latch) = 0;
  virtual EziIOError ClearLatch(uint32_t latchMask) = 0;

  // ============================================================================
  // OUTPUT OPERATIONS
  // ============================================================================
  virtual EziIOError ReadOutputs(uint32_t& outputs, uint32_t& status) = 0;
  virtual EziIOError GetLastOutputStatus(uint32_t& outputs, uint32_t& status) = 0;
  virtual EziIOError SetOutputs(uint32_t setMask, uint32_t clearMask) = 0;
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
};