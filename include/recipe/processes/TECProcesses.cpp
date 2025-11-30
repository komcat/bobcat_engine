// TECProcesses.cpp
// TEC and Laser process plugins: Set TEC Temp, Set Laser Current, Read Temperature, Read Laser Current

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
// SET TEC TEMP PLUGIN
//=============================================================================

class SetTECTempPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Set TEC Temp";
  }

  std::string GetCategory() const override {
    return "TEC & Laser";
  }

  std::string GetDescription() const override {
    return "Set TEC device temperature and power state";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "tec"},
      {"temperature", "25.0"},
      {"power_state", "on"}  // "on" or "off"
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addParameterField("device", "Device Name", params.at("device"));
    widget->addParameterField("temperature", "Temperature (°C)", params.at("temperature"));
    widget->addParameterField("power_state", "Power State (on/off)", params.at("power_state"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string device = params.at("device");
    std::string temperature = params.at("temperature");
    std::string powerState = params.at("power_state");

    sequence->AddAction(device, "SetTemperature", {
      {"temperature", temperature}
    });

    if (powerState == "on") {
      sequence->AddAction(device, "TEC_On", {});
    }
    else if (powerState == "off") {
      sequence->AddAction(device, "TEC_Off", {});
    }

    std::cout << "[TECProcesses] Built Set TEC Temp sequence for: " << step->userNickname << std::endl;
  }
};

//=============================================================================
// SET LASER CURRENT PLUGIN
//=============================================================================

class SetLaserCurrentPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Set Laser Current";
  }

  std::string GetCategory() const override {
    return "TEC & Laser";
  }

  std::string GetDescription() const override {
    return "Set laser current and power state";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "laser"},
      {"current_ma", "150"},
      {"power_state", "on"}  // "on" or "off"
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addParameterField("device", "Device Name", params.at("device"));
    widget->addParameterField("current_ma", "Current (mA)", params.at("current_ma"));
    widget->addParameterField("power_state", "Power State (on/off)", params.at("power_state"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string device = params.at("device");
    std::string currentMa = params.at("current_ma");
    std::string powerState = params.at("power_state");

    sequence->AddAction(device, "SetLaserCurrent", {
      {"current", currentMa}
    });

    if (powerState == "on") {
      sequence->AddAction(device, "Laser_On", {});
    }
    else if (powerState == "off") {
      sequence->AddAction(device, "Laser_Off", {});
    }

    std::cout << "[TECProcesses] Built Set Laser Current sequence for: " << step->userNickname << std::endl;
  }
};

//=============================================================================
// READ TEMPERATURE PLUGIN
//=============================================================================

class ReadTemperaturePlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Read Temperature";
  }

  std::string GetCategory() const override {
    return "TEC & Laser";
  }

  std::string GetDescription() const override {
    return "Read current temperature from TEC device";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"tec_device", "TEC1"},
      {"log_level", "Info"},
      {"captureAs", ""}  // Optional - stores temperature in context
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addParameterField("tec_device", "TEC Device Name", params.at("tec_device"));
    widget->addParameterField("log_level", "Log Level (Info/Debug/Warning)", params.at("log_level"));
    widget->addParameterField("captureAs", "Capture As (optional)", params.at("captureAs"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string tecDevice = params.at("tec_device");
    std::string logLevel = params.at("log_level");

    // Build action parameters
    std::map<std::string, std::string> actionParams = {
      {"logLevel", logLevel}
    };

    // Add optional captureAs parameter if present
    auto captureIt = params.find("captureAs");
    if (captureIt != params.end() && !captureIt->second.empty()) {
      actionParams["captureAs"] = captureIt->second;
    }

    sequence->AddAction(tecDevice, "ReadTemperature", actionParams);

    std::cout << "[TECProcesses] Built Read Temperature sequence for: " << step->userNickname << std::endl;
  }
};

//=============================================================================
// READ LASER CURRENT PLUGIN
//=============================================================================

class ReadLaserCurrentPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Read Laser Current";
  }

  std::string GetCategory() const override {
    return "TEC & Laser";
  }

  std::string GetDescription() const override {
    return "Read current laser current from TEC device";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"tec_device", "TEC1"},
      {"log_level", "Info"},
      {"captureAs", ""}  // Optional - stores laser current in context
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addParameterField("tec_device", "TEC Device Name", params.at("tec_device"));
    widget->addParameterField("log_level", "Log Level (Info/Debug/Warning)", params.at("log_level"));
    widget->addParameterField("captureAs", "Capture As (optional)", params.at("captureAs"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string tecDevice = params.at("tec_device");
    std::string logLevel = params.at("log_level");

    // Build action parameters
    std::map<std::string, std::string> actionParams = {
      {"logLevel", logLevel}
    };

    // Add optional captureAs parameter if present
    auto captureIt = params.find("captureAs");
    if (captureIt != params.end() && !captureIt->second.empty()) {
      actionParams["captureAs"] = captureIt->second;
    }

    sequence->AddAction(tecDevice, "ReadLaserCurrent", actionParams);

    std::cout << "[TECProcesses] Built Read Laser Current sequence for: " << step->userNickname << std::endl;
  }
};

//=============================================================================
// TEC CONNECT PLUGIN
//=============================================================================

class TECConnectPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "TEC Connect";
  }

  std::string GetCategory() const override {
    return "TEC & Laser";
  }

  std::string GetDescription() const override {
    return "Connect to a TEC device";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "TEC1"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;
    widget->addParameterField("device", "TEC Device Name", params.at("device"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;
    std::string device = params.at("device");

    sequence->AddAction(device, "Connect", {});

    std::cout << "[TECProcesses] Built TEC Connect for: " << device << std::endl;
  }
};

//=============================================================================
// TEC DISCONNECT PLUGIN
//=============================================================================

class TECDisconnectPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "TEC Disconnect";
  }

  std::string GetCategory() const override {
    return "TEC & Laser";
  }

  std::string GetDescription() const override {
    return "Disconnect from a TEC device";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "TEC1"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;
    widget->addParameterField("device", "TEC Device Name", params.at("device"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;
    std::string device = params.at("device");

    sequence->AddAction(device, "Disconnect", {});

    std::cout << "[TECProcesses] Built TEC Disconnect for: " << device << std::endl;
  }
};

//=============================================================================
// AUTO-REGISTRATION
//=============================================================================

namespace {
  struct RegisterTECProcesses {
    RegisterTECProcesses() {
      auto& registry = ProcessPluginRegistry::Instance();

      registry.RegisterPlugin(std::make_unique<TECConnectPlugin>());
      registry.RegisterPlugin(std::make_unique<TECDisconnectPlugin>());
      registry.RegisterPlugin(std::make_unique<SetTECTempPlugin>());
      registry.RegisterPlugin(std::make_unique<SetLaserCurrentPlugin>());
      registry.RegisterPlugin(std::make_unique<ReadTemperaturePlugin>());
      registry.RegisterPlugin(std::make_unique<ReadLaserCurrentPlugin>());

      std::cout << "[TECProcesses] Registered 6 TEC/Laser process plugins" << std::endl;
    }
  };

  // Static instance causes registration at program startup
  static RegisterTECProcesses g_registerTECProcesses;
}
