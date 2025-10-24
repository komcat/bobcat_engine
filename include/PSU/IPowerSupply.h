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
  virtual bool setOutputOn() = 0;
  virtual bool setOutputOff() = 0;

  // Voltage/Current control
  virtual bool setVoltage(double voltage) = 0;
  virtual bool setCurrent(double current) = 0;

  // Protection settings
  virtual bool setOVP(double voltage) = 0;  // Over-Voltage Protection
  virtual bool setOCP(double current) = 0;  // Over-Current Protection

  // Read measurements
  virtual double readVoltage() = 0;
  virtual double readCurrent() = 0;

  // Error handling
  virtual std::string getLastError() const = 0;
};

using PowerSupplyPtr = std::shared_ptr<IPowerSupply>;