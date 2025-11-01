// SMUJsonLoader.h - Load SMU device configuration from JSON
#pragma once

#include "SMUManager.h"
#include "SMUTypes.h"
#include <string>

/**
 * @brief Loads SMU device configuration from JSON file
 *
 * Supported device types:
 * - "keithley_6482"         - Keithley 6482 Picoammeter (dual channel)
 * - "keithley_2400"         - Keithley 2400 SourceMeter (future)
 * - "keysight_b2900"        - Keysight B2900 SMU (future)
 *
 * Expected JSON format:
 * {
 *   "devices": [
 *     {
 *       "name": "SMU1",
 *       "type": "keithley_6482",
 *       "visa_resource": "GPIB0::1::INSTR",
 *       "enabled": true,
 *       "default_voltage": 0.0,
 *       "default_current": 0.0,
 *       "default_compliance": 0.001,
 *       "measurement_nplc": 1,
 *       "auto_range": true,
 *       "fixed_range": 0.0
 *     }
 *   ]
 * }
 *
 * Example (Keithley 6482):
 * {
 *   "devices": [
 *     {
 *       "name": "Picoammeter_1",
 *       "type": "keithley_6482",
 *       "visa_resource": "GPIB0::1::INSTR",
 *       "enabled": true,
 *       "measurement_nplc": 5,
 *       "auto_range": true
 *     }
 *   ]
 * }
 */
class SMUJsonLoader
{
public:
  /**
   * @brief Load SMU configuration and populate manager
   * @param filePath Path to JSON configuration file
   * @param manager SMUManager to populate
   * @return true if successful, false otherwise
   */
  static bool LoadConfiguration(const std::string& filePath, SMUManager* manager);

private:
  static SMUDevicePtr CreateSMUDevice(const std::string& type,
    const std::string& visaResource,
    const SMUDeviceConfig& config);
};
