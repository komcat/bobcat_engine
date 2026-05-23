// TECJsonLoader.cpp
#include "TECJsonLoader.h"
#include "nlohmann/json.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>

using json = nlohmann::json;

int TECJsonLoader::LoadFromFile(TECManager& manager, const std::string& filepath) {
  std::cout << "[INFO] TECJsonLoader: Loading from " << filepath << std::endl;

  try {
    // Open and parse JSON file
    std::ifstream file(filepath);
    if (!file.is_open()) {
      std::cerr << "[ERROR] TECJsonLoader: Failed to open file: " << filepath << std::endl;
      return 0;
    }

    json config;
    file >> config;
    file.close();

    // Load devices
    LoadDevices(manager, config);

    // Return device count
    size_t deviceCount = manager.GetTotalDeviceCount();
    std::cout << "[INFO] TECJsonLoader: Loaded " << deviceCount << " devices" << std::endl;

    return static_cast<int>(deviceCount);

  }
  catch (const json::exception& e) {
    std::cerr << "[ERROR] TECJsonLoader: JSON parse error: " << e.what() << std::endl;
    return 0;
  }
  catch (const std::exception& e) {
    std::cerr << "[ERROR] TECJsonLoader: Error: " << e.what() << std::endl;
    return 0;
  }
}

void TECJsonLoader::LoadDevices(TECManager& manager, const json& config) {
  if (!config.contains("tec_devices")) {
    std::cout << "[WARN] TECJsonLoader: No 'tec_devices' section found" << std::endl;
    return;
  }

  const auto& devices = config["tec_devices"];
  if (!devices.is_array()) {
    std::cerr << "[ERROR] TECJsonLoader: 'tec_devices' must be an array" << std::endl;
    return;
  }

  std::cout << "[INFO] TECJsonLoader: Loading " << devices.size() << " devices..." << std::endl;

  for (const auto& deviceJson : devices) {
    try {
      TECDevice device;

      // Required fields
      if (deviceJson.contains("name")) {
        device.Name = deviceJson["name"].get<std::string>();
      }
      else {
        std::cerr << "[ERROR] TECJsonLoader: Device missing 'name' field" << std::endl;
        continue;
      }

      if (deviceJson.contains("controller_type")) {
        device.TypeController = deviceJson["controller_type"].get<std::string>();
      }
      else {
        std::cerr << "[ERROR] TECJsonLoader: Device '" << device.Name
          << "' missing 'controller_type' field" << std::endl;
        continue;
      }

      // Check controller type to determine required fields
      // Direct VISA adapters: CLD101x_Direct, TED4015_Direct, TED4015
      // Kinesis (USB) adapters: TTC001, TTC-001
      std::string typeUpper = device.TypeController;
      std::transform(typeUpper.begin(), typeUpper.end(), typeUpper.begin(), ::toupper);

      bool isDirect = (typeUpper == "CLD101X_DIRECT" ||
                       typeUpper == "TED4015_DIRECT" ||
                       typeUpper == "TED4015");
      bool isKinesis = (typeUpper == "TTC001" || typeUpper == "TTC-001");

      // For Kinesis adapters, require serial_number
      // For direct VISA adapters, require visa_resource
      // For TCP-based adapters, require ip_address
      if (isKinesis) {
        if (deviceJson.contains("serial_number")) {
          device.SerialNumber = deviceJson["serial_number"].get<std::string>();
        }
        else {
          std::cerr << "[ERROR] TECJsonLoader: Device '" << device.Name
            << "' (type: " << device.TypeController << ") missing 'serial_number' field" << std::endl;
          continue;
        }
      }
      else if (isDirect) {
        if (deviceJson.contains("visa_resource")) {
          device.VISAResource = deviceJson["visa_resource"].get<std::string>();
        }
        else {
          std::cerr << "[ERROR] TECJsonLoader: Device '" << device.Name
            << "' (type: " << device.TypeController << ") missing 'visa_resource' field" << std::endl;
          continue;
        }
        // IP address is optional for direct adapters
        if (deviceJson.contains("ip_address")) {
          device.IpAddress = deviceJson["ip_address"].get<std::string>();
        }
      }
      else {
        // TCP-based adapter requires ip_address
        if (deviceJson.contains("ip_address")) {
          device.IpAddress = deviceJson["ip_address"].get<std::string>();
        }
        else {
          std::cerr << "[ERROR] TECJsonLoader: Device '" << device.Name
            << "' missing 'ip_address' field" << std::endl;
          continue;
        }
        // VISA resource is optional for TCP adapters
        if (deviceJson.contains("visa_resource")) {
          device.VISAResource = deviceJson["visa_resource"].get<std::string>();
        }
      }

      // Optional fields with defaults
      if (deviceJson.contains("port")) {
        device.Port = deviceJson["port"].get<int>();
      }
      else {
        device.Port = 65432;  // Default port
      }

      if (deviceJson.contains("enabled")) {
        device.Enabled = deviceJson["enabled"].get<bool>();
      }
      else {
        device.Enabled = true;  // Default enabled
      }

      if (deviceJson.contains("default_temperature")) {
        device.DefaultTemperature = deviceJson["default_temperature"].get<double>();
      }

      if (deviceJson.contains("default_laser_current")) {
        device.DefaultLaserCurrent = deviceJson["default_laser_current"].get<double>();
      }

      // Thermistor parameters (currently only consumed by TTC-001 in thermistor mode).
      if (deviceJson.contains("thermistor_beta")) {
        device.ThermistorBeta = deviceJson["thermistor_beta"].get<double>();
      }
      if (deviceJson.contains("thermistor_r0_ohms")) {
        device.ThermistorR0Ohms = deviceJson["thermistor_r0_ohms"].get<double>();
      }
      if (deviceJson.contains("thermistor_t0_celsius")) {
        device.ThermistorT0Celsius = deviceJson["thermistor_t0_celsius"].get<double>();
      }

      // TTC-001 sensor mode (transducer / thermistor_20k / thermistor_200k).
      // Validation happens in the adapter; here we just pass the string through.
      if (deviceJson.contains("sensor_mode")) {
        device.SensorMode = deviceJson["sensor_mode"].get<std::string>();
      }

      // Add device to manager
      manager.AddDevice(device);

      std::cout << "[INFO] TECJsonLoader: Loaded device '" << device.Name
        << "' (" << device.TypeController << ")" << std::endl;

    }
    catch (const json::exception& e) {
      std::cerr << "[ERROR] TECJsonLoader: Error parsing device: " << e.what() << std::endl;
    }
  }
}