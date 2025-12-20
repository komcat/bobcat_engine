// AutoFocusProcesses.cpp
// Auto-focus calibration process plugin
// Uses gantry Z-axis movement and camera to find optimal focus position

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
// AUTO FOCUS PLUGIN
//=============================================================================

class AutoFocusPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Auto Focus";
  }

  std::string GetCategory() const override {
    return "Vision";
  }

  std::string GetDescription() const override {
    return "Automatically find optimal focus by scanning Z-axis and measuring image sharpness";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"motion_device", "gantry-main"},
      {"camera", "camera-main"},
      {"z_range_mm", "0.2"},
      {"z_step_mm", "0.05"},
      {"settle_time_ms", "100"},
      {"use_roi", "true"},
      {"roi_width", "100"},
      {"roi_height", "100"},
      {"save_to_context", "true"},
      {"context_prefix", "autofocus"}
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

    // Motion device selector (gantry for Z-axis movement)
    widget->addDeviceSelector("motion_device", "Motion Device",
                              getParam(params, "motion_device", "gantry-main"),
                              static_cast<int>(DeviceComboBox::DeviceType::Motion));

    // Camera selector
    widget->addDeviceSelector("camera", "Camera",
                              getParam(params, "camera", "camera-main"),
                              static_cast<int>(DeviceComboBox::DeviceType::Camera));

    // Z range parameter (min: 0.01, max: 5.0, step: 0.01, decimals: 2, suffix: mm)
    widget->addNumericParameter("z_range_mm", "Z Range (mm)",
                                getParam(params, "z_range_mm", "0.2"),
                                0.01, 5.0, 0.01, 2, "mm");

    // Z step parameter (min: 0.001, max: 1.0, step: 0.001, decimals: 3, suffix: mm)
    widget->addNumericParameter("z_step_mm", "Z Step (mm)",
                                getParam(params, "z_step_mm", "0.05"),
                                0.001, 1.0, 0.001, 3, "mm");

    // Settle time parameter (min: 10, max: 5000, step: 10, decimals: 0, suffix: ms)
    widget->addNumericParameter("settle_time_ms", "Settle Time (ms)",
                                getParam(params, "settle_time_ms", "100"),
                                10.0, 5000.0, 10.0, 0, "ms");

    // ROI checkbox
    widget->addCheckboxParameter("use_roi", "Use Center ROI",
                                 getParam(params, "use_roi", "true"),
                                 "Use centered region of interest for focus calculation");

    // ROI size parameters (centered automatically on image)
    widget->addNumericParameter("roi_width", "ROI Width",
                                getParam(params, "roi_width", "100"),
                                10.0, 2048.0, 10.0, 0, "px");

    widget->addNumericParameter("roi_height", "ROI Height",
                                getParam(params, "roi_height", "100"),
                                10.0, 2048.0, 10.0, 0, "px");

    // Context save options
    widget->addCheckboxParameter("save_to_context", "Save to Context",
                                 getParam(params, "save_to_context", "true"),
                                 "Store focus result in context for later use");

    // Context prefix as text field
    widget->addParameterField("context_prefix", "Context Prefix",
                              getParam(params, "context_prefix", "autofocus"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string motionDevice = getParam(params, "motion_device", "gantry-main");
    std::string camera = getParam(params, "camera", "camera-main");
    std::string zRangeMm = getParam(params, "z_range_mm", "0.2");
    std::string zStepMm = getParam(params, "z_step_mm", "0.05");
    std::string settleTimeMs = getParam(params, "settle_time_ms", "100");

    bool useRoi = (getParam(params, "use_roi", "true") == "true");
    std::string roiWidth = getParam(params, "roi_width", "100");
    std::string roiHeight = getParam(params, "roi_height", "100");

    std::string contextPrefix = getParam(params, "context_prefix", "autofocus");

    // Build the auto_focus command parameters
    std::map<std::string, std::string> afParams = {
      {"motion_device", motionDevice},
      {"camera", camera},
      {"z_range_mm", zRangeMm},
      {"z_step_mm", zStepMm},
      {"settle_time_ms", settleTimeMs}
    };

    // Add ROI parameters if enabled (centered automatically)
    if (useRoi) {
      afParams["use_center_roi"] = "true";
      afParams["roi_width"] = roiWidth;
      afParams["roi_height"] = roiHeight;
    }

    // Add context prefix for result storage
    afParams["context_prefix"] = contextPrefix;

    // =========================================================================
    // STEP 1: EXECUTE AUTO FOCUS
    // =========================================================================

    sequence->AddAction("vision", "auto_focus", afParams);

    // Results are stored in context and logged to console:
    //   autofocus.success, autofocus.best_z_mm, autofocus.best_focus_value

    std::cout << "[AutoFocusPlugin] Built sequence for: "
      << step->userNickname << " (Device: " << motionDevice
      << ", Camera: " << camera << ", Range: +/-" << zRangeMm << "mm)" << std::endl;
  }
};

//=============================================================================
// PLUGIN REGISTRATION (Static initialization)
//=============================================================================

namespace {
  struct AutoFocusProcessRegistrar {
    AutoFocusProcessRegistrar() {
      auto& registry = ProcessPluginRegistry::Instance();
      registry.RegisterPlugin(std::make_unique<AutoFocusPlugin>());
      std::cout << "[AutoFocusProcesses] Registered Auto Focus plugin" << std::endl;
    }
  };

  static AutoFocusProcessRegistrar s_registrar;
}
