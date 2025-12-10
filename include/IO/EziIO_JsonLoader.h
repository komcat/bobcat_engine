// EziIO_JsonLoader.h - JSON configuration loader with pin name mapping
#pragma once

#include "EziIOManager.h"
#include "EziIO_Types.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>

using json = nlohmann::json;

/**
 * @brief Utility class for loading EziIO device configurations from JSON
 *
 * Example JSON format:
 * {
 *   "eziio": [
 *     {
 *       "ip": "192.168.0.3",
 *       "deviceId": 0,
 *       "inputCount": 16,
 *       "outputCount": 16,
 *       "name": "IOBottom",
 *       "enabled": true,
 *       "ioConfig": {
 *         "inputs": [
 *           { "name": "Sensor1", "pin": 0 }
 *         ],
 *         "outputs": [
 *           { "name": "L_Gripper", "pin": 0 }
 *         ]
 *       }
 *     }
 *   ]
 * }
 */
class EziIOJsonLoader {
public:
  /**
   * @brief Load devices from JSON file into manager
   * @param manager The EziIOManager to add devices to
   * @param filepath Path to JSON configuration file
   * @return Number of devices successfully loaded
   */
  static int LoadFromFile(EziIOManager& manager, const std::string& filepath) {
    std::cout << "[EziIOJsonLoader] Loading devices from: " << filepath << std::endl;

    try {
      // Read JSON file
      std::ifstream file(filepath);
      if (!file.is_open()) {
        std::cerr << "[EziIOJsonLoader] ERROR: Cannot open file: " << filepath << std::endl;
        return 0;
      }

      json j;
      file >> j;

      int addedCount = 0;

      // Load WAGO devices first (if present)
      if (j.contains("wago") && j["wago"].is_array()) {
        std::cout << "[EziIOJsonLoader] Found WAGO device section" << std::endl;
        std::vector<EziIODeviceConfig> wagoConfigs = ParseJsonArray(j["wago"], "wago");
        for (const auto& config : wagoConfigs) {
          if (manager.AddWagoDevice(config)) {
            ApplyPinMappingsFromArray(manager, j["wago"], config.Name);
            addedCount++;
          }
        }
      }

      // Load EziIO devices (if present)
      if (j.contains("eziio") && j["eziio"].is_array()) {
        std::cout << "[EziIOJsonLoader] Found EziIO device section" << std::endl;
        std::vector<EziIODeviceConfig> eziioConfigs = ParseJsonArray(j["eziio"], "eziio");
        for (const auto& config : eziioConfigs) {
          if (manager.AddDevice(config)) {
            ApplyPinMappingsFromArray(manager, j["eziio"], config.Name);
            addedCount++;
          }
        }
      }

      std::cout << "[EziIOJsonLoader] Successfully loaded " << addedCount << " devices" << std::endl;
      return addedCount;

    }
    catch (const json::exception& e) {
      std::cerr << "[EziIOJsonLoader] JSON parse error: " << e.what() << std::endl;
      return 0;
    }
    catch (const std::exception& e) {
      std::cerr << "[EziIOJsonLoader] Error: " << e.what() << std::endl;
      return 0;
    }
  }

  /**
   * @brief Parse a JSON array and extract device configurations
   * @param arr JSON array of devices
   * @param deviceType Type name for logging (e.g., "wago", "eziio")
   * @return Vector of device configurations
   */
  static std::vector<EziIODeviceConfig> ParseJsonArray(const json& arr, const std::string& deviceType) {
    std::vector<EziIODeviceConfig> configs;

    for (const auto& deviceJson : arr) {
      try {
        EziIODeviceConfig config;
        config.DeviceId = deviceJson.at("deviceId").get<int>();
        config.Name = deviceJson.at("name").get<std::string>();
        config.IpAddress = deviceJson.at("ip").get<std::string>();
        config.InputCount = deviceJson.at("inputCount").get<int>();
        config.OutputCount = deviceJson.at("outputCount").get<int>();
        config.Enabled = deviceJson.value("enabled", true);
        configs.push_back(config);

        std::cout << "[EziIOJsonLoader] Parsed " << deviceType << " device: " << config.Name
          << " (ID: " << config.DeviceId << ", IP: " << config.IpAddress << ")" << std::endl;
      }
      catch (const json::exception& e) {
        std::cerr << "[EziIOJsonLoader] Error parsing " << deviceType << " device: " << e.what() << std::endl;
      }
    }
    return configs;
  }

  /**
   * @brief Apply pin name mappings from a JSON array to device
   */
  static void ApplyPinMappingsFromArray(EziIOManager& manager, const json& arr, const std::string& deviceName) {
    for (const auto& deviceJson : arr) {
      if (deviceJson["name"].get<std::string>() != deviceName) continue;
      if (!deviceJson.contains("ioConfig")) return;

      const auto& ioConfig = deviceJson["ioConfig"];
      IEziIODevice* device = manager.GetDevice(deviceName);
      if (!device) return;

      if (ioConfig.contains("inputs") && ioConfig["inputs"].is_array()) {
        for (const auto& cfg : ioConfig["inputs"]) {
          device->MapInputPinName(cfg.at("name").get<std::string>(), cfg.at("pin").get<int>());
        }
      }
      if (ioConfig.contains("outputs") && ioConfig["outputs"].is_array()) {
        for (const auto& cfg : ioConfig["outputs"]) {
          device->MapOutputPinName(cfg.at("name").get<std::string>(), cfg.at("pin").get<int>());
        }
      }
      std::cout << "[EziIOJsonLoader] Applied pin mappings to " << deviceName << std::endl;
      return;
    }
  }

  /**
   * @brief Parse JSON and extract device configurations (legacy)
   * @param j JSON object
   * @return Vector of device configurations
   */
  static std::vector<EziIODeviceConfig> ParseJson(const json& j) {
    std::vector<EziIODeviceConfig> configs;

    // Check if "eziio" array exists
    if (!j.contains("eziio") || !j["eziio"].is_array()) {
      std::cerr << "[EziIOJsonLoader] ERROR: Missing or invalid 'eziio' array in JSON" << std::endl;
      return configs;
    }

    // Parse each device
    for (const auto& deviceJson : j["eziio"]) {
      try {
        EziIODeviceConfig config;

        // Required fields
        config.DeviceId = deviceJson.at("deviceId").get<int>();
        config.Name = deviceJson.at("name").get<std::string>();
        config.IpAddress = deviceJson.at("ip").get<std::string>();
        config.InputCount = deviceJson.at("inputCount").get<int>();
        config.OutputCount = deviceJson.at("outputCount").get<int>();

        // Optional fields
        if (deviceJson.contains("enabled")) {
          config.Enabled = deviceJson["enabled"].get<bool>();
        }
        else {
          config.Enabled = true; // Default to enabled
        }

        configs.push_back(config);

        std::cout << "[EziIOJsonLoader] Parsed device: " << config.Name
          << " (ID: " << config.DeviceId
          << ", IP: " << config.IpAddress
          << ", Inputs: " << config.InputCount
          << ", Outputs: " << config.OutputCount
          << ", Enabled: " << (config.Enabled ? "Yes" : "No") << ")" << std::endl;

      }
      catch (const json::exception& e) {
        std::cerr << "[EziIOJsonLoader] Error parsing device: " << e.what() << std::endl;
        continue;
      }
    }

    return configs;
  }

  /**
   * @brief Apply pin name mappings from ioConfig to device
   * @param manager The EziIOManager containing the device
   * @param j JSON object containing full configuration
   * @param deviceName Name of the device to apply mappings to
   */
   // In EziIO_JsonLoader.h, update the ApplyPinMappings function:

  static void ApplyPinMappings(EziIOManager& manager, const json& j, const std::string& deviceName) {
    try {
      std::cout << "[EziIOJsonLoader] === ApplyPinMappings for: " << deviceName << " ===" << std::endl;

      // Find the device configuration in JSON
      for (const auto& deviceJson : j["eziio"]) {
        if (deviceJson["name"].get<std::string>() != deviceName) {
          continue;
        }

        std::cout << "[EziIOJsonLoader] Found device in JSON: " << deviceName << std::endl;

        // Check if ioConfig exists
        if (!deviceJson.contains("ioConfig")) {
          std::cout << "[EziIOJsonLoader] WARNING: No ioConfig found for " << deviceName << std::endl;
          return;
        }

        const auto& ioConfig = deviceJson["ioConfig"];
        IEziIODevice* device = manager.GetDevice(deviceName);
        if (!device) {
          std::cerr << "[EziIOJsonLoader] ERROR: Device not found in manager: " << deviceName << std::endl;
          return;
        }

        int inputMappingCount = 0;
        int outputMappingCount = 0;

        // Map input pin names
        if (ioConfig.contains("inputs") && ioConfig["inputs"].is_array()) {
          std::cout << "[EziIOJsonLoader] Processing " << ioConfig["inputs"].size() << " input mappings..." << std::endl;
          for (const auto& inputConfig : ioConfig["inputs"]) {
            std::string name = inputConfig.at("name").get<std::string>();
            int pin = inputConfig.at("pin").get<int>();

            std::cout << "[EziIOJsonLoader]   Mapping input: " << name << " -> Pin " << pin << std::endl;

            device->MapInputPinName(name, pin);
            inputMappingCount++;
          }
        }

        // Map output pin names
        if (ioConfig.contains("outputs") && ioConfig["outputs"].is_array()) {
          std::cout << "[EziIOJsonLoader] Processing " << ioConfig["outputs"].size() << " output mappings..." << std::endl;
          for (const auto& outputConfig : ioConfig["outputs"]) {
            std::string name = outputConfig.at("name").get<std::string>();
            int pin = outputConfig.at("pin").get<int>();

            std::cout << "[EziIOJsonLoader]   Mapping output: " << name << " -> Pin " << pin << std::endl;

            device->MapOutputPinName(name, pin);
            outputMappingCount++;
          }
        }

        std::cout << "[EziIOJsonLoader] ✓ Applied " << inputMappingCount << " input + "
          << outputMappingCount << " output mappings to " << deviceName << std::endl;

        return; // Found and processed the device
      }

      std::cerr << "[EziIOJsonLoader] WARNING: Device " << deviceName << " not found in JSON!" << std::endl;

    }
    catch (const json::exception& e) {
      std::cerr << "[EziIOJsonLoader] ERROR applying pin mappings for "
        << deviceName << ": " << e.what() << std::endl;
    }
    catch (const std::exception& e) {
      std::cerr << "[EziIOJsonLoader] ERROR: " << e.what() << std::endl;
    }
  }



  /**
   * @brief Save current device configurations to JSON file
   * @param manager The EziIOManager to get devices from
   * @param filepath Path to output JSON file
   * @return True if successful
   */
  static bool SaveToFile(const EziIOManager& manager, const std::string& filepath) {
    try {
      json j;
      json devicesArray = json::array();

      // Get all device names
      auto deviceNames = manager.GetAllDeviceNames();

      for (const auto& name : deviceNames) {
        IEziIODevice* device = const_cast<EziIOManager&>(manager).GetDevice(name);
        if (device) {
          json deviceJson;
          deviceJson["deviceId"] = device->GetDeviceId();
          deviceJson["name"] = device->GetName();
          deviceJson["ip"] = device->GetIpAddress();
          deviceJson["inputCount"] = device->GetInputCount();
          deviceJson["outputCount"] = device->GetOutputCount();
          deviceJson["enabled"] = true;
          deviceJson["ioConfig"] = json::object(); // Placeholder for future config

          devicesArray.push_back(deviceJson);
        }
      }

      j["eziio"] = devicesArray;

      // Write to file
      std::ofstream file(filepath);
      if (!file.is_open()) {
        std::cerr << "[EziIOJsonLoader] ERROR: Cannot open file for writing: "
          << filepath << std::endl;
        return false;
      }

      file << j.dump(2); // Pretty print with 2 spaces indent
      file.close();

      std::cout << "[EziIOJsonLoader] Saved " << devicesArray.size()
        << " devices to: " << filepath << std::endl;

      return true;

    }
    catch (const std::exception& e) {
      std::cerr << "[EziIOJsonLoader] Error saving: " << e.what() << std::endl;
      return false;
    }
  }
};