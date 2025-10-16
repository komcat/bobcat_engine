// PneumaticJsonLoader.h - JSON configuration loader for pneumatic slides
#pragma once

#include "PneumaticManager.h"
#include "Pneumatic_Types.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>

using json = nlohmann::json;

/**
 * @brief Utility class for loading pneumatic configurations from JSON
 *
 * Example JSON format:
 * {
 *   "pneumaticSlides": [
 *     {
 *       "name": "UV_Head",
 *       "output": {
 *         "deviceName": "IOBottom",
 *         "pinName": "UV_Head"
 *       },
 *       "extendedInput": {
 *         "deviceName": "IOTop",
 *         "pinName": "UV_Head_Down"
 *       },
 *       "retractedInput": {
 *         "deviceName": "IOTop",
 *         "pinName": "UV_Head_Up"
 *       },
 *       "timeoutMs": 5000
 *     }
 *   ]
 * }
 */
class PneumaticJsonLoader {
public:
  /**
   * @brief Load pneumatic slides from JSON file into manager
   * @param manager The PneumaticManager to add slides to
   * @param filepath Path to JSON configuration file
   * @return Number of slides successfully loaded
   */
  static int LoadFromFile(PneumaticManager& manager, const std::string& filepath) {
    std::cout << "[PneumaticJsonLoader] Loading slides from: " << filepath << std::endl;

    try {
      // Read JSON file
      std::ifstream file(filepath);
      if (!file.is_open()) {
        std::cerr << "[PneumaticJsonLoader] ERROR: Cannot open file: " << filepath << std::endl;
        return 0;
      }

      json j;
      file >> j;

      // Parse slide configurations
      std::vector<PneumaticConfig> configs = ParseJson(j);

      // Add slides to manager
      int addedCount = 0;
      for (const auto& config : configs) {
        if (manager.AddSlide(config)) {
          addedCount++;
        }
      }

      std::cout << "[PneumaticJsonLoader] Successfully loaded " << addedCount
        << "/" << configs.size() << " slides" << std::endl;

      return addedCount;

    }
    catch (const json::exception& e) {
      std::cerr << "[PneumaticJsonLoader] JSON parse error: " << e.what() << std::endl;
      return 0;
    }
    catch (const std::exception& e) {
      std::cerr << "[PneumaticJsonLoader] Error: " << e.what() << std::endl;
      return 0;
    }
  }

  /**
   * @brief Parse JSON and extract pneumatic configurations
   * @param j JSON object
   * @return Vector of pneumatic configurations
   */
  static std::vector<PneumaticConfig> ParseJson(const json& j) {
    std::vector<PneumaticConfig> configs;

    // Check if "pneumaticSlides" array exists
    if (!j.contains("pneumaticSlides") || !j["pneumaticSlides"].is_array()) {
      std::cerr << "[PneumaticJsonLoader] ERROR: Missing or invalid 'pneumaticSlides' array in JSON" << std::endl;
      return configs;
    }

    // Parse each slide
    for (const auto& slideJson : j["pneumaticSlides"]) {
      try {
        PneumaticConfig config;

        // Required fields
        config.Name = slideJson.at("name").get<std::string>();

        // Output pin
        const auto& outputJson = slideJson.at("output");
        config.Output.DeviceName = outputJson.at("deviceName").get<std::string>();
        config.Output.PinName = outputJson.at("pinName").get<std::string>();

        // Extended sensor
        const auto& extendedJson = slideJson.at("extendedInput");
        config.ExtendedSensor.DeviceName = extendedJson.at("deviceName").get<std::string>();
        config.ExtendedSensor.PinName = extendedJson.at("pinName").get<std::string>();

        // Retracted sensor
        const auto& retractedJson = slideJson.at("retractedInput");
        config.RetractedSensor.DeviceName = retractedJson.at("deviceName").get<std::string>();
        config.RetractedSensor.PinName = retractedJson.at("pinName").get<std::string>();

        // Optional timeout
        if (slideJson.contains("timeoutMs")) {
          config.TimeoutMs = slideJson["timeoutMs"].get<uint32_t>();
        }
        else {
          config.TimeoutMs = 5000; // Default 5 seconds
        }

        configs.push_back(config);

        std::cout << "[PneumaticJsonLoader] Parsed slide: " << config.Name
          << " (Timeout: " << config.TimeoutMs << "ms)" << std::endl;
        std::cout << "  Output: " << config.Output.DeviceName << "." << config.Output.PinName << std::endl;
        std::cout << "  Extended: " << config.ExtendedSensor.DeviceName << "." << config.ExtendedSensor.PinName << std::endl;
        std::cout << "  Retracted: " << config.RetractedSensor.DeviceName << "." << config.RetractedSensor.PinName << std::endl;

      }
      catch (const json::exception& e) {
        std::cerr << "[PneumaticJsonLoader] Error parsing slide: " << e.what() << std::endl;
        continue;
      }
    }

    return configs;
  }

  /**
   * @brief Save current slide configurations to JSON file
   * @param manager The PneumaticManager to get slides from
   * @param filepath Path to output JSON file
   * @return True if successful
   */
  static bool SaveToFile(const PneumaticManager& manager, const std::string& filepath) {
    try {
      json j;
      json slidesArray = json::array();

      // Get all slide names
      auto slideNames = manager.GetAllSlideNames();

      for (const auto& name : slideNames) {
        IPneumaticSlide* slide = const_cast<PneumaticManager&>(manager).GetSlide(name);
        if (slide) {
          json slideJson;
          slideJson["name"] = name;
          slideJson["timeoutMs"] = slide->GetTimeout();

          // Note: Cannot retrieve IO pin references from interface
          // This is a limitation - consider storing config separately if needed

          slidesArray.push_back(slideJson);
        }
      }

      j["pneumaticSlides"] = slidesArray;

      // Write to file
      std::ofstream file(filepath);
      if (!file.is_open()) {
        std::cerr << "[PneumaticJsonLoader] ERROR: Cannot open file for writing: "
          << filepath << std::endl;
        return false;
      }

      file << j.dump(2); // Pretty print with 2 spaces indent
      file.close();

      std::cout << "[PneumaticJsonLoader] Saved " << slidesArray.size()
        << " slides to: " << filepath << std::endl;

      return true;

    }
    catch (const std::exception& e) {
      std::cerr << "[PneumaticJsonLoader] Error saving: " << e.what() << std::endl;
      return false;
    }
  }
};