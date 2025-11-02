// SMUJsonLoader.cpp - Implementation of SMU JSON configuration loader
#include "SMUJsonLoader.h"
#include "Keithley6482Adapter.h"
#include "KeithleySMUAdapter.h"  // ✅ ADD: Generic Keithley SMU adapter
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <algorithm>

using json = nlohmann::json;

bool SMUJsonLoader::LoadConfiguration(const std::string& filePath, SMUManager* manager)
{
  if (!manager) {
    std::cerr << "[SMUJsonLoader] Manager is null" << std::endl;
    return false;
  }

  std::cout << "[SMUJsonLoader] Loading configuration from: " << filePath << std::endl;

  // Read JSON file
  std::ifstream file(filePath);
  if (!file.is_open()) {
    std::cerr << "[SMUJsonLoader] Failed to open file: " << filePath << std::endl;
    return false;
  }

  json config;
  try {
    file >> config;
  }
  catch (const json::exception& e) {
    std::cerr << "[SMUJsonLoader] JSON parse error: " << e.what() << std::endl;
    return false;
  }

  // Check for devices array
  if (!config.contains("devices") || !config["devices"].is_array()) {
    std::cerr << "[SMUJsonLoader] No 'devices' array found in config" << std::endl;
    return false;
  }

  // Parse each device
  int deviceCount = 0;
  for (const auto& deviceJson : config["devices"]) {
    // Extract required fields
    if (!deviceJson.contains("name") || !deviceJson.contains("type") ||
      !deviceJson.contains("visa_resource")) {
      std::cerr << "[SMUJsonLoader] Device missing required fields (name, type, visa_resource)" << std::endl;
      continue;
    }

    std::string name = deviceJson["name"].get<std::string>();
    std::string type = deviceJson["type"].get<std::string>();
    std::string visaResource = deviceJson["visa_resource"].get<std::string>();

    // Check if device is enabled
    bool enabled = deviceJson.value("enabled", true);
    if (!enabled) {
      std::cout << "[SMUJsonLoader] Device disabled: " << name << std::endl;
      continue;
    }

    // Build device config
    SMUDeviceConfig deviceConfig;
    deviceConfig.Name = name;
    deviceConfig.Type = type;
    deviceConfig.VISAResource = visaResource;
    deviceConfig.Enabled = enabled;

    // Channel configuration
    deviceConfig.ChannelCount = deviceJson.value("channel_count", 1);
    if (deviceJson.contains("enabled_channels") && deviceJson["enabled_channels"].is_array()) {
      deviceConfig.EnabledChannels.clear();
      for (const auto& ch : deviceJson["enabled_channels"]) {
        deviceConfig.EnabledChannels.push_back(ch.get<int>());
      }
    }
    else {
      // Default: enable channel 1
      deviceConfig.EnabledChannels = { 1 };
    }

    // Optional fields
    deviceConfig.DefaultVoltage = deviceJson.value("default_voltage", 0.0);
    deviceConfig.DefaultCurrent = deviceJson.value("default_current", 0.0);
    deviceConfig.DefaultCompliance = deviceJson.value("default_compliance", 0.001);
    deviceConfig.MeasurementNPLC = deviceJson.value("measurement_nplc", 1);
    deviceConfig.AutoRange = deviceJson.value("auto_range", true);
    deviceConfig.FixedRange = deviceJson.value("fixed_range", 0.0);

    // Create device
    SMUDevicePtr device = CreateSMUDevice(type, visaResource, deviceConfig);

    if (device) {
      manager->AddDevice(name, device);
      deviceCount++;
      std::cout << "[SMUJsonLoader] Added device: " << name << " (" << type << ")" << std::endl;
    }
    else {
      std::cerr << "[SMUJsonLoader] Failed to create device: " << name << " (" << type << ")" << std::endl;
    }
  }

  std::cout << "[SMUJsonLoader] Loaded " << deviceCount << " device(s)" << std::endl;
  return deviceCount > 0;
}

SMUDevicePtr SMUJsonLoader::CreateSMUDevice(const std::string& type,
  const std::string& visaResource,
  const SMUDeviceConfig& config)
{
  // Normalize type string (lowercase, remove spaces)
  std::string normalizedType = type;
  std::transform(normalizedType.begin(), normalizedType.end(), normalizedType.begin(),
    [](unsigned char c) { return std::tolower(c); });

  // Remove spaces and underscores for flexible matching
  normalizedType.erase(std::remove(normalizedType.begin(), normalizedType.end(), ' '), normalizedType.end());
  normalizedType.erase(std::remove(normalizedType.begin(), normalizedType.end(), '_'), normalizedType.end());

  std::cout << "[SMUJsonLoader] Creating device type: " << normalizedType << std::endl;

  // Factory: Create appropriate adapter based on type
  if (normalizedType == "keithley6482" || normalizedType == "k6482") {
    return std::make_shared<Keithley6482Adapter>(visaResource, config);
  }
  // ✅ ADD: Generic Keithley SMU adapter (2400/2600 series)
  else if (normalizedType == "keithleysmu" || normalizedType == "ksmu" ||
           normalizedType == "keithley2400" || normalizedType == "k2400" ||
           normalizedType == "keithley2401" || normalizedType == "k2401" ||
           normalizedType == "keithley2410" || normalizedType == "k2410" ||
           normalizedType == "keithley2420" || normalizedType == "k2420" ||
           normalizedType == "keithley2430" || normalizedType == "k2430" ||
           normalizedType == "keithley2440" || normalizedType == "k2440" ||
           normalizedType == "keithley2450" || normalizedType == "k2450" ||
           normalizedType == "keithley2600" || normalizedType == "k2600" ||
           normalizedType == "keithley2601b" || normalizedType == "k2601b" ||
           normalizedType == "keithley2602b" || normalizedType == "k2602b" ||
           normalizedType == "keithley2604b" || normalizedType == "k2604b" ||
           normalizedType == "keithley2611b" || normalizedType == "k2611b" ||
           normalizedType == "keithley2612b" || normalizedType == "k2612b" ||
           normalizedType == "keithley2614b" || normalizedType == "k2614b") {
    return std::make_shared<KeithleySMUAdapter>(visaResource, config);
  }
  // Future adapters:
  // else if (normalizedType == "keysightb2900" || normalizedType == "b2900") {
  //   return std::make_shared<KeysightB2900Adapter>(visaResource, config);
  // }
  else {
    std::cerr << "[SMUJsonLoader] Unknown device type: " << type << std::endl;
    return nullptr;
  }
}
