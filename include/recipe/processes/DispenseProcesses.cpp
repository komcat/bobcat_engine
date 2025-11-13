// DispenseProcesses.cpp
// Dispense process plugin for automated dispensing with needle offset

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
// DISPENSE PLUGIN
//=============================================================================

class DispensePlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Dispense";
  }

  std::string GetCategory() const override {
    return "Dispensing";
  }

  std::string GetDescription() const override {
    return "Automated dispensing with camera-to-needle offset, Z-height positioning, and user confirmation";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "gantry-main"},
      {"pneumatic_slide", "Dispenser_Head"},
      {"io_device", "EziIO-1"},
      {"dispense_pin", "0"},
      {"dispense_interval_ms", "300"},
      {"velocity_approach", "20.0"},
      {"safety_height", "1.0"},
      {"velocity_safety", "5.0"}
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

    // Device selector - hardcoded to gantry-main
    widget->addDeviceSelector("device", "Motion Device",
                              getParam("device"),
                              static_cast<int>(DeviceComboBox::DeviceType::Motion));

    // Pneumatic slide selector
    widget->addSlideSelector("pneumatic_slide", "Dispenser Slide",
                            getParam("pneumatic_slide"));

    // IO Device and pin for dispensing
    widget->addDeviceSelector("io_device", "IO Device",
                              getParam("io_device"),
                              static_cast<int>(DeviceComboBox::DeviceType::IO));

    widget->addIOPinSelector("dispense_pin", "Dispenser Output Pin",
                             getParam("dispense_pin"), "io_device", "output");

    // Dispense parameters
    widget->addParameterField("dispense_interval_ms", "Dispense Interval (ms)",
                             getParam("dispense_interval_ms"));

    // Motion parameters
    widget->addParameterField("velocity_approach", "Approach Velocity (mm/s)",
                             getParam("velocity_approach"));

    widget->addParameterField("safety_height", "Safety Height (mm)",
                             getParam("safety_height"));

    widget->addParameterField("velocity_safety", "Safety Velocity (mm/s)",
                             getParam("velocity_safety"));
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
    std::string ioDevice = getParam("io_device");
    std::string dispensePin = getParam("dispense_pin");
    std::string dispenseInterval = getParam("dispense_interval_ms");
    std::string velocityApproach = getParam("velocity_approach");
    std::string safetyHeight = getParam("safety_height");
    std::string velocitySafety = getParam("velocity_safety");

    // Get the process instance UID for unique dispense position files
    std::string uid = step->uniqueId;

    // =========================================================================
    // STEP 1: LOAD OR CREATE CAMERA DISPENSING POSITION
    // =========================================================================

    // Note: Device validation happens automatically during execution.
    // If devices are not found, the sequence will fail with clear error messages:
    //   - Motion device: check config/motor_devices.json
    //   - Pneumatic slide: check config/pneumatic_slides.json
    //   - IO device: check config/io_devices.json

    sequence->AddAction("system", "UserPrompt", {
      {"title", "Dispense Process Starting"},
      {"message", "Starting automated dispense process.\n\n"
                  "System will guide you through positioning."},
      {"blocking", "false"}
    });

    // Load existing dispense camera position (if exists)
    sequence->AddAction("calibration", "LoadExistingCalibration", {
      {"uid", uid}
    });

    // Check if prior position exists
    sequence->BeginIf("navigation_exists", ConditionalBlock::ComparisonType::IS_TRUE);
    {
      // Ask user if they want to use previous position
      sequence->AddAction("system", "UserPrompt", {
        {"title", "Use Previous Dispense Position?"},
        {"message", "Previous dispense position found:\n"
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
      {"message", "Position camera over desired dispensing location.\n\n"
                  "Adjust as needed, then click OK."},
      {"device_name", device},
      {"step_size_um", "100"},
      {"enable_x", "true"},
      {"enable_y", "true"},
      {"enable_z", "true"}
    });

    // Capture camera position - this is physical dispensing position
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
    // STEP 2: LOAD CAMERA-TO-NEEDLE OFFSET AND CALCULATE NEEDLE POSITION
    // =========================================================================

    // Load latest camera-to-needle XY calibration data
    sequence->AddAction("calibration", "LoadCameraToNeedleOffset", {});

    // Check if offset file exists - if not, show error and skip the rest
    sequence->BeginIf("system_offset_exists", ConditionalBlock::ComparisonType::IS_TRUE);
    {
      // Offset exists - continue with dispense sequence

    // Calculate needle XY position: needle_XY = camera_XY + offset
    sequence->AddAction("context", "Calculate", {
      {"resultKey", "needle_target_x"},
      {"key1", "camera_x"},
      {"key2", "system_offset_x"},
      {"operation", "+"}
    });
    sequence->AddAction("context", "Calculate", {
      {"resultKey", "needle_target_y"},
      {"key1", "camera_y"},
      {"key2", "system_offset_y"},
      {"operation", "+"}
    });

    // Move to needle XY position, keep Z at camera Z (safe height)
    sequence->AddAction(device, "MoveToAbs", {
      {"x", "context:needle_target_x"},
      {"y", "context:needle_target_y"},
      {"z", "context:camera_z"},
      {"blocking", "true"}
    });
    sequence->AddAction(device, "WaitForMotionComplete", {});

    // =========================================================================
    // STEP 3: EXTEND DISPENSER HEAD
    // =========================================================================

    sequence->AddAction(pneumaticSlide, "Extend", {
      {"debounce_delay_ms", "300"},
      {"timeout_ms", "10000"}
    });

    sequence->AddAction("system", "Wait", {
      {"milliseconds", "500"}
    });

    // =========================================================================
    // STEP 4: Z-HEIGHT POSITIONING (LOAD OR CREATE)
    // =========================================================================

    // Load existing Z-height for this dispense instance
    sequence->AddAction("calibration", "LoadDispenseZHeight", {
      {"uid", uid}
    });

    // Check if Z-height exists
    sequence->BeginIf("dispense_z_exists", ConditionalBlock::ComparisonType::IS_TRUE);
    {
      // =========================================================================
      // PATH A: Z-height exists - Do automated two-stage approach
      // =========================================================================

      // Store safety_height in context for calculations
      sequence->AddAction("context", "SetNumeric", {
        {"key", "safety_height"},
        {"value", safetyHeight}
      });

      // Capture current Z position
      sequence->AddAction("calibration", "CapturePosition", {
        {"device", device},
        {"key_prefix", "current"}
      });

      // Calculate target position: dispense_z - safety_height
      sequence->AddAction("context", "Calculate", {
        {"resultKey", "target_safety_z"},
        {"key1", "dispense_z"},
        {"key2", "safety_height"},
        {"operation", "-"}
      });

      // Calculate distance from current_z to target_safety_z
      sequence->AddAction("context", "Calculate", {
        {"resultKey", "distance_to_safety"},
        {"key1", "target_safety_z"},
        {"key2", "current_z"},
        {"operation", "-"}
      });

      // Set fast approach velocity
      sequence->AddAction(device, "SetVelocity", {
        {"axis", "Z"},
        {"velocity", velocityApproach}
      });

      // Move to safety height position (fast)
      sequence->AddAction(device, "MoveRelative", {
        {"axis", "Z"},
        {"distance", "context:distance_to_safety"}
      });
      sequence->AddAction(device, "WaitForMotionComplete", {});

      // Set slow safety velocity
      sequence->AddAction(device, "SetVelocity", {
        {"axis", "Z"},
        {"velocity", velocitySafety}
      });

      // Final slow approach: move safety_height to reach dispense_z
      sequence->AddAction(device, "MoveRelative", {
        {"axis", "Z"},
        {"distance", "context:safety_height"}
      });
      sequence->AddAction(device, "WaitForMotionComplete", {});
    }
    sequence->Else();
    {
      // =========================================================================
      // PATH B: No saved Z-height - Manual adjust directly to dispense height
      // =========================================================================

      sequence->AddAction("system", "UserPrompt", {
        {"title", "First Time Dispense Setup"},
        {"message", "No saved Z-height found for this dispense location.\n\n"
                    "Next step: Manually adjust Z to desired dispensing height."},
        {"blocking", "true"}
      });

      sequence->AddAction("system", "ManualAdjust", {
        {"message", "Adjust Z to desired dispensing height.\n\n"
                    "Fine-tune the height carefully.\n\n"
                    "Click OK when positioned correctly."},
        {"device_name", device},
        {"step_size_um", "10"},
        {"enable_x", "false"},
        {"enable_y", "false"},
        {"enable_z", "true"}
      });

      // Capture and save Z-height
      sequence->AddAction("calibration", "CapturePosition", {
        {"device", device},
        {"key_prefix", "dispense"}
      });

      sequence->AddAction("calibration", "SaveDispenseZHeight", {
        {"uid", uid},
        {"device", device}
      });

      sequence->AddAction("system", "UserPrompt", {
        {"title", "✅ Z-Height Saved"},
        {"message", "Dispensing height saved successfully.\n\n"
                    "This height will be used automatically for future dispenses.\n\n"
                    "Ready to proceed with dispensing."},
        {"blocking", "true"}
      });

      // Already at correct dispense height - skip automated approach
    }
    sequence->EndIf();

    // =========================================================================
    // STEP 5: CONFIRM AND DISPENSE
    // =========================================================================

    sequence->AddAction("system", "UserPrompt", {
      {"title", "Ready to Dispense"},
      {"message", "Needle positioned at dispensing height.\n\n"
                  "OK: Proceed with dispense\n"
                  "Cancel: Abort dispense"},
      {"blocking", "true"},
      {"allow_cancel", "true"}
    });

    // Check if user confirmed
    sequence->BeginIf("user_confirmed", ConditionalBlock::ComparisonType::IS_TRUE);
    {
      // User confirmed - trigger dispense
      sequence->AddAction(ioDevice, "SetOutput", {
        {"pinName", dispensePin},
        {"state", "true"}
      });

      sequence->AddAction("system", "Wait", {
        {"milliseconds", dispenseInterval}
      });

      sequence->AddAction(ioDevice, "SetOutput", {
        {"pinName", dispensePin},
        {"state", "false"}
      });

      sequence->AddAction("system", "UserPrompt", {
        {"title", "✅ Dispense Complete"},
        {"message", "Dispensing completed successfully.\n\n"
                    "Retracting and returning to safe position."},
        {"blocking", "false"}
      });
    }
    sequence->Else();
    {
      // User cancelled
      sequence->AddAction("system", "UserPrompt", {
        {"title", "❌ Dispense Cancelled"},
        {"message", "Dispense operation cancelled by user.\n\n"
                    "Retracting and returning to safe position."},
        {"blocking", "false"}
      });
    }
    sequence->EndIf();

    // =========================================================================
    // STEP 6: RETRACT AND RETURN TO SAFE
    // =========================================================================

    // Retract dispenser head
    sequence->AddAction(pneumaticSlide, "Retract", {
      {"debounce_delay_ms", "300"},
      {"timeout_ms", "10000"}
    });

    // Move back to Safe position
    sequence->AddAction(device, "MoveToNamedPosition", {
      {"positionName", "safe"}
    });
    sequence->AddAction(device, "WaitForMotionComplete", {});

    }  // End IF (offset exists)
    sequence->Else();
    {
      // Offset file missing - show error
      sequence->AddAction("system", "UserPrompt", {
        {"title", "❌ Needle XY Offset Missing"},
        {"message", "Camera-to-needle XY offset file not found!\n\n"
                    "Please run 'Calibrate Camera-to-Needle Offset' process first.\n\n"
                    "Dispense cannot proceed without offset calibration."},
        {"blocking", "true"}
      });
    }
    sequence->EndIf();

    std::cout << "[DispensePlugin] Built dispense sequence for: "
      << step->userNickname << " (Device: " << device << ", Slide: " << pneumaticSlide << ")" << std::endl;
  }
};

//=============================================================================
// PLUGIN REGISTRATION (Static initialization)
//=============================================================================

namespace {
  // Self-register at startup
  struct DispenseProcessRegistrar {
    DispenseProcessRegistrar() {
      auto& registry = ProcessPluginRegistry::Instance();
      registry.RegisterPlugin(std::make_unique<DispensePlugin>());
      std::cout << "[DispenseProcesses] Registered Dispense plugin" << std::endl;
    }
  };

  // Static instance triggers registration at startup
  static DispenseProcessRegistrar s_registrar;
}
