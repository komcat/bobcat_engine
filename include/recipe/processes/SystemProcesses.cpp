// SystemProcesses.cpp
// System process plugins: Wait, Log Message, Ask User, Manual Adjust

#include "../IProcessPlugin.h"
#include "../ProcessPluginRegistry.h"
#include "../../sequence/SequenceBuilder.h"
#include "../../AppUI/RecipePage/ProcessConfigWidget.h"
#include "../../AppUI/RecipePage/DeviceSelectorWidgets.h"
#include "../RecipeManager.h"

#include <iostream>
#include <memory>
#include <map>
#include <string>

//=============================================================================
// WAIT PLUGIN
//=============================================================================

class WaitPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Wait";
  }

  std::string GetCategory() const override {
    return "System";
  }

  std::string GetDescription() const override {
    return "Pause execution for a specified duration";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"duration_ms", "1000"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;
    widget->addParameterField("duration_ms", "Duration (ms)", params.at("duration_ms"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;
    std::map<std::string, std::string> waitParams = {
      {"duration_ms", params.at("duration_ms")}
    };
    sequence->AddAction("system", "Wait", waitParams);
  }
};

//=============================================================================
// LOG MESSAGE PLUGIN
//=============================================================================

class LogMessagePlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Log Message";
  }

  std::string GetCategory() const override {
    return "System";
  }

  std::string GetDescription() const override {
    return "Write a message to the execution log";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"message", "Log message here"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;
    widget->addParameterField("message", "Log Message", params.at("message"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;
    std::map<std::string, std::string> logParams = {
      {"message", params.at("message")}
    };
    sequence->AddAction("system", "LogMessage", logParams);
  }
};

//=============================================================================
// MANUAL ADJUST PLUGIN
//=============================================================================

class ManualAdjustPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Manual Adjust";
  }

  std::string GetCategory() const override {
    return "System";
  }

  std::string GetDescription() const override {
    return "Pause execution and allow user to manually adjust motor position";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"message", "Please adjust the position as needed"},
      {"device_name", "gantry-main"},
      {"step_size_um", "10"},
      {"enable_x", "true"},
      {"enable_y", "true"},
      {"enable_z", "true"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    // Message field
    widget->addParameterField("message", "Instruction Message",
      params.at("message"));

    // Device name dropdown
    widget->addDeviceSelector("device_name", "Motor Device",
      params.at("device_name"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));

    // Step size input
    widget->addParameterField("step_size_um", "Step Size (μm)",
      params.at("step_size_um"));

    // Enable X/Y/Z axis checkboxes
    widget->addCheckboxParameter("enable_x", "Enable X axis movement",
      params.at("enable_x"), "Allow user to jog in X direction (left/right)");

    widget->addCheckboxParameter("enable_y", "Enable Y axis movement",
      params.at("enable_y"), "Allow user to jog in Y direction (in/out)");

    widget->addCheckboxParameter("enable_z", "Enable Z axis movement",
      params.at("enable_z"), "Allow user to jog in Z direction (up/down)");
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::map<std::string, std::string> adjustParams = {
      {"message", params.at("message")},
      {"device_name", params.at("device_name")},
      {"step_size_um", params.at("step_size_um")},
      {"enable_x", params.at("enable_x")},
      {"enable_y", params.at("enable_y")},
      {"enable_z", params.at("enable_z")}
    };

    sequence->AddAction("system", "ManualAdjust", adjustParams);

    std::cout << "[SystemProcesses] Built Manual Adjust sequence for: "
      << step->userNickname << " (device=" << params.at("device_name")
      << ", step=" << params.at("step_size_um") << "um)" << std::endl;
  }
};

//=============================================================================
// AUTO-REGISTRATION
//=============================================================================

namespace {
  struct RegisterSystemProcesses {
    RegisterSystemProcesses() {
      auto& registry = ProcessPluginRegistry::Instance();

      registry.RegisterPlugin(std::make_unique<WaitPlugin>());
      registry.RegisterPlugin(std::make_unique<LogMessagePlugin>());
      registry.RegisterPlugin(std::make_unique<ManualAdjustPlugin>());

      std::cout << "[SystemProcesses] Registered 3 system process plugins" << std::endl;
    }
  };

  // Static instance causes registration at program startup
  static RegisterSystemProcesses g_registerSystemProcesses;
}
