// MotionJsonLoader.h
#pragma once
#include "unified_motion_manager.h"
#include <string>

#include "nlohmann/json.hpp"

using json = nlohmann::json;

/**
 * @brief JSON loader for UnifiedMotionManager
 *
 * Loads motion devices and named positions from JSON configuration files.
 * JSON format:
 * {
 *   "motion_devices": [
 *     {
 *       "name": "gantry-main",
 *       "controller_type": "ACS",
 *       "ip_address": "192.168.1.100",
 *       "port": 701,
 *       "axes": "XYZUVW",
 *       "enabled": true
 *     }
 *   ],
 *   "named_positions": {
 *     "gantry-main": {
 *       "home": {
 *         "x": 0, "y": 0, "z": 0, "u": 0, "v": 0, "w": 0,
 *         "waypoints": [],
 *         "exit_waypoints": []
 *       },
 *       "safe": {
 *         "x": 100, "y": 100, "z": 50, "u": 0, "v": 0, "w": 0,
 *         "waypoints": [],
 *         "exit_waypoints": []
 *       },
 *       "seepic": {
 *         "x": 180.708, "y": 81.238, "z": 16.345, "u": 0, "v": 0, "w": 0,
 *         "waypoints": ["safe"],
 *         "exit_waypoints": ["safe"]
 *       }
 *     }
 *   }
 * }
 */
class MotionJsonLoader {
public:
  /**
   * @brief Load motion devices and named positions from JSON file
   * @param manager Reference to UnifiedMotionManager
   * @param filepath Path to JSON configuration file
   * @return Number of devices loaded (0 if failed)
   */
  static int LoadFromFile(UnifiedMotionManager& manager, const std::string& filepath);

private:
  /**
   * @brief Load motion devices from JSON config
   */
  static void LoadDevices(UnifiedMotionManager& manager, const nlohmann::json& config);

  /**
   * @brief Load named positions from JSON config (with waypoints support)
   */
  static void LoadNamedPositions(UnifiedMotionManager& manager, const nlohmann::json& config);
};