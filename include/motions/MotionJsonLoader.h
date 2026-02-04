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

  // ============================================================================
  // ✅ NEW - POSITION SAVE HELPER (for RetraceEdit feature)
  // ============================================================================

  /**
   * @brief Save a single position to the local config file
   *
   * This method handles the JSON read-modify-write cycle for saving a position.
   * If the .local.json file doesn't exist, it creates one from the base .json file.
   *
   * @param localConfigPath Path to .local.json file to write to
   * @param baseConfigPath Path to base .json file (used if local doesn't exist)
   * @param deviceName Device name (e.g., "gantry-main")
   * @param positionName Position name (e.g., "lensgrip")
   * @param position Position coordinates to save
   * @param preserveWaypoints If true, keeps existing waypoints; if false, clears them
   * @param errorMessage Output: error description if failed
   * @return true if save succeeded, false on error
   */
  static bool SavePositionToFile(
    const std::string& localConfigPath,
    const std::string& baseConfigPath,
    const std::string& deviceName,
    const std::string& positionName,
    const PositionStruct& position,
    bool preserveWaypoints,
    std::string& errorMessage
  );

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