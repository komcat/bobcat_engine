// DataStoreProcesses.cpp
// DataStore process plugins: Read DataStore, Set Context Value, Set Context Boolean

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
// SET CONTEXT VALUE PLUGIN - Set a value in SequenceContext for conditionals
//=============================================================================

class SetContextValuePlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Set Context Value";
  }

  std::string GetCategory() const override {
    return "Data";
  }

  std::string GetDescription() const override {
    return "Set a value in the execution context (for use with Conditional If)";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"key", "MyValue"},
      {"value", "0"},
      {"value_type", "auto"}  // auto, numeric, string, boolean
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addParameterField("key", "Context Key", params.at("key"));
    widget->addParameterField("value", "Value", params.at("value"));
    widget->addParameterField("value_type", "Value Type (auto/numeric/string/boolean)", params.at("value_type"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string key = params.at("key");
    std::string value = params.at("value");
    std::string valueType = params.at("value_type");

    // For boolean type, use SetValue with proper parsing
    // For numeric, use SetNumeric which can resolve context references
    // For auto/string, use SetValue which auto-detects
    if (valueType == "numeric") {
      sequence->AddAction("context", "SetNumeric", {
        {"key", key},
        {"value", value}
      });
    } else {
      sequence->AddAction("context", "SetValue", {
        {"key", key},
        {"value", value}
      });
    }

    std::cout << "[DataStoreProcesses] Built Set Context Value: " << key << " = " << value
              << " (type: " << valueType << ")" << std::endl;
  }
};

//=============================================================================
// SET CONTEXT BOOLEAN PLUGIN - Set a boolean value for IS_TRUE/IS_FALSE conditionals
//=============================================================================

class SetContextBooleanPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Set Context Boolean";
  }

  std::string GetCategory() const override {
    return "Data";
  }

  std::string GetDescription() const override {
    return "Set a boolean (true/false) value in context (for IS_TRUE/IS_FALSE conditionals)";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"key", "MyFlag"},
      {"value", "true"}  // true or false
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addParameterField("key", "Context Key", params.at("key"));
    widget->addParameterField("value", "Value (true/false)", params.at("value"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string key = params.at("key");
    std::string value = params.at("value");

    // Convert to 1 or 0 for context storage (boolean stored as numeric)
    std::string numericValue = (value == "true" || value == "1" || value == "yes") ? "1" : "0";

    sequence->AddAction("context", "SetValue", {
      {"key", key},
      {"value", numericValue}
    });

    std::cout << "[DataStoreProcesses] Built Set Context Boolean: " << key << " = " << value
              << " (" << numericValue << ")" << std::endl;
  }
};

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
    return "Data";
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

      registry.RegisterPlugin(std::make_unique<SetContextValuePlugin>());
      registry.RegisterPlugin(std::make_unique<SetContextBooleanPlugin>());
      registry.RegisterPlugin(std::make_unique<ReadDataStorePlugin>());

      std::cout << "[DataStoreProcesses] Registered 3 DataStore process plugins" << std::endl;
    }
  };

  // Static instance causes registration at program startup
  static RegisterDataStoreProcesses g_registerDataStoreProcesses;
}
