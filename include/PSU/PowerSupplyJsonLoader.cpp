// PowerSupplyJsonLoader.cpp
#include "PowerSupplyJsonLoader.h"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

bool PowerSupplyJsonLoader::loadConfiguration(const std::string& filePath, PowerSupplyManager* manager)
{
  if (!manager) {
    std::cerr << "[PowerSupplyJsonLoader] Manager is null" << std::endl;
    return false;
  }

  try {
    // Open and parse JSON file
    std::ifstream file(filePath);
    if (!file.is_open()) {
      std::cerr << "[PowerSupplyJsonLoader] Failed to open: " << filePath << std::endl;
      return false;
    }

    json config;
    file >> config;
    file.close();

    // Extract server configuration
    if (!config.contains("server")) {
      std::cerr << "[PowerSupplyJsonLoader] Missing 'server' section in config" << std::endl;
      return false;
    }

    std::string serverHost = config["server"].value("host", "127.0.0.60");
    int serverPort = config["server"].value("port", 5000);

    std::cout << "[PowerSupplyJsonLoader] Server: " << serverHost << ":" << serverPort << std::endl;

    // Load devices
    if (!config.contains("devices") || !config["devices"].is_array()) {
      std::cerr << "[PowerSupplyJsonLoader] Missing or invalid 'devices' section" << std::endl;
      return false;
    }

    int deviceCount = 0;
    for (const auto& deviceConfig : config["devices"]) {
      // Extract device parameters
      std::string name = deviceConfig.value("name", "Unknown");
      std::string type = deviceConfig.value("type", "keysight_e36103b");
      std::string deviceIP = deviceConfig.value("ip", "");
      int devicePort = deviceConfig.value("port", 5025);

      if (deviceIP.empty()) {
        std::cerr << "[PowerSupplyJsonLoader] Device '" << name << "' missing IP address" << std::endl;
        continue;
      }

      // Create power supply adapter
      PowerSupplyPtr psu = createPowerSupply(type, name, serverHost, serverPort, deviceIP, devicePort);

      if (psu) {
        manager->addPowerSupply(name, psu);
        deviceCount++;
        std::cout << "[PowerSupplyJsonLoader] Loaded device: " << name
          << " (Type: " << type << ", IP: " << deviceIP << ")" << std::endl;
      }
      else {
        std::cerr << "[PowerSupplyJsonLoader] Failed to create device: " << name << std::endl;
      }
    }

    std::cout << "[PowerSupplyJsonLoader] Successfully loaded " << deviceCount << " device(s)" << std::endl;
    return deviceCount > 0;

  }
  catch (const json::exception& e) {
    std::cerr << "[PowerSupplyJsonLoader] JSON error: " << e.what() << std::endl;
    return false;
  }
  catch (const std::exception& e) {
    std::cerr << "[PowerSupplyJsonLoader] Error: " << e.what() << std::endl;
    return false;
  }
}

PowerSupplyPtr PowerSupplyJsonLoader::createPowerSupply(const std::string& type,
  const std::string& name,
  const std::string& serverHost,
  int serverPort,
  const std::string& deviceIP,
  int devicePort)
{
  // Currently only supports Keysight E36103B
  // Can be extended to support other PSU types
  if (type == "keysight_e36103b" || type == "keysight") {
    return std::make_shared<KeysightPSUAdapter>(name, serverHost, serverPort, deviceIP, devicePort);
  }

  std::cerr << "[PowerSupplyJsonLoader] Unknown PSU type: " << type << std::endl;
  return nullptr;
}