// PowerSupplyJsonLoader.h - Load power supply configuration from JSON
#pragma once

#include "PowerSupplyManager.h"
#include "KeysightPSUAdapter.h"
#include <string>

/**
 * @brief Loads power supply configuration from JSON file
 *
 * Expected JSON format:
 * {
 *   "server": {
 *     "host": "127.0.0.60",
 *     "port": 5000
 *   },
 *   "devices": [
 *     {
 *       "name": "PSU1",
 *       "type": "keysight_e36103b",
 *       "ip": "192.168.0.60",
 *       "port": 5025
 *     }
 *   ]
 * }
 */
class PowerSupplyJsonLoader
{
public:
  /**
   * @brief Load PSU configuration and populate manager
   * @param filePath Path to JSON configuration file
   * @param manager PowerSupplyManager to populate
   * @return true if successful, false otherwise
   */
  static bool loadConfiguration(const std::string& filePath, PowerSupplyManager* manager);

private:
  static PowerSupplyPtr createPowerSupply(const std::string& type,
    const std::string& name,
    const std::string& serverHost,
    int serverPort,
    const std::string& deviceIP,
    int devicePort);
};