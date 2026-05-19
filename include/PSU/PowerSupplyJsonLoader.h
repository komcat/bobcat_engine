// PowerSupplyJsonLoader.h - Load power supply configuration from JSON
#pragma once

#include "PowerSupplyManager.h"
#include "KeysightPSUAdapter.h"
#include <string>

/**
 * @brief Loads power supply configuration from JSON file
 *
 * Supported device types:
 * - "keysight_e36103b_direct" or "keysight_direct" - Direct TCP/IP control (no Python server)
 * - "keysight_e36441a_direct" - Direct TCP/IP control for 4-channel E36441A
 * - "keysight_e36103b" or "keysight" - Server-based control (via Python server)
 * - "unit_t_udp3305s" - UNI-T UDP3305S-E 3-channel PSU via USB/VISA (NI-VISA required)
 *
 * Expected JSON format (DIRECT control - recommended):
 * {
 *   "devices": [
 *     {
 *       "name": "PSU1",
 *       "type": "keysight_e36103b_direct",
 *       "ip": "192.168.0.60",
 *       "port": 5025,
 *       "channels": 1
 *     },
 *     {
 *       "name": "PSU2",
 *       "type": "keysight_e36441a_direct",
 *       "ip": "192.168.0.32",
 *       "port": 5025,
 *       "channels": 4
 *     },
 *     {
 *       "name": "PSU3",
 *       "type": "unit_t_udp3305s",
 *       "visa_resource": "USB0::0x0483::0x5740::ADP3424500154::INSTR",
 *       "channels": 3
 *     }
 *   ]
 * }
 *
 * Expected JSON format (SERVER-based control - legacy):
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
    int devicePort,
    const std::string& visaResource);
};