// ITECDevice.h
#pragma once
#include "TECTypes.h"
#include <string>

/**
 * @brief Interface for TEC (Thermoelectric Cooler) device control
 *
 * All TEC hardware adapters must implement this interface.
 * Similar to IMotionControllerManager pattern.
 */
class ITECDevice {
public:
  virtual ~ITECDevice() = default;

  // ============================================================================
  // CONNECTION MANAGEMENT
  // ============================================================================

  /**
   * @brief Connect to TEC device
   * @return true if connection successful
   */
  virtual bool Connect() = 0;

  /**
   * @brief Disconnect from TEC device
   */
  virtual void Disconnect() = 0;

  /**
   * @brief Check if device is connected
   */
  virtual bool IsConnected() const = 0;

  /**
   * @brief Reconnect to device
   */
  virtual bool Reconnect() = 0;

  // ============================================================================
  // TEC CONTROL
  // ============================================================================

  /**
   * @brief Set TEC temperature setpoint
   * @param temperature Temperature in Celsius
   * @return Command result
   */
  virtual TECCommandResult SetTemperature(double temperature) = 0;

  /**
   * @brief Turn TEC on
   * @return Command result
   */
  virtual TECCommandResult TEC_On() = 0;

  /**
   * @brief Turn TEC off
   * @return Command result
   */
  virtual TECCommandResult TEC_Off() = 0;

  /**
   * @brief Read current TEC temperature
   * @return Command result with temperature value
   */
  virtual TECCommandResult ReadTemperature() = 0;

  // ============================================================================
  // LASER CONTROL
  // ============================================================================

  /**
   * @brief Set laser current
   * @param current Current in milliamps (mA)
   * @return Command result
   */
  virtual TECCommandResult SetLaserCurrent(double current) = 0;

  /**
   * @brief Turn laser on
   * @return Command result
   */
  virtual TECCommandResult Laser_On() = 0;

  /**
   * @brief Turn laser off
   * @return Command result
   */
  virtual TECCommandResult Laser_Off() = 0;

  /**
   * @brief Read current laser current
   * @return Command result with current value in mA
   */
  virtual TECCommandResult ReadLaserCurrent() = 0;

  // ============================================================================
  // STATUS QUERIES
  // ============================================================================

  /**
   * @brief Get device status (all states)
   * @return Current device status
   */
  virtual TECStatus GetStatus() const = 0;

  /**
   * @brief Get device name
   */
  virtual std::string GetDeviceName() const = 0;

  /**
   * @brief Get device type/model
   */
  virtual std::string GetDeviceType() const = 0;
};