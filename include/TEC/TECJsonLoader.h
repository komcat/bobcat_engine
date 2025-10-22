// TECJsonLoader.h
#pragma once
#include "TECManager.h"
#include <string>
#include <nlohmann/json.hpp>

/**
 * @brief JSON loader for TECManager
 *
 * Loads TEC device configurations from JSON file.
 * Follows the same pattern as MotionJsonLoader.
 *
 * JSON format:
 * {
 *   "tec_devices": [
 *     {
 *       "name": "laser-main",
 *       "controller_type": "CLD101x",
 *       "ip_address": "127.0.0.88",
 *       "port": 65432,
 *       "enabled": true,
 *       "default_temperature": 25.0,
 *       "default_laser_current": 50.0
 *     }
 *   ]
 * }
 */
class TECJsonLoader {
public:
  /**
   * @brief Load TEC devices from JSON file
   * @param manager Reference to TECManager
   * @param filepath Path to JSON configuration file
   * @return Number of devices loaded (0 if failed)
   */
  static int LoadFromFile(TECManager& manager, const std::string& filepath);

private:
  /**
   * @brief Load TEC devices from JSON config
   */
  static void LoadDevices(TECManager& manager, const nlohmann::json& config);
};