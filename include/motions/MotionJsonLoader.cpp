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

      manager.AddNamedPosition(
        deviceName,
        posName,
        pos.value("x", 0.0),
        pos.value("y", 0.0),
        pos.value("z", 0.0),
        pos.value("u", 0.0),
        pos.value("v", 0.0),
        pos.value("w", 0.0)
      );

      devicePositionCount++;
      totalPositions++;
    }

    std::cout << "[MotionJsonLoader] Loaded " << devicePositionCount
      << " named positions for device: " << deviceName << std::endl;
  }

  std::cout << "[MotionJsonLoader] Total named positions loaded: " << totalPositions << std::endl;
}