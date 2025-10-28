// CalibrateCameraToNeedleOffset.cpp
// System-level calibration to determine camera-to-needle offset

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
// CALIBRATE CAMERA-TO-NEEDLE OFFSET PLUGIN
//=============================================================================

class CalibrateCameraToNeedleOffsetPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Calibrate Camera-to-Needle Offset";
  }

  std::string GetCategory() const override {
    return "Calibration";
  }

  std::string GetDescription() const override {
    return "System-level calibration: Determine XY offset from camera center to needle tip by imprinting and measuring";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "gantry-main"},
      {"pneumatic_slide", "Dispenser_Head"},
      {"settle_time_ms", "500"},
      {"z_retract_height", "10.0"},
      {"io_device", "EziIO-1"},
      {"dispense_pin", "0"},
      {"dispense_duration_ms", "150"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;
    auto defaults = GetDefaultParameters();

    // Helper to get parameter with fallback to default
    auto getParam = [&](const std::string& key) -> std::string {
      auto it = params.find(key);
      return (it != params.end()) ? it->second : defaults[key];
    };

    // Device selector
    widget->addDeviceSelector("device", "Motion Device",
                              getParam("device"),
                              static_cast<int>(DeviceComboBox::DeviceType::Motion));

    // Pneumatic slide selector
    widget->addSlideSelector("pneumatic_slide", "Dispenser Slide",
                            getParam("pneumatic_slide"));

    // Timing parameters
    widget->addParameterField("settle_time_ms", "Settle Time (ms)",
                             getParam("settle_time_ms"));

    // Z heights
    widget->addParameterField("z_retract_height", "Z Retract Height (mm)",
                             getParam("z_retract_height"));

    // Dispenser IO control
    widget->addDeviceSelector("io_device", "IO Device",
                              getParam("io_device"),
                              static_cast<int>(DeviceComboBox::DeviceType::IO));
    widget->addParameterField("dispense_pin", "Dispenser Output Pin",
                             getParam("dispense_pin"));
    widget->addParameterField("dispense_duration_ms", "Dispense Duration (ms)",
                             getParam("dispense_duration_ms"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;
    auto defaults = GetDefaultParameters();

    // Helper to get parameter with fallback to default
    auto getParam = [&](const std::string& key) -> std::string {
      auto it = params.find(key);
      return (it != params.end()) ? it->second : defaults[key];
    };

    std::string device = getParam("device");
    std::string pneumaticSlide = getParam("pneumatic_slide");
    std::string settleTime = getParam("settle_time_ms");
    std::string zRetractHeight = getParam("z_retract_height");
    std::string ioDevice = getParam("io_device");
    std::string dispensePin = getParam("dispense_pin");
    std::string dispenseDuration = getParam("dispense_duration_ms");

    // Get the process instance UID for unique navigation/calibration files
    std::string uid = step->uniqueId;

    // Step 1: Instructions (non-blocking notification)
    sequence->AddAction("system", "UserPrompt", {
      {"title", "Calibrate Camera-to-Needle XY Offset"},
      {"message", "Starting XY offset calibration.\n\n"
                  "Ensure flat surface with good contrast is ready."},
      {"blocking", "false"}
    });

    // Step 2: Load existing navigation data (if exists)
    sequence->AddAction("calibration", "LoadExistingCalibration", {
      {"uid", uid}
    });

    // Step 3: Check if navigation data exists and navigate accordingly
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
          {"message", "Position camera over flat surface suitable for imprinting.\n\n"
                      "Manually jog " + device + " to position.\n"
                      "Surface should have good contrast.\n\n"
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
        {"message", "Position camera over flat surface suitable for imprinting.\n\n"
                    "Manually jog " + device + " to position.\n"
                    "Surface should have good contrast.\n\n"
                    "Click OK when positioned."},
        {"device_name", device},
        {"step_size_um", "100"},
        {"enable_x", "true"},
        {"enable_y", "true"},
        {"enable_z", "true"}
      });
    }
    sequence->EndIf();

    // Step 4: Capture camera position (after manual adjustment/confirmation)
    sequence->AddAction("calibration", "CapturePosition", {
      {"device", device},
      {"key_prefix", "camera_initial"}
    });

    // Step 5: Copy camera_initial values to camera for SaveNavigationData
    sequence->AddAction("context", "SetNumeric", {
      {"key", "camera_x"},
      {"value", "context:camera_initial_x"}
    });
    sequence->AddAction("context", "SetNumeric", {
      {"key", "camera_y"},
      {"value", "context:camera_initial_y"}
    });
    sequence->AddAction("context", "SetNumeric", {
      {"key", "camera_z"},
      {"value", "context:camera_initial_z"}
    });

    // Step 6: Save navigation data for future auto-navigation
    sequence->AddAction("calibration", "SaveNavigationData", {
      {"uid", uid},
      {"device", device}
    });

    // Step 7: Load existing camera-to-needle offset (if exists)
    sequence->AddAction("calibration", "LoadCameraToNeedleOffset", {});

    // Step 7a: Save old offset values for comparison later
    sequence->BeginIf("system_offset_exists", ConditionalBlock::ComparisonType::IS_TRUE);
    {
      sequence->AddAction("context", "SetNumeric", {
        {"key", "old_offset_x"},
        {"value", "context:system_offset_x"}
      });
      sequence->AddAction("context", "SetNumeric", {
        {"key", "old_offset_y"},
        {"value", "context:system_offset_y"}
      });
    }
    sequence->EndIf();

    // Step 8: Auto-calculate and move to needle position using saved offset
    sequence->BeginIf("system_offset_exists", ConditionalBlock::ComparisonType::IS_TRUE);
    {
      // Calculate needle target position (camera + offset)
      sequence->AddAction("context", "Calculate", {
        {"resultKey", "needle_target_x"},
        {"key1", "camera_initial_x"},
        {"key2", "system_offset_x"},
        {"operation", "+"}
      });
      sequence->AddAction("context", "Calculate", {
        {"resultKey", "needle_target_y"},
        {"key1", "camera_initial_y"},
        {"key2", "system_offset_y"},
        {"operation", "+"}
      });
      // Keep Z at safe height (use captured camera position Z for safety)
      sequence->AddAction("context", "SetNumeric", {
        {"key", "needle_target_z"},
        {"value", "context:camera_initial_z"}
      });

      // Move to calculated needle XY position at captured camera Z (safe height)
      sequence->AddAction(device, "MoveToAbs", {
        {"x", "context:needle_target_x"},
        {"y", "context:needle_target_y"},
        {"z", "context:needle_target_z"},
        {"blocking", "true"}
      });
      sequence->AddAction(device, "WaitForMotionComplete", {});
    }
    sequence->EndIf();

    // Step 9: Confirm before extending pneumatic slide
    sequence->AddAction("system", "UserPrompt", {
      {"title", "⚠️ Confirm Pneumatic Extension"},
      {"message", "System has moved to calculated needle XY position at safe Z.\n\n"
                  "⚠️ SAFETY: Verify position is safe before extending " + pneumaticSlide + ".\n\n"
                  "Click OK to extend, or Cancel to abort."},
      {"blocking", "true"}
    });

    // Step 10: Extend pneumatic dispenser head
    sequence->AddAction(pneumaticSlide, "Extend", {
      {"debounce_delay_ms", "300"},
      {"timeout_ms", "10000"}
    });

    // Step 11: Settling time
    sequence->AddAction("system", "Wait", {
      {"milliseconds", settleTime}
    });

    // Step 12: Load previous needle Z calibration (if exists)
    sequence->AddAction("calibration", "LoadNeedleZCalibration", {});

    // Step 13: If needle Z calibration exists, offer to use it
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

    // Step 14: Fine-tune needle position for imprint (manual adjustment)
    sequence->AddAction("system", "ManualAdjust", {
      {"message", "Adjust needle position as needed.\n\n"
                  "Fine-tune XYZ to position needle for imprinting.\n\n"
                  "Click OK when positioned."},
      {"device_name", device},
      {"step_size_um", "10"},
      {"enable_x", "true"},
      {"enable_y", "true"},
      {"enable_z", "true"}
    });

    // Step 15: Capture needle position (before imprint)
    sequence->AddAction("calibration", "CapturePosition", {
      {"device", device},
      {"key_prefix", "needle"}
    });

    // Step 16: Automatic dispense to make imprint
    sequence->AddAction("system", "UserPrompt", {
      {"title", "Ready to Dispense"},
      {"message", "Needle positioned at calibration point.\n\n"
                  "System will automatically trigger dispenser to make imprint mark.\n\n"
                  "Click OK to dispense."},
      {"blocking", "true"}
    });

    // Turn ON dispenser output
    sequence->AddAction(ioDevice, "SetOutputPin", {
      {"pin", dispensePin},
      {"state", "true"}
    });

    // Wait for dispense duration
    sequence->AddAction("system", "Wait", {
      {"milliseconds", dispenseDuration}
    });

    // Turn OFF dispenser output
    sequence->AddAction(ioDevice, "SetOutputPin", {
      {"pin", dispensePin},
      {"state", "false"}
    });

    // Step 17: Retract Z to safe height
    sequence->AddAction(device, "MoveRelative", {
      {"axis", "Z"},
      {"distance", zRetractHeight},
      {"blocking", "true"}
    });
    sequence->AddAction(device, "WaitForMotionComplete", {});

    // Step 18: Retract dispenser head
    sequence->AddAction(pneumaticSlide, "Retract", {
      {"debounce_delay_ms", "300"},
      {"timeout_ms", "10000"}
    });

    // Step 19: Automatically move back to initial camera XY position (so camera can see imprint)
    sequence->AddAction(device, "MoveToAbs", {
      {"x", "context:camera_initial_x"},
      {"y", "context:camera_initial_y"},
      {"z", "context:camera_initial_z"},  // Use current Z (already retracted to safe height)
      {"blocking", "true"}
    });
    sequence->AddAction(device, "WaitForMotionComplete", {});

    // Step 20: Move camera to center on the imprint mark (manual adjustment)
    sequence->AddAction("system", "ManualAdjust", {
      {"message", "Move camera to center on the imprint mark.\n\n"
                  "Use camera view to precisely center crosshair on imprint.\n\n"
                  "Click OK when centered."},
      {"device_name", device},
      {"step_size_um", "100"},
      {"enable_x", "true"},
      {"enable_y", "true"},
      {"enable_z", "false"}
    });

    // Step 21: Fine adjust camera centering
    sequence->AddAction("system", "ManualAdjust", {
      {"message", "Fine adjust camera to precisely center on imprint"},
      {"device_name", device},
      {"step_size_um", "10"},
      {"enable_x", "true"},
      {"enable_y", "true"},
      {"enable_z", "false"}
    });

    // Step 22: Capture camera position (centered on imprint)
    sequence->AddAction("calibration", "CapturePosition", {
      {"device", device},
      {"key_prefix", "camera"}
    });

    // Step 23: Calculate XY offset only (needle position - camera position)
    // Z offset is handled by separate "Calibrate Needle Z" process
    sequence->AddAction("context", "Calculate", {
      {"resultKey", "offset_x"},
      {"key1", "needle_x"},
      {"key2", "camera_x"},
      {"operation", "-"}
    });
    sequence->AddAction("context", "Calculate", {
      {"resultKey", "offset_y"},
      {"key1", "needle_y"},
      {"key2", "camera_y"},
      {"operation", "-"}
    });

    // Step 23a: Calculate delta from previous calibration (if exists)
    sequence->BeginIf("system_offset_exists", ConditionalBlock::ComparisonType::IS_TRUE);
    {
      sequence->AddAction("context", "Calculate", {
        {"resultKey", "delta_x"},
        {"key1", "offset_x"},
        {"key2", "old_offset_x"},
        {"operation", "-"}
      });
      sequence->AddAction("context", "Calculate", {
        {"resultKey", "delta_y"},
        {"key1", "offset_y"},
        {"key2", "old_offset_y"},
        {"operation", "-"}
      });
    }
    sequence->EndIf();

    // Step 24: Save XY offset to file
    sequence->AddAction("calibration", "SaveCameraToNeedleOffset", {
      {"device", device},
      {"pneumatic_slide", pneumaticSlide}
    });

    // Step 25: Also save needle Z calibration (for future auto-positioning)
    sequence->AddAction("calibration", "SaveNeedleZCalibration", {
      {"device", device},
      {"pneumatic_slide", pneumaticSlide}
    });

    // Step 26: Success notification with calculated offset values and comparison
    sequence->BeginIf("system_offset_exists", ConditionalBlock::ComparisonType::IS_TRUE);
    {
      // Show comparison with previous calibration
      sequence->AddAction("system", "UserPrompt", {
        {"title", "✅ XY Offset Calibration Complete"},
        {"message", "Camera-to-Needle XY offset has been calibrated and saved.\n\n"
                    "NEW OFFSET:\n"
                    "  X: context:offset_x mm\n"
                    "  Y: context:offset_y mm\n\n"
                    "PREVIOUS OFFSET:\n"
                    "  X: context:old_offset_x mm\n"
                    "  Y: context:old_offset_y mm\n\n"
                    "CHANGE (Δ):\n"
                    "  X: context:delta_x mm\n"
                    "  Y: context:delta_y mm\n\n"
                    "Needle Z also saved for next calibration.\n\n"
                    "Files: config/calibration/needleXYoffsetCamera.json\n"
                    "       config/calibration/needle_z_calibration.json"},
        {"blocking", "false"}
      });
    }
    sequence->Else();
    {
      // First calibration - no comparison
      sequence->AddAction("system", "UserPrompt", {
        {"title", "✅ XY Offset Calibration Complete"},
        {"message", "Camera-to-Needle XY offset has been calibrated and saved.\n\n"
                    "OFFSET:\n"
                    "  X: context:offset_x mm\n"
                    "  Y: context:offset_y mm\n\n"
                    "(First calibration - no previous data to compare)\n\n"
                    "Needle Z also saved for next calibration.\n\n"
                    "Files: config/calibration/needleXYoffsetCamera.json\n"
                    "       config/calibration/needle_z_calibration.json"},
        {"blocking", "false"}
      });
    }
    sequence->EndIf();

    std::cout << "[CalibrateCameraToNeedleOffsetPlugin] Built sequence for: "
      << step->userNickname << " (Device: " << device << ")" << std::endl;
  }
};

//=============================================================================
// PLUGIN REGISTRATION (Static initialization)
//=============================================================================

namespace {
  struct CameraToNeedleOffsetRegistrar {
    CameraToNeedleOffsetRegistrar() {
      auto& registry = ProcessPluginRegistry::Instance();
      registry.RegisterPlugin(std::make_unique<CalibrateCameraToNeedleOffsetPlugin>());
      std::cout << "[CalibrationProcesses] Registered Calibrate Camera-to-Needle Offset plugin" << std::endl;
    }
  };

  static CameraToNeedleOffsetRegistrar s_offsetRegistrar;
}
