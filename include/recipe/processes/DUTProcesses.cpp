// DUTProcesses.cpp
// Device Under Test (DUT) process plugins

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
// DUT CREATE PLUGIN
//=============================================================================

class DUTCreatePlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "DUT Create";
  }

  std::string GetCategory() const override {
    return "Data";
  }

  std::string GetDescription() const override {
    return "Create new Device Under Test (DUT) data file. Uses loaded product name if available.";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"productName", ""}  // Empty = auto-use loaded product from Position Manager
      // No serialNumber - will be prompted at runtime
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addParameterField("productName", "Product Name (leave empty to use loaded product)", params.at("productName"));
    // Serial number will be prompted at runtime - no UI field needed
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string productName = params.at("productName");

    // Step 1: Ask user for serial number and store in context
    sequence->AddAction("system", "AskForInput", {
      {"title", "Enter Serial Number"},
      {"prompt", "Please enter the device serial number for " + productName + ":"},
      {"default_value", ""},
      {"context_key", "serialNumber"}
    });

    // Step 2: Load existing DUT if it exists, otherwise create new
    // This preserves existing data when running multiple tests on same device
    sequence->AddAction("dut", "LoadOrCreate", {
      {"productName", productName},
      {"serialNumber", ""}  // Will be read from context["serialNumber"]
    });

    std::cout << "[DUTProcesses] Built DUT LoadOrCreate sequence (preserves existing data)" << std::endl;
  }
};

//=============================================================================
// DUT LOAD PLUGIN
//=============================================================================

class DUTLoadPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "DUT Load";
  }

  std::string GetCategory() const override {
    return "Data";
  }

  std::string GetDescription() const override {
    return "Load existing Device Under Test (DUT) data file. Uses loaded product name if available.";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"productName", ""}  // Empty = auto-use loaded product from Position Manager
      // No serialNumber - will be prompted at runtime
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addParameterField("productName", "Product Name (leave empty to use loaded product)", params.at("productName"));
    // Serial number will be prompted at runtime - no UI field needed
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string productName = params.at("productName");

    // Step 1: Ask user for serial number and store in context
    sequence->AddAction("system", "AskForInput", {
      {"title", "Enter Serial Number"},
      {"prompt", "Please enter the serial number to load for " + productName + ":"},
      {"default_value", ""},
      {"context_key", "serialNumber"}
    });

    // Step 2: Load DUT using serial number from context
    sequence->AddAction("dut", "Load", {
      {"productName", productName},
      {"serialNumber", ""}  // Will be read from context["serialNumber"]
    });

    std::cout << "[DUTProcesses] Built DUT Load sequence with runtime serial number prompt" << std::endl;
  }
};

//=============================================================================
// DUT CLOSE PLUGIN
//=============================================================================

class DUTClosePlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "DUT Close";
  }

  std::string GetCategory() const override {
    return "Data";
  }

  std::string GetDescription() const override {
    return "Close current active Device Under Test (DUT)";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {};  // No parameters needed
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    // No configuration needed - just a simple close action
    (void)widget;
    (void)instance;
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    sequence->AddAction("dut", "Close", {});

    std::cout << "[DUTProcesses] Built DUT Close sequence" << std::endl;
  }
};

//=============================================================================
// DUT INSERT STRING PLUGIN
//=============================================================================

class DUTInsertStringPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "DUT Insert String";
  }

  std::string GetCategory() const override {
    return "Data";
  }

  std::string GetDescription() const override {
    return "Insert string value into active DUT";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"key", "status"},
      {"value", "PASS"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addParameterField("key", "Entry Key", params.at("key"));
    widget->addParameterField("value", "String Value", params.at("value"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string key = params.at("key");
    std::string value = params.at("value");

    sequence->AddAction("dut", "InsertString", {
      {"key", key},
      {"value", value}
    });

    std::cout << "[DUTProcesses] Built DUT Insert String: " << key << " = " << value << std::endl;
  }
};

//=============================================================================
// DUT INSERT DOUBLE PLUGIN
//=============================================================================

class DUTInsertDoublePlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "DUT Insert Double";
  }

  std::string GetCategory() const override {
    return "Data";
  }

  std::string GetDescription() const override {
    return "Insert double/float value into active DUT";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"key", "temperature"},
      {"value", "25.5"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addParameterField("key", "Entry Key", params.at("key"));
    widget->addParameterField("value", "Double Value", params.at("value"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string key = params.at("key");
    std::string value = params.at("value");

    sequence->AddAction("dut", "InsertDouble", {
      {"key", key},
      {"value", value}
    });

    std::cout << "[DUTProcesses] Built DUT Insert Double: " << key << " = " << value << std::endl;
  }
};

//=============================================================================
// DUT INSERT INTEGER PLUGIN
//=============================================================================

class DUTInsertIntegerPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "DUT Insert Integer";
  }

  std::string GetCategory() const override {
    return "Data";
  }

  std::string GetDescription() const override {
    return "Insert integer value into active DUT";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"key", "test_count"},
      {"value", "1"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addParameterField("key", "Entry Key", params.at("key"));
    widget->addParameterField("value", "Integer Value", params.at("value"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string key = params.at("key");
    std::string value = params.at("value");

    sequence->AddAction("dut", "InsertInteger", {
      {"key", key},
      {"value", value}
    });

    std::cout << "[DUTProcesses] Built DUT Insert Integer: " << key << " = " << value << std::endl;
  }
};

//=============================================================================
// DUT INSERT FROM CONTEXT PLUGIN
//=============================================================================

class DUTInsertFromContextPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "DUT Insert from Context";
  }

  std::string GetCategory() const override {
    return "Data";
  }

  std::string GetDescription() const override {
    return "Insert value from context (captured by previous command) into active DUT";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"entry_key", "measurement"},
      {"context_key", "voltage1"},
      {"data_type", "auto"}  // auto, string, double, integer
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addParameterField("entry_key", "Entry Key (name in DUT)", params.at("entry_key"));
    widget->addParameterField("context_key", "Context Key (from Capture As)", params.at("context_key"));
    widget->addParameterField("data_type", "Data Type (auto/string/double/integer)", params.at("data_type"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string entryKey = params.at("entry_key");
    std::string contextKey = params.at("context_key");
    std::string dataType = params.at("data_type");

    sequence->AddAction("dut", "InsertFromContext", {
      {"entry_key", entryKey},
      {"context_key", contextKey},
      {"data_type", dataType}
    });

    std::cout << "[DUTProcesses] Built DUT Insert from Context: " << entryKey << " <- context[" << contextKey << "]" << std::endl;
  }
};

//=============================================================================
// AUTO-REGISTRATION
//=============================================================================

namespace {
  struct RegisterDUTProcesses {
    RegisterDUTProcesses() {
      auto& registry = ProcessPluginRegistry::Instance();

      registry.RegisterPlugin(std::make_unique<DUTCreatePlugin>());
      registry.RegisterPlugin(std::make_unique<DUTLoadPlugin>());
      registry.RegisterPlugin(std::make_unique<DUTClosePlugin>());
      registry.RegisterPlugin(std::make_unique<DUTInsertStringPlugin>());
      registry.RegisterPlugin(std::make_unique<DUTInsertDoublePlugin>());
      registry.RegisterPlugin(std::make_unique<DUTInsertIntegerPlugin>());
      registry.RegisterPlugin(std::make_unique<DUTInsertFromContextPlugin>());

      std::cout << "[DUTProcesses] Registered 7 DUT process plugins" << std::endl;
    }
  };

  // Static instance causes registration at program startup
  static RegisterDUTProcesses g_registerDUTProcesses;
}
