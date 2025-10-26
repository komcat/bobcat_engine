// DataStoreProcesses.cpp
// DataStore process plugins: Read DataStore

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
// READ DATASTORE PLUGIN
//=============================================================================

class ReadDataStorePlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Read DataStore";
  }

  std::string GetCategory() const override {
    return "Data & Storage";
  }

  std::string GetDescription() const override {
    return "Read value from DataStore channel and log it";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"channel_name", "GPIB-Current"},
      {"log_level", "Info"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addParameterField("channel_name", "Channel Name", params.at("channel_name"));
    widget->addParameterField("log_level", "Log Level (Info/Debug/Warning)", params.at("log_level"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string channelName = params.at("channel_name");
    std::string logLevel = params.at("log_level");

    sequence->AddAction("datastore", "ReadAndLog", {
      {"channelName", channelName},
      {"logLevel", logLevel}
    });

    std::cout << "[DataStoreProcesses] Built Read DataStore sequence for: " << step->userNickname
              << " (Channel: " << channelName << ")" << std::endl;
  }
};

//=============================================================================
// AUTO-REGISTRATION
//=============================================================================

namespace {
  struct RegisterDataStoreProcesses {
    RegisterDataStoreProcesses() {
      auto& registry = ProcessPluginRegistry::Instance();

      registry.RegisterPlugin(std::make_unique<ReadDataStorePlugin>());

      std::cout << "[DataStoreProcesses] Registered 1 DataStore process plugin" << std::endl;
    }
  };

  // Static instance causes registration at program startup
  static RegisterDataStoreProcesses g_registerDataStoreProcesses;
}
