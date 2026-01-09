// UpdatePositionProcesses.cpp
// Process plugin to update a position axis value from context variable
// Useful for auto-focus workflows: save best Z to a named position

#include "../IProcessPlugin.h"
#include "../ProcessPluginRegistry.h"
#include "../RecipeManager.h"
#include "../../sequence/SequenceBuilder.h"
#include "../../AppUI/RecipePage/ProcessConfigWidget.h"
#include "../../AppUI/RecipePage/DeviceSelectorWidgets.h"

#include <iostream>
#include <memory>
#include <map>
#include <string>

//=============================================================================
// UPDATE POSITION AXIS PLUGIN
//=============================================================================
// Updates a single axis (X/Y/Z/U/V/W) of a saved position with a value
// from the execution context, then optionally saves the product file.

class UpdatePositionAxisPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Update Position Axis";
  }

  std::string GetCategory() const override {
    return "Motion Control";
  }

  std::string GetDescription() const override {
    return "Update a position's axis value from context variable and save to file";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"motion_device", "gantry-main"},
      {"position_name", ""},
      {"axis", "Z"},
      {"context_key", "autofocus.best_z_mm"},
      {"save_to_file", "true"}
    };
  }

  // Helper to safely get parameter with default value
  std::string getParam(const std::map<std::string, std::string>& params,
                       const std::string& key, const std::string& defaultVal) const {
    auto it = params.find(key);
    return (it != params.end()) ? it->second : defaultVal;
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    // Motion device selector
    widget->addDeviceSelector("motion_device", "Motion Device",
                              getParam(params, "motion_device", "gantry-main"),
                              static_cast<int>(DeviceComboBox::DeviceType::Motion));

    // Position name selector (linked to motion_device)
    widget->addNodeSelector("position_name", "Position Name",
                            getParam(params, "position_name", ""),
                            "motion_device");

    // Axis dropdown
    widget->addDropdownParameter("axis", "Axis to Update",
                                 getParam(params, "axis", "Z"),
                                 {"X", "Y", "Z", "U", "V", "W"});

    // Context key field
    widget->addParameterField("context_key", "Context Key (source value)",
                              getParam(params, "context_key", "autofocus.best_z_mm"));

    // Save to file checkbox
    widget->addCheckboxParameter("save_to_file", "Save to Product File",
                                 getParam(params, "save_to_file", "true"),
                                 "Save the updated position to the product JSON file");
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string motionDevice = getParam(params, "motion_device", "gantry-main");
    std::string positionName = getParam(params, "position_name", "");
    std::string axis = getParam(params, "axis", "Z");
    std::string contextKey = getParam(params, "context_key", "autofocus.best_z_mm");
    std::string saveToFile = getParam(params, "save_to_file", "true");

    if (positionName.empty()) {
      std::cerr << "[UpdatePositionAxisPlugin] Error: No position name specified" << std::endl;
      return;
    }

    // Build the UpdatePositionAxis command
    // Note: AddAction signature is (deviceName, methodName, params)
    // The device name tells SequenceEngine which handler to use (motion -> MotionCommandHandler)
    std::map<std::string, std::string> cmdParams = {
      {"position_name", positionName},
      {"axis", axis},
      {"context_key", contextKey},
      {"save_to_file", saveToFile}
    };

    sequence->AddAction(motionDevice, "UpdatePositionAxis", cmdParams);

    std::cout << "[UpdatePositionAxisPlugin] Built sequence for: "
      << step->userNickname << " (Device: " << motionDevice
      << ", Position: " << positionName << ", Axis: " << axis
      << ", Source: " << contextKey << ")" << std::endl;
  }
};

//=============================================================================
// UPDATE POSITION ALL PLUGIN
//=============================================================================
// Updates ALL available axes of a saved position with current device position
// Useful for capturing complete current position to a named position

class UpdatePositionAllPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Update Position All";
  }

  std::string GetCategory() const override {
    return "Motion Control";
  }

  std::string GetDescription() const override {
    return "Update all axes of a position with current device position and save to file";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"motion_device", "gantry-main"},
      {"position_name", ""},
      {"save_to_file", "true"}
    };
  }

  // Helper to safely get parameter with default value
  std::string getParam(const std::map<std::string, std::string>& params,
                       const std::string& key, const std::string& defaultVal) const {
    auto it = params.find(key);
    return (it != params.end()) ? it->second : defaultVal;
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    // Motion device selector
    widget->addDeviceSelector("motion_device", "Motion Device",
                              getParam(params, "motion_device", "gantry-main"),
                              static_cast<int>(DeviceComboBox::DeviceType::Motion));

    // Position name selector (linked to motion_device)
    widget->addNodeSelector("position_name", "Position Name",
                            getParam(params, "position_name", ""),
                            "motion_device");

    // Save to file checkbox
    widget->addCheckboxParameter("save_to_file", "Save to Product File",
                                 getParam(params, "save_to_file", "true"),
                                 "Save the updated position to the product JSON file");
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string motionDevice = getParam(params, "motion_device", "gantry-main");
    std::string positionName = getParam(params, "position_name", "");
    std::string saveToFile = getParam(params, "save_to_file", "true");

    if (positionName.empty()) {
      std::cerr << "[UpdatePositionAllPlugin] Error: No position name specified" << std::endl;
      return;
    }

    // Build the UpdatePositionAll command
    std::map<std::string, std::string> cmdParams = {
      {"position_name", positionName},
      {"save_to_file", saveToFile}
    };

    sequence->AddAction(motionDevice, "UpdatePositionAll", cmdParams);

    std::cout << "[UpdatePositionAllPlugin] Built sequence for: "
      << step->userNickname << " (Device: " << motionDevice
      << ", Position: " << positionName << ")" << std::endl;
  }
};

//=============================================================================
// PLUGIN REGISTRATION
//=============================================================================

namespace {
  struct UpdatePositionProcessRegistrar {
    UpdatePositionProcessRegistrar() {
      auto& registry = ProcessPluginRegistry::Instance();
      registry.RegisterPlugin(std::make_unique<UpdatePositionAxisPlugin>());
      registry.RegisterPlugin(std::make_unique<UpdatePositionAllPlugin>());
      std::cout << "[UpdatePositionProcesses] Registered Update Position plugins" << std::endl;
    }
  };

  static UpdatePositionProcessRegistrar s_registrar;
}
