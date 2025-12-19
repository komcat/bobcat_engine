// YawCorrectionToTemplateProcesses.cpp
// Yaw Correction to Template calibration process plugin
// Measures yaw angle of target object (corners 3,4) and template object (corners 1,2)
// Calculates correction needed to align target with template

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
// YAW CORRECTION TO TEMPLATE PLUGIN
//=============================================================================

class YawCorrectionToTemplatePlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Yaw Correction to Template";
  }

  std::string GetCategory() const override {
    return "Calibration";
  }

  std::string GetDescription() const override {
    return "Align target object yaw angle to match a template/reference object by measuring both";
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
    // STEP 1: LOAD EXISTING DATA (if exists)
    // =========================================================================

    sequence->AddAction("calibration", "LoadYawToTemplateData", {
      {"uid", uid}
    });

    // =========================================================================
    // STEP 2: TEMPLATE CORNER 1 (Fixed reference - first corner)
    // =========================================================================

    sequence->BeginIf("yaw_template_data_exists", ConditionalBlock::ComparisonType::IS_TRUE);
    {
      sequence->AddAction(gantryDevice, "MoveToAbsFromContext", {
        {"key_prefix", "template_corner1"},
        {"keep_z", "true"}
      });
    }
    sequence->EndIf();

    sequence->AddAction("system", "ManualAdjust", {
      {"message", "Position gantry over TEMPLATE CORNER 1 (Fixed/Reference object).\n\n"
                  "This is the FIRST corner of the TEMPLATE object.\n"
                  "Click OK when positioned."},
      {"device_name", gantryDevice},
      {"step_size_um", "100"},
      {"enable_x", "true"},
      {"enable_y", "true"},
      {"enable_z", "false"}
    });

    sequence->AddAction(gantryDevice, "SavePositionToContext", {
      {"key_prefix", "template_corner1"}
    });

    // =========================================================================
    // STEP 3: TEMPLATE CORNER 2 (Fixed reference - second corner)
    // =========================================================================

    sequence->BeginIf("yaw_template_data_exists", ConditionalBlock::ComparisonType::IS_TRUE);
    {
      sequence->AddAction(gantryDevice, "MoveToAbsFromContext", {
        {"key_prefix", "template_corner2"},
        {"keep_z", "true"}
      });
    }
    sequence->EndIf();

    sequence->AddAction("system", "ManualAdjust", {
      {"message", "Position gantry over TEMPLATE CORNER 2 (Fixed/Reference object).\n\n"
                  "This is the SECOND corner of the TEMPLATE object.\n"
                  "Click OK when positioned."},
      {"device_name", gantryDevice},
      {"step_size_um", "100"},
      {"enable_x", "true"},
      {"enable_y", "true"},
      {"enable_z", "false"}
    });

    sequence->AddAction(gantryDevice, "SavePositionToContext", {
      {"key_prefix", "template_corner2"}
    });

    // =========================================================================
    // STEP 4: TARGET CORNER 3 (Target object - first corner)
    // =========================================================================

    sequence->BeginIf("yaw_template_data_exists", ConditionalBlock::ComparisonType::IS_TRUE);
    {
      sequence->AddAction(gantryDevice, "MoveToAbsFromContext", {
        {"key_prefix", "target_corner3"},
        {"keep_z", "true"}
      });
    }
    sequence->EndIf();

    sequence->AddAction("system", "ManualAdjust", {
      {"message", "Position gantry over TARGET CORNER 3 (Object to be corrected).\n\n"
                  "This is the FIRST corner of the TARGET object.\n"
                  "Click OK when positioned."},
      {"device_name", gantryDevice},
      {"step_size_um", "100"},
      {"enable_x", "true"},
      {"enable_y", "true"},
      {"enable_z", "false"}
    });

    sequence->AddAction(gantryDevice, "SavePositionToContext", {
      {"key_prefix", "target_corner3"}
    });

    // =========================================================================
    // STEP 5: TARGET CORNER 4 (Target object - second corner)
    // =========================================================================

    sequence->BeginIf("yaw_template_data_exists", ConditionalBlock::ComparisonType::IS_TRUE);
    {
      sequence->AddAction(gantryDevice, "MoveToAbsFromContext", {
        {"key_prefix", "target_corner4"},
        {"keep_z", "true"}
      });
    }
    sequence->EndIf();

    sequence->AddAction("system", "ManualAdjust", {
      {"message", "Position gantry over TARGET CORNER 4 (Object to be corrected).\n\n"
                  "This is the SECOND corner of the TARGET object.\n"
                  "Click OK when positioned."},
      {"device_name", gantryDevice},
      {"step_size_um", "100"},
      {"enable_x", "true"},
      {"enable_y", "true"},
      {"enable_z", "false"}
    });

    sequence->AddAction(gantryDevice, "SavePositionToContext", {
      {"key_prefix", "target_corner4"}
    });

    // =========================================================================
    // STEP 6: CALCULATE YAW ANGLES AND CORRECTION
    // =========================================================================

    sequence->AddAction("calibration", "CalculateYawToTemplateCorrection", {
      {"uid", uid},
      {"gantry_device", gantryDevice}
    });

    // =========================================================================
    // STEP 7: APPLY YAW CORRECTION TO TARGET DEVICE
    // =========================================================================

    sequence->AddAction("calibration", "ApplyYawToTemplateCorrection", {
      {"uid", uid},
      {"target_device", targetDevice},
      {"x_direction", xDirection}
    });

    // =========================================================================
    // STEP 8: SUCCESS NOTIFICATION
    // =========================================================================

    sequence->AddAction("calibration", "ShowYawToTemplateResult", {
      {"uid", uid},
      {"target_device", targetDevice}
    });

    // =========================================================================
    // STEP 9: SAVE U TO NAMED POSITION (optional)
    // =========================================================================

    if (applyToPosition && !positionName.empty()) {
      sequence->AddAction("calibration", "SaveYawCorrectionToPosition", {
        {"position_device", positionDevice},
        {"position_name", positionName}
      });

      std::cout << "[YawCorrectionToTemplatePlugin] Will save U correction to position: "
        << positionDevice << "/" << positionName << std::endl;
    }

    std::cout << "[YawCorrectionToTemplatePlugin] Built sequence for: "
      << step->userNickname << " (Gantry: " << gantryDevice
      << ", Target: " << targetDevice << ", X: " << xDirection << ")" << std::endl;
  }
};

//=============================================================================
// PLUGIN REGISTRATION (Static initialization)
//=============================================================================

namespace {
  struct YawCorrectionToTemplateProcessRegistrar {
    YawCorrectionToTemplateProcessRegistrar() {
      auto& registry = ProcessPluginRegistry::Instance();
      registry.RegisterPlugin(std::make_unique<YawCorrectionToTemplatePlugin>());
      std::cout << "[YawCorrectionToTemplateProcesses] Registered Yaw Correction to Template plugin" << std::endl;
    }
  };

  static YawCorrectionToTemplateProcessRegistrar s_registrar;
}
