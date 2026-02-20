// AccuracyTrainerProcesses.cpp
// Recipe plugin: Run Accuracy Trainer Alignment

#include "../IProcessPlugin.h"
#include "../ProcessPluginRegistry.h"
#include "../RecipeManager.h"
#include "../../sequence/SequenceBuilder.h"
#include "../../sequence/SequenceEngine.h"
#include "../../AppUI/RecipePage/ProcessConfigWidget.h"
#include "../../AppUI/AccuracyTrainer/AccuracyTrainerStore.h"

#include <iostream>
#include <memory>
#include <map>
#include <string>

// =============================================================================
// RUN ACCURACY TRAINER ALIGNMENT PLUGIN
// =============================================================================

class RunAccuracyTrainerAlignmentPlugin : public IProcessPlugin
{
public:
    std::string GetName() const override {
        return "Run Accuracy Trainer Alignment";
    }

    std::string GetCategory() const override {
        return "Calibration";
    }

    std::string GetDescription() const override {
        return "Run headless alignment using a previously taught Accuracy Trainer training set. "
               "Auto-detects references via vision with manual fallback, computes correction "
               "transform, and optionally moves to corrected bonding positions.";
    }

    std::map<std::string, std::string> GetDefaultParameters() const override {
        return {
            {"training_set_name", ""},
            {"register_hex_left", "false"},
            {"register_hex_right", "false"},
            {"move_to_bonding", "true"}
        };
    }

    void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
        const auto& params = instance->parameters;
        auto defaults = GetDefaultParameters();

        // Safe param getter - falls back to default if key missing
        auto getParam = [&](const std::string& key) -> std::string {
            auto it = params.find(key);
            if (it != params.end()) return it->second;
            auto dit = defaults.find(key);
            if (dit != defaults.end()) return dit->second;
            return "";
        };

        // Training set dropdown (populated from stored training sets)
        AccuracyTrainerStore store;
        std::vector<std::string> names = store.ListTrainingSets();

        widget->addDropdownParameter("training_set_name", "Training Set",
                                     getParam("training_set_name"), names, false);

        // Per-hex-device registration checkboxes
        widget->addCheckboxParameter("register_hex_left", "Register Hex-Left",
                                     getParam("register_hex_left"));
        widget->addCheckboxParameter("register_hex_right", "Register Hex-Right",
                                     getParam("register_hex_right"));

        // Move to bonding checkbox
        widget->addCheckboxParameter("move_to_bonding", "Move to Corrected Bonding Positions",
                                     getParam("move_to_bonding"));
    }

    void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
        const auto& params = step->parameters;
        auto defaults = GetDefaultParameters();

        auto getParam = [&](const std::string& key) -> std::string {
            auto it = params.find(key);
            if (it != params.end()) return it->second;
            auto dit = defaults.find(key);
            if (dit != defaults.end()) return dit->second;
            return "";
        };

        std::string tsName = getParam("training_set_name");
        std::string hexLeft = getParam("register_hex_left");
        std::string hexRight = getParam("register_hex_right");
        std::string moveToBonding = getParam("move_to_bonding");

        sequence->AddAction("accuracytrainer", "RunAlignment", {
            {"training_set_name", tsName},
            {"register_hex_left", hexLeft},
            {"register_hex_right", hexRight},
            {"move_to_bonding", moveToBonding}
        });

        std::cout << "[AccuracyTrainerProcesses] Built sequence for: "
                  << step->userNickname << " (Training Set: " << tsName
                  << ", HexL: " << hexLeft << ", HexR: " << hexRight
                  << ", Bond: " << moveToBonding << ")" << std::endl;
    }
};

// =============================================================================
// PLUGIN REGISTRATION (Static initialization)
// =============================================================================

namespace {
    struct AccuracyTrainerProcessRegistrar {
        AccuracyTrainerProcessRegistrar() {
            auto& registry = ProcessPluginRegistry::Instance();
            registry.RegisterPlugin(std::make_unique<RunAccuracyTrainerAlignmentPlugin>());
            std::cout << "[AccuracyTrainerProcesses] Registered Run Accuracy Trainer Alignment plugin" << std::endl;
        }
    };

    static AccuracyTrainerProcessRegistrar s_registrar;
}
