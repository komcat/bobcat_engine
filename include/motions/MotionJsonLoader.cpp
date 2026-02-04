// MotionJsonLoader.cpp
#include "MotionJsonLoader.h"
#include <fstream>
#include <iostream>
#include <filesystem>

int MotionJsonLoader::LoadFromFile(UnifiedMotionManager& manager, const std::string& filepath) {
  std::cout << "[MotionJsonLoader] Loading from: " << filepath << std::endl;

  std::ifstream file(filepath);
  if (!file.is_open()) {
    std::cout << "[MotionJsonLoader] Cannot open file: " << filepath << std::endl;
    return 0;
  }

  try {
    json config = json::parse(file);
    file.close();

    int deviceCount = 0;

    // Load devices first
    if (config.contains("motion_devices")) {
      LoadDevices(manager, config);
      deviceCount = static_cast<int>(config["motion_devices"].size());
      std::cout << "[MotionJsonLoader] Loaded " << deviceCount << " devices" << std::endl;
    }
    else {
      std::cout << "[MotionJsonLoader] No 'motion_devices' section found" << std::endl;
    }

    // Load named positions after devices
    if (config.contains("named_positions")) {
      LoadNamedPositions(manager, config);
      std::cout << "[MotionJsonLoader] Loaded named positions" << std::endl;
    }
    else {
      std::cout << "[MotionJsonLoader] No 'named_positions' section found" << std::endl;
    }

    return deviceCount;
  }
  catch (const json::parse_error& e) {
    std::cerr << "[MotionJsonLoader] JSON parse error: " << e.what() << std::endl;
    return 0;
  }
  catch (const std::exception& e) {
    std::cerr << "[MotionJsonLoader] Error: " << e.what() << std::endl;
    return 0;
  }
}

void MotionJsonLoader::LoadDevices(UnifiedMotionManager& manager, const json& config) {
  for (const auto& deviceJson : config["motion_devices"]) {
    MotionDevice device;
    device.Name = deviceJson.value("name", "");
    device.TypeController = deviceJson.value("controller_type", "");
    device.IpAddress = deviceJson.value("ip_address", "");
    device.Port = deviceJson.value("port", 0);
    device.InstalledAxes = deviceJson.value("axes", "XYZUVW");
    device.Enabled = deviceJson.value("enabled", false);

    if (device.Name.empty()) {
      std::cout << "[MotionJsonLoader] Skipping device with empty name" << std::endl;
      continue;
    }

    manager.AddDevice(device);

    std::cout << "[MotionJsonLoader] Added device: " << device.Name
      << " (Type: " << device.TypeController
      << ", Enabled: " << (device.Enabled ? "Yes" : "No") << ")" << std::endl;
  }
}

void MotionJsonLoader::LoadNamedPositions(UnifiedMotionManager& manager, const json& config) {
  int totalPositions = 0;

  for (const auto& deviceEntry : config["named_positions"].items()) {
    std::string deviceName = deviceEntry.key();

    // Check if device exists
    if (!manager.HasDevice(deviceName)) {
      std::cout << "[MotionJsonLoader] Warning: Device '" << deviceName
        << "' not found, skipping named positions" << std::endl;
      continue;
    }

    int devicePositionCount = 0;

    for (const auto& posEntry : deviceEntry.value().items()) {
      std::string posName = posEntry.key();
      const auto& pos = posEntry.value();

      // Create PositionStruct with coordinates
      PositionStruct position;
      position.x = pos.value("x", 0.0);
      position.y = pos.value("y", 0.0);
      position.z = pos.value("z", 0.0);
      position.u = pos.value("u", 0.0);
      position.v = pos.value("v", 0.0);
      position.w = pos.value("w", 0.0);

      // ✅ NEW - Load entry waypoints
      if (pos.contains("waypoints") && pos["waypoints"].is_array()) {
        for (const auto& waypoint : pos["waypoints"]) {
          if (waypoint.is_string()) {
            position.waypoints.push_back(waypoint.get<std::string>());
          }
        }
      }

      // ✅ NEW - Load exit waypoints
      if (pos.contains("exit_waypoints") && pos["exit_waypoints"].is_array()) {
        for (const auto& exitWaypoint : pos["exit_waypoints"]) {
          if (exitWaypoint.is_string()) {
            position.exit_waypoints.push_back(exitWaypoint.get<std::string>());
          }
        }
      }

      // Add position with waypoints to manager
      manager.AddNamedPosition(deviceName, posName, position);

      devicePositionCount++;
      totalPositions++;

      // ✅ NEW - Log waypoint info
      if (!position.waypoints.empty() || !position.exit_waypoints.empty()) {
        std::cout << "[MotionJsonLoader]   Position '" << posName << "' waypoints: ";
        if (!position.waypoints.empty()) {
          std::cout << "entry=[";
          for (size_t i = 0; i < position.waypoints.size(); ++i) {
            std::cout << position.waypoints[i];
            if (i < position.waypoints.size() - 1) std::cout << ", ";
          }
          std::cout << "] ";
        }
        if (!position.exit_waypoints.empty()) {
          std::cout << "exit=[";
          for (size_t i = 0; i < position.exit_waypoints.size(); ++i) {
            std::cout << position.exit_waypoints[i];
            if (i < position.exit_waypoints.size() - 1) std::cout << ", ";
          }
          std::cout << "]";
        }
        std::cout << std::endl;
      }
    }

    std::cout << "[MotionJsonLoader] Loaded " << devicePositionCount
      << " named positions for device: " << deviceName << std::endl;
  }

  std::cout << "[MotionJsonLoader] Total named positions loaded: " << totalPositions << std::endl;
}

// ============================================================================
// POSITION SAVE HELPER (for RetraceEdit feature)
// ============================================================================

bool MotionJsonLoader::SavePositionToFile(
  const std::string& localConfigPath,
  const std::string& baseConfigPath,
  const std::string& deviceName,
  const std::string& positionName,
  const PositionStruct& position,
  bool preserveWaypoints,
  std::string& errorMessage
) {
  std::cout << "[MotionJsonLoader] SavePositionToFile: " << deviceName << "/" << positionName << std::endl;

  json config;

  // Step 1: Load existing config (local first, then base)
  std::string sourceFile = localConfigPath;
  std::ifstream localFile(localConfigPath);

  if (localFile.is_open() && localFile.good()) {
    // Local file exists - use it
    try {
      config = json::parse(localFile);
      localFile.close();
      std::cout << "[MotionJsonLoader] Loaded existing local config: " << localConfigPath << std::endl;
    }
    catch (const json::parse_error& e) {
      errorMessage = "Failed to parse local config: " + std::string(e.what());
      std::cerr << "[MotionJsonLoader] " << errorMessage << std::endl;
      localFile.close();
      return false;
    }
  }
  else {
    // Local file doesn't exist - try to copy from base
    std::ifstream baseFile(baseConfigPath);
    if (!baseFile.is_open()) {
      errorMessage = "Cannot open base config file: " + baseConfigPath;
      std::cerr << "[MotionJsonLoader] " << errorMessage << std::endl;
      return false;
    }

    try {
      config = json::parse(baseFile);
      baseFile.close();
      std::cout << "[MotionJsonLoader] Loaded base config (will create local): " << baseConfigPath << std::endl;
    }
    catch (const json::parse_error& e) {
      errorMessage = "Failed to parse base config: " + std::string(e.what());
      std::cerr << "[MotionJsonLoader] " << errorMessage << std::endl;
      baseFile.close();
      return false;
    }
  }

  // Step 2: Ensure named_positions section exists
  if (!config.contains("named_positions")) {
    config["named_positions"] = json::object();
  }

  // Step 3: Ensure device section exists
  if (!config["named_positions"].contains(deviceName)) {
    config["named_positions"][deviceName] = json::object();
  }

  // Step 4: Get existing position data (for waypoint preservation)
  json existingPos;
  if (config["named_positions"][deviceName].contains(positionName)) {
    existingPos = config["named_positions"][deviceName][positionName];
  }

  // Step 5: Build new position JSON
  json newPosJson;
  newPosJson["x"] = position.x;
  newPosJson["y"] = position.y;
  newPosJson["z"] = position.z;
  newPosJson["u"] = position.u;
  newPosJson["v"] = position.v;
  newPosJson["w"] = position.w;

  // Handle waypoints
  if (preserveWaypoints && !existingPos.is_null()) {
    // Preserve existing waypoints from config file
    if (existingPos.contains("waypoints") && existingPos["waypoints"].is_array()) {
      newPosJson["waypoints"] = existingPos["waypoints"];
    }
    else {
      newPosJson["waypoints"] = json::array();
    }

    if (existingPos.contains("exit_waypoints") && existingPos["exit_waypoints"].is_array()) {
      newPosJson["exit_waypoints"] = existingPos["exit_waypoints"];
    }
    else {
      newPosJson["exit_waypoints"] = json::array();
    }
  }
  else {
    // Use waypoints from the position struct (or empty)
    newPosJson["waypoints"] = json::array();
    for (const auto& wp : position.waypoints) {
      newPosJson["waypoints"].push_back(wp);
    }

    newPosJson["exit_waypoints"] = json::array();
    for (const auto& wp : position.exit_waypoints) {
      newPosJson["exit_waypoints"].push_back(wp);
    }
  }

  // Step 6: Update the position in config
  config["named_positions"][deviceName][positionName] = newPosJson;

  // Step 7: Create directory if needed
  std::filesystem::path filePath(localConfigPath);
  std::filesystem::path dirPath = filePath.parent_path();
  if (!dirPath.empty() && !std::filesystem::exists(dirPath)) {
    try {
      std::filesystem::create_directories(dirPath);
    }
    catch (const std::exception& e) {
      errorMessage = "Failed to create directory: " + std::string(e.what());
      std::cerr << "[MotionJsonLoader] " << errorMessage << std::endl;
      return false;
    }
  }

  // Step 8: Write to local config file
  std::ofstream outFile(localConfigPath);
  if (!outFile.is_open()) {
    errorMessage = "Cannot open file for writing: " + localConfigPath;
    std::cerr << "[MotionJsonLoader] " << errorMessage << std::endl;
    return false;
  }

  try {
    outFile << config.dump(2);  // Pretty print with 2-space indent
    outFile.close();

    std::cout << "[MotionJsonLoader] Saved position '" << positionName
              << "' for device '" << deviceName << "' to: " << localConfigPath << std::endl;
    std::cout << "[MotionJsonLoader]   Position: ("
              << position.x << ", " << position.y << ", " << position.z
              << ", " << position.u << ", " << position.v << ", " << position.w << ")" << std::endl;

    return true;
  }
  catch (const std::exception& e) {
    errorMessage = "Failed to write config file: " + std::string(e.what());
    std::cerr << "[MotionJsonLoader] " << errorMessage << std::endl;
    outFile.close();
    return false;
  }
}