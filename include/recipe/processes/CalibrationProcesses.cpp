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

    // Step 0a: Load system-level camera-to-needle offset (REQUIRED)
    sequence->AddAction("calibration", "LoadCameraToNeedleOffset", {});

    // Step 0b: Check if XY offset exists - this is a prerequisite
    sequence->BeginIf("system_offset_exists", ConditionalBlock::ComparisonType::IS_FALSE);
    {
      // XY offset file does not exist - cannot proceed
      sequence->AddAction("system", "UserPrompt", {
        {"title", "❌ Prerequisite Missing"},
        {"message", "Camera-to-Needle XY offset file not found!\n\n"
                    "You must run 'Calibrate Camera-to-Needle Offset' first\n"
                    "to establish the XY relationship between camera and needle.\n\n"
                    "This process only updates Z height and requires\n"
                    "the XY offset data to function correctly.\n\n"
                    "Calibration aborted."},
        {"blocking", "true"}
      });
      // Stop sequence - cannot continue without XY offset
      return;
    }
    sequence->EndIf();

    // Step 0c: Use system offset (confirmed to exist)
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
      // Ask user for permission to auto-navigate
      sequence->AddAction("system", "UserPrompt", {
        {"title", "Auto-Navigate to Saved Position?"},
        {"message", "Saved camera position found.\n\n"
                    "Position: (context:camera_x, context:camera_y, context:camera_z) mm\n\n"
                    "Click OK to auto-navigate, or Cancel to position manually."},
        {"blocking", "true"},
        {"allow_cancel", "true"}
      });

      // Check if user confirmed auto-navigation
      sequence->BeginIf("user_confirmed", ConditionalBlock::ComparisonType::IS_TRUE);
      {
        // User clicked OK - auto-navigate to saved camera position
        sequence->AddAction(device, "MoveToAbs", {
          {"x", "context:camera_x"},
          {"y", "context:camera_y"},
          {"z", "context:camera_z"},
          {"blocking", "true"}
        });
        sequence->AddAction(device, "WaitForMotionComplete", {});

        // Manual adjust to confirm/fine-tune auto-navigated position
        sequence->AddAction("system", "ManualAdjust", {
          {"message", "Auto-navigated to saved position.\n\n"
                      "Confirm position is correct or adjust as needed.\n\n"
                      "Click OK when confirmed."},
          {"device_name", device},
          {"step_size_um", "100"},
          {"enable_x", "true"},
          {"enable_y", "true"},
          {"enable_z", "true"}
        });
      }
      sequence->Else();
      {
        // User clicked Cancel - manual positioning
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

    // Step 4: Copy camera values for SaveNavigationData
    sequence->AddAction("context", "SetNumeric", {
      {"key", "camera_x"},
      {"value", "context:camera_x"}
    });
    sequence->AddAction("context", "SetNumeric", {
      {"key", "camera_y"},
      {"value", "context:camera_y"}
    });
    sequence->AddAction("context", "SetNumeric", {
      {"key", "camera_z"},
      {"value", "context:camera_z"}
    });

    // Step 5: Save navigation data for future auto-navigation
    sequence->AddAction("calibration", "SaveNavigationData", {
      {"uid", uid},
      {"device", device}
    });

    // Step 6: Calculate needle XY position (where camera sees needle)
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

    // Step 7: Move to needle XY position (keep Z at camera level for safety)
    sequence->AddAction(device, "MoveToAbs", {
      {"x", "context:needle_target_x"},
      {"y", "context:needle_target_y"},
      {"z", "context:camera_z"},
      {"blocking", "true"}
    });
    sequence->AddAction(device, "WaitForMotionComplete", {});

    // Step 8: Safety check - Allow user to verify position before extending slide
    sequence->AddAction("system", "ManualAdjust", {
      {"message", "⚠️ SAFETY CHECK: Verify needle position is safe before extending dispenser head.\n\n"
                  "Adjust XYZ if needed, then click OK to extend " + pneumaticSlide + "."},
      {"device_name", device},
      {"step_size_um", "100"},
      {"enable_x", "true"},
      {"enable_y", "true"},
      {"enable_z", "true"}
    });

    // Step 9: Extend pneumatic dispenser head
    sequence->AddAction(pneumaticSlide, "Extend", {
      {"debounce_delay_ms", "300"},
      {"timeout_ms", "10000"}
    });

    // Step 10: Settling time
    sequence->AddAction("system", "Wait", {
      {"milliseconds", settleTime}
    });

    // Step 11: Load previous needle Z calibration (if exists)
    sequence->AddAction("calibration", "LoadNeedleZCalibration", {});

    // Step 12: If needle Z calibration exists, offer to use it
    sequence->BeginIf("needle_z_calibration_exists", ConditionalBlock::ComparisonType::IS_TRUE);
    {
      // Ask user if they want to move to previous needle Z height
      sequence->AddAction("system", "UserPrompt", {
        {"title", "Use Previous Needle Z Height?"},
        {"message", "Previous needle Z calibration found: context:previous_needle_z mm\n\n"
                    "OK: Move to Z - 1mm (close positioning)\n"
                    "Cancel: Move to Z - 10mm (safe positioning)\n\n"
                    "Both options move closer to target height."},
        {"blocking", "true"},
        {"allow_cancel", "true"}
      });

      // Capture current XY position (we'll only change Z)
      sequence->AddAction("calibration", "CapturePosition", {
        {"device", device},
        {"key_prefix", "current"}
      });

      // Calculate target Z based on user choice
      sequence->BeginIf("user_confirmed", ConditionalBlock::ComparisonType::IS_TRUE);
      {
        // OK: Move to previous_z - 1mm (close positioning)
        sequence->AddAction("context", "Calculate", {
          {"resultKey", "target_needle_z"},
          {"key1", "previous_needle_z"},
          {"key2", "1.0"},
          {"operation", "-"}
        });
      }
      sequence->Else();
      {
        // Cancel: Move to previous_z - 10mm (safe positioning)
        sequence->AddAction("context", "Calculate", {
          {"resultKey", "target_needle_z"},
          {"key1", "previous_needle_z"},
          {"key2", "10.0"},
          {"operation", "-"}
        });
      }
      sequence->EndIf();

      // Move to calculated Z position
      sequence->AddAction(device, "MoveToAbs", {
        {"x", "context:current_x"},
        {"y", "context:current_y"},
        {"z", "context:target_needle_z"},
        {"blocking", "true"}
      });
      sequence->AddAction(device, "WaitForMotionComplete", {});
    }
    sequence->EndIf();

    // Step 13: Coarse Z positioning
    sequence->AddAction("system", "ManualAdjust", {
      {"message", "Move Z down until needle is close to surface (do not touch yet).\n\n"
                  "Click OK when ready for fine adjustment."},
      {"device_name", device},
      {"step_size_um", "100"},
      {"enable_x", "false"},
      {"enable_y", "false"},
      {"enable_z", "true"}
    });

    // Step 14: Manual adjust for fine tuning
    sequence->AddAction("system", "ManualAdjust", {
      {"message", "Fine adjust Z to touch the calibration surface"},
      {"device_name", device},
      {"step_size_um", "10"},
      {"enable_x", "false"},
      {"enable_y", "false"},
      {"enable_z", "true"}
    });

    // Step 15: Capture final needle position
    sequence->AddAction("calibration", "CapturePosition", {
      {"device", device},
      {"key_prefix", "needle"}
    });

    // Step 16: Save old needle Z for comparison (before saving new value)
    sequence->BeginIf("needle_z_calibration_exists", ConditionalBlock::ComparisonType::IS_TRUE);
    {
      sequence->AddAction("context", "SetNumeric", {
        {"key", "old_needle_z"},
        {"value", "context:previous_needle_z"}
      });
    }
    sequence->EndIf();

    // Step 17: Save calibration data to global file
    sequence->AddAction("calibration", "SaveNeedleZCalibration", {
      {"device", device},
      {"pneumatic_slide", pneumaticSlide}
    });

    // Step 18: Calculate delta from previous calibration (if exists)
    sequence->BeginIf("needle_z_calibration_exists", ConditionalBlock::ComparisonType::IS_TRUE);
    {
      sequence->AddAction("context", "Calculate", {
        {"resultKey", "delta_z"},
        {"key1", "needle_z"},
        {"key2", "old_needle_z"},
        {"operation", "-"}
      });
    }
    sequence->EndIf();

    // Step 19: Retract dispenser head
    sequence->AddAction(pneumaticSlide, "Retract", {
      {"debounce_delay_ms", "300"},
      {"timeout_ms", "10000"}
    });

    // Step 20: Move Z up to safe height
    sequence->AddAction(device, "MoveRelative", {
      {"axis", "Z"},
      {"distance", zRetractHeight},
      {"blocking", "true"}
    });
    sequence->AddAction(device, "WaitForMotionComplete", {});

    // Step 21: Success notification with comparison
    sequence->BeginIf("needle_z_calibration_exists", ConditionalBlock::ComparisonType::IS_TRUE);
    {
      // Show comparison with previous calibration
      sequence->AddAction("system", "UserPrompt", {
        {"title", "✅ Needle Z Calibration Complete"},
        {"message", "Needle Z height has been calibrated and saved.\n\n"
                    "NEW Z HEIGHT:\n"
                    "  Z: context:needle_z mm\n\n"
                    "PREVIOUS Z HEIGHT:\n"
                    "  Z: context:old_needle_z mm\n\n"
                    "CHANGE (Δ):\n"
                    "  Z: context:delta_z mm\n\n"
                    "File: config/calibration/needle_z_calibration.json"},
        {"blocking", "false"}
      });
    }
    sequence->Else();
    {
      // First calibration - no comparison
      sequence->AddAction("system", "UserPrompt", {
        {"title", "✅ Needle Z Calibration Complete"},
        {"message", "Needle Z height has been calibrated and saved.\n\n"
                    "Z HEIGHT:\n"
                    "  Z: context:needle_z mm\n\n"
                    "(First calibration - no previous data to compare)\n\n"
                    "File: config/calibration/needle_z_calibration.json"},
        {"blocking", "false"}
      });
    }
    sequence->EndIf();

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
