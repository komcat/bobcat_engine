// AuxWindowProcesses.cpp
// Process plugins for Auxiliary Window controls:
// - Enable Crosshair
// - Enable Click-to-Move
// - Set Chart Channel

#include "../IProcessPlugin.h"
#include "../ProcessPluginRegistry.h"
#include "../RecipeManager.h"  // For ProcessInstance
#include "../../sequence/SequenceBuilder.h"
#include "../../AppUI/RecipePage/ProcessConfigWidget.h"

#include <iostream>
#include <memory>
#include <map>
#include <string>

//=============================================================================
// ENABLE CROSSHAIR PLUGIN
//=============================================================================

class EnableCrosshairPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Enable Crosshair";
  }

  std::string GetCategory() const override {
    return "Aux Window";
  }

  std::string GetDescription() const override {
    return "Enable/disable crosshair overlay on auxiliary camera view";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"enabled", "true"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addCheckboxParameter("enabled", "Enable Crosshair", params.at("enabled"),
      "Show crosshair overlay on camera view");
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    sequence->AddAction("auxwindow", "EnableCrosshair", {
      {"enabled", params.at("enabled")}
    });

    std::cout << "[AuxWindowProcesses] Built Enable Crosshair sequence for: " << step->userNickname << std::endl;
  }
};

//=============================================================================
// ENABLE CLICK-TO-MOVE PLUGIN
//=============================================================================

class EnableClickToMovePlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Enable Click-to-Move";
  }

  std::string GetCategory() const override {
    return "Aux Window";
  }

  std::string GetDescription() const override {
    return "Enable/disable click-to-move feature on auxiliary camera view";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"enabled", "true"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addCheckboxParameter("enabled", "Enable Click-to-Move", params.at("enabled"),
      "Enable click-to-move feature for camera_1");
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    sequence->AddAction("auxwindow", "EnableClickToMove", {
      {"enabled", params.at("enabled")}
    });

    std::cout << "[AuxWindowProcesses] Built Enable Click-to-Move sequence for: " << step->userNickname << std::endl;
  }
};

//=============================================================================
// SET CHART CHANNEL PLUGIN
//=============================================================================

class SetChartChannelPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Set Chart Channel";
  }

  std::string GetCategory() const override {
    return "Aux Window";
  }

  std::string GetDescription() const override {
    return "Set which DataStore channel to display in auxiliary chart view";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"channel", "SMU_Keithley_6482_Ch1"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    // Use data channel selector (refreshable dropdown)
    widget->addDataChannelSelector("channel", "Chart Channel", params.at("channel"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    sequence->AddAction("auxwindow", "SetChartChannel", {
      {"channel", params.at("channel")}
    });

    std::cout << "[AuxWindowProcesses] Built Set Chart Channel sequence for: " << step->userNickname
              << " (Channel: " << params.at("channel") << ")" << std::endl;
  }
};

//=============================================================================
// AUTO-REGISTRATION
//=============================================================================

namespace {
  struct RegisterAuxWindowProcesses {
    RegisterAuxWindowProcesses() {
      auto& registry = ProcessPluginRegistry::Instance();

      registry.RegisterPlugin(std::make_unique<EnableCrosshairPlugin>());
      registry.RegisterPlugin(std::make_unique<EnableClickToMovePlugin>());
      registry.RegisterPlugin(std::make_unique<SetChartChannelPlugin>());

      std::cout << "[AuxWindowProcesses] Registered 3 auxiliary window process plugins" << std::endl;
    }
  };

  // Static instance causes registration at program startup
  static RegisterAuxWindowProcesses g_registerAuxWindowProcesses;
}
