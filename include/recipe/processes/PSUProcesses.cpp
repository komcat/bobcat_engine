// PSUProcesses.cpp
// Power Supply process plugins: PSU Output On/Off, PSU Set Voltage, PSU Set Current, PSU Read Voltage, PSU Read Current

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
    } catch (...) {}
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
// PSU OUTPUT ON/OFF PLUGIN
//=============================================================================

class PSUOutputPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "PSU Output On/Off";
  }

  std::string GetCategory() const override {
    return "Power Supply";
  }

  std::string GetDescription() const override {
    return "Turn power supply output ON or OFF";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"psu_device", "PSU1"},
      {"output_state", "on"},  // "on" or "off"
      {"channel", "1"},  // Channel number (1-4)
      {"delay_before_ms", "0"},
      {"delay_after_ms", "0"}
    };
  }

  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addParameterField("psu_device", "PSU Device Name", params.at("psu_device"));
    widget->addDropdownParameter("output_state", "Output State", params.at("output_state"), {"on", "off"});
    widget->addPSUChannelSelector("channel", "Channel", params.at("channel"), "psu_device");
    addDelayFormUI(widget, params);
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    table->addCategoryHeader("Device");
    table->addTextProperty("psu_device", "PSU Device", params.at("psu_device"),
      "Power supply device name");
    table->addDropdownProperty("output_state", "Output State", params.at("output_state"),
      {"on", "off"}, "Turn output ON or OFF");
    table->addTextProperty("channel", "Channel", params.at("channel"),
      "Output channel number (1-4)");

    addDelayTableUI(table, params);
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string psuDevice = params.at("psu_device");
    std::string outputState = params.at("output_state");
    std::string channel = params.at("channel");

    addDelayAction(sequence, getParamOr(params, "delay_before_ms", "0"));

    if (outputState == "on") {
      sequence->AddAction(psuDevice, "SetOutputOn", {{"channel", channel}});
    }
    else {
      sequence->AddAction(psuDevice, "SetOutputOff", {{"channel", channel}});
    }

    addDelayAction(sequence, getParamOr(params, "delay_after_ms", "0"));

    std::cout << "[PSUProcesses] Built PSU Output " << outputState << " sequence for: "
              << step->userNickname << std::endl;
  }
};

//=============================================================================
// PSU SET VOLTAGE PLUGIN
//=============================================================================

class PSUSetVoltagePlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "PSU Set Voltage";
  }

  std::string GetCategory() const override {
    return "Power Supply";
  }

  std::string GetDescription() const override {
    return "Set power supply output voltage (CV mode)";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"psu_device", "PSU1"},
      {"voltage", "3.0"},
      {"channel", "1"},  // Channel number (1-4)
      {"delay_before_ms", "0"},
      {"delay_after_ms", "0"}
    };
  }

  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addParameterField("psu_device", "PSU Device Name", params.at("psu_device"));
    widget->addParameterField("voltage", "Voltage (V)", params.at("voltage"));
    widget->addPSUChannelSelector("channel", "Channel", params.at("channel"), "psu_device");
    addDelayFormUI(widget, params);
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    table->addCategoryHeader("Device");
    table->addTextProperty("psu_device", "PSU Device", params.at("psu_device"),
      "Power supply device name");
    table->addTextProperty("channel", "Channel", params.at("channel"),
      "Output channel number (1-4)");

    table->addCategoryHeader("Voltage");
    table->addNumericProperty("voltage", "Voltage",
      std::stod(params.at("voltage")),
      0, 60, 0.1, 2, "V",
      "Output voltage setpoint");

    addDelayTableUI(table, params);
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string psuDevice = params.at("psu_device");
    std::string voltage = params.at("voltage");
    std::string channel = params.at("channel");

    addDelayAction(sequence, getParamOr(params, "delay_before_ms", "0"));

    sequence->AddAction(psuDevice, "SetVoltage", {
      {"voltage", voltage},
      {"channel", channel}
    });

    addDelayAction(sequence, getParamOr(params, "delay_after_ms", "0"));

    std::cout << "[PSUProcesses] Built PSU Set Voltage sequence for: " << step->userNickname
              << " (Device: " << psuDevice << ", Voltage: " << voltage << "V)" << std::endl;
  }
};

//=============================================================================
// PSU SET CURRENT PLUGIN
//=============================================================================

class PSUSetCurrentPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "PSU Set Current";
  }

  std::string GetCategory() const override {
    return "Power Supply";
  }

  std::string GetDescription() const override {
    return "Set power supply output current limit (CC mode)";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"psu_device", "PSU1"},
      {"current", "150"},
      {"channel", "1"},  // Channel number (1-4)
      {"delay_before_ms", "0"},
      {"delay_after_ms", "0"}
    };
  }

  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addParameterField("psu_device", "PSU Device Name", params.at("psu_device"));
    widget->addParameterField("current", "Current (mA)", params.at("current"));
    widget->addPSUChannelSelector("channel", "Channel", params.at("channel"), "psu_device");
    addDelayFormUI(widget, params);
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    table->addCategoryHeader("Device");
    table->addTextProperty("psu_device", "PSU Device", params.at("psu_device"),
      "Power supply device name");
    table->addTextProperty("channel", "Channel", params.at("channel"),
      "Output channel number (1-4)");

    table->addCategoryHeader("Current");
    table->addNumericProperty("current", "Current",
      std::stod(params.at("current")),
      0, 5000, 1, 0, "mA",
      "Output current limit");

    addDelayTableUI(table, params);
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string psuDevice = params.at("psu_device");
    std::string current = params.at("current");
    std::string channel = params.at("channel");

    addDelayAction(sequence, getParamOr(params, "delay_before_ms", "0"));

    sequence->AddAction(psuDevice, "SetCurrent", {
      {"current", current},
      {"channel", channel}
    });

    addDelayAction(sequence, getParamOr(params, "delay_after_ms", "0"));

    std::cout << "[PSUProcesses] Built PSU Set Current sequence for: " << step->userNickname
              << " (Device: " << psuDevice << ", Current: " << current << "mA)" << std::endl;
  }
};

//=============================================================================
// PSU READ VOLTAGE PLUGIN
//=============================================================================

class PSUReadVoltagePlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "PSU Read Voltage";
  }

  std::string GetCategory() const override {
    return "Power Supply";
  }

  std::string GetDescription() const override {
    return "Read current voltage from power supply (multiple readings)";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"psu_device", "PSU1"},
      {"read_count", "3"},
      {"delay_ms", "100"},
      {"channel", "1"},  // Channel number (1-4)
      {"captureAs", ""},  // Variable name to store result (optional)
      {"delay_before_ms", "0"},
      {"delay_after_ms", "0"}
    };
  }

  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addParameterField("psu_device", "PSU Device Name", params.at("psu_device"));
    widget->addParameterField("read_count", "Read Count", params.at("read_count"));
    widget->addParameterField("delay_ms", "Delay Between Reads (ms)", params.at("delay_ms"));
    widget->addPSUChannelSelector("channel", "Channel", params.at("channel"), "psu_device");
    widget->addParameterField("captureAs", "Capture As (Variable Name)",
      getParamOr(params, "captureAs", ""));
    addDelayFormUI(widget, params);
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    table->addCategoryHeader("Device");
    table->addTextProperty("psu_device", "PSU Device", params.at("psu_device"),
      "Power supply device name");
    table->addTextProperty("channel", "Channel", params.at("channel"),
      "Output channel number (1-4)");

    table->addCategoryHeader("Read Settings");
    table->addNumericProperty("read_count", "Read Count",
      std::stod(params.at("read_count")),
      1, 100, 1, 0, "",
      "Number of voltage readings to take");
    table->addNumericProperty("delay_ms", "Delay Between Reads",
      std::stod(params.at("delay_ms")),
      0, 10000, 50, 0, "ms",
      "Delay between consecutive readings");

    table->addCategoryHeader("Result Capture");
    table->addTextProperty("captureAs", "Capture As",
      getParamOr(params, "captureAs", ""),
      "Context variable name to store result (leave empty to disable)");

    addDelayTableUI(table, params);
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string psuDevice = params.at("psu_device");
    std::string readCount = params.at("read_count");
    std::string delayMs = params.at("delay_ms");
    std::string channel = params.at("channel");

    addDelayAction(sequence, getParamOr(params, "delay_before_ms", "0"));

    // Build action parameters
    std::map<std::string, std::string> actionParams = {
      {"read_count", readCount},
      {"delay_ms", delayMs},
      {"channel", channel}
    };

    // Add optional captureAs parameter if present
    auto captureIt = params.find("captureAs");
    if (captureIt != params.end() && !captureIt->second.empty()) {
      actionParams["captureAs"] = captureIt->second;
    }

    sequence->AddAction(psuDevice, "ReadVoltageMultiple", actionParams);

    addDelayAction(sequence, getParamOr(params, "delay_after_ms", "0"));

    std::cout << "[PSUProcesses] Built PSU Read Voltage sequence for: " << step->userNickname << std::endl;
  }
};

//=============================================================================
// PSU READ CURRENT PLUGIN
//=============================================================================

class PSUReadCurrentPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "PSU Read Current";
  }

  std::string GetCategory() const override {
    return "Power Supply";
  }

  std::string GetDescription() const override {
    return "Read current from power supply (multiple readings)";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"psu_device", "PSU1"},
      {"read_count", "3"},
      {"delay_ms", "100"},
      {"channel", "1"},  // Channel number (1-4)
      {"captureAs", ""},  // Variable name to store result (optional)
      {"delay_before_ms", "0"},
      {"delay_after_ms", "0"}
    };
  }

  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addParameterField("psu_device", "PSU Device Name", params.at("psu_device"));
    widget->addParameterField("read_count", "Read Count", params.at("read_count"));
    widget->addParameterField("delay_ms", "Delay Between Reads (ms)", params.at("delay_ms"));
    widget->addPSUChannelSelector("channel", "Channel", params.at("channel"), "psu_device");
    widget->addParameterField("captureAs", "Capture As (Variable Name)",
      getParamOr(params, "captureAs", ""));
    addDelayFormUI(widget, params);
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    table->addCategoryHeader("Device");
    table->addTextProperty("psu_device", "PSU Device", params.at("psu_device"),
      "Power supply device name");
    table->addTextProperty("channel", "Channel", params.at("channel"),
      "Output channel number (1-4)");

    table->addCategoryHeader("Read Settings");
    table->addNumericProperty("read_count", "Read Count",
      std::stod(params.at("read_count")),
      1, 100, 1, 0, "",
      "Number of current readings to take");
    table->addNumericProperty("delay_ms", "Delay Between Reads",
      std::stod(params.at("delay_ms")),
      0, 10000, 50, 0, "ms",
      "Delay between consecutive readings");

    table->addCategoryHeader("Result Capture");
    table->addTextProperty("captureAs", "Capture As",
      getParamOr(params, "captureAs", ""),
      "Context variable name to store result (leave empty to disable)");

    addDelayTableUI(table, params);
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string psuDevice = params.at("psu_device");
    std::string readCount = params.at("read_count");
    std::string delayMs = params.at("delay_ms");
    std::string channel = params.at("channel");

    addDelayAction(sequence, getParamOr(params, "delay_before_ms", "0"));

    // Build action parameters
    std::map<std::string, std::string> actionParams = {
      {"read_count", readCount},
      {"delay_ms", delayMs},
      {"channel", channel}
    };

    // Add optional captureAs parameter if present
    auto captureIt = params.find("captureAs");
    if (captureIt != params.end() && !captureIt->second.empty()) {
      actionParams["captureAs"] = captureIt->second;
    }

    sequence->AddAction(psuDevice, "ReadCurrentMultiple", actionParams);

    addDelayAction(sequence, getParamOr(params, "delay_after_ms", "0"));

    std::cout << "[PSUProcesses] Built PSU Read Current sequence for: " << step->userNickname << std::endl;
  }
};

//=============================================================================
// PSU CONNECT PLUGIN
//=============================================================================

class PSUConnectPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "PSU Connect";
  }

  std::string GetCategory() const override {
    return "Power Supply";
  }

  std::string GetDescription() const override {
    return "Connect to a power supply device";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"psu_device", "PSU1"},
      {"delay_before_ms", "0"},
      {"delay_after_ms", "0"}
    };
  }

  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;
    widget->addParameterField("psu_device", "PSU Device Name", params.at("psu_device"));
    addDelayFormUI(widget, params);
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    table->addCategoryHeader("Device");
    table->addTextProperty("psu_device", "PSU Device", params.at("psu_device"),
      "Power supply device name to connect");

    addDelayTableUI(table, params);
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;
    std::string device = params.at("psu_device");

    addDelayAction(sequence, getParamOr(params, "delay_before_ms", "0"));
    sequence->AddAction(device, "Connect", {});
    addDelayAction(sequence, getParamOr(params, "delay_after_ms", "0"));

    std::cout << "[PSUProcesses] Built PSU Connect for: " << device << std::endl;
  }
};

//=============================================================================
// PSU DISCONNECT PLUGIN
//=============================================================================

class PSUDisconnectPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "PSU Disconnect";
  }

  std::string GetCategory() const override {
    return "Power Supply";
  }

  std::string GetDescription() const override {
    return "Disconnect from a power supply device";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"psu_device", "PSU1"},
      {"delay_before_ms", "0"},
      {"delay_after_ms", "0"}
    };
  }

  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;
    widget->addParameterField("psu_device", "PSU Device Name", params.at("psu_device"));
    addDelayFormUI(widget, params);
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    table->addCategoryHeader("Device");
    table->addTextProperty("psu_device", "PSU Device", params.at("psu_device"),
      "Power supply device name to disconnect");

    addDelayTableUI(table, params);
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;
    std::string device = params.at("psu_device");

    addDelayAction(sequence, getParamOr(params, "delay_before_ms", "0"));
    sequence->AddAction(device, "Disconnect", {});
    addDelayAction(sequence, getParamOr(params, "delay_after_ms", "0"));

    std::cout << "[PSUProcesses] Built PSU Disconnect for: " << device << std::endl;
  }
};

//=============================================================================
// PSU CONFIGURE PLUGIN - One-step preset: connect + set V/I + enable
//=============================================================================

class PSUConfigurePlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "PSU Configure";
  }

  std::string GetCategory() const override {
    return "Power Supply";
  }

  std::string GetDescription() const override {
    return "Configure power supply in one step: connect, set mode (CC/CV), "
           "set voltage & current, and optionally enable output";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"psu_device", "PSU1"},
      {"channel", "1"},
      {"mode", "CC"},           // CC = Constant Current, CV = Constant Voltage
      {"voltage", "2.0"},       // CC: compliance voltage, CV: voltage setpoint
      {"current", "400"},       // CC: current setpoint, CV: current compliance limit
      {"enable_output", "true"},
      {"delay_before_ms", "0"},
      {"delay_after_ms", "0"}
    };
  }

  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addParameterField("psu_device", "PSU Device Name", params.at("psu_device"));
    widget->addPSUChannelSelector("channel", "Channel", params.at("channel"), "psu_device");
    widget->addDropdownParameter("mode", "Mode", getParamOr(params, "mode", "CC"), {"CC", "CV"});
    widget->addParameterField("voltage", "Voltage (V)", params.at("voltage"));
    widget->addParameterField("current", "Current (mA)", params.at("current"));
    widget->addCheckboxParameter("enable_output", "Enable Output",
      getParamOr(params, "enable_output", "true"),
      "Turn output ON after configuring");
    addDelayFormUI(widget, params);
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    table->addCategoryHeader("Device");
    table->addTextProperty("psu_device", "PSU Device", params.at("psu_device"),
      "Power supply device name");
    table->addTextProperty("channel", "Channel", params.at("channel"),
      "Output channel number (1-4)");

    table->addCategoryHeader("Mode");
    table->addDropdownProperty("mode", "Operating Mode", getParamOr(params, "mode", "CC"),
      {"CC", "CV"},
      "CC = Constant Current (current is setpoint, voltage is compliance limit). "
      "CV = Constant Voltage (voltage is setpoint, current is compliance limit)");

    table->addCategoryHeader("Configuration");
    table->addNumericProperty("voltage", "Voltage",
      std::stod(params.at("voltage")),
      0, 60, 0.1, 2, "V",
      "CC mode: compliance voltage limit. CV mode: voltage setpoint");
    table->addNumericProperty("current", "Current",
      std::stod(params.at("current")),
      0, 5000, 1, 0, "mA",
      "CC mode: current setpoint. CV mode: compliance current limit");

    table->addCategoryHeader("Output");
    table->addCheckboxProperty("enable_output", "Enable Output",
      getParamOr(params, "enable_output", "true") == "true",
      "Turn output ON after configuring voltage and current");

    addDelayTableUI(table, params);
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string psuDevice = params.at("psu_device");
    std::string channel = params.at("channel");
    std::string mode = getParamOr(params, "mode", "CC");
    std::string voltage = params.at("voltage");
    std::string current = params.at("current");
    std::string enableOutput = getParamOr(params, "enable_output", "true");

    // 1. Delay before
    addDelayAction(sequence, getParamOr(params, "delay_before_ms", "0"));

    // 2. Connect (safe if already connected - handler auto-detects)
    sequence->AddAction(psuDevice, "Connect", {});

    // 3. Set voltage and current
    // In CC mode: set voltage first (compliance), then current (setpoint)
    // In CV mode: set current first (compliance), then voltage (setpoint)
    // The PSU hardware determines actual mode based on load, but setting
    // the compliance limit first prevents unexpected overcurrent/overvoltage
    if (mode == "CV") {
      // CV: current compliance first, then voltage setpoint
      sequence->AddAction(psuDevice, "SetCurrent", {
        {"current", current},
        {"channel", channel}
      });
      sequence->AddAction(psuDevice, "SetVoltage", {
        {"voltage", voltage},
        {"channel", channel}
      });
    } else {
      // CC (default): voltage compliance first, then current setpoint
      sequence->AddAction(psuDevice, "SetVoltage", {
        {"voltage", voltage},
        {"channel", channel}
      });
      sequence->AddAction(psuDevice, "SetCurrent", {
        {"current", current},
        {"channel", channel}
      });
    }

    // 4. Enable output (optional)
    if (enableOutput == "true" || enableOutput == "1") {
      sequence->AddAction(psuDevice, "SetOutputOn", {{"channel", channel}});
    }

    // 5. Delay after
    addDelayAction(sequence, getParamOr(params, "delay_after_ms", "0"));

    std::cout << "[PSUProcesses] Built PSU Configure (" << mode << ") for: " << step->userNickname
              << " (Device: " << psuDevice << ", Ch: " << channel
              << ", " << voltage << "V / " << current << "mA"
              << ", Output: " << enableOutput << ")" << std::endl;
  }
};

//=============================================================================
// AUTO-REGISTRATION
//=============================================================================

namespace {
  struct RegisterPSUProcesses {
    RegisterPSUProcesses() {
      auto& registry = ProcessPluginRegistry::Instance();

      registry.RegisterPlugin(std::make_unique<PSUConfigurePlugin>());
      registry.RegisterPlugin(std::make_unique<PSUConnectPlugin>());
      registry.RegisterPlugin(std::make_unique<PSUDisconnectPlugin>());
      registry.RegisterPlugin(std::make_unique<PSUOutputPlugin>());
      registry.RegisterPlugin(std::make_unique<PSUSetVoltagePlugin>());
      registry.RegisterPlugin(std::make_unique<PSUSetCurrentPlugin>());
      registry.RegisterPlugin(std::make_unique<PSUReadVoltagePlugin>());
      registry.RegisterPlugin(std::make_unique<PSUReadCurrentPlugin>());

      std::cout << "[PSUProcesses] Registered 8 PSU process plugins" << std::endl;
    }
  };

  // Static instance causes registration at program startup
  static RegisterPSUProcesses g_registerPSUProcesses;
}
