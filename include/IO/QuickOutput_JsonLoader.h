// QuickOutput_JsonLoader.h - Load quick output configuration from JSON
#pragma once

#include "QuickOutputManager.h"
#include <string>

/**
 * @brief Loads QuickOutput configuration from JSON file
 *
 * JSON Format:
 * {
 *   "quick_outputs": [
 *     {
 *       "device_name": "IO_MAIN",
 *       "pin_number": 0,
 *       "display_name": "Gripper Open"
 *     },
 *     {
 *       "device_name": "IO_MAIN",
 *       "pin_name": "VACUUM_ENABLE",
 *       "display_name": "Vacuum ON"
 *     }
 *   ]
 * }
 */
class QuickOutput_JsonLoader {
public:
  /**
   * @brief Load quick outputs from JSON file into manager
   * @param filePath Path to JSON configuration file
   * @param manager QuickOutputManager to populate
   * @return true if successful, false otherwise
   */
  static bool LoadFromFile(const std::string& filePath, QuickOutputManager* manager);

  /**
   * @brief Load quick outputs from JSON string
   * @param jsonString JSON content as string
   * @param manager QuickOutputManager to populate
   * @return true if successful, false otherwise
   */
  static bool LoadFromString(const std::string& jsonString, QuickOutputManager* manager);

  /**
   * @brief Save current quick output configuration to JSON file
   * @param filePath Path to save JSON configuration
   * @param manager QuickOutputManager to save from
   * @return true if successful, false otherwise
   */
  static bool SaveToFile(const std::string& filePath, QuickOutputManager* manager);
};
