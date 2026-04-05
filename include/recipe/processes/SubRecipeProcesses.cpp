// SubRecipeProcesses.cpp
// Sub-Recipe plugin: loads another recipe file and inlines its steps at build time

#include "../IProcessPlugin.h"
#include "../ProcessPluginRegistry.h"
#include "../ProcessSequenceFactory.h"
#include "../RecipeManager.h"
#include "../../sequence/SequenceBuilder.h"
#include "../../AppUI/RecipePage/ProcessConfigWidget.h"

#include <iostream>
#include <memory>
#include <map>
#include <string>
#include <filesystem>

//=============================================================================
// SUB-RECIPE PLUGIN
//=============================================================================

class SubRecipePlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Sub-Recipe";
  }

  std::string GetCategory() const override {
    return "Sub-Recipe";
  }

  std::string GetDescription() const override {
    return "Execute another recipe inline as a reusable sub-sequence. "
           "The sub-recipe's steps are flattened into the parent sequence at build time. "
           "Sub-recipes cannot contain other Sub-Recipe steps (no nesting).";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {{"recipe_file", ""}};
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;
    auto getParam = [&params](const std::string& key, const std::string& defaultVal = "") {
      auto it = params.find(key);
      return (it != params.end()) ? it->second : defaultVal;
    };

    widget->addRecipeSelector("recipe_file", "Recipe File",
      getParam("recipe_file"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;
    auto it = params.find("recipe_file");
    if (it == params.end() || it->second.empty()) {
      std::cerr << "[SubRecipe] No recipe file specified in step '"
                << step->userNickname << "'" << std::endl;
      return;
    }

    const std::string& recipeFile = it->second;

    // Safety check: prevent self-reference (sub-recipe pointing to parent recipe)
    std::string parentPath = sequence->GetRecipeFilePath();
    if (!parentPath.empty()) {
      std::error_code ec;
      auto parentCanonical = std::filesystem::weakly_canonical(parentPath, ec);
      auto subCanonical = std::filesystem::weakly_canonical(recipeFile, ec);
      if (!ec && parentCanonical == subCanonical) {
        std::cerr << "[SubRecipe] Self-reference detected: '"
                  << recipeFile << "' is the parent recipe. Skipping." << std::endl;
        return;
      }
    }

    // Get factory from SequenceBuilder
    ProcessSequenceFactory* factory = sequence->GetFactory();
    if (!factory) {
      std::cerr << "[SubRecipe] No factory available on SequenceBuilder. "
                << "Sub-recipe inlining requires factory context." << std::endl;
      return;
    }

    // Load sub-recipe into an independent RecipeManager
    RecipeManager subRecipeManager;
    if (!subRecipeManager.loadRecipe(recipeFile)) {
      std::cerr << "[SubRecipe] Failed to load recipe: " << recipeFile << std::endl;
      return;
    }

    // Safety check: sub-recipe must not contain any Sub-Recipe steps (no nesting)
    for (const auto& subStep : subRecipeManager.getAllSteps()) {
      if (subStep->processType == "Sub-Recipe") {
        std::cerr << "[SubRecipe] Nesting not allowed: '"
                  << subRecipeManager.getRecipeName()
                  << "' contains a Sub-Recipe step ('"
                  << subStep->userNickname << "'). Skipping entire sub-recipe." << std::endl;
        return;
      }
    }

    // Inline each enabled step from the sub-recipe
    int inlinedCount = 0;
    for (const auto& subStep : subRecipeManager.getAllSteps()) {
      if (!subStep->enabled) continue;

      std::string errorMsg;
      if (!factory->BuildStep(sequence, subStep.get(), errorMsg)) {
        std::cerr << "[SubRecipe] Failed to build sub-step '"
                  << subStep->userNickname << "': " << errorMsg << std::endl;
        return;
      }
      ++inlinedCount;
    }

    std::cout << "[SubRecipe] Inlined " << inlinedCount << " step(s) from '"
              << subRecipeManager.getRecipeName() << "' (" << recipeFile << ")" << std::endl;
  }
};

//=============================================================================
// AUTO-REGISTRATION
//=============================================================================

namespace {
  struct RegisterSubRecipeProcesses {
    RegisterSubRecipeProcesses() {
      auto& registry = ProcessPluginRegistry::Instance();
      registry.RegisterPlugin(std::make_unique<SubRecipePlugin>());
      std::cout << "[SubRecipeProcesses] Registered Sub-Recipe plugin" << std::endl;
    }
  };
  static RegisterSubRecipeProcesses g_registerSubRecipeProcesses;
}
