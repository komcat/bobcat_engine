// product_json_loader.cpp
#include "product_json_loader.h"
#include "product_position_manager.h"
#include "MotionTypes.h"
#include <iostream>
#include <fstream>

// Include JSON library
#include "nlohmann/json.hpp"
using json = nlohmann::json;

int ProductJsonLoader::LoadFromFile(ProductPositionManager& manager,
  const std::string& filePath) {
  std::cout << "[INFO] ProductJsonLoader: Loading from " << filePath << std::endl;

  try {
    // Open file
    std::ifstream file(filePath);
    if (!file.is_open()) {
      std::cout << "[ERROR] ProductJsonLoader: Cannot open file " << filePath << std::endl;
      return -1;
    }

    // Parse JSON
    json j;
    file >> j;
    file.close();

    // Validate structure
    if (!j.is_object()) {
      std::cout << "[ERROR] ProductJsonLoader: Invalid JSON format - root must be object"
        << std::endl;
      return -1;
    }

    int positionCount = 0;

    // Parse structure:
    // {
    //   "device-name": {
    //     "position-name": { 
    //       "x": 1.0, "y": 2.0, "z": 3.0, 
    //       "u": 0, "v": 0, "w": 0,
    //       "waypoints": ["safe", "clearance"]  // ✅ NEW
    //     }
    //   }
    // }

    for (auto& [deviceName, devicePositions] : j.items()) {
      if (!devicePositions.is_object()) {
        std::cout << "[WARNING] ProductJsonLoader: Skipping invalid device entry: "
          << deviceName << std::endl;
        continue;
      }

      for (auto& [posName, posData] : devicePositions.items()) {
        if (!posData.is_object()) {
          std::cout << "[WARNING] ProductJsonLoader: Skipping invalid position: "
            << deviceName << "." << posName << std::endl;
          continue;
        }

        // Parse position coordinates
        PositionStruct pos;
        pos.x = posData.value("x", 0.0);
        pos.y = posData.value("y", 0.0);
        pos.z = posData.value("z", 0.0);
        pos.u = posData.value("u", 0.0);
        pos.v = posData.value("v", 0.0);
        pos.w = posData.value("w", 0.0);

        // ✅ NEW - Parse waypoints array
        if (posData.contains("waypoints") && posData["waypoints"].is_array()) {
          for (const auto& waypoint : posData["waypoints"]) {
            if (waypoint.is_string()) {
              pos.waypoints.push_back(waypoint.get<std::string>());
            }
          }

          if (!pos.waypoints.empty()) {
            std::cout << "[DEBUG] ProductJsonLoader: Position " << deviceName
              << "." << posName << " has waypoints: ";
            for (const auto& wp : pos.waypoints) {
              std::cout << wp << " ";
            }
            std::cout << std::endl;
          }
        }

        // Add to manager
        manager.AddPosition(deviceName, posName, pos);
        positionCount++;

        std::cout << "[DEBUG] ProductJsonLoader: Loaded " << deviceName
          << "." << posName << " -> ("
          << pos.x << ", " << pos.y << ", " << pos.z << ")" << std::endl;
      }
    }

    std::cout << "[INFO] ProductJsonLoader: Successfully loaded " << positionCount
      << " positions from " << filePath << std::endl;

    return positionCount;

  }
  catch (const json::parse_error& e) {
    std::cout << "[ERROR] ProductJsonLoader: JSON parse error: " << e.what() << std::endl;
    return -1;
  }
  catch (const std::exception& e) {
    std::cout << "[ERROR] ProductJsonLoader: Exception: " << e.what() << std::endl;
    return -1;
  }
}

bool ProductJsonLoader::SaveToFile(const ProductPositionManager& manager,
  const std::string& filePath) {
  std::cout << "[INFO] ProductJsonLoader: Saving to " << filePath << std::endl;

  try {
    json j;

    // Get all devices
    std::vector<std::string> devices = manager.GetAllDeviceNames();

    if (devices.empty()) {
      std::cout << "[WARNING] ProductJsonLoader: No positions to save" << std::endl;
    }

    // Build JSON structure
    for (const auto& deviceName : devices) {
      json deviceJson;

      // Get all positions for this device
      std::map<std::string, PositionStruct> positions =
        manager.GetAllPositions(deviceName);

      for (const auto& [posName, pos] : positions) {
        json posJson = {
          {"x", pos.x},
          {"y", pos.y},
          {"z", pos.z},
          {"u", pos.u},
          {"v", pos.v},
          {"w", pos.w}
        };

        // ✅ NEW - Save waypoints array
        if (!pos.waypoints.empty()) {
          posJson["waypoints"] = pos.waypoints;
        }
        else {
          posJson["waypoints"] = json::array();  // Empty array
        }

        deviceJson[posName] = posJson;
      }

      j[deviceName] = deviceJson;
    }

    // Write to file with pretty formatting
    std::ofstream file(filePath);
    if (!file.is_open()) {
      std::cout << "[ERROR] ProductJsonLoader: Cannot create file " << filePath << std::endl;
      return false;
    }

    file << j.dump(4); // 4-space indentation
    file.close();

    std::cout << "[INFO] ProductJsonLoader: Successfully saved "
      << manager.GetTotalPositionCount() << " positions to "
      << filePath << std::endl;

    return true;

  }
  catch (const std::exception& e) {
    std::cout << "[ERROR] ProductJsonLoader: Exception: " << e.what() << std::endl;
    return false;
  }
}

bool ProductJsonLoader::ValidateFile(const std::string& filePath) {
  try {
    std::ifstream file(filePath);
    if (!file.is_open()) {
      return false;
    }

    json j;
    file >> j;
    file.close();

    // Check basic structure
    if (!j.is_object()) {
      return false;
    }

    // Validate each device entry
    for (auto& [deviceName, devicePositions] : j.items()) {
      if (!devicePositions.is_object()) {
        return false;
      }

      // Validate each position
      for (auto& [posName, posData] : devicePositions.items()) {
        if (!posData.is_object()) {
          return false;
        }

        // Check required fields
        if (!posData.contains("x") || !posData.contains("y") || !posData.contains("z")) {
          return false;
        }

        // ✅ NEW - Validate waypoints if present
        if (posData.contains("waypoints") && !posData["waypoints"].is_array()) {
          return false;
        }
      }
    }

    return true;

  }
  catch (const std::exception&) {
    return false;
  }
}

bool ProductJsonLoader::GetFileInfo(const std::string& filePath,
  int& outDeviceCount,
  int& outPositionCount) {
  outDeviceCount = 0;
  outPositionCount = 0;

  try {
    std::ifstream file(filePath);
    if (!file.is_open()) {
      return false;
    }

    json j;
    file >> j;
    file.close();

    if (!j.is_object()) {
      return false;
    }

    outDeviceCount = j.size();

    for (auto& [deviceName, devicePositions] : j.items()) {
      if (devicePositions.is_object()) {
        outPositionCount += devicePositions.size();
      }
    }

    return true;

  }
  catch (const std::exception&) {
    return false;
  }
}