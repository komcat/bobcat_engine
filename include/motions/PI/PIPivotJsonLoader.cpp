// PIPivotJsonLoader.cpp
#include "PIPivotJsonLoader.h"
#include <fstream>
#include <iostream>

using json = nlohmann::json;

std::map<std::string, PIPivotConfig> PIPivotJsonLoader::LoadFromFile(const std::string& filepath) {
  std::cout << "📂 [INFO] PIPivotJsonLoader: Loading from " << filepath << std::endl;

  std::map<std::string, PIPivotConfig> configs;

  try {
    // Open and parse JSON file
    std::ifstream file(filepath);
    if (!file.is_open()) {
      std::cerr << "❌ [ERROR] PIPivotJsonLoader: Failed to open file: " << filepath << std::endl;
      return configs;
    }

    json config;
    file >> config;
    file.close();

    // Load pivot points
    LoadPivotPoints(configs, config);

    std::cout << "✅ [INFO] PIPivotJsonLoader: Loaded " << configs.size()
              << " pivot configurations" << std::endl;

    return configs;

  }
  catch (const json::exception& e) {
    std::cerr << "❌ [ERROR] PIPivotJsonLoader: JSON parse error: " << e.what() << std::endl;
    return configs;
  }
  catch (const std::exception& e) {
    std::cerr << "❌ [ERROR] PIPivotJsonLoader: Error: " << e.what() << std::endl;
    return configs;
  }
}

void PIPivotJsonLoader::LoadPivotPoints(std::map<std::string, PIPivotConfig>& configs,
                                         const json& config) {
  if (!config.contains("pi_pivot_points")) {
    std::cout << "⚠️ [WARN] PIPivotJsonLoader: No 'pi_pivot_points' section found" << std::endl;
    return;
  }

  const auto& pivotPoints = config["pi_pivot_points"];
  if (!pivotPoints.is_array()) {
    std::cerr << "❌ [ERROR] PIPivotJsonLoader: 'pi_pivot_points' must be an array" << std::endl;
    return;
  }

  std::cout << "📋 [INFO] PIPivotJsonLoader: Loading " << pivotPoints.size()
            << " pivot configurations..." << std::endl;

  for (const auto& pivotJson : pivotPoints) {
    try {
      PIPivotConfig pivotConfig;

      // Required fields
      if (pivotJson.contains("device_name")) {
        pivotConfig.DeviceName = pivotJson["device_name"].get<std::string>();
      }
      else {
        std::cerr << "❌ [ERROR] PIPivotJsonLoader: Pivot config missing 'device_name' field" << std::endl;
        continue;
      }

      if (pivotJson.contains("pivot_x")) {
        pivotConfig.PivotX = pivotJson["pivot_x"].get<double>();
      }
      else {
        std::cerr << "❌ [ERROR] PIPivotJsonLoader: Device '" << pivotConfig.DeviceName
                  << "' missing 'pivot_x' field" << std::endl;
        continue;
      }

      if (pivotJson.contains("pivot_y")) {
        pivotConfig.PivotY = pivotJson["pivot_y"].get<double>();
      }
      else {
        std::cerr << "❌ [ERROR] PIPivotJsonLoader: Device '" << pivotConfig.DeviceName
                  << "' missing 'pivot_y' field" << std::endl;
        continue;
      }

      if (pivotJson.contains("pivot_z")) {
        pivotConfig.PivotZ = pivotJson["pivot_z"].get<double>();
      }
      else {
        std::cerr << "❌ [ERROR] PIPivotJsonLoader: Device '" << pivotConfig.DeviceName
                  << "' missing 'pivot_z' field" << std::endl;
        continue;
      }

      // Add to map
      configs[pivotConfig.DeviceName] = pivotConfig;

      std::cout << "✅ [INFO] PIPivotJsonLoader: Loaded pivot for '" << pivotConfig.DeviceName
                << "': X=" << pivotConfig.PivotX
                << ", Y=" << pivotConfig.PivotY
                << ", Z=" << pivotConfig.PivotZ << std::endl;

    }
    catch (const json::exception& e) {
      std::cerr << "❌ [ERROR] PIPivotJsonLoader: Error parsing pivot config: " << e.what() << std::endl;
    }
  }
}
