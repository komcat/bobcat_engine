// QuickOutput_JsonLoader.cpp
#include "QuickOutput_JsonLoader.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

using json = nlohmann::json;

// Forward declaration of helper function
static bool ParseJsonAndLoadOutputs(const json& j, QuickOutputManager* manager);

bool QuickOutput_JsonLoader::LoadFromFile(const std::string& filePath, QuickOutputManager* manager)
{
  if (!manager) {
    std::cerr << "QuickOutput_JsonLoader: Manager is null" << std::endl;
    return false;
  }

  // Read JSON file
  std::ifstream file(filePath);
  if (!file.is_open()) {
    std::cerr << "QuickOutput_JsonLoader: Failed to open file: " << filePath << std::endl;
    return false;
  }

  try {
    json j;
    file >> j;
    file.close();

    return ParseJsonAndLoadOutputs(j, manager);
  }
  catch (const std::exception& e) {
    std::cerr << "QuickOutput_JsonLoader: JSON parse error: " << e.what() << std::endl;
    return false;
  }
}

bool QuickOutput_JsonLoader::LoadFromString(const std::string& jsonString, QuickOutputManager* manager)
{
  if (!manager) {
    std::cerr << "QuickOutput_JsonLoader: Manager is null" << std::endl;
    return false;
  }

  try {
    json j = json::parse(jsonString);
    return ParseJsonAndLoadOutputs(j, manager);
  }
  catch (const std::exception& e) {
    std::cerr << "QuickOutput_JsonLoader: JSON parse error: " << e.what() << std::endl;
    return false;
  }
}

bool QuickOutput_JsonLoader::SaveToFile(const std::string& filePath, QuickOutputManager* manager)
{
  if (!manager) {
    std::cerr << "QuickOutput_JsonLoader: Manager is null" << std::endl;
    return false;
  }

  try {
    json j;
    json outputsArray = json::array();

    // Get all outputs from manager
    size_t pinCount = manager->GetPinCount();
    for (size_t i = 0; i < pinCount; ++i) {
      const OutputPinDefinition* pinDef = manager->GetPinDefinition(i);
      if (!pinDef) {
        continue;
      }

      json outputObj;
      outputObj["device_name"] = pinDef->deviceName;
      outputObj["display_name"] = pinDef->displayName;

      // Use pin_number if available, otherwise pin_name
      if (pinDef->pinNumber >= 0) {
        outputObj["pin_number"] = pinDef->pinNumber;
      }
      else if (!pinDef->pinName.empty()) {
        outputObj["pin_name"] = pinDef->pinName;
      }

      outputsArray.push_back(outputObj);
    }

    j["quick_outputs"] = outputsArray;

    // Write to file with pretty formatting
    std::ofstream file(filePath);
    if (!file.is_open()) {
      std::cerr << "QuickOutput_JsonLoader: Failed to create file: " << filePath << std::endl;
      return false;
    }

    file << j.dump(2);  // Indent with 2 spaces
    file.close();

    std::cout << "QuickOutput_JsonLoader: Saved " << pinCount << " outputs to " << filePath << std::endl;
    return true;
  }
  catch (const std::exception& e) {
    std::cerr << "QuickOutput_JsonLoader: Save error: " << e.what() << std::endl;
    return false;
  }
}

// Helper function implementation
static bool ParseJsonAndLoadOutputs(const json& j, QuickOutputManager* manager)
{
  if (!j.contains("quick_outputs")) {
    std::cerr << "QuickOutput_JsonLoader: Missing 'quick_outputs' field" << std::endl;
    return false;
  }

  const auto& outputs = j["quick_outputs"];
  if (!outputs.is_array()) {
    std::cerr << "QuickOutput_JsonLoader: 'quick_outputs' must be an array" << std::endl;
    return false;
  }

  // Clear existing outputs
  manager->ClearAllPins();

  int loadedCount = 0;
  for (const auto& outputObj : outputs) {
    try {
      // Required fields
      if (!outputObj.contains("device_name") || !outputObj.contains("display_name")) {
        std::cerr << "QuickOutput_JsonLoader: Missing required fields (device_name, display_name)" << std::endl;
        continue;
      }

      std::string deviceName = outputObj["device_name"];
      std::string displayName = outputObj["display_name"];

      OutputPinDefinition pinDef;
      pinDef.deviceName = deviceName;
      pinDef.displayName = displayName;

      // Either pin_number or pin_name must be provided
      if (outputObj.contains("pin_number")) {
        pinDef.pinNumber = outputObj["pin_number"];
      }
      else if (outputObj.contains("pin_name")) {
        pinDef.pinName = outputObj["pin_name"];
        pinDef.pinNumber = -1;  // Will be resolved later
      }
      else {
        std::cerr << "QuickOutput_JsonLoader: Missing pin_number or pin_name for " << displayName << std::endl;
        continue;
      }

      manager->AddOutputPin(pinDef);
      loadedCount++;
    }
    catch (const std::exception& e) {
      std::cerr << "QuickOutput_JsonLoader: Error parsing output: " << e.what() << std::endl;
      continue;
    }
  }

  std::cout << "QuickOutput_JsonLoader: Loaded " << loadedCount << " quick outputs" << std::endl;
  return loadedCount > 0;
}
