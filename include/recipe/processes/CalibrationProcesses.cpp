// CalibrationProcesses.cpp
// Calibration process plugins: Calibrate Needle Z

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
// CALIBRATE NEEDLE Z PLUGIN
//=============================================================================

class CalibrateNeedleZPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Calibrate Needle Z";
  }

  std::string GetCategory() const override {
    return "Calibration";
  }

  std::string GetDescription() const override {
    return "Calibrate needle Z height against a flat reference surface with camera-to-needle offset";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "gantry-main"},
      {"camera_offset_x", "50.0"},
      {"camera_offset_y", "10.0"},
      {"camera_offset_z", "0.0"},
      {"pneumatic_slide", "Dispenser_Head"},
      {"settle_time_ms", "500"},
      {"z_retract_height", "10.0"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    // Device selector - hardcoded to gantry-main
    widget->addDeviceSelector("device", "Motion Device",
                              params.at("device"),
                              static_cast<int>(DeviceComboBox::DeviceType::Motion));

    // Camera to Needle Offsets
    widget->addParameterField("camera_offset_x", "Camera Offset X (mm)",
                             params.at("camera_offset_x"));
    widget->addParameterField("camera_offset_y", "Camera Offset Y (mm)",
                             params.at("camera_offset_y"));
    widget->addParameterField("camera_offset_z", "Camera Offset Z (mm)",
                             params.at("camera_offset_z"));

    // Pneumatic slide selector
    widget->addSlideSelector("pneumatic_slide", "Dispenser Slide",
                            params.at("pneumatic_slide"));

    // Timing parameters
    widget->addParameterField("settle_time_ms", "Settle Time (ms)",
                             params.at("settle_time_ms"));
    widget->addParameterField("z_retract_height", "Z Retract Height (mm)",
                             params.at("z_retract_height"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string device = params.at("device");
    std::string offsetX = params.at("camera_offset_x");
    std::string offsetY = params.at("camera_offset_y");
    std::string offsetZ = params.at("camera_offset_z");
    std::string pneumaticSlide = params.at("pneumatic_slide");
    std::string settleTime = params.at("settle_time_ms");
    std::string zRetractHeight = params.at("z_retract_height");

    // Step 0: Load existing calibration (if exists)
    sequence->AddAction("calibration", "LoadExistingCalibration", {});

    // Step 1: Check if calibration exists and navigate accordingly
    sequence->BeginIf("calibration_exists", ConditionalBlock::ComparisonType::IS_TRUE);
    {
      // THEN: Calibration exists - auto-navigate to saved camera position
      sequence->AddAction(device, "MoveToAbs", {
        {"x", "context:camera_x"},
        {"y", "context:camera_y"},
        {"z", "context:camera_z"},
        {"blocking", "true"}
      });
      sequence->AddAction(device, "WaitForMotionComplete", {});
    }
    sequence->Else();
    {
      // ELSE: First calibration - ask user to position manually
      sequence->AddAction("system", "UserPrompt", {
        {"title", "Position Camera Over Calibration Surface"},
        {"message", "Manually move " + device + " so the camera sees the flat calibration surface. Click OK when positioned."},
        {"blocking", "true"}
      });
    }
    sequence->EndIf();

    // Step 2: Capture camera position
    sequence->AddAction("calibration", "CapturePosition", {
      {"device", device},
      {"key_prefix", "camera"}
    });

    // Step 3: Store offsets in context
    sequence->AddAction("context", "SetNumeric", {
      {"key", "offset_x"},
      {"value", offsetX}
    });
    sequence->AddAction("context", "SetNumeric", {
      {"key", "offset_y"},
      {"value", offsetY}
    });
    sequence->AddAction("context", "SetNumeric", {
      {"key", "offset_z"},
      {"value", offsetZ}
    });

    // Step 4: Calculate needle position
    sequence->AddAction("context", "Calculate", {
      {"result_key", "needle_target_x"},
      {"expression", "camera_x + offset_x"}
    });
    sequence->AddAction("context", "Calculate", {
      {"result_key", "needle_target_y"},
      {"expression", "camera_y + offset_y"}
    });
    sequence->AddAction("context", "Calculate", {
      {"result_key", "needle_target_z"},
      {"expression", "camera_z + offset_z"}
    });

    // Step 5: Move to needle position
    sequence->AddAction(device, "MoveToAbs", {
      {"x", "context:needle_target_x"},
      {"y", "context:needle_target_y"},
      {"z", "context:needle_target_z"},
      {"blocking", "true"}
    });
    sequence->AddAction(device, "WaitForMotionComplete", {});

    // Step 6: Extend pneumatic dispenser head
    sequence->AddAction(pneumaticSlide, "Extend", {
      {"debounce_delay_ms", "300"},
      {"timeout_ms", "10000"}
    });

    // Step 7: Settling time
    sequence->AddAction("system", "Wait", {
      {"milliseconds", settleTime}
    });

    // Step 8: Ask user to move Z down close to surface
    sequence->AddAction("system", "UserPrompt", {
      {"title", "Move Z Down Close to Surface"},
      {"message", "Manually move the Z axis down until the needle is just close to the flat surface (do not touch yet). Click OK when ready for fine adjustment."},
      {"blocking", "true"}
    });

    // Step 9: Manual adjust for fine tuning
    sequence->AddAction("system", "ManualAdjust", {
      {"message", "Fine adjust Z to touch the calibration surface"},
      {"device_name", device},
      {"step_size_um", "5"},
      {"enable_x", "false"},
      {"enable_y", "false"},
      {"enable_z", "true"}
    });

    // Step 10: Capture final needle position
    sequence->AddAction("calibration", "CapturePosition", {
      {"device", device},
      {"key_prefix", "needle"}
    });

    // Step 11: Save calibration data
    sequence->AddAction("calibration", "SaveNeedleZCalibration", {
      {"device", device},
      {"pneumatic_slide", pneumaticSlide}
    });

    // Step 12: Retract dispenser head
    sequence->AddAction(pneumaticSlide, "Retract", {
      {"debounce_delay_ms", "300"},
      {"timeout_ms", "10000"}
    });

    // Step 13: Move Z up to safe height
    sequence->AddAction(device, "MoveRelative", {
      {"axis", "Z"},
      {"distance", zRetractHeight},
      {"blocking", "true"}
    });
    sequence->AddAction(device, "WaitForMotionComplete", {});

    // Step 14: Success notification with delta
    sequence->AddAction("system", "Notify", {
      {"title", "Calibration Complete"},
      {"message", "context:calibration_result"}
    });

    std::cout << "[CalibrateNeedleZPlugin] Built sequence for: "
      << step->userNickname << " (Device: " << device << ", Slide: " << pneumaticSlide << ")" << std::endl;
  }
};

//=============================================================================
// PLUGIN REGISTRATION (Static initialization)
//=============================================================================

namespace {
  // Self-register at startup
  struct CalibrationProcessRegistrar {
    CalibrationProcessRegistrar() {
      auto& registry = ProcessPluginRegistry::Instance();
      registry.RegisterPlugin(std::make_unique<CalibrateNeedleZPlugin>());
      std::cout << "[CalibrationProcesses] Registered Calibrate Needle Z plugin" << std::endl;
    }
  };

  // Static instance triggers registration at startup
  static CalibrationProcessRegistrar s_registrar;
}
