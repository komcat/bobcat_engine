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
    std::string pneumaticSlide = params.at("pneumatic_slide");
    std::string settleTime = params.at("settle_time_ms");
    std::string zRetractHeight = params.at("z_retract_height");

    // Get the process instance UID for unique navigation files
    std::string uid = step->uniqueId;

    // =========================================================================
    // STEP 1: START
    // =========================================================================

    // =========================================================================
    // STEP 2: POSITION CAMERA OVER FLAT AREA & STORE POSITION
    // =========================================================================

    // Load existing navigation data (if exists for this calibration instance)
    sequence->AddAction("calibration", "LoadExistingCalibration", {
      {"uid", uid}
    });

    // Check if prior position exists
    sequence->BeginIf("navigation_exists", ConditionalBlock::ComparisonType::IS_TRUE);
    {
      // Ask user if they want to use previous position
      sequence->AddAction("system", "UserPrompt", {
        {"title", "Use Previous Camera Position?"},
        {"message", "Previous camera position found:\n"
                    "  X: context:camera_x mm\n"
                    "  Y: context:camera_y mm\n"
                    "  Z: context:camera_z mm\n\n"
                    "OK: Auto-navigate to saved position\n"
                    "Cancel: Position manually"},
        {"blocking", "true"},
        {"allow_cancel", "true"}
      });

      // User chose to use previous position
      sequence->BeginIf("user_confirmed", ConditionalBlock::ComparisonType::IS_TRUE);
      {
        // Auto-navigate to saved camera position
        sequence->AddAction(device, "MoveToAbs", {
          {"x", "context:camera_x"},
          {"y", "context:camera_y"},
          {"z", "context:camera_z"},
          {"blocking", "true"}
        });
        sequence->AddAction(device, "WaitForMotionComplete", {});
      }
      sequence->EndIf();
    }
    sequence->EndIf();

    // Manual adjust to position camera (either from scratch or to confirm auto-nav)
    sequence->AddAction("system", "ManualAdjust", {
      {"message", "Position camera over flat calibration surface.\n\n"
                  "Adjust as needed, then click OK."},
      {"device_name", device},
      {"step_size_um", "100"},
      {"enable_x", "true"},
      {"enable_y", "true"},
      {"enable_z", "true"}
    });

    // Capture camera position (cam'XYZ)
    sequence->AddAction("calibration", "CapturePosition", {
      {"device", device},
      {"key_prefix", "camera"}
    });

    // Save navigation data for future use
    sequence->AddAction("calibration", "SaveNavigationData", {
      {"uid", uid},
      {"device", device}
    });

    // =========================================================================
    // STEP 3: LOAD FACTORY OFFSET & MOVE TO NEEDLE POSITION
    // =========================================================================

    // Load offset from .local.json (if exists) or fall back to base .json
    sequence->AddAction("calibration", "LoadCameraToNeedleOffset", {});

    // Check if offset file exists
    sequence->BeginIf("system_offset_exists", ConditionalBlock::ComparisonType::IS_FALSE);
    {
      // Offset file missing - abort
      sequence->AddAction("system", "UserPrompt", {
        {"title", "❌ Needle XY Offset Missing"},
        {"message", "Needle XY offset file not found!\n\n"
                    "Checked: config/SYSTEM/needlexyoffset.local.json\n"
                    "         config/SYSTEM/needlexyoffset.json\n\n"
                    "Run 'Calibrate Camera-to-Needle Offset' first.\n\n"
                    "Calibration aborted."},
        {"blocking", "true"}
      });
      return;  // Stop sequence
    }
    sequence->EndIf();

    // Use loaded factory offset values
    sequence->AddAction("context", "SetNumeric", {
      {"key", "offset_x"},
      {"value", "context:system_offset_x"}
    });
    sequence->AddAction("context", "SetNumeric", {
      {"key", "offset_y"},
      {"value", "context:system_offset_y"}
    });

    // Calculate needle XY position: needle'XY = cam'XY + offset
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

    // Move to needle XY position, keep Z at cam'Z (safe height)
    sequence->AddAction(device, "MoveToAbs", {
      {"x", "context:needle_target_x"},
      {"y", "context:needle_target_y"},
      {"z", "context:camera_z"},
      {"blocking", "true"}
    });
    sequence->AddAction(device, "WaitForMotionComplete", {});

    // =========================================================================
    // STEP 4: AUTOMATICALLY EXTEND DISPENSER HEAD
    // =========================================================================

    sequence->AddAction(pneumaticSlide, "Extend", {
      {"debounce_delay_ms", "300"},
      {"timeout_ms", "10000"}
    });

    // Settling time after extension
    sequence->AddAction("system", "Wait", {
      {"milliseconds", settleTime}
    });

    // =========================================================================
    // STEP 5: USER MANUALLY MOVE TO TOUCH DOWN HEIGHT
    // =========================================================================

    // Manual Z adjustment to touch surface
    sequence->AddAction("system", "ManualAdjust", {
      {"message", "Adjust Z down until needle touches calibration surface.\n\n"
                  "Use fine steps for precision.\n\n"
                  "Click OK when needle is touching."},
      {"device_name", device},
      {"step_size_um", "10"},
      {"enable_x", "false"},
      {"enable_y", "false"},
      {"enable_z", "true"}
    });

    // Capture final needle position
    sequence->AddAction("calibration", "CapturePosition", {
      {"device", device},
      {"key_prefix", "needle"}
    });

    // =========================================================================
    // STEP 6: RETRACT DISPENSER HEAD, THEN MOVE TO CAM'XY
    // =========================================================================

    // Retract dispenser head
    sequence->AddAction(pneumaticSlide, "Retract", {
      {"debounce_delay_ms", "300"},
      {"timeout_ms", "10000"}
    });

    // Move back to cam'XY (stay at current Z - needle touchdown height)
    sequence->AddAction(device, "MoveToAbs", {
      {"x", "context:camera_x"},
      {"y", "context:camera_y"},
      {"blocking", "true"}
    });
    sequence->AddAction(device, "WaitForMotionComplete", {});

    // =========================================================================
    // STEP 7: SAVE CALIBRATION DATA
    // =========================================================================

    // Save needle Z calibration to /config/SYSTEM
    sequence->AddAction("calibration", "SaveNeedleZCalibration", {
      {"device", device},
      {"pneumatic_slide", pneumaticSlide}
    });

    // Success notification
    sequence->AddAction("system", "UserPrompt", {
      {"title", "✅ Needle Z Calibration Complete"},
      {"message", "Needle Z height calibrated and saved.\n\n"
                  "Needle Z: context:needle_z mm\n\n"
                  "File: config/SYSTEM/needle_z_calibration.json"},
      {"blocking", "false"}
    });

    std::cout << "[CalibrateNeedleZPlugin] Built simplified sequence (7 steps) for: "
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
