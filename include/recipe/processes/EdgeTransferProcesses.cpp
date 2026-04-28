// EdgeTransferProcesses.cpp
// Recipe plugin: Run Edge Transfer Alignment

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
// RUN EDGE TRANSFER ALIGNMENT PLUGIN
// =============================================================================

class RunEdgeTransferAlignmentPlugin : public IProcessPlugin
{
public:
    std::string GetName() const override {
        return "Run Edge Transfer Alignment";
    }

    std::string GetCategory() const override {
        return "Calibration";
    }

    std::string GetDescription() const override {
        return "Run headless orientation transfer using a previously taught Edge Transfer set. "
               "Loads the set, computes per-axis corrections from reference vs target edge angles, "
               "and optionally applies them to the target hex device's U/V/W axes.";
    }

    std::map<std::string, std::string> GetDefaultParameters() const override {
        return {
            {"training_set_name", ""},
            {"apply_corrections", "true"}
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

        widget->addEdgeTransferSetSelector("training_set_name", "Edge Transfer Set",
                                           getParam("training_set_name"));

        widget->addCheckboxParameter("apply_corrections", "Apply Corrections to Hex",
                                     getParam("apply_corrections"),
                                     "If checked, move target hex U/V/W by the computed corrections. "
                                     "If unchecked, publish results to SequenceContext only.");
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
        std::string applyCorr = getParam("apply_corrections");

        sequence->AddAction("edgetransfer", "RunAlignment", {
            {"training_set_name", tsName},
            {"apply_corrections", applyCorr},
            {"timeout", "0"}
        });

        std::cout << "[EdgeTransferProcesses] Built sequence for: "
                  << step->userNickname << " (Set: " << tsName
                  << ", Apply: " << applyCorr << ")" << std::endl;
    }
};

// =============================================================================
// PLUGIN REGISTRATION (Static initialization)
// =============================================================================

namespace {
    struct EdgeTransferProcessRegistrar {
        EdgeTransferProcessRegistrar() {
            auto& registry = ProcessPluginRegistry::Instance();
            registry.RegisterPlugin(std::make_unique<RunEdgeTransferAlignmentPlugin>());
            std::cout << "[EdgeTransferProcesses] Registered Run Edge Transfer Alignment plugin" << std::endl;
        }
    };

    static EdgeTransferProcessRegistrar s_registrar;
}
