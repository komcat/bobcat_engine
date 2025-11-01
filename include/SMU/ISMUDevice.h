// ISMUDevice.h - Interface for Source Measure Unit devices
#pragma once

#include "SMUTypes.h"
#include <string>
#include <memory>

/**
 * @brief Interface for SMU (Source Measure Unit) control
 *
 * Defines common operations for different SMU brands (Keithley, Keysight, etc.)
 *
 * An SMU can:
 * - Source voltage and measure current
 * - Source current and measure voltage
 * - Perform precision measurements with compliance limits
 *
 * Note: Some devices (like Keithley 6482) are picoammeters (measure-only),
 *       so not all methods will be applicable for all implementations.
 */
class ISMUDevice
{
public:
  virtual ~ISMUDevice() = default;

  // ========== Connection Management ==========
  virtual bool Connect() = 0;
  virtual void Disconnect() = 0;
  virtual bool IsConnected() const = 0;

  // ========== Device Information ==========
  virtual std::string GetDeviceName() const = 0;
  virtual std::string GetDeviceID() const = 0;          // Query *IDN?
  virtual SMUMode GetSupportedMode() const = 0;         // What mode does this device support?

  // ========== Output Control ==========
  virtual bool SetOutputOn() = 0;
  virtual bool SetOutputOff() = 0;
  virtual bool IsOutputOn() = 0;

  // ========== Source Control ==========
  /**
   * @brief Set source voltage (for voltage source mode)
   * @param voltage Voltage in volts
   * @return true if successful
   */
  virtual bool SetVoltage(double voltage) = 0;

  /**
   * @brief Set source current (for current source mode)
   * @param current Current in amperes
   * @return true if successful
   */
  virtual bool SetCurrent(double current) = 0;

  /**
   * @brief Set compliance limit
   * @param limit Compliance limit (current in A for voltage source, voltage in V for current source)
   * @return true if successful
   */
  virtual bool SetCompliance(double limit) = 0;

  // ========== Measurement ==========
  /**
   * @brief Read voltage measurement (from default/current channel)
   * @return Voltage in volts (or -999.0 on error)
   */
  virtual double ReadVoltage() = 0;

  /**
   * @brief Read current measurement (from default/current channel)
   * @return Current in amperes (or -999.0 on error)
   */
  virtual double ReadCurrent() = 0;

  /**
   * @brief Read measurement with detailed result
   * @return SMUMeasurement structure with value and status
   */
  virtual SMUMeasurement MeasureCurrent() = 0;
  virtual SMUMeasurement MeasureVoltage() = 0;

  // ========== Multi-Channel Measurement ==========
  /**
   * @brief Get total number of channels supported by this device
   * @return Number of channels (1-4)
   */
  virtual int GetChannelCount() const = 0;

  /**
   * @brief Get list of enabled channels
   * @return Vector of enabled channel numbers (e.g., {1, 2})
   */
  virtual std::vector<int> GetEnabledChannels() const = 0;

  /**
   * @brief Read current from a specific channel
   * @param channel Channel number (1-4)
   * @return Current in amperes (or -999.0 on error)
   */
  virtual double ReadChannel(int channel) = 0;

  /**
   * @brief Read all enabled channels at once
   * @return Vector of current values (one per enabled channel)
   *         Returns -999.0 for failed/disabled channels
   */
  virtual std::vector<double> ReadAllChannels() = 0;

  // ========== Configuration ==========
  /**
   * @brief Set measurement range
   * @param range Range value (e.g., 2e-9 for 2nA range)
   * @param autoRange If true, use auto-ranging
   * @return true if successful
   */
  virtual bool SetMeasurementRange(double range, bool autoRange = false) = 0;

  /**
   * @brief Set integration time (NPLC - Number of Power Line Cycles)
   * @param nplc Integration time (0.01 to 10 typical)
   * @return true if successful
   */
  virtual bool SetIntegrationTime(double nplc) = 0;

  /**
   * @brief Set zero check on/off (for low-current measurements)
   * @param enabled true to enable zero check
   * @return true if successful
   */
  virtual bool SetZeroCheck(bool enabled) = 0;

  /**
   * @brief Perform zero correction (auto-zero)
   * @return true if successful
   */
  virtual bool PerformZeroCorrection() = 0;

  // ========== Error Handling ==========
  virtual std::string GetLastError() const = 0;
  virtual void ClearError() = 0;
};

using SMUDevicePtr = std::shared_ptr<ISMUDevice>;
