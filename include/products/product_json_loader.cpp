// product_json_loader.cpp
#include "product_json_loader.h"
#include "product_log.h"
#include "product_position_manager.h"
#include "MotionTypes.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

// Include JSON library
#include "nlohmann/json.hpp"
using json = nlohmann::json;

// ============================================================================
// HELPER FUNCTIONS (forward declarations)
// ============================================================================

static int loadDevicePositions(ProductPositionManager& manager,
                                const std::string& deviceName,
                                const json& devicePositions);

static void loadGraphs(ProductPositionManager& manager, const json& graphsJson);

static QJsonObject nlohmannToQt(const json& j);
static json qtToNlohmann(const QJsonObject& qj);

// ============================================================================
// LOAD FROM FILE
// ============================================================================

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
      std::cout << "[ERROR] ProductJsonLoader: Invalid JSON format - root must be object" << std::endl;
      return -1;
    }

    // ✅ DETECT FILE VERSION
    std::string version = "1.0";  // Default to v1.0 if no version field
    if (j.contains("version") && j["version"].is_string()) {
      version = j["version"].get<std::string>();
      std::cout << "[INFO] ProductJsonLoader: Detected file format version " << version << std::endl;
    }
    else {
      std::cout << "[INFO] ProductJsonLoader: No version field - assuming v1.0 format" << std::endl;
    }

    int positionCount = 0;

    // ✅ HANDLE v2.0 FORMAT
    if (version == "2.0") {
      // v2.0 format:
      // {
      //   "version": "2.0",
      //   "positions": { device-name: { position-name: {...} } },
      //   "graphs": { device-name: { graph-name: {...} } }
      // }

      // Load positions from "positions" section
      if (j.contains("positions") && j["positions"].is_object()) {
        for (auto& [deviceName, devicePositions] : j["positions"].items()) {
          positionCount += loadDevicePositions(manager, deviceName, devicePositions);
        }
      }

      // ✅ Load graphs from "graphs" section
      if (j.contains("graphs") && j["graphs"].is_object()) {
        loadGraphs(manager, j["graphs"]);
      }
    }
    // ✅ HANDLE v1.0 FORMAT (backward compatibility)
    else {
      // v1.0 format (original):
      // {
      //   "device-name": {
      //     "position-name": { x, y, z, u, v, w, waypoints, exit_waypoints }
      //   }
      // }

      for (auto& [deviceName, devicePositions] : j.items()) {
        positionCount += loadDevicePositions(manager, deviceName, devicePositions);
      }

      // No graphs in v1.0 format
      std::cout << "[INFO] ProductJsonLoader: v1.0 format - no graphs loaded" << std::endl;
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

// ============================================================================
// SAVE TO FILE
// ============================================================================

bool ProductJsonLoader::SaveToFile(const ProductPositionManager& manager,
  const std::string& filePath) {
  std::cout << "[INFO] ProductJsonLoader: Saving to " << filePath << std::endl;

  try {
    json j;

    // ✅ Always save as v2.0 format
    j["version"] = "2.0";

    // ============================================================================
    // SAVE POSITIONS
    // ============================================================================
    json positionsJson;

    // Get all devices
    std::vector<std::string> devices = manager.GetAllDeviceNames();

    if (devices.empty()) {
      std::cout << "[WARNING] ProductJsonLoader: No positions to save" << std::endl;
    }

    // Build positions JSON structure
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

        // Save entry waypoints array
        if (!pos.waypoints.empty()) {
          posJson["waypoints"] = pos.waypoints;
        }
        else {
          posJson["waypoints"] = json::array();
        }

        // Save exit waypoints array
        if (!pos.exit_waypoints.empty()) {
          posJson["exit_waypoints"] = pos.exit_waypoints;
        }
        else {
          posJson["exit_waypoints"] = json::array();
        }

        deviceJson[posName] = posJson;
      }

      positionsJson[deviceName] = deviceJson;
    }

    j["positions"] = positionsJson;

    // ============================================================================
    // ✅ SAVE GRAPHS
    // ============================================================================
    const MotionGraphManager& graphManager = manager.GetGraphManager();
    QJsonObject graphsQtJson = graphManager.toJson();

    // Convert Qt JSON to nlohmann json
    json graphsJson = qtToNlohmann(graphsQtJson);
    j["graphs"] = graphsJson;

    std::cout << "[INFO] ProductJsonLoader: Saved " << graphManager.getTotalGraphCount()
      << " graphs across " << graphManager.getDeviceCount() << " devices" << std::endl;

    // ============================================================================
    // WRITE TO FILE
    // ============================================================================

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

// ============================================================================
// VALIDATION
// ============================================================================

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

    // Detect version
    std::string version = "1.0";
    if (j.contains("version") && j["version"].is_string()) {
      version = j["version"].get<std::string>();
    }

    // Validate v2.0 format
    if (version == "2.0") {
      if (!j.contains("positions") || !j["positions"].is_object()) {
        return false;
      }

      // Validate positions structure
      for (auto& [deviceName, devicePositions] : j["positions"].items()) {
        if (!devicePositions.is_object()) {
          return false;
        }

        for (auto& [posName, posData] : devicePositions.items()) {
          if (!posData.is_object()) {
            return false;
          }

          // Check required fields
          if (!posData.contains("x") || !posData.contains("y") || !posData.contains("z")) {
            return false;
          }
        }
      }

      // Graphs are optional, but if present, must be valid
      if (j.contains("graphs")) {
        if (!j["graphs"].is_object()) {
          return false;
        }
      }
    }
    // Validate v1.0 format
    else {
      for (auto& [deviceName, devicePositions] : j.items()) {
        if (!devicePositions.is_object()) {
          return false;
        }

        for (auto& [posName, posData] : devicePositions.items()) {
          if (!posData.is_object()) {
            return false;
          }

          if (!posData.contains("x") || !posData.contains("y") || !posData.contains("z")) {
            return false;
          }
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

    // Detect version
    std::string version = "1.0";
    if (j.contains("version") && j["version"].is_string()) {
      version = j["version"].get<std::string>();
    }

    // Count for v2.0
    if (version == "2.0") {
      if (j.contains("positions") && j["positions"].is_object()) {
        outDeviceCount = static_cast<int>(j["positions"].size());

        for (auto& [deviceName, devicePositions] : j["positions"].items()) {
          if (devicePositions.is_object()) {
            outPositionCount += static_cast<int>(devicePositions.size());
          }
        }
      }
    }
    // Count for v1.0
    else {
      outDeviceCount = static_cast<int>(j.size());

      for (auto& [deviceName, devicePositions] : j.items()) {
        if (devicePositions.is_object()) {
          outPositionCount += static_cast<int>(devicePositions.size());
        }
      }
    }

    return true;

  }
  catch (const std::exception&) {
    return false;
  }
}

// ============================================================================
// HELPER FUNCTIONS IMPLEMENTATION
// ============================================================================

static int loadDevicePositions(ProductPositionManager& manager,
                                const std::string& deviceName,
                                const json& devicePositions) {
  int count = 0;

  if (!devicePositions.is_object()) {
    std::cout << "[WARNING] ProductJsonLoader: Skipping invalid device entry: "
      << deviceName << std::endl;
    return 0;
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

    // Parse entry waypoints array
    if (posData.contains("waypoints") && posData["waypoints"].is_array()) {
      for (const auto& waypoint : posData["waypoints"]) {
        if (waypoint.is_string()) {
          pos.waypoints.push_back(waypoint.get<std::string>());
        }
      }
    }

    // Parse exit waypoints array
    if (posData.contains("exit_waypoints") && posData["exit_waypoints"].is_array()) {
      for (const auto& waypoint : posData["exit_waypoints"]) {
        if (waypoint.is_string()) {
          pos.exit_waypoints.push_back(waypoint.get<std::string>());
        }
      }
    }

    // Add to manager
    manager.AddPosition(deviceName, posName, pos);
    count++;

    if constexpr (ProductLog::kVerbosePositions) {
      std::cout << "[DEBUG] ProductJsonLoader: Loaded " << deviceName
        << "." << posName << " -> ("
        << pos.x << ", " << pos.y << ", " << pos.z << ")" << std::endl;
    }
  }

  return count;
}

static void loadGraphs(ProductPositionManager& manager, const json& graphsJson) {
  if (!graphsJson.is_object()) {
    std::cout << "[WARNING] ProductJsonLoader: Invalid graphs section" << std::endl;
    return;
  }

  // Convert nlohmann json to Qt JSON
  QJsonObject graphsQtJson = nlohmannToQt(graphsJson);

  // Load into graph manager
  MotionGraphManager graphManager = MotionGraphManager::fromJson(graphsQtJson);
  manager.SetGraphManager(graphManager);

  std::cout << "[INFO] ProductJsonLoader: Loaded " << graphManager.getTotalGraphCount()
    << " graphs across " << graphManager.getDeviceCount() << " devices" << std::endl;
}

// Convert nlohmann::json to QJsonObject
static QJsonObject nlohmannToQt(const json& j) {
  // Convert to string, then parse with Qt
  std::string jsonString = j.dump();
  QByteArray jsonBytes = QByteArray::fromStdString(jsonString);
  QJsonDocument doc = QJsonDocument::fromJson(jsonBytes);
  return doc.object();
}

// Convert QJsonObject to nlohmann::json
static json qtToNlohmann(const QJsonObject& qj) {
  // Convert to string, then parse with nlohmann
  QJsonDocument doc(qj);
  QByteArray jsonBytes = doc.toJson(QJsonDocument::Compact);
  std::string jsonString = jsonBytes.toStdString();
  return json::parse(jsonString);
}
