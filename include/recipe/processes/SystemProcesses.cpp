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

    // Convert parameter name from UI format to SystemCommandHandler format
    std::map<std::string, std::string> waitParams = {
      {"milliseconds", params.at("duration_ms")}  // duration_ms (UI) → milliseconds (handler)
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
// ASK USER PLUGIN
//=============================================================================

class AskUserPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Ask User";
  }

  std::string GetCategory() const override {
    return "System";
  }

  std::string GetDescription() const override {
    return "Prompt the user with a message and wait for response";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"title", "Confirmation Required"},
      {"message", "Please confirm to continue"},
      {"blocking", "true"}  // "true" = wait for response, "false" = notification only
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addParameterField("title", "Dialog Title", params.at("title"));
    widget->addParameterField("message", "Message Text", params.at("message"));
    widget->addCheckboxParameter("blocking", "Wait for user response (blocking)",
      params.at("blocking"), "If checked, sequence pauses until user responds");
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::map<std::string, std::string> promptParams = {
      {"title", params.at("title")},
      {"message", params.at("message")},
      {"blocking", params.at("blocking")}
    };

    sequence->AddAction("system", "UserPrompt", promptParams);
  }
};

//=============================================================================
// GROUP START PLUGIN (Organizational - no sequence action)
//=============================================================================

class GroupStartPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Group Start";
  }

  std::string GetCategory() const override {
    return "Control Flow";
  }

  std::string GetDescription() const override {
    return "Organizational marker to start a group of related steps";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"group_name", "Process Group"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;
    widget->addParameterField("group_name", "Group Name", params.at("group_name"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    // Groups are organizational only - they don't add sequence actions
    std::cout << "[SystemProcesses] Group Start (organizational): "
              << step->parameters.at("group_name") << std::endl;
    (void)sequence;  // Unused
  }
};

//=============================================================================
// GROUP END PLUGIN (Organizational - no sequence action)
//=============================================================================

class GroupEndPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Group End";
  }

  std::string GetCategory() const override {
    return "Control Flow";
  }

  std::string GetDescription() const override {
    return "Organizational marker to end a group of related steps";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {};  // No parameters
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    // No parameters - widget will show empty form or info message
    (void)widget;
    (void)instance;
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    // Groups are organizational only - they don't add sequence actions
    std::cout << "[SystemProcesses] Group End (organizational)" << std::endl;
    (void)sequence;
    (void)step;
  }
};

//=============================================================================
// STOP SEQUENCE PLUGIN
//=============================================================================

class StopSequencePlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Stop Sequence";
  }

  std::string GetCategory() const override {
    return "Control Flow";
  }

  std::string GetDescription() const override {
    return "Immediately stop recipe execution (often used inside conditional blocks)";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {};  // No parameters
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    // No parameters - widget will show empty form or info message
    (void)widget;
    (void)instance;
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    sequence->AddAction("system", "StopSequence", {});
    std::cout << "[SystemProcesses] Built Stop Sequence command" << std::endl;
    (void)step;
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
      registry.RegisterPlugin(std::make_unique<AskUserPlugin>());
      registry.RegisterPlugin(std::make_unique<GroupStartPlugin>());
      registry.RegisterPlugin(std::make_unique<GroupEndPlugin>());
      registry.RegisterPlugin(std::make_unique<StopSequencePlugin>());

      std::cout << "[SystemProcesses] Registered 7 system process plugins" << std::endl;
    }
  };

  // Static instance causes registration at program startup
  static RegisterSystemProcesses g_registerSystemProcesses;
}
