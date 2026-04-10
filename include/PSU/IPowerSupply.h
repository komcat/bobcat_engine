// IPowerSupply.h - Interface for power supply devices
#pragma once

#include <string>
#include <memory>

/**
 * @brief Interface for power supply control
 * Defines common operations for different PSU brands
 */
class IPowerSupply
{
public:
  virtual ~IPowerSupply() = default;

  // Connection management
  virtual bool connect() = 0;
  virtual void disconnect() = 0;
  virtual bool isConnected() const = 0;

  // Device info
  virtual std::string getDeviceName() const = 0;
  virtual std::string getDeviceID() const = 0;

  // Output control
  // channel parameter: 1-based channel number (default = 1 for single-channel devices)
  virtual bool setOutputOn(int channel = 1) = 0;
  virtual bool setOutputOff(int channel = 1) = 0;

  // Voltage/Current control
  // channel parameter: 1-based channel number (default = 1 for single-channel devices)
  virtual bool setVoltage(double voltage, int channel = 1) = 0;
  virtual bool setCurrent(double current, int channel = 1) = 0;

  // Protection settings
  // channel parameter: 1-based channel number (default = 1 for single-channel devices)
  virtual bool setOVP(double voltage, int channel = 1) = 0;  // Over-Voltage Protection
  virtual bool setOCP(double current, int channel = 1) = 0;  // Over-Current Protection

  // Read measurements
  // channel parameter: 1-based channel number (default = 1 for single-channel devices)
  virtual double readVoltage(int channel = 1) = 0;
  virtual double readCurrent(int channel = 1) = 0;

  // Read programmed (set) voltage level
  // Returns the voltage setting value (not measured), or -1.0 on error/unsupported.
  // Default: returns readVoltage() as fallback for adapters that don't distinguish.
  virtual double readSetVoltage(int channel = 1) { return readVoltage(channel); }

  // Read programmed (set) current level
  // Returns the current setting value (not measured), or -1.0 on error/unsupported.
  // Default: returns readCurrent() as fallback for adapters that don't distinguish.
  virtual double readSetCurrent(int channel = 1) { return readCurrent(channel); }

  // Read output state
  // Returns true if output is ON, false if OFF or error
  virtual bool readOutputState(int channel = 1) = 0;

  // Error handling
  virtual std::string getLastError() const = 0;
};

using PowerSupplyPtr = std::shared_ptr<IPowerSupply>;