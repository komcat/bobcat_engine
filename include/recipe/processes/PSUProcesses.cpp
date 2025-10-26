// PSUProcesses.cpp
// Power Supply process plugins: PSU Output On/Off, PSU Set Voltage, PSU Set Current, PSU Read Voltage, PSU Read Current

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
      {"output_state", "on"}  // "on" or "off"
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addParameterField("psu_device", "PSU Device Name", params.at("psu_device"));
    widget->addParameterField("output_state", "Output State (on/off)", params.at("output_state"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string psuDevice = params.at("psu_device");
    std::string outputState = params.at("output_state");

    if (outputState == "on") {
      sequence->AddAction(psuDevice, "SetOutputOn", {});
    }
    else {
      sequence->AddAction(psuDevice, "SetOutputOff", {});
    }

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
      {"voltage", "3.0"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addParameterField("psu_device", "PSU Device Name", params.at("psu_device"));
    widget->addParameterField("voltage", "Voltage (V)", params.at("voltage"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string psuDevice = params.at("psu_device");
    std::string voltage = params.at("voltage");

    sequence->AddAction(psuDevice, "SetVoltage", {
      {"voltage", voltage}
    });

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
      {"current", "150"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addParameterField("psu_device", "PSU Device Name", params.at("psu_device"));
    widget->addParameterField("current", "Current (mA)", params.at("current"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string psuDevice = params.at("psu_device");
    std::string current = params.at("current");

    sequence->AddAction(psuDevice, "SetCurrent", {
      {"current", current}
    });

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
      {"delay_ms", "100"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addParameterField("psu_device", "PSU Device Name", params.at("psu_device"));
    widget->addParameterField("read_count", "Read Count", params.at("read_count"));
    widget->addParameterField("delay_ms", "Delay Between Reads (ms)", params.at("delay_ms"));

    // Optional: Add captureAs field if exists
    if (params.find("captureAs") != params.end()) {
      widget->addParameterField("captureAs", "Capture As (optional)", params.at("captureAs"));
    }
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string psuDevice = params.at("psu_device");
    std::string readCount = params.at("read_count");
    std::string delayMs = params.at("delay_ms");

    // Build action parameters
    std::map<std::string, std::string> actionParams = {
      {"read_count", readCount},
      {"delay_ms", delayMs}
    };

    // Add optional captureAs parameter if present
    auto captureIt = params.find("captureAs");
    if (captureIt != params.end() && !captureIt->second.empty()) {
      actionParams["captureAs"] = captureIt->second;
    }

    sequence->AddAction(psuDevice, "ReadVoltageMultiple", actionParams);

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
      {"delay_ms", "100"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addParameterField("psu_device", "PSU Device Name", params.at("psu_device"));
    widget->addParameterField("read_count", "Read Count", params.at("read_count"));
    widget->addParameterField("delay_ms", "Delay Between Reads (ms)", params.at("delay_ms"));

    // Optional: Add captureAs field if exists
    if (params.find("captureAs") != params.end()) {
      widget->addParameterField("captureAs", "Capture As (optional)", params.at("captureAs"));
    }
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string psuDevice = params.at("psu_device");
    std::string readCount = params.at("read_count");
    std::string delayMs = params.at("delay_ms");

    // Build action parameters
    std::map<std::string, std::string> actionParams = {
      {"read_count", readCount},
      {"delay_ms", delayMs}
    };

    // Add optional captureAs parameter if present
    auto captureIt = params.find("captureAs");
    if (captureIt != params.end() && !captureIt->second.empty()) {
      actionParams["captureAs"] = captureIt->second;
    }

    sequence->AddAction(psuDevice, "ReadCurrentMultiple", actionParams);

    std::cout << "[PSUProcesses] Built PSU Read Current sequence for: " << step->userNickname << std::endl;
  }
};

//=============================================================================
// AUTO-REGISTRATION
//=============================================================================

namespace {
  struct RegisterPSUProcesses {
    RegisterPSUProcesses() {
      auto& registry = ProcessPluginRegistry::Instance();

      registry.RegisterPlugin(std::make_unique<PSUOutputPlugin>());
      registry.RegisterPlugin(std::make_unique<PSUSetVoltagePlugin>());
      registry.RegisterPlugin(std::make_unique<PSUSetCurrentPlugin>());
      registry.RegisterPlugin(std::make_unique<PSUReadVoltagePlugin>());
      registry.RegisterPlugin(std::make_unique<PSUReadCurrentPlugin>());

      std::cout << "[PSUProcesses] Registered 5 PSU process plugins" << std::endl;
    }
  };

  // Static instance causes registration at program startup
  static RegisterPSUProcesses g_registerPSUProcesses;
}
