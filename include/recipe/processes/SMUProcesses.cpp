// SMUProcesses.cpp - SMU (Source Measure Unit) process plugins
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

namespace {
// Safe parameter access - returns defaultValue if key not found (for backward compat with old recipes)
std::string getParam(const std::map<std::string, std::string>& params,
                     const std::string& key,
                     const std::string& defaultValue = "") {
  auto it = params.find(key);
  return (it != params.end()) ? it->second : defaultValue;
}
} // namespace

//=============================================================================
// SMU START POLLING PLUGIN
//=============================================================================

class SMUStartPollingPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "SMU Start Polling";
  }

  std::string GetCategory() const override {
    return "SMU Control";
  }

  std::string GetDescription() const override {
    return "Start continuous current/voltage measurement polling on SMU device";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "Keithley_6482"},  // ✅ FIXED: Hardcoded default like IO processes
      {"mode", "ACCURATE"},
      {"intervalMs", "100"},
      {"channel", "1"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    // SMU device selector (dropdown with available devices)
    widget->addDeviceSelector("device", "SMU Device", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::SMU));

    // Measurement mode dropdown
    widget->addParameterField("mode", "Mode (ACCURATE/FAST/ULTRA_FAST)", params.at("mode"));

    // Polling interval
    widget->addNumericParameter("intervalMs", "Polling Interval (ms)",
      params.at("intervalMs"),
      10,      // min
      1000,    // max
      10,      // step
      0,       // decimals (integer)
      "ms"     // suffix
    );

    // Channel number
    widget->addNumericParameter("channel", "Channel",
      getParam(params, "channel", "1"),
      1,       // min
      4,       // max
      1,       // step
      0,       // decimals (integer)
      ""       // suffix
    );
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string device = params.at("device");
    std::string mode = params.at("mode");
    std::string intervalMs = params.at("intervalMs");
    std::string channel = getParam(params, "channel", "1");

    // ✅ DEBUG: Log all parameters
    std::cout << "[SMUStartPollingPlugin] BuildSequence called for: " << step->userNickname << std::endl;
    std::cout << "[SMUStartPollingPlugin]   device='" << device << "'" << std::endl;
    std::cout << "[SMUStartPollingPlugin]   mode='" << mode << "'" << std::endl;
    std::cout << "[SMUStartPollingPlugin]   intervalMs='" << intervalMs << "'" << std::endl;
    std::cout << "[SMUStartPollingPlugin]   channel='" << channel << "'" << std::endl;

    if (device.empty()) {
      std::cerr << "[SMUStartPollingPlugin] WARNING: device parameter is EMPTY!" << std::endl;
    }

    sequence->AddAction(device, "StartPolling", {
      {"mode", mode},
      {"intervalMs", intervalMs},
      {"channel", channel}
    });

    std::cout << "[SMUStartPollingPlugin] Built StartPolling sequence for: "
              << step->userNickname << " on device " << device << std::endl;
  }
};

//=============================================================================
// SMU STOP POLLING PLUGIN
//=============================================================================

class SMUStopPollingPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "SMU Stop Polling";
  }

  std::string GetCategory() const override {
    return "SMU Control";
  }

  std::string GetDescription() const override {
    return "Stop continuous measurement polling on SMU device";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "Keithley_6482"}  // ✅ FIXED: Hardcoded default like IO processes
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    // SMU device selector (dropdown with available devices)
    widget->addDeviceSelector("device", "SMU Device", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::SMU));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string device = params.at("device");

    // ✅ DEBUG: Log parameters
    std::cout << "[SMUStopPollingPlugin] BuildSequence called for: " << step->userNickname << std::endl;
    std::cout << "[SMUStopPollingPlugin]   device='" << device << "'" << std::endl;

    if (device.empty()) {
      std::cerr << "[SMUStopPollingPlugin] WARNING: device parameter is EMPTY!" << std::endl;
    }

    sequence->AddAction(device, "StopPolling", {});

    std::cout << "[SMUStopPollingPlugin] Built StopPolling sequence for: "
              << step->userNickname << " on device " << device << std::endl;
  }
};

//=============================================================================
// SMU READ CURRENT PLUGIN
//=============================================================================

class SMUReadCurrentPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "SMU Read Current";
  }

  std::string GetCategory() const override {
    return "SMU Control";
  }

  std::string GetDescription() const override {
    return "Read current measurement from SMU device (single shot)";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "Keithley_6482"},  // ✅ FIXED: Hardcoded default like IO processes
      {"channel", "1"},
      {"captureAs", ""}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    // SMU device selector (dropdown with available devices)
    widget->addDeviceSelector("device", "SMU Device", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::SMU));

    // Channel number
    widget->addNumericParameter("channel", "Channel",
      getParam(params, "channel", "1"),
      1,       // min
      4,       // max
      1,       // step
      0,       // decimals (integer)
      ""       // suffix
    );

    // Optional: Capture variable name
    widget->addParameterField("captureAs", "Capture As (Variable Name)", params.at("captureAs"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string device = params.at("device");
    std::string channel = getParam(params, "channel", "1");
    std::string captureAs = params.at("captureAs");

    // ✅ DEBUG: Log parameters
    std::cout << "[SMUReadCurrentPlugin] BuildSequence called for: " << step->userNickname << std::endl;
    std::cout << "[SMUReadCurrentPlugin]   device='" << device << "'" << std::endl;
    std::cout << "[SMUReadCurrentPlugin]   channel='" << channel << "'" << std::endl;
    std::cout << "[SMUReadCurrentPlugin]   captureAs='" << captureAs << "'" << std::endl;

    if (device.empty()) {
      std::cerr << "[SMUReadCurrentPlugin] WARNING: device parameter is EMPTY!" << std::endl;
    }

    std::map<std::string, std::string> actionParams = {
      {"channel", channel}
    };

    if (!captureAs.empty()) {
      actionParams["captureAs"] = captureAs;
    }

    sequence->AddAction(device, "ReadCurrent", actionParams);

    std::cout << "[SMUReadCurrentPlugin] Built ReadCurrent sequence for: "
              << step->userNickname << " on device " << device
              << " channel " << channel << std::endl;
  }
};

//=============================================================================
// SMU READ VOLTAGE PLUGIN
//=============================================================================

class SMUReadVoltagePlugin : public IProcessPlugin
{
public:
  std::string GetName() const override { return "SMU Read Voltage"; }
  std::string GetCategory() const override { return "SMU Control"; }
  std::string GetDescription() const override {
    return "Read single voltage measurement from SMU device";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "Keithley_6482"},
      {"channel", "1"},
      {"captureAs", ""}  // Optional: store result with custom variable name
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addDeviceSelector("device", "SMU Device", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::SMU));

    // Channel number
    widget->addNumericParameter("channel", "Channel",
      getParam(params, "channel", "1"),
      1,       // min
      4,       // max
      1,       // step
      0,       // decimals (integer)
      ""       // suffix
    );

    widget->addParameterField("captureAs", "Capture As (optional)", params.at("captureAs"));
  }

  void BuildSequence(SequenceBuilder* seq, const ProcessInstance* step) override {
    std::string device = step->parameters.at("device");
    std::string channel = getParam(step->parameters, "channel", "1");
    std::string captureAs = step->parameters.at("captureAs");

    std::map<std::string, std::string> params = {
      {"channel", channel}
    };
    if (!captureAs.empty()) {
      params["captureAs"] = captureAs;
    }

    seq->AddAction(device, "ReadVoltage", params);

    std::cout << "[SMUReadVoltagePlugin] Built sequence: ReadVoltage on " << device
              << " channel " << channel << std::endl;
  }
};

//=============================================================================
// SMU SET VOLTAGE PLUGIN
//=============================================================================

class SMUSetVoltagePlugin : public IProcessPlugin
{
public:
  std::string GetName() const override { return "SMU Set Voltage"; }
  std::string GetCategory() const override { return "SMU Control"; }
  std::string GetDescription() const override {
    return "Set source voltage on SMU device (for voltage source devices)";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "Keithley_6482"},
      {"channel", "1"},
      {"voltage", "0.0"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addDeviceSelector("device", "SMU Device", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::SMU));

    // Channel number
    widget->addNumericParameter("channel", "Channel",
      getParam(params, "channel", "1"),
      1,       // min
      4,       // max
      1,       // step
      0,       // decimals (integer)
      ""       // suffix
    );

    widget->addNumericParameter("voltage", "Voltage (V)", params.at("voltage"), -100, 100, 0.1, 3, "V");
  }

  void BuildSequence(SequenceBuilder* seq, const ProcessInstance* step) override {
    std::string device = step->parameters.at("device");
    std::string channel = getParam(step->parameters, "channel", "1");
    std::string voltage = step->parameters.at("voltage");

    seq->AddAction(device, "SetVoltage", {{"voltage", voltage}, {"channel", channel}});

    std::cout << "[SMUSetVoltagePlugin] Built sequence: SetVoltage " << voltage << "V on " << device
              << " channel " << channel << std::endl;
  }
};

//=============================================================================
// SMU SET CURRENT PLUGIN
//=============================================================================

class SMUSetCurrentPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override { return "SMU Set Current"; }
  std::string GetCategory() const override { return "SMU Control"; }
  std::string GetDescription() const override {
    return "Set source current on SMU device (for current source devices)";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "Keithley_6482"},
      {"channel", "1"},
      {"current", "0.0"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addDeviceSelector("device", "SMU Device", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::SMU));

    // Channel number
    widget->addNumericParameter("channel", "Channel",
      getParam(params, "channel", "1"),
      1,       // min
      4,       // max
      1,       // step
      0,       // decimals (integer)
      ""       // suffix
    );

    widget->addNumericParameter("current", "Current (A)", params.at("current"), -1, 1, 0.001, 6, "A");
  }

  void BuildSequence(SequenceBuilder* seq, const ProcessInstance* step) override {
    std::string device = step->parameters.at("device");
    std::string channel = getParam(step->parameters, "channel", "1");
    std::string current = step->parameters.at("current");

    seq->AddAction(device, "SetCurrent", {{"current", current}, {"channel", channel}});

    std::cout << "[SMUSetCurrentPlugin] Built sequence: SetCurrent " << current << "A on " << device
              << " channel " << channel << std::endl;
  }
};

//=============================================================================
// SMU SET COMPLIANCE PLUGIN
//=============================================================================

class SMUSetCompliancePlugin : public IProcessPlugin
{
public:
  std::string GetName() const override { return "SMU Set Compliance"; }
  std::string GetCategory() const override { return "SMU Control"; }
  std::string GetDescription() const override {
    return "Set compliance limit for SMU device";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "Keithley_6482"},
      {"channel", "1"},
      {"limit", "0.1"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addDeviceSelector("device", "SMU Device", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::SMU));

    // Channel number
    widget->addNumericParameter("channel", "Channel",
      getParam(params, "channel", "1"),
      1,       // min
      4,       // max
      1,       // step
      0,       // decimals (integer)
      ""       // suffix
    );

    widget->addNumericParameter("limit", "Compliance Limit", params.at("limit"), 0.001, 10, 0.01, 3, "");
  }

  void BuildSequence(SequenceBuilder* seq, const ProcessInstance* step) override {
    std::string device = step->parameters.at("device");
    std::string channel = getParam(step->parameters, "channel", "1");
    std::string limit = step->parameters.at("limit");

    seq->AddAction(device, "SetCompliance", {{"limit", limit}, {"channel", channel}});

    std::cout << "[SMUSetCompliancePlugin] Built sequence: SetCompliance " << limit << " on " << device
              << " channel " << channel << std::endl;
  }
};

//=============================================================================
// SMU OUTPUT ON PLUGIN
//=============================================================================

class SMUOutputOnPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override { return "SMU Output ON"; }
  std::string GetCategory() const override { return "SMU Control"; }
  std::string GetDescription() const override {
    return "Turn SMU output ON (enable sourcing)";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "Keithley_2400"},
      {"channel", "1"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;
    widget->addDeviceSelector("device", "SMU Device", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::SMU));

    // Channel number
    widget->addNumericParameter("channel", "Channel",
      getParam(params, "channel", "1"),
      1,       // min
      4,       // max
      1,       // step
      0,       // decimals (integer)
      ""       // suffix
    );
  }

  void BuildSequence(SequenceBuilder* seq, const ProcessInstance* step) override {
    std::string device = step->parameters.at("device");
    std::string channel = getParam(step->parameters, "channel", "1");
    seq->AddAction(device, "OutputOn", {{"channel", channel}});
    std::cout << "[SMUOutputOnPlugin] Built sequence: OutputOn on " << device
              << " channel " << channel << std::endl;
  }
};

//=============================================================================
// SMU OUTPUT OFF PLUGIN
//=============================================================================

class SMUOutputOffPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override { return "SMU Output OFF"; }
  std::string GetCategory() const override { return "SMU Control"; }
  std::string GetDescription() const override {
    return "Turn SMU output OFF (disable sourcing) - SAFETY";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "Keithley_2400"},
      {"channel", "1"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;
    widget->addDeviceSelector("device", "SMU Device", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::SMU));

    // Channel number
    widget->addNumericParameter("channel", "Channel",
      getParam(params, "channel", "1"),
      1,       // min
      4,       // max
      1,       // step
      0,       // decimals (integer)
      ""       // suffix
    );
  }

  void BuildSequence(SequenceBuilder* seq, const ProcessInstance* step) override {
    std::string device = step->parameters.at("device");
    std::string channel = getParam(step->parameters, "channel", "1");
    seq->AddAction(device, "OutputOff", {{"channel", channel}});
    std::cout << "[SMUOutputOffPlugin] Built sequence: OutputOff on " << device
              << " channel " << channel << std::endl;
  }
};

//=============================================================================
// SMU CONNECT PLUGIN
//=============================================================================

class SMUConnectPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override { return "SMU Connect"; }
  std::string GetCategory() const override { return "SMU Control"; }
  std::string GetDescription() const override {
    return "Connect to SMU device";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "Keithley_6482"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;
    widget->addDeviceSelector("device", "SMU Device", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::SMU));
  }

  void BuildSequence(SequenceBuilder* seq, const ProcessInstance* step) override {
    std::string device = step->parameters.at("device");
    seq->AddAction(device, "Connect", {});
    std::cout << "[SMUConnectPlugin] Built sequence: Connect on " << device << std::endl;
  }
};

//=============================================================================
// SMU DISCONNECT PLUGIN
//=============================================================================

class SMUDisconnectPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override { return "SMU Disconnect"; }
  std::string GetCategory() const override { return "SMU Control"; }
  std::string GetDescription() const override {
    return "Disconnect from SMU device";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "Keithley_6482"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;
    widget->addDeviceSelector("device", "SMU Device", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::SMU));
  }

  void BuildSequence(SequenceBuilder* seq, const ProcessInstance* step) override {
    std::string device = step->parameters.at("device");
    seq->AddAction(device, "Disconnect", {});
    std::cout << "[SMUDisconnectPlugin] Built sequence: Disconnect on " << device << std::endl;
  }
};

//=============================================================================
// PLUGIN REGISTRATION
//=============================================================================

// Register all SMU plugins with the registry
static bool RegisterSMUPlugins()
{
  auto& registry = ProcessPluginRegistry::Instance();

  registry.RegisterPlugin(std::make_unique<SMUConnectPlugin>());
  registry.RegisterPlugin(std::make_unique<SMUDisconnectPlugin>());
  registry.RegisterPlugin(std::make_unique<SMUStartPollingPlugin>());
  registry.RegisterPlugin(std::make_unique<SMUStopPollingPlugin>());
  registry.RegisterPlugin(std::make_unique<SMUReadCurrentPlugin>());
  registry.RegisterPlugin(std::make_unique<SMUReadVoltagePlugin>());
  registry.RegisterPlugin(std::make_unique<SMUSetVoltagePlugin>());
  registry.RegisterPlugin(std::make_unique<SMUSetCurrentPlugin>());
  registry.RegisterPlugin(std::make_unique<SMUSetCompliancePlugin>());
  registry.RegisterPlugin(std::make_unique<SMUOutputOnPlugin>());
  registry.RegisterPlugin(std::make_unique<SMUOutputOffPlugin>());

  std::cout << "[SMUProcesses] Registered 11 SMU process plugins" << std::endl;
  return true;
}

// Auto-register on startup
static bool smu_plugins_registered = RegisterSMUPlugins();
