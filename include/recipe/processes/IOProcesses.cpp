// IOProcesses.cpp
// I/O and Pneumatic process plugins: Set Output, Slide Extend, Slide Retract

#include "../IProcessPlugin.h"
#include "../ProcessPluginRegistry.h"
#include "../RecipeManager.h"  // For ProcessInstance
#include "../../sequence/SequenceBuilder.h"
#include "../../AppUI/RecipePage/ProcessConfigWidget.h"
#include "../../AppUI/RecipePage/DeviceSelectorWidgets.h"
#include "../../AppUI/RecipePage/PropertyTableWidget.h"

#include <iostream>
#include <memory>
#include <map>
#include <string>

//=============================================================================
// SET OUTPUT PLUGIN (Table-Based UI)
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

  // Use table-based UI
  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    // Fallback for compatibility
    const auto& params = instance->parameters;
    widget->addDeviceSelector("io_device", "I/O Device", params.at("io_device"),
      static_cast<int>(DeviceComboBox::DeviceType::IO));
    widget->addIOPinSelector("pin", "Output Pin", params.at("pin"), "io_device", "output");
    widget->addCheckboxParameter("state", "Output State (ON/OFF)", params.at("state"),
      "Set to ON (checked) or OFF (unchecked)");
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    table->addCategoryHeader("Device Configuration");
    table->addDeviceProperty("io_device", "I/O Device", params.at("io_device"),
      static_cast<int>(DeviceComboBox::DeviceType::IO),
      "Select the I/O device");
    table->addIOPinProperty("pin", "Output Pin", params.at("pin"),
      "io_device", "output",
      "Select the output pin to control");

    table->addCategoryHeader("Output Settings");
    table->addCheckboxProperty("state", "Output State (ON)",
      params.at("state") == "true",
      "Set to ON (checked) or OFF (unchecked)");
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    // ✅ Use "SetOutput" command which accepts pinName (not SetOutputPin which expects numeric pin)
    sequence->AddAction(params.at("io_device"), "SetOutput", {
      {"pinName", params.at("pin")},
      {"state", params.at("state")}
    });

    std::cout << "[IOProcesses] Built Set Output sequence for: " << step->userNickname << std::endl;
  }
};

//=============================================================================
// SET OUTPUT NAME PLUGIN (Turn ON by name) - Table-Based UI
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

  // Use table-based UI
  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    // Fallback for compatibility
    const auto& params = instance->parameters;
    widget->addDeviceSelector("io_device", "I/O Device", params.at("io_device"),
      static_cast<int>(DeviceComboBox::DeviceType::IO));
    widget->addIOPinSelector("pin_name", "Output Pin Name", params.at("pin_name"), "io_device", "output");
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    table->addCategoryHeader("Device Configuration");
    table->addDeviceProperty("io_device", "I/O Device", params.at("io_device"),
      static_cast<int>(DeviceComboBox::DeviceType::IO),
      "Select the I/O device");
    table->addIOPinProperty("pin_name", "Output Pin Name", params.at("pin_name"),
      "io_device", "output",
      "Select the output pin to turn ON");
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
// CLEAR OUTPUT NAME PLUGIN (Turn OFF by name) - Table-Based UI
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

  // Use table-based UI
  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    // Fallback for compatibility
    const auto& params = instance->parameters;
    widget->addDeviceSelector("io_device", "I/O Device", params.at("io_device"),
      static_cast<int>(DeviceComboBox::DeviceType::IO));
    widget->addIOPinSelector("pin_name", "Output Pin Name", params.at("pin_name"), "io_device", "output");
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    table->addCategoryHeader("Device Configuration");
    table->addDeviceProperty("io_device", "I/O Device", params.at("io_device"),
      static_cast<int>(DeviceComboBox::DeviceType::IO),
      "Select the I/O device");
    table->addIOPinProperty("pin_name", "Output Pin Name", params.at("pin_name"),
      "io_device", "output",
      "Select the output pin to turn OFF");
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
// SLIDE EXTEND PLUGIN (Table-Based UI)
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

  // Use table-based UI
  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    // Fallback for compatibility
    const auto& params = instance->parameters;
    widget->addSlideSelector("slide_name", "Pneumatic Slide", params.at("slide_name"));
    widget->addParameterField("inbounce_delay_ms", "Inbounce Delay (ms)", params.at("inbounce_delay_ms"));
    widget->addParameterField("timeout_ms", "Timeout (ms)", params.at("timeout_ms"));
    widget->addParameterField("debounce_delay_ms", "Debounce Delay (ms)", params.at("debounce_delay_ms"));
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    // Slide configuration
    table->addCategoryHeader("Slide Configuration");
    table->addSlideProperty("slide_name", "Pneumatic Slide", params.at("slide_name"),
      "Name of the pneumatic slide to extend");

    // Timing configuration
    table->addCategoryHeader("Timing Configuration");
    table->addNumericProperty("inbounce_delay_ms", "Inbounce Delay",
      std::stod(params.at("inbounce_delay_ms")),
      0, 10000, 50, 0, "ms",
      "Delay before starting extend operation");
    table->addNumericProperty("timeout_ms", "Timeout",
      std::stod(params.at("timeout_ms")),
      0, 60000, 100, 0, "ms",
      "Maximum time to wait for extend completion");
    table->addNumericProperty("debounce_delay_ms", "Debounce Delay",
      std::stod(params.at("debounce_delay_ms")),
      0, 10000, 50, 0, "ms",
      "Delay after extend completes (settling time)");
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
// SLIDE RETRACT PLUGIN (Table-Based UI)
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

  // Use table-based UI
  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    // Fallback for compatibility
    const auto& params = instance->parameters;
    widget->addSlideSelector("slide_name", "Pneumatic Slide", params.at("slide_name"));
    widget->addParameterField("inbounce_delay_ms", "Inbounce Delay (ms)", params.at("inbounce_delay_ms"));
    widget->addParameterField("timeout_ms", "Timeout (ms)", params.at("timeout_ms"));
    widget->addParameterField("debounce_delay_ms", "Debounce Delay (ms)", params.at("debounce_delay_ms"));
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    // Slide configuration
    table->addCategoryHeader("Slide Configuration");
    table->addSlideProperty("slide_name", "Pneumatic Slide", params.at("slide_name"),
      "Name of the pneumatic slide to retract");

    // Timing configuration
    table->addCategoryHeader("Timing Configuration");
    table->addNumericProperty("inbounce_delay_ms", "Inbounce Delay",
      std::stod(params.at("inbounce_delay_ms")),
      0, 10000, 50, 0, "ms",
      "Delay before starting retract operation");
    table->addNumericProperty("timeout_ms", "Timeout",
      std::stod(params.at("timeout_ms")),
      0, 60000, 100, 0, "ms",
      "Maximum time to wait for retract completion");
    table->addNumericProperty("debounce_delay_ms", "Debounce Delay",
      std::stod(params.at("debounce_delay_ms")),
      0, 10000, 50, 0, "ms",
      "Delay after retract completes (settling time)");
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
// SLIDE RETRACT ALL PLUGIN (Table-Based UI)
//=============================================================================

class SlideRetractAllPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Slide Retract All";
  }

  std::string GetCategory() const override {
    return "I/O & Pneumatics";
  }

  std::string GetDescription() const override {
    return "Retract all pneumatic slides to their retracted positions";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"inbounce_delay_ms", "300"},
      {"debounce_delay_ms", "300"}
    };
  }

  // Use table-based UI
  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;
    widget->addParameterField("inbounce_delay_ms", "Inbounce Delay (ms)", params.at("inbounce_delay_ms"));
    widget->addParameterField("debounce_delay_ms", "Debounce Delay (ms)", params.at("debounce_delay_ms"));
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    // Timing configuration
    table->addCategoryHeader("Timing Configuration");
    table->addNumericProperty("inbounce_delay_ms", "Inbounce Delay",
      std::stod(params.at("inbounce_delay_ms")),
      0, 10000, 50, 0, "ms",
      "Delay before starting retract operation");
    table->addNumericProperty("debounce_delay_ms", "Debounce Delay",
      std::stod(params.at("debounce_delay_ms")),
      0, 10000, 50, 0, "ms",
      "Delay after all slides retracted (settling time)");
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string inbounceDelay = params.at("inbounce_delay_ms");
    std::string debounceDelay = params.at("debounce_delay_ms");

    // Inbounce delay (wait before action)
    if (inbounceDelay != "0" && !inbounceDelay.empty()) {
      sequence->AddAction("system", "Wait", {
        {"milliseconds", inbounceDelay}
      });
    }

    // Retract all slides via the pneumatic manager
    sequence->AddAction("pneumatic", "RetractAll", {});

    // Debounce delay (wait after action completed)
    if (debounceDelay != "0" && !debounceDelay.empty()) {
      sequence->AddAction("system", "Wait", {
        {"milliseconds", debounceDelay}
      });
    }

    std::cout << "[IOProcesses] Built Slide Retract All sequence for: " << step->userNickname << std::endl;
  }
};

//=============================================================================
// TOGGLE OUTPUT PLUGIN (Table-Based UI)
//=============================================================================

class ToggleOutputPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Toggle Output";
  }

  std::string GetCategory() const override {
    return "I/O & Pneumatics";
  }

  std::string GetDescription() const override {
    return "Toggle digital output pin with configurable timing: inbounce delay -> toggle -> dwell -> toggle back -> outbounce delay";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"io_device", "IOBottom"},
      {"pin_name", ""},
      {"start_state", "off"},  // "off" or "on"
      {"inbounce_delay_ms", "300"},
      {"dwell_time_ms", "300"},
      {"outbounce_delay_ms", "300"}
    };
  }

  // Use table-based UI for this plugin
  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    // Fallback for non-table UI (kept for compatibility)
    const auto& params = instance->parameters;

    widget->addDeviceSelector("io_device", "I/O Device", params.at("io_device"),
      static_cast<int>(DeviceComboBox::DeviceType::IO));
    widget->addIOPinSelector("pin_name", "Output Pin", params.at("pin_name"), "io_device", "output");
    widget->addDropdownParameter("start_state", "Start State", params.at("start_state"),
      {"off", "on"});
    widget->addNumericParameter("inbounce_delay_ms", "Inbounce Delay (ms)",
      params.at("inbounce_delay_ms"), 0, 10000, 50, 0, "ms");
    widget->addNumericParameter("dwell_time_ms", "Dwell Time (ms)",
      params.at("dwell_time_ms"), 0, 10000, 50, 0, "ms");
    widget->addNumericParameter("outbounce_delay_ms", "Outbounce Delay (ms)",
      params.at("outbounce_delay_ms"), 0, 10000, 50, 0, "ms");
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    // Device configuration section
    table->addCategoryHeader("Device Configuration");
    table->addDeviceProperty("io_device", "I/O Device", params.at("io_device"),
      static_cast<int>(DeviceComboBox::DeviceType::IO),
      "Select the I/O device to control");
    table->addIOPinProperty("pin_name", "Output Pin", params.at("pin_name"),
      "io_device", "output",
      "Select the output pin to toggle");

    // Toggle settings section
    table->addCategoryHeader("Toggle Settings");
    table->addDropdownProperty("start_state", "Start State", params.at("start_state"),
      {"off", "on"},
      "Initial state before toggle sequence begins");

    // Timing section
    table->addCategoryHeader("Timing Configuration");
    table->addNumericProperty("inbounce_delay_ms", "Inbounce Delay",
      std::stod(params.at("inbounce_delay_ms")),
      0, 10000, 50, 0, "ms",
      "Delay before first toggle (debounce input)");
    table->addNumericProperty("dwell_time_ms", "Dwell Time",
      std::stod(params.at("dwell_time_ms")),
      0, 10000, 50, 0, "ms",
      "Time to hold the toggled state");
    table->addNumericProperty("outbounce_delay_ms", "Outbounce Delay",
      std::stod(params.at("outbounce_delay_ms")),
      0, 10000, 50, 0, "ms",
      "Delay after returning to original state");
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string ioDevice = params.at("io_device");
    std::string pinName = params.at("pin_name");
    std::string startState = params.at("start_state");
    std::string inbounceDelay = params.at("inbounce_delay_ms");
    std::string dwellTime = params.at("dwell_time_ms");
    std::string outbounceDelay = params.at("outbounce_delay_ms");

    // Determine toggle states based on start state
    bool firstToggleState = (startState == "off") ? true : false;  // opposite of start state
    bool secondToggleState = !firstToggleState;  // back to start state

    // Step 1: Inbounce delay
    if (inbounceDelay != "0" && !inbounceDelay.empty()) {
      sequence->AddAction("system", "Wait", {
        {"milliseconds", inbounceDelay}
      });
    }

    // Step 2: First toggle (opposite of start state)
    sequence->AddAction(ioDevice, "SetOutput", {
      {"pinName", pinName},
      {"state", firstToggleState ? "true" : "false"}
    });

    // Step 3: Dwell time
    if (dwellTime != "0" && !dwellTime.empty()) {
      sequence->AddAction("system", "Wait", {
        {"milliseconds", dwellTime}
      });
    }

    // Step 4: Second toggle (back to start state)
    sequence->AddAction(ioDevice, "SetOutput", {
      {"pinName", pinName},
      {"state", secondToggleState ? "true" : "false"}
    });

    // Step 5: Outbounce delay
    if (outbounceDelay != "0" && !outbounceDelay.empty()) {
      sequence->AddAction("system", "Wait", {
        {"milliseconds", outbounceDelay}
      });
    }

    std::cout << "[IOProcesses] Built Toggle Output sequence for: " << step->userNickname
              << " (Device: " << ioDevice << ", Pin: " << pinName
              << ", Start: " << startState << ")" << std::endl;
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
      registry.RegisterPlugin(std::make_unique<SlideRetractAllPlugin>());
      registry.RegisterPlugin(std::make_unique<ToggleOutputPlugin>());

      std::cout << "[IOProcesses] Registered 7 I/O process plugins" << std::endl;
    }
  };

  // Static instance causes registration at program startup
  static RegisterIOProcesses g_registerIOProcesses;
}
