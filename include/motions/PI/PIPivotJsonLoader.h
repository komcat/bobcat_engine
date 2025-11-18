// PIPivotJsonLoader.h
#pragma once
#include <string>
#include <map>
#include <nlohmann/json.hpp>

/**
 * @brief Structure to hold PI pivot point configuration
 */
struct PIPivotConfig {
  std::string DeviceName;
  double PivotX;
  double PivotY;
  double PivotZ;
};

/**
 * @brief JSON loader for PI pivot point configurations
 *
 * Loads PI pivot point configurations from JSON file.
 * Used during startup to verify and set correct pivot points.
 *
 * JSON format:
 * {
 *   "pi_pivot_points": [
 *     {
 *       "device_name": "hex-left",
 *       "pivot_x": -129.50,
 *       "pivot_y": 0.0,
 *       "pivot_z": 109.750
 *     }
 *   ]
 * }
 */
class PIPivotJsonLoader {
public:
  /**
   * @brief Load PI pivot configurations from JSON file
   * @param filepath Path to JSON configuration file
   * @return Map of device name to pivot configuration
   */
  static std::map<std::string, PIPivotConfig> LoadFromFile(const std::string& filepath);

private:
  /**
   * @brief Load pivot points from JSON config
   */
  static void LoadPivotPoints(std::map<std::string, PIPivotConfig>& configs,
                               const nlohmann::json& config);
};
