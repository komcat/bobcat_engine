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

    // Get the process instance UID for unique navigation/calibration files
    std::string uid = step->uniqueId;

    // Step 0a: Load system-level camera-to-needle offset (if exists)
    sequence->AddAction("calibration", "LoadCameraToNeedleOffset", {});

    // Step 0b: Use system offset as default, or use configured offset
    sequence->BeginIf("system_offset_exists", ConditionalBlock::ComparisonType::IS_TRUE);
    {
      // Use system-calibrated offset values
      sequence->AddAction("context", "SetNumeric", {
        {"key", "offset_x"},
        {"value", "context:system_offset_x"}
      });
      sequence->AddAction("context", "SetNumeric", {
        {"key", "offset_y"},
        {"value", "context:system_offset_y"}
      });
      sequence->AddAction("context", "SetNumeric", {
        {"key", "offset_z"},
        {"value", "context:system_offset_z"}
      });
    }
    sequence->Else();
    {
      // Use configured offset values from parameters
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
    }
    sequence->EndIf();

    // Step 1: Load existing navigation data (if exists)
    sequence->AddAction("calibration", "LoadExistingCalibration", {
      {"uid", uid}
    });

    // Step 2: Check if navigation data exists and navigate accordingly
    sequence->BeginIf("navigation_exists", ConditionalBlock::ComparisonType::IS_TRUE);
    {
      // THEN: Navigation exists - auto-navigate to saved camera position
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
      // ELSE: First time - use ManualAdjust to position
      sequence->AddAction("system", "ManualAdjust", {
        {"message", "Position camera over flat calibration surface.\n\n"
                    "Manually jog " + device + " to position.\n\n"
                    "Click OK when positioned."},
        {"device_name", device},
        {"step_size_um", "100"},
        {"enable_x", "true"},
        {"enable_y", "true"},
        {"enable_z", "true"}
      });
    }
    sequence->EndIf();

    // Step 3: Capture camera position
    sequence->AddAction("calibration", "CapturePosition", {
      {"device", device},
      {"key_prefix", "camera"}
    });

    // Step 4: Save navigation data for future auto-navigation
    sequence->AddAction("calibration", "SaveNavigationData", {
      {"uid", uid},
      {"device", device}
    });

    // Step 5: Calculate needle XY position (where camera sees needle)
    sequence->AddAction("context", "Calculate", {
      {"resultKey", "needle_target_x"},
      {"key1", "camera_x"},
      {"key2", "offset_x"},
      {"operation", "+"}
    });
    sequence->AddAction("context", "Calculate", {
      {"resultKey", "needle_target_y"},
      {"key1", "camera_y"},
      {"key2", "offset_y"},
      {"operation", "+"}
    });

    // Step 6: Move to needle XY position (keep Z at camera level for safety)
    sequence->AddAction(device, "MoveToAbs", {
      {"x", "context:needle_target_x"},
      {"y", "context:needle_target_y"},
      {"z", "context:camera_z"},
      {"blocking", "true"}
    });
    sequence->AddAction(device, "WaitForMotionComplete", {});

    // Step 7: Safety check - Allow user to verify position before extending slide
    sequence->AddAction("system", "ManualAdjust", {
      {"message", "⚠️ SAFETY CHECK: Verify needle position is safe before extending dispenser head.\n\n"
                  "Adjust XYZ if needed, then click OK to extend " + pneumaticSlide + "."},
      {"device_name", device},
      {"step_size_um", "100"},
      {"enable_x", "true"},
      {"enable_y", "true"},
      {"enable_z", "true"}
    });

    // Step 8: Extend pneumatic dispenser head
    sequence->AddAction(pneumaticSlide, "Extend", {
      {"debounce_delay_ms", "300"},
      {"timeout_ms", "10000"}
    });

    // Step 9: Settling time
    sequence->AddAction("system", "Wait", {
      {"milliseconds", settleTime}
    });

    // Step 10: Coarse Z positioning
    sequence->AddAction("system", "ManualAdjust", {
      {"message", "Move Z down until needle is close to surface (do not touch yet).\n\n"
                  "Click OK when ready for fine adjustment."},
      {"device_name", device},
      {"step_size_um", "100"},
      {"enable_x", "false"},
      {"enable_y", "false"},
      {"enable_z", "true"}
    });

    // Step 11: Manual adjust for fine tuning
    sequence->AddAction("system", "ManualAdjust", {
      {"message", "Fine adjust Z to touch the calibration surface"},
      {"device_name", device},
      {"step_size_um", "5"},
      {"enable_x", "false"},
      {"enable_y", "false"},
      {"enable_z", "true"}
    });

    // Step 12: Capture final needle position
    sequence->AddAction("calibration", "CapturePosition", {
      {"device", device},
      {"key_prefix", "needle"}
    });

    // Step 13: Save calibration data to global file
    sequence->AddAction("calibration", "SaveNeedleZCalibration", {
      {"device", device},
      {"pneumatic_slide", pneumaticSlide}
    });

    // Step 14: Retract dispenser head
    sequence->AddAction(pneumaticSlide, "Retract", {
      {"debounce_delay_ms", "300"},
      {"timeout_ms", "10000"}
    });

    // Step 15: Move Z up to safe height
    sequence->AddAction(device, "MoveRelative", {
      {"axis", "Z"},
      {"distance", zRetractHeight},
      {"blocking", "true"}
    });
    sequence->AddAction(device, "WaitForMotionComplete", {});

    // Step 16: Success notification with delta
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
