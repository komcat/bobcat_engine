// PlacementCorrectionProcesses.cpp
// Recipe plugin: Placement Correction Retrace

#include "../IProcessPlugin.h"
#include "../ProcessPluginRegistry.h"
#include "../RecipeManager.h"
#include "../../sequence/SequenceBuilder.h"
#include "../../sequence/SequenceEngine.h"
#include "../../AppUI/RecipePage/ProcessConfigWidget.h"

#include <iostream>
#include <memory>
#include <map>
#include <string>

// =============================================================================
// PLACEMENT CORRECTION RETRACE PLUGIN
// =============================================================================

class PlacementCorrectionRetracePlugin : public IProcessPlugin
{
public:
    std::string GetName() const override {
        return "Placement Correction Retrace";
    }

    std::string GetCategory() const override {
        return "Calibration";
    }

    std::string GetDescription() const override {
        return "Pause execution and run the placement correction retrace wizard. "
               "Operator re-measures reference and target lines, computes angular "
               "correction, and saves corrected hex device position.";
    }

    std::map<std::string, std::string> GetDefaultParameters() const override {
        return {
            {"training_set_name", ""},
            {"auto_start", "true"}
        };
    }

    void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
        const auto& params = instance->parameters;
        auto defaults = GetDefaultParameters();

        auto getParam = [&](const std::string& key) -> std::string {
            auto it = params.find(key);
            if (it != params.end()) return it->second;
            auto dit = defaults.find(key);
            if (dit != defaults.end()) return dit->second;
            return "";
        };

        widget->addPlacementSetSelector("training_set_name", "Training Set",
                                       getParam("training_set_name"));

        widget->addCheckboxParameter("auto_start", "Auto-start retrace wizard",
                                     getParam("auto_start"));
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
        std::string autoStart = getParam("auto_start");

        sequence->AddAction("system", "PlacementCorrectionRetrace", {
            {"training_set_name", tsName},
            {"auto_start", autoStart},
            {"timeout", "0"}  // Disable action timeout (interactive wizard may block indefinitely)
        });

        std::cout << "[PlacementCorrectionProcesses] Built sequence for: "
                  << step->userNickname << " (Training Set: " << tsName
                  << ", Auto-start: " << autoStart << ")" << std::endl;
    }
};

// =============================================================================
// PLUGIN REGISTRATION (Static initialization)
// =============================================================================

namespace {
    struct PlacementCorrectionProcessRegistrar {
        PlacementCorrectionProcessRegistrar() {
            auto& registry = ProcessPluginRegistry::Instance();
            registry.RegisterPlugin(std::make_unique<PlacementCorrectionRetracePlugin>());
            std::cout << "[PlacementCorrectionProcesses] Registered Placement Correction Retrace plugin" << std::endl;
        }
    };

    static PlacementCorrectionProcessRegistrar s_registrar;
}
