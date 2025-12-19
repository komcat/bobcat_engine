// YawCorrectionProcesses.cpp
// Yaw Correction calibration process plugin
// Measures yaw angle between two corners and applies correction to hexapod U axis

#include "../IProcessPlugin.h"
#include "../ProcessPluginRegistry.h"
#include "../../sequence/SequenceBuilder.h"
#include "../../sequence/SequenceEngine.h"
#include "../../AppUI/RecipePage/ProcessConfigWidget.h"
#include "../../AppUI/RecipePage/DeviceSelectorWidgets.h"
#include "../RecipeManager.h"

#include <iostream>
#include <memory>
#include <map>
#include <string>

//=============================================================================
// YAW CORRECTION PLUGIN
//=============================================================================

class YawCorrectionPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Yaw Correction";
  }

  std::string GetCategory() const override {
    return "Calibration";
  }

  std::string GetDescription() const override {
    return "Calibrate yaw angle by measuring two corner positions and apply U-axis correction to hexapods";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"gantry_device", "gantry-main"},
      {"target_device", "hex-left"},
      {"x_direction", "up"},
      {"apply_to_position", "false"},
      {"position_device", "hex-left"},
      {"position_name", ""}
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

    // Gantry device selector (for measuring corners)
    widget->addDeviceSelector("gantry_device", "Gantry Device",
                              getParam(params, "gantry_device", "gantry-main"),
                              static_cast<int>(DeviceComboBox::DeviceType::Motion));

    // Target device (hexapod to correct)
    widget->addDeviceSelector("target_device", "Target Device (Hexapod)",
                              getParam(params, "target_device", "hex-left"),
                              static_cast<int>(DeviceComboBox::DeviceType::Motion));

    // X-axis direction (right-hand rule orientation)
    widget->addDropdownParameter("x_direction", "X Direction (Right-Hand Rule)",
                                 getParam(params, "x_direction", "up"),
                                 {"up", "down"});

    // Checkbox to apply yaw correction to a saved position
    widget->addCheckboxParameter("apply_to_position", "Apply U to Position",
                                 getParam(params, "apply_to_position", "false"),
                                 "Save the U correction value to a named position");

    // Device selector for the position to update
    widget->addDeviceSelector("position_device", "Position Device",
                              getParam(params, "position_device", "hex-left"),
                              static_cast<int>(DeviceComboBox::DeviceType::Motion));

    // Node selector for position name (linked to position_device)
    widget->addNodeSelector("position_name", "Position Name",
                            getParam(params, "position_name", ""),
                            "position_device");
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string gantryDevice = getParam(params, "gantry_device", "gantry-main");
    std::string targetDevice = getParam(params, "target_device", "hex-left");
    std::string xDirection = getParam(params, "x_direction", "up");

    // Apply to position parameters (with safe defaults)
    bool applyToPosition = (getParam(params, "apply_to_position", "false") == "true");
    std::string positionDevice = getParam(params, "position_device", "hex-left");
    std::string positionName = getParam(params, "position_name", "");

    // Get the process instance UID for unique data files
    std::string uid = step->uniqueId;

    // =========================================================================
    // STEP 1: LOAD EXISTING YAW CORRECTION DATA (if exists)
    // =========================================================================

    sequence->AddAction("calibration", "LoadYawCorrectionData", {
      {"uid", uid}
    });

    // =========================================================================
    // STEP 2: CORNER 1 - Navigate to saved position (if exists) then manual adjust
    // =========================================================================

    // If saved data exists, navigate to corner 1 first (using generic motion command)
    sequence->BeginIf("yaw_data_exists", ConditionalBlock::ComparisonType::IS_TRUE);
    {
      sequence->AddAction(gantryDevice, "MoveToAbsFromContext", {
        {"key_prefix", "corner1"},
        {"keep_z", "true"}  // Maintain current Z height
      });
    }
    sequence->EndIf();

    // Manual adjust for corner 1
    sequence->AddAction("system", "ManualAdjust", {
      {"message", "Position gantry over CORNER 1.\n\n"
                  "Adjust X/Y to align with the first corner reference point.\n"
                  "Click OK when positioned."},
      {"device_name", gantryDevice},
      {"step_size_um", "100"},
      {"enable_x", "true"},
      {"enable_y", "true"},
      {"enable_z", "false"}
    });

    // Capture corner 1 position (using generic motion command)
    sequence->AddAction(gantryDevice, "SavePositionToContext", {
      {"key_prefix", "corner1"}
    });

    // =========================================================================
    // STEP 3: CORNER 2 - Navigate to saved position (if exists) then manual adjust
    // =========================================================================

    // If saved data exists, navigate to corner 2 first (using generic motion command)
    sequence->BeginIf("yaw_data_exists", ConditionalBlock::ComparisonType::IS_TRUE);
    {
      sequence->AddAction(gantryDevice, "MoveToAbsFromContext", {
        {"key_prefix", "corner2"},
        {"keep_z", "true"}  // Maintain current Z height
      });
    }
    sequence->EndIf();

    // Manual adjust for corner 2
    sequence->AddAction("system", "ManualAdjust", {
      {"message", "Position gantry over CORNER 2.\n\n"
                  "Adjust X/Y to align with the second corner reference point.\n"
                  "Click OK when positioned."},
      {"device_name", gantryDevice},
      {"step_size_um", "100"},
      {"enable_x", "true"},
      {"enable_y", "true"},
      {"enable_z", "false"}
    });

    // Capture corner 2 position (using generic motion command)
    sequence->AddAction(gantryDevice, "SavePositionToContext", {
      {"key_prefix", "corner2"}
    });

    // =========================================================================
    // STEP 4: CALCULATE YAW ANGLE AND SAVE DATA
    // =========================================================================

    sequence->AddAction("calibration", "CalculateAndSaveYawCorrection", {
      {"uid", uid},
      {"gantry_device", gantryDevice}
    });

    // =========================================================================
    // STEP 5: APPLY YAW CORRECTION TO TARGET DEVICE
    // =========================================================================

    sequence->AddAction("calibration", "ApplyYawCorrection", {
      {"uid", uid},
      {"target_device", targetDevice},
      {"x_direction", xDirection}
    });

    // =========================================================================
    // STEP 6: SUCCESS NOTIFICATION (with formatted values from context)
    // =========================================================================

    sequence->AddAction("calibration", "ShowYawCorrectionResult", {
      {"uid", uid},
      {"target_device", targetDevice}
    });

    // =========================================================================
    // STEP 7: SAVE U TO NAMED POSITION (optional, based on checkbox)
    // =========================================================================

    if (applyToPosition && !positionName.empty()) {
      sequence->AddAction("calibration", "SaveYawCorrectionToPosition", {
        {"position_device", positionDevice},
        {"position_name", positionName}
      });

      std::cout << "[YawCorrectionPlugin] Will save U correction to position: "
        << positionDevice << "/" << positionName << std::endl;
    }

    std::cout << "[YawCorrectionPlugin] Built sequence for: "
      << step->userNickname << " (Gantry: " << gantryDevice
      << ", Target: " << targetDevice << ", X: " << xDirection << ")" << std::endl;
  }
};

//=============================================================================
// PLUGIN REGISTRATION (Static initialization)
//=============================================================================

namespace {
  // Self-register at startup
  struct YawCorrectionProcessRegistrar {
    YawCorrectionProcessRegistrar() {
      auto& registry = ProcessPluginRegistry::Instance();
      registry.RegisterPlugin(std::make_unique<YawCorrectionPlugin>());
      std::cout << "[YawCorrectionProcesses] Registered Yaw Correction plugin" << std::endl;
    }
  };

  // Static instance triggers registration at startup
  static YawCorrectionProcessRegistrar s_registrar;
}
