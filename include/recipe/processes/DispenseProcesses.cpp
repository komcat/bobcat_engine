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
#include <filesystem>
#include <QCheckBox>
#include <QMessageBox>

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
      {"delay_before_dispense_ms", "0"},
      {"delay_after_dispense_ms", "0"},
      {"velocity", "0"},           // XY travel velocity (0 = use controller default)
      {"velocity_approach", "20.0"},
      {"safety_height", "1.0"},
      {"velocity_safety", "5.0"},
      {"auto_confirm", "false"},  // Auto-confirm after first-time setup
      {"skip_camera_verify", "false"},  // Skip camera view over dispensing position
      {"reteach_z_height", "false"},  // Force re-teach XY and Z height and save to data file
      {"save_xy_after_confirm", "false"}  // Save camera XY to data file after verification
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

    widget->addParameterField("delay_before_dispense_ms", "Delay Before Dispense (ms)",
                             getParam("delay_before_dispense_ms"));

    widget->addParameterField("delay_after_dispense_ms", "Delay After Dispense (ms)",
                             getParam("delay_after_dispense_ms"));

    // Motion parameters
    widget->addParameterField("velocity", "XY Travel Velocity (mm/s, 0=default)",
                             getParam("velocity"));
    widget->addParameterField("velocity_approach", "Z Approach Velocity (mm/s)",
                             getParam("velocity_approach"));

    widget->addParameterField("safety_height", "Safety Height (mm)",
                             getParam("safety_height"));

    widget->addParameterField("velocity_safety", "Safety Velocity (mm/s)",
                             getParam("velocity_safety"));

    // Reteach XY and Z height checkbox
    widget->addCheckboxParameter("reteach_z_height", "Re-teach XY and Z height, save to data file",
                                getParam("reteach_z_height"),
                                "Force manual XY positioning and Z-height adjustment even if saved data exists. "
                                "The new XY position and Z height will be saved to their respective data files.");

    // Save XY after camera confirm checkbox
    widget->addCheckboxParameter("save_xy_after_confirm", "After camera XY confirm, save to data file",
                                getParam("save_xy_after_confirm"),
                                "Save the camera XY position to the navigation data file "
                                "after camera verification of the dispense result.");

    // Auto-confirm checkbox
    widget->addCheckboxParameter("auto_confirm", "Auto-confirm (skip manual confirmations)",
                                getParam("auto_confirm"),
                                "Automatically confirm all prompts after first-time setup. "
                                "Only works if camera position, Z-height, and needle offset are calibrated.");

    // Skip camera verify checkbox
    widget->addCheckboxParameter("skip_camera_verify", "Skip camera verification after dispense",
                                getParam("skip_camera_verify"),
                                "Skip moving camera back to dispensing position for visual verification. "
                                "Useful for faster cycle times when verification is not needed.");

    // ✅ Add validation when user toggles auto_confirm checkbox
    QCheckBox* autoConfirmCheckbox = widget->getCheckbox("auto_confirm");
    if (autoConfirmCheckbox) {
      // Add custom validation connection (don't disconnect existing signals)
      QObject::connect(autoConfirmCheckbox, &QCheckBox::clicked, widget,
        [autoConfirmCheckbox, instance](bool checked) {
        if (checked) {
          // User is trying to enable auto-confirm - validate setup
          std::string uid = instance->uniqueId;
          std::string navigationFile = "config/SYSTEM/navigation_" + uid + ".json";
          std::string zHeightFile = "config/SYSTEM/dispense_z_" + uid + ".json";
          std::string systemOffsetFileLocal = "config/SYSTEM/needlexyoffset.local.json";
          std::string systemOffsetFileBase = "config/SYSTEM/needlexyoffset.json";

          // Check .local.json first, then fall back to base .json
          bool offsetExists = std::filesystem::exists(systemOffsetFileLocal) ||
                              std::filesystem::exists(systemOffsetFileBase);
          bool navigationExists = std::filesystem::exists(navigationFile);
          bool zHeightExists = std::filesystem::exists(zHeightFile);
          bool setupComplete = offsetExists && navigationExists && zHeightExists;

          if (!setupComplete) {
            // Setup incomplete - uncheck it
            autoConfirmCheckbox->setChecked(false);

            // Update tooltip to show what's missing
            QString tooltipMsg = "⚠️ Setup incomplete - auto-confirm disabled\n\nMissing:\n";
            if (!offsetExists) tooltipMsg += "  • Camera-to-Needle offset\n";
            if (!navigationExists) tooltipMsg += "  • Camera position (UID: " + QString::fromStdString(uid) + ")\n";
            if (!zHeightExists) tooltipMsg += "  • Z-height (UID: " + QString::fromStdString(uid) + ")\n";
            tooltipMsg += "\nRun this process once manually to complete setup.";
            autoConfirmCheckbox->setToolTip(tooltipMsg);

            std::cout << "[DispensePlugin] ⚠️ Auto-confirm disabled - setup incomplete for UID: " << uid << std::endl;
          } else {
            // Setup complete - update tooltip to show success
            autoConfirmCheckbox->setToolTip(
              "✅ Setup complete!\n\n"
              "Auto-confirm enabled - all prompts will be skipped.\n"
              "Camera position, Z-height, and needle offset are calibrated.");
            std::cout << "[DispensePlugin] ✅ Auto-confirm enabled - setup complete for UID: " << uid << std::endl;
          }
        }
      });
    }
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
    std::string delayBeforeDispense = getParam("delay_before_dispense_ms");
    std::string delayAfterDispense = getParam("delay_after_dispense_ms");
    std::string velocityApproach = getParam("velocity_approach");
    std::string safetyHeight = getParam("safety_height");
    std::string velocitySafety = getParam("velocity_safety");
    std::string velocity = getParam("velocity");
    bool autoConfirm = (getParam("auto_confirm") == "true");
    bool skipCameraVerify = (getParam("skip_camera_verify") == "true");
    bool reteachZHeight = (getParam("reteach_z_height") == "true");
    bool saveXyAfterConfirm = (getParam("save_xy_after_confirm") == "true");

    // Get the process instance UID for unique dispense position files
    std::string uid = step->uniqueId;

    // =========================================================================
    // SET XY TRAVEL VELOCITY (if configured, 0 = use controller default)
    // Only set X and Y axes - Z velocity is managed separately by
    // velocity_approach and velocity_safety for the two-stage Z descent
    // =========================================================================
    {
      double vel = std::stod(velocity);
      if (vel > 0.0) {
        sequence->AddAction(device, "SetVelocity", {
          {"axis", "X"}, {"velocity", velocity}
        });
        sequence->AddAction(device, "SetVelocity", {
          {"axis", "Y"}, {"velocity", velocity}
        });
      }
    }

    // =========================================================================
    // SETUP VALIDATION: Check if all calibration files exist for auto-confirm
    // =========================================================================
    bool setupComplete = false;

    if (autoConfirm) {
      // Check if all required calibration files exist
      std::string navigationFile = "config/SYSTEM/navigation_" + uid + ".json";
      std::string zHeightFile = "config/SYSTEM/dispense_z_" + uid + ".json";  // ✅ Fixed: no "_height"
      std::string systemOffsetFileLocal = "config/SYSTEM/needlexyoffset.local.json";
      std::string systemOffsetFileBase = "config/SYSTEM/needlexyoffset.json";

      // Check .local.json first, then fall back to base .json
      bool offsetExists = std::filesystem::exists(systemOffsetFileLocal) ||
                          std::filesystem::exists(systemOffsetFileBase);
      bool navigationExists = std::filesystem::exists(navigationFile);
      bool zHeightExists = std::filesystem::exists(zHeightFile);

      setupComplete = offsetExists && navigationExists && zHeightExists;

      if (setupComplete) {
        std::cout << "[DispensePlugin] ✅ Setup complete - auto-confirm enabled for UID: " << uid << std::endl;
      } else {
        std::cout << "[DispensePlugin] ⚠️ Setup incomplete - auto-confirm disabled:" << std::endl;
        if (!offsetExists) std::cout << "  - Missing: Camera-to-Needle offset" << std::endl;
        if (!navigationExists) std::cout << "  - Missing: Navigation position (UID: " << uid << ")" << std::endl;
        if (!zHeightExists) std::cout << "  - Missing: Dispense Z-height (UID: " << uid << ")" << std::endl;
      }
    }

    // Determine if we should auto-accept prompts
    std::string autoAcceptValue = (autoConfirm && setupComplete) ? "true" : "false";

    // Log auto-confirm mode status
    if (autoConfirm && setupComplete) {
      std::cout << "[DispensePlugin] 🚀 FULL AUTO MODE - Zero interaction, all prompts skipped" << std::endl;
    }

    // =========================================================================
    // STEP 1: LOAD OR CREATE CAMERA DISPENSING POSITION
    // =========================================================================

    // Note: Device validation happens automatically during execution.
    // If devices are not found, the sequence will fail with clear error messages:
    //   - Motion device: check config/motor_devices.json
    //   - Pneumatic slide: check config/pneumatic_slides.json
    //   - IO device: check config/io_devices.json

    // Load existing dispense camera position (if exists)
    sequence->AddAction("calibration", "LoadExistingCalibration", {
      {"uid", uid}
    });

    // Check if prior position exists
    sequence->BeginIf("navigation_exists", ConditionalBlock::ComparisonType::IS_TRUE);
    {
      // If skip_camera_verify is enabled, don't move to camera position - go directly to needle
      if (!skipCameraVerify) {
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
          {"allow_cancel", "true"},
          {"auto_accept", autoAcceptValue}
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
      // When skipCameraVerify is true, camera_x/y/z are already loaded from file
      // and will be used to calculate needle position without moving to camera first
    }
    sequence->EndIf();

    // ✅ Only show manual adjust if NOT in auto-confirm mode with complete setup
    // AND NOT skipping camera verification (which implies using saved positions directly)
    // When auto-confirm is enabled and position exists, skip manual adjustment
    // reteachZHeight forces XY re-teach as well (re-teach both XY and Z)
    if (((!setupComplete || !autoConfirm) && !skipCameraVerify) || reteachZHeight) {
      // Manual adjust to position camera (either from scratch or to confirm auto-nav)
      sequence->AddAction("system", "ManualAdjust", {
        {"message", "Position camera over desired dispensing location.\n\n"
                    "Adjust X/Y as needed, then click OK.\n"
                    "(Z height is calibrated separately)"},
        {"device_name", device},
        {"step_size_um", "100"},
        {"enable_x", "true"},
        {"enable_y", "true"},
        {"enable_z", "true"}
      });

      // Capture current position to "adjusted" prefix (not overwriting original camera_z)
      sequence->AddAction("calibration", "CapturePosition", {
        {"device", device},
        {"key_prefix", "adjusted"}
      });

      // Copy adjusted X/Y to camera_x/camera_y, but KEEP original camera_z if it exists
      // This ensures Z height from prior calibration is preserved
      sequence->AddAction("context", "CopyValue", {
        {"source_key", "adjusted_x"},
        {"dest_key", "camera_x"}
      });
      sequence->AddAction("context", "CopyValue", {
        {"source_key", "adjusted_y"},
        {"dest_key", "camera_y"}
      });

      // Only update camera_z if this is first-time setup (no prior navigation data)
      sequence->BeginIf("navigation_exists", ConditionalBlock::ComparisonType::IS_FALSE);
      {
        // First time - use the adjusted Z position
        sequence->AddAction("context", "CopyValue", {
          {"source_key", "adjusted_z"},
          {"dest_key", "camera_z"}
        });
      }
      sequence->EndIf();
      // If navigation_exists was true, camera_z keeps its loaded value from file

      // Save navigation data for future use (new X/Y, preserved Z)
      sequence->AddAction("calibration", "SaveNavigationData", {
        {"uid", uid},
        {"device", device}
      });
    } else {
      // Auto-confirm mode with complete setup - position already loaded and navigated
      std::cout << "[DispensePlugin] Skipping manual adjust - using saved position (auto-confirm enabled)" << std::endl;
    }

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

    // Check if Z-height exists AND reteach is not requested
    if (reteachZHeight) {
      // Force context value to false so we always take PATH B (manual adjust)
      sequence->AddAction("context", "SetValue", {
        {"key", "dispense_z_exists"},
        {"value", "false"}
      });
    }

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
        {"title", "Dispense Z-Height Setup"},
        {"message", "No saved Z-height found (or re-teach requested).\n\n"
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

    // Reset Z velocity to approach speed after Z-height positioning
    // (PATH A leaves Z at slow safety velocity, PATH B doesn't set it at all)
    sequence->AddAction(device, "SetVelocity", {
      {"axis", "Z"}, {"velocity", velocityApproach}
    });

    // =========================================================================
    // STEP 5: CONFIRM AND DISPENSE
    // =========================================================================

    sequence->AddAction("system", "UserPrompt", {
      {"title", "Ready to Dispense"},
      {"message", "Needle positioned at dispensing height.\n\n"
                  "OK: Proceed with dispense\n"
                  "Cancel: Abort dispense"},
      {"blocking", "true"},
      {"allow_cancel", "true"},
      {"auto_accept", autoAcceptValue}
    });

    // Check if user confirmed
    sequence->BeginIf("user_confirmed", ConditionalBlock::ComparisonType::IS_TRUE);
    {
      // Delay before dispense (if configured)
      if (std::stoi(delayBeforeDispense) > 0) {
        sequence->AddAction("system", "Wait", {
          {"milliseconds", delayBeforeDispense}
        });
      }

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

      // Delay after dispense (if configured)
      if (std::stoi(delayAfterDispense) > 0) {
        sequence->AddAction("system", "Wait", {
          {"milliseconds", delayAfterDispense}
        });
      }

      // Only show completion notification if NOT in auto-confirm mode
      if (!autoConfirm || !setupComplete) {
        sequence->AddAction("system", "UserPrompt", {
          {"title", "✅ Dispense Complete"},
          {"message", "Dispensing completed successfully.\n\n"
                      "Retracting and returning to safe position."},
          {"blocking", "false"}
        });
      }
    }
    sequence->Else();
    {
      // User cancelled - always show this (shouldn't happen in auto-confirm mode)
      sequence->AddAction("system", "UserPrompt", {
        {"title", "❌ Dispense Cancelled"},
        {"message", "Dispense operation cancelled by user.\n\n"
                    "Retracting and returning to safe position."},
        {"blocking", "false"}
      });
    }
    sequence->EndIf();

    // =========================================================================
    // STEP 6: RETRACT AND VERIFY DISPENSE RESULT
    // =========================================================================

    // Retract dispenser head
    sequence->AddAction(pneumaticSlide, "Retract", {
      {"debounce_delay_ms", "300"},
      {"timeout_ms", "10000"}
    });

    // Move camera back to dispensed location so user can verify (unless skipped)
    if (!skipCameraVerify) {
      sequence->AddAction(device, "MoveToAbs", {
        {"x", "context:camera_x"},
        {"y", "context:camera_y"},
        {"z", "context:camera_z"},
        {"blocking", "true"}
      });
      sequence->AddAction(device, "WaitForMotionComplete", {});

      // Show verification prompt (unless auto-confirm mode)
      if (!autoConfirm || !setupComplete) {
        sequence->AddAction("system", "UserPrompt", {
          {"title", "Verify Dispense Result"},
          {"message", "Camera is now positioned over the dispense location.\n\n"
                      "Please verify the dispense result.\n\n"
                      "Click OK to move to safe position."},
          {"blocking", "true"}
        });
      }

      // Save camera XY to navigation data file after verification (if enabled)
      if (saveXyAfterConfirm) {
        // Capture current camera position after any manual adjustments
        sequence->AddAction("calibration", "CapturePosition", {
          {"device", device},
          {"key_prefix", "verified"}
        });

        // Update camera_x/camera_y with verified position
        sequence->AddAction("context", "CopyValue", {
          {"source_key", "verified_x"},
          {"dest_key", "camera_x"}
        });
        sequence->AddAction("context", "CopyValue", {
          {"source_key", "verified_y"},
          {"dest_key", "camera_y"}
        });

        // Save updated navigation data
        sequence->AddAction("calibration", "SaveNavigationData", {
          {"uid", uid},
          {"device", device}
        });

        sequence->AddAction("system", "UserPrompt", {
          {"title", "XY Position Saved"},
          {"message", "Camera XY position saved to navigation data file."},
          {"blocking", "false"}
        });
      }
    }

    // =========================================================================
    // STEP 7: Dispensing complete (no move to safe - let next step handle positioning)
    // =========================================================================

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
      << step->userNickname << " (Device: " << device << ", Slide: " << pneumaticSlide
      << ", DelayBefore: " << delayBeforeDispense << "ms, DelayAfter: " << delayAfterDispense << "ms"
      << ", SkipCameraVerify: " << (skipCameraVerify ? "yes" : "no")
      << ", ReteachZ: " << (reteachZHeight ? "yes" : "no")
      << ", SaveXYAfterConfirm: " << (saveXyAfterConfirm ? "yes" : "no") << ")" << std::endl;
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
