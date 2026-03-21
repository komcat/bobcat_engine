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
#include <cmath>

namespace {
  // Safe parameter access with fallback (backward compatibility for old recipes)
  std::string getParamOr(const std::map<std::string, std::string>& params,
                          const std::string& key, const std::string& defaultValue) {
    auto it = params.find(key);
    return (it != params.end()) ? it->second : defaultValue;
  }

  // Helper to add a delay/wait action (if ms > 0)
  void addDelayAction(SequenceBuilder* sequence, const std::string& msStr) {
    if (msStr.empty() || msStr == "0") return;
    try {
      int ms = static_cast<int>(std::stod(msStr));
      if (ms <= 0) return;
      sequence->AddAction("system", "Wait", {{"milliseconds", std::to_string(ms)}});
    } catch (...) {
      // Invalid value, skip delay
    }
  }

  // Helper to add delay table UI entries
  void addDelayTableUI(PropertyTableWidget* table,
                        const std::map<std::string, std::string>& params) {
    table->addCategoryHeader("Timing");
    table->addNumericProperty("delay_before_ms", "Delay Before",
      std::stod(getParamOr(params, "delay_before_ms", "0")),
      0, 60000, 100, 0, "ms",
      "Wait before executing the action (0 = no delay)");
    table->addNumericProperty("delay_after_ms", "Delay After",
      std::stod(getParamOr(params, "delay_after_ms", "0")),
      0, 60000, 100, 0, "ms",
      "Wait after the action completes (0 = no delay)");
  }

  // Helper to add delay form UI entries
  void addDelayFormUI(ProcessConfigWidget* widget,
                       const std::map<std::string, std::string>& params) {
    widget->addNumericParameter("delay_before_ms", "Delay Before (ms)",
      getParamOr(params, "delay_before_ms", "0"), 0, 60000, 100, 0, "ms");
    widget->addNumericParameter("delay_after_ms", "Delay After (ms)",
      getParamOr(params, "delay_after_ms", "0"), 0, 60000, 100, 0, "ms");
  }
}

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
      {"state", "true"},  // "true" for ON, "false" for OFF
      {"delay_before_ms", "0"},
      {"delay_after_ms", "0"}
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
    addDelayFormUI(widget, params);
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

    addDelayTableUI(table, params);
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    addDelayAction(sequence, getParamOr(params, "delay_before_ms", "0"));

    // Use "SetOutput" command which accepts pinName (not SetOutputPin which expects numeric pin)
    sequence->AddAction(params.at("io_device"), "SetOutput", {
      {"pinName", params.at("pin")},
      {"state", params.at("state")}
    });

    addDelayAction(sequence, getParamOr(params, "delay_after_ms", "0"));

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
      {"pin_name", ""},
      {"delay_before_ms", "0"},
      {"delay_after_ms", "0"}
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
    addDelayFormUI(widget, params);
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

    addDelayTableUI(table, params);
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    addDelayAction(sequence, getParamOr(params, "delay_before_ms", "0"));

    sequence->AddAction(params.at("io_device"), "SetOutputName", {
      {"pinName", params.at("pin_name")}
    });

    addDelayAction(sequence, getParamOr(params, "delay_after_ms", "0"));

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
      {"pin_name", ""},
      {"delay_before_ms", "0"},
      {"delay_after_ms", "0"}
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
    addDelayFormUI(widget, params);
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

    addDelayTableUI(table, params);
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    addDelayAction(sequence, getParamOr(params, "delay_before_ms", "0"));

    sequence->AddAction(params.at("io_device"), "ClearOutputName", {
      {"pinName", params.at("pin_name")}
    });

    addDelayAction(sequence, getParamOr(params, "delay_after_ms", "0"));

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
    const auto& params = instance->parameters;
    widget->addSlideSelector("slide_name", "Pneumatic Slide", params.at("slide_name"));
    widget->addParameterField("inbounce_delay_ms", "Inbounce Delay (ms)",
      getParamOr(params, "inbounce_delay_ms", "300"));
    widget->addParameterField("timeout_ms", "Timeout (ms)",
      getParamOr(params, "timeout_ms", "10000"));
    widget->addParameterField("debounce_delay_ms", "Debounce Delay (ms)",
      getParamOr(params, "debounce_delay_ms", "300"));
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
      std::stod(getParamOr(params, "inbounce_delay_ms", "300")),
      0, 10000, 50, 0, "ms",
      "Delay before starting extend operation");
    table->addNumericProperty("timeout_ms", "Timeout",
      std::stod(getParamOr(params, "timeout_ms", "10000")),
      0, 60000, 100, 0, "ms",
      "Maximum time to wait for extend completion");
    table->addNumericProperty("debounce_delay_ms", "Debounce Delay",
      std::stod(getParamOr(params, "debounce_delay_ms", "300")),
      0, 10000, 50, 0, "ms",
      "Delay after extend completes (settling time)");
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string slideName = params.at("slide_name");
    std::string inbounceDelay = getParamOr(params, "inbounce_delay_ms", "300");
    std::string timeout = getParamOr(params, "timeout_ms", "10000");
    std::string debounceDelay = getParamOr(params, "debounce_delay_ms", "300");

    // Inbounce delay (wait before action)
    addDelayAction(sequence, inbounceDelay);

    // Extend the slide (blocking - waits for completion or timeout)
    sequence->AddAction(slideName, "Extend", {
      {"timeout_ms", timeout}
    });

    // Debounce delay (wait after action completed)
    addDelayAction(sequence, debounceDelay);

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
    const auto& params = instance->parameters;
    widget->addSlideSelector("slide_name", "Pneumatic Slide", params.at("slide_name"));
    widget->addParameterField("inbounce_delay_ms", "Inbounce Delay (ms)",
      getParamOr(params, "inbounce_delay_ms", "300"));
    widget->addParameterField("timeout_ms", "Timeout (ms)",
      getParamOr(params, "timeout_ms", "10000"));
    widget->addParameterField("debounce_delay_ms", "Debounce Delay (ms)",
      getParamOr(params, "debounce_delay_ms", "300"));
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
      std::stod(getParamOr(params, "inbounce_delay_ms", "300")),
      0, 10000, 50, 0, "ms",
      "Delay before starting retract operation");
    table->addNumericProperty("timeout_ms", "Timeout",
      std::stod(getParamOr(params, "timeout_ms", "10000")),
      0, 60000, 100, 0, "ms",
      "Maximum time to wait for retract completion");
    table->addNumericProperty("debounce_delay_ms", "Debounce Delay",
      std::stod(getParamOr(params, "debounce_delay_ms", "300")),
      0, 10000, 50, 0, "ms",
      "Delay after retract completes (settling time)");
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string slideName = params.at("slide_name");
    std::string inbounceDelay = getParamOr(params, "inbounce_delay_ms", "300");
    std::string timeout = getParamOr(params, "timeout_ms", "10000");
    std::string debounceDelay = getParamOr(params, "debounce_delay_ms", "300");

    // Inbounce delay (wait before action)
    addDelayAction(sequence, inbounceDelay);

    // Retract the slide (blocking - waits for completion or timeout)
    sequence->AddAction(slideName, "Retract", {
      {"timeout_ms", timeout}
    });

    // Debounce delay (wait after action completed)
    addDelayAction(sequence, debounceDelay);

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
    widget->addParameterField("inbounce_delay_ms", "Inbounce Delay (ms)",
      getParamOr(params, "inbounce_delay_ms", "300"));
    widget->addParameterField("debounce_delay_ms", "Debounce Delay (ms)",
      getParamOr(params, "debounce_delay_ms", "300"));
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    // Timing configuration
    table->addCategoryHeader("Timing Configuration");
    table->addNumericProperty("inbounce_delay_ms", "Inbounce Delay",
      std::stod(getParamOr(params, "inbounce_delay_ms", "300")),
      0, 10000, 50, 0, "ms",
      "Delay before starting retract operation");
    table->addNumericProperty("debounce_delay_ms", "Debounce Delay",
      std::stod(getParamOr(params, "debounce_delay_ms", "300")),
      0, 10000, 50, 0, "ms",
      "Delay after all slides retracted (settling time)");
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string inbounceDelay = getParamOr(params, "inbounce_delay_ms", "300");
    std::string debounceDelay = getParamOr(params, "debounce_delay_ms", "300");

    // Inbounce delay (wait before action)
    addDelayAction(sequence, inbounceDelay);

    // Retract all slides via the pneumatic manager
    sequence->AddAction("pneumatic", "RetractAll", {});

    // Debounce delay (wait after action completed)
    addDelayAction(sequence, debounceDelay);

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
    const auto& params = instance->parameters;

    widget->addDeviceSelector("io_device", "I/O Device", params.at("io_device"),
      static_cast<int>(DeviceComboBox::DeviceType::IO));
    widget->addIOPinSelector("pin_name", "Output Pin", params.at("pin_name"), "io_device", "output");
    widget->addDropdownParameter("start_state", "Start State",
      getParamOr(params, "start_state", "off"), {"off", "on"});
    widget->addNumericParameter("inbounce_delay_ms", "Inbounce Delay (ms)",
      getParamOr(params, "inbounce_delay_ms", "300"), 0, 10000, 50, 0, "ms");
    widget->addNumericParameter("dwell_time_ms", "Dwell Time (ms)",
      getParamOr(params, "dwell_time_ms", "300"), 0, 10000, 50, 0, "ms");
    widget->addNumericParameter("outbounce_delay_ms", "Outbounce Delay (ms)",
      getParamOr(params, "outbounce_delay_ms", "300"), 0, 10000, 50, 0, "ms");
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
    table->addDropdownProperty("start_state", "Start State",
      getParamOr(params, "start_state", "off"),
      {"off", "on"},
      "Initial state before toggle sequence begins");

    // Timing section
    table->addCategoryHeader("Timing Configuration");
    table->addNumericProperty("inbounce_delay_ms", "Inbounce Delay",
      std::stod(getParamOr(params, "inbounce_delay_ms", "300")),
      0, 10000, 50, 0, "ms",
      "Delay before first toggle (debounce input)");
    table->addNumericProperty("dwell_time_ms", "Dwell Time",
      std::stod(getParamOr(params, "dwell_time_ms", "300")),
      0, 10000, 50, 0, "ms",
      "Time to hold the toggled state");
    table->addNumericProperty("outbounce_delay_ms", "Outbounce Delay",
      std::stod(getParamOr(params, "outbounce_delay_ms", "300")),
      0, 10000, 50, 0, "ms",
      "Delay after returning to original state");
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string ioDevice = params.at("io_device");
    std::string pinName = params.at("pin_name");
    std::string startState = getParamOr(params, "start_state", "off");
    std::string inbounceDelay = getParamOr(params, "inbounce_delay_ms", "300");
    std::string dwellTime = getParamOr(params, "dwell_time_ms", "300");
    std::string outbounceDelay = getParamOr(params, "outbounce_delay_ms", "300");

    // Determine toggle states based on start state
    bool firstToggleState = (startState == "off") ? true : false;  // opposite of start state
    bool secondToggleState = !firstToggleState;  // back to start state

    // Step 1: Inbounce delay
    addDelayAction(sequence, inbounceDelay);

    // Step 2: First toggle (opposite of start state)
    sequence->AddAction(ioDevice, "SetOutput", {
      {"pinName", pinName},
      {"state", firstToggleState ? "true" : "false"}
    });

    // Step 3: Dwell time
    addDelayAction(sequence, dwellTime);

    // Step 4: Second toggle (back to start state)
    sequence->AddAction(ioDevice, "SetOutput", {
      {"pinName", pinName},
      {"state", secondToggleState ? "true" : "false"}
    });

    // Step 5: Outbounce delay
    addDelayAction(sequence, outbounceDelay);

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
