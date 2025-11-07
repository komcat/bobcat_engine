// IOProcesses.cpp
// I/O and Pneumatic process plugins: Set Output, Slide Extend, Slide Retract

#include "../IProcessPlugin.h"
#include "../ProcessPluginRegistry.h"
#include "../RecipeManager.h"  // For ProcessInstance
#include "../../sequence/SequenceBuilder.h"
#include "../../AppUI/RecipePage/ProcessConfigWidget.h"
#include "../../AppUI/RecipePage/DeviceSelectorWidgets.h"

#include <iostream>
#include <memory>
#include <map>
#include <string>

//=============================================================================
// SET OUTPUT PLUGIN
//=============================================================================

class SetOutputPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Set Output";
  }

  std::string GetCategory() const override {
    return "I/O & Pneumatics";
  }

  std::string GetDescription() const override {
    return "Set digital output pin to ON or OFF state";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"io_device", "IOBottom"},
      {"pin", "0"},
      {"state", "true"}  // "true" for ON, "false" for OFF
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addDeviceSelector("io_device", "I/O Device", params.at("io_device"),
      static_cast<int>(DeviceComboBox::DeviceType::IO));

    // ✅ IMPROVED: IO pin selector (dropdown with pin names from IO device)
    widget->addIOPinSelector("pin", "Output Pin", params.at("pin"), "io_device", "output");

    widget->addCheckboxParameter("state", "Output State (ON/OFF)", params.at("state"),
      "Set to ON (checked) or OFF (unchecked)");
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    sequence->AddAction(params.at("io_device"), "SetOutputPin", {
      {"pin", params.at("pin")},
      {"state", params.at("state")}
    });

    std::cout << "[IOProcesses] Built Set Output sequence for: " << step->userNickname << std::endl;
  }
};

//=============================================================================
// SET OUTPUT NAME PLUGIN (Turn ON by name)
//=============================================================================

class SetOutputNamePlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Set Output Name";
  }

  std::string GetCategory() const override {
    return "I/O & Pneumatics";
  }

  std::string GetDescription() const override {
    return "Turn ON a digital output pin by its name";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"io_device", "IOBottom"},
      {"pin_name", ""}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addDeviceSelector("io_device", "I/O Device", params.at("io_device"),
      static_cast<int>(DeviceComboBox::DeviceType::IO));

    // ✅ IMPROVED: IO pin selector (dropdown with pin names from IO device)
    widget->addIOPinSelector("pin_name", "Output Pin Name", params.at("pin_name"), "io_device", "output");
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    sequence->AddAction(params.at("io_device"), "SetOutputName", {
      {"pinName", params.at("pin_name")}
    });

    std::cout << "[IOProcesses] Built Set Output Name (ON) sequence for: " << step->userNickname << std::endl;
  }
};

//=============================================================================
// CLEAR OUTPUT NAME PLUGIN (Turn OFF by name)
//=============================================================================

class ClearOutputNamePlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Clear Output Name";
  }

  std::string GetCategory() const override {
    return "I/O & Pneumatics";
  }

  std::string GetDescription() const override {
    return "Turn OFF a digital output pin by its name";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"io_device", "IOBottom"},
      {"pin_name", ""}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addDeviceSelector("io_device", "I/O Device", params.at("io_device"),
      static_cast<int>(DeviceComboBox::DeviceType::IO));

    // ✅ IMPROVED: IO pin selector (dropdown with pin names from IO device)
    widget->addIOPinSelector("pin_name", "Output Pin Name", params.at("pin_name"), "io_device", "output");
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    sequence->AddAction(params.at("io_device"), "ClearOutputName", {
      {"pinName", params.at("pin_name")}
    });

    std::cout << "[IOProcesses] Built Clear Output Name (OFF) sequence for: " << step->userNickname << std::endl;
  }
};

//=============================================================================
// SLIDE EXTEND PLUGIN
//=============================================================================

class SlideExtendPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Slide Extend";
  }

  std::string GetCategory() const override {
    return "I/O & Pneumatics";
  }

  std::string GetDescription() const override {
    return "Extend a pneumatic slide to the extended position";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"slide_name", "UV_Head"},
      {"inbounce_delay_ms", "300"},
      {"timeout_ms", "10000"},
      {"debounce_delay_ms", "300"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addSlideSelector("slide_name", "Pneumatic Slide", params.at("slide_name"));
    widget->addParameterField("inbounce_delay_ms", "Inbounce Delay (ms)", params.at("inbounce_delay_ms"));
    widget->addParameterField("timeout_ms", "Timeout (ms)", params.at("timeout_ms"));
    widget->addParameterField("debounce_delay_ms", "Debounce Delay (ms)", params.at("debounce_delay_ms"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string slideName = params.at("slide_name");
    std::string inbounceDelay = params.at("inbounce_delay_ms");
    std::string timeout = params.at("timeout_ms");
    std::string debounceDelay = params.at("debounce_delay_ms");

    // Inbounce delay (wait before action)
    if (inbounceDelay != "0" && !inbounceDelay.empty()) {
      sequence->AddAction("system", "Wait", {
        {"milliseconds", inbounceDelay}
      });
    }

    // Extend the slide (blocking - waits for completion or timeout)
    sequence->AddAction(slideName, "Extend", {
      {"timeout_ms", timeout}
    });

    // Debounce delay (wait after action completed)
    if (debounceDelay != "0" && !debounceDelay.empty()) {
      sequence->AddAction("system", "Wait", {
        {"milliseconds", debounceDelay}
      });
    }

    std::cout << "[IOProcesses] Built Slide Extend sequence for: " << step->userNickname
              << " (Slide: " << slideName << ")" << std::endl;
  }
};

//=============================================================================
// SLIDE RETRACT PLUGIN
//=============================================================================

class SlideRetractPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Slide Retract";
  }

  std::string GetCategory() const override {
    return "I/O & Pneumatics";
  }

  std::string GetDescription() const override {
    return "Retract a pneumatic slide to the retracted position";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"slide_name", "UV_Head"},
      {"inbounce_delay_ms", "300"},
      {"timeout_ms", "10000"},
      {"debounce_delay_ms", "300"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addSlideSelector("slide_name", "Pneumatic Slide", params.at("slide_name"));
    widget->addParameterField("inbounce_delay_ms", "Inbounce Delay (ms)", params.at("inbounce_delay_ms"));
    widget->addParameterField("timeout_ms", "Timeout (ms)", params.at("timeout_ms"));
    widget->addParameterField("debounce_delay_ms", "Debounce Delay (ms)", params.at("debounce_delay_ms"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string slideName = params.at("slide_name");
    std::string inbounceDelay = params.at("inbounce_delay_ms");
    std::string timeout = params.at("timeout_ms");
    std::string debounceDelay = params.at("debounce_delay_ms");

    // Inbounce delay (wait before action)
    if (inbounceDelay != "0" && !inbounceDelay.empty()) {
      sequence->AddAction("system", "Wait", {
        {"milliseconds", inbounceDelay}
      });
    }

    // Retract the slide (blocking - waits for completion or timeout)
    sequence->AddAction(slideName, "Retract", {
      {"timeout_ms", timeout}
    });

    // Debounce delay (wait after action completed)
    if (debounceDelay != "0" && !debounceDelay.empty()) {
      sequence->AddAction("system", "Wait", {
        {"milliseconds", debounceDelay}
      });
    }

    std::cout << "[IOProcesses] Built Slide Retract sequence for: " << step->userNickname
              << " (Slide: " << slideName << ")" << std::endl;
  }
};

//=============================================================================
// AUTO-REGISTRATION
//=============================================================================

namespace {
  struct RegisterIOProcesses {
    RegisterIOProcesses() {
      auto& registry = ProcessPluginRegistry::Instance();

      registry.RegisterPlugin(std::make_unique<SetOutputPlugin>());
      registry.RegisterPlugin(std::make_unique<SetOutputNamePlugin>());
      registry.RegisterPlugin(std::make_unique<ClearOutputNamePlugin>());
      registry.RegisterPlugin(std::make_unique<SlideExtendPlugin>());
      registry.RegisterPlugin(std::make_unique<SlideRetractPlugin>());

      std::cout << "[IOProcesses] Registered 5 I/O process plugins" << std::endl;
    }
  };

  // Static instance causes registration at program startup
  static RegisterIOProcesses g_registerIOProcesses;
}
