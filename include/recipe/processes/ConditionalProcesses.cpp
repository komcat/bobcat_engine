// ConditionalProcesses.cpp
// Conditional control flow process plugins: Conditional If, Conditional Else, Conditional End

#include "../IProcessPlugin.h"
#include "../ProcessPluginRegistry.h"
#include "../RecipeManager.h"  // For ProcessInstance
#include "../../sequence/SequenceBuilder.h"
#include "../../sequence/SequenceEngine.h"  // For ConditionalBlock::ComparisonType
#include "../../AppUI/RecipePage/ProcessConfigWidget.h"

#include <iostream>
#include <memory>
#include <map>
#include <string>

//=============================================================================
// CONDITIONAL IF PLUGIN
//=============================================================================

class ConditionalIfPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Conditional If";
  }

  std::string GetCategory() const override {
    return "Control Flow";
  }

  std::string GetDescription() const override {
    return "Begin conditional block based on context value comparison";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"context_key", "MeasuredValue"},
      {"comparison", ">"},
      {"threshold", "0.0"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addParameterField("context_key", "Context Key", params.at("context_key"));
    widget->addParameterField("comparison", "Comparison (>, <, ==, !=, >=, <=, IS_TRUE, IS_FALSE, EXISTS, NOT_EXISTS)",
      params.at("comparison"));
    widget->addParameterField("threshold", "Threshold Value", params.at("threshold"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string contextKey = params.at("context_key");
    std::string comparisonStr = params.at("comparison");

    // Parse threshold with error handling - default to 0.0 if empty or invalid
    float threshold = 0.0f;
    std::string thresholdStr = params.at("threshold");
    if (!thresholdStr.empty()) {
      try {
        threshold = std::stof(thresholdStr);
      } catch (const std::exception& e) {
        std::cerr << "[ConditionalProcesses] Warning: Invalid threshold value '"
                  << thresholdStr << "', using 0.0. Error: " << e.what() << std::endl;
        threshold = 0.0f;
      }
    }

    // Convert comparison string to enum
    ConditionalBlock::ComparisonType comparison;
    if (comparisonStr == ">") comparison = ConditionalBlock::ComparisonType::GREATER_THAN;
    else if (comparisonStr == "<") comparison = ConditionalBlock::ComparisonType::LESS_THAN;
    else if (comparisonStr == "==") comparison = ConditionalBlock::ComparisonType::EQUAL;
    else if (comparisonStr == "!=") comparison = ConditionalBlock::ComparisonType::NOT_EQUAL;
    else if (comparisonStr == ">=") comparison = ConditionalBlock::ComparisonType::GREATER_OR_EQUAL;
    else if (comparisonStr == "<=") comparison = ConditionalBlock::ComparisonType::LESS_OR_EQUAL;
    else if (comparisonStr == "IS_TRUE") comparison = ConditionalBlock::ComparisonType::IS_TRUE;
    else if (comparisonStr == "IS_FALSE") comparison = ConditionalBlock::ComparisonType::IS_FALSE;
    else if (comparisonStr == "EXISTS") comparison = ConditionalBlock::ComparisonType::EXISTS;
    else if (comparisonStr == "NOT_EXISTS") comparison = ConditionalBlock::ComparisonType::NOT_EXISTS;
    else comparison = ConditionalBlock::ComparisonType::GREATER_THAN; // default

    sequence->BeginIf(contextKey, comparison, threshold);

    std::cout << "[ConditionalProcesses] Built Conditional If: "
              << contextKey << " " << comparisonStr << " " << threshold << std::endl;
  }
};

//=============================================================================
// CONDITIONAL ELSE PLUGIN
//=============================================================================

class ConditionalElsePlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Conditional Else";
  }

  std::string GetCategory() const override {
    return "Control Flow";
  }

  std::string GetDescription() const override {
    return "Else branch for conditional block";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {};  // No parameters
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    // No parameters - widget will show empty form or info message
    (void)widget;
    (void)instance;
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    sequence->Else();
    std::cout << "[ConditionalProcesses] Built Conditional Else" << std::endl;
    (void)step;
  }
};

//=============================================================================
// CONDITIONAL END PLUGIN
//=============================================================================

class ConditionalEndPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Conditional End";
  }

  std::string GetCategory() const override {
    return "Control Flow";
  }

  std::string GetDescription() const override {
    return "End conditional block";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {};  // No parameters
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    // No parameters - widget will show empty form or info message
    (void)widget;
    (void)instance;
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    sequence->EndIf();
    std::cout << "[ConditionalProcesses] Built Conditional End" << std::endl;
    (void)step;
  }
};

//=============================================================================
// LOOP START PLUGIN
//=============================================================================

class LoopStartPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Loop Start";
  }

  std::string GetCategory() const override {
    return "Control Flow";
  }

  std::string GetDescription() const override {
    return "Begin a loop block that repeats contained steps N times";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"count", "1"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;
    widget->addParameterField("count", "Loop Count", params.at("count"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    int count = 1;
    std::string countStr = params.at("count");
    if (!countStr.empty()) {
      try {
        count = std::stoi(countStr);
      } catch (const std::exception& e) {
        std::cerr << "[ConditionalProcesses] Warning: Invalid loop count '"
                  << countStr << "', using 1. Error: " << e.what() << std::endl;
        count = 1;
      }
    }
    if (count < 1) count = 1;

    sequence->BeginLoop(count);

    std::cout << "[ConditionalProcesses] Built Loop Start: " << count
              << " iterations for: " << step->userNickname << std::endl;
  }
};

//=============================================================================
// LOOP END PLUGIN
//=============================================================================

class LoopEndPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Loop End";
  }

  std::string GetCategory() const override {
    return "Control Flow";
  }

  std::string GetDescription() const override {
    return "End a loop block";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {};
  }

  void CreateConfigUI(ProcessConfigWidget* /*widget*/, const ProcessInstance* /*instance*/) override {
    // No parameters
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    sequence->EndLoop();
    std::cout << "[ConditionalProcesses] Built Loop End for: "
              << step->userNickname << std::endl;
    (void)step;
  }
};

//=============================================================================
// AUTO-REGISTRATION
//=============================================================================

namespace {
  struct RegisterConditionalProcesses {
    RegisterConditionalProcesses() {
      auto& registry = ProcessPluginRegistry::Instance();

      registry.RegisterPlugin(std::make_unique<ConditionalIfPlugin>());
      registry.RegisterPlugin(std::make_unique<ConditionalElsePlugin>());
      registry.RegisterPlugin(std::make_unique<ConditionalEndPlugin>());
      registry.RegisterPlugin(std::make_unique<LoopStartPlugin>());
      registry.RegisterPlugin(std::make_unique<LoopEndPlugin>());

      std::cout << "[ConditionalProcesses] Registered 5 control flow process plugins" << std::endl;
    }
  };

  // Static instance causes registration at program startup
  static RegisterConditionalProcesses g_registerConditionalProcesses;
}
