// MotionJsonLoader.cpp
#include "MotionJsonLoader.h"
#include <fstream>
#include <iostream>

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