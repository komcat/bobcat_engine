// ZOffsetProcesses.cpp
// Recipe plugin: Run Z Offset Correction

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
// RUN Z OFFSET PLUGIN
// =============================================================================

class RunZOffsetPlugin : public IProcessPlugin
{
public:
    std::string GetName() const override {
        return "Run Z Offset";
    }

    std::string GetCategory() const override {
        return "Calibration";
    }

    std::string GetDescription() const override {
        return "Apply a Z offset correction to a hexapod Z axis using a previously "
               "taught two-surface Z offset training set.";
    }

    std::map<std::string, std::string> GetDefaultParameters() const override {
        return {
            {"training_set_name", ""},
            {"target_device", "hex-right"},
            {"target_z_diff_um", "150"},
            {"reverse_sign", "false"}
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

        // Training set selector (searches z_offset/ folder)
        widget->addZOffsetSetSelector("training_set_name", "Training Set",
                                       getParam("training_set_name"));

        // Target device
        widget->addDeviceSelector("target_device", "Target Device",
                                  getParam("target_device"), 0);

        // Target Z difference
        widget->addParameterField("target_z_diff_um", "Target Z Diff (um)",
                                  getParam("target_z_diff_um"));

        // Reverse sign
        widget->addCheckboxParameter("reverse_sign", "Reverse Sign",
                                     getParam("reverse_sign"));
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
        std::string targetDevice = getParam("target_device");
        std::string targetZDiffUm = getParam("target_z_diff_um");
        std::string reverseSign = getParam("reverse_sign");

        sequence->AddAction("zoffset", "RunZOffset", {
            {"training_set_name", tsName},
            {"target_device", targetDevice},
            {"target_z_diff_um", targetZDiffUm},
            {"reverse_sign", reverseSign},
            {"timeout", "0"}  // Disable action timeout (manual callbacks may block indefinitely)
        });

        std::cout << "[ZOffsetProcesses] Built sequence for: "
                  << step->userNickname << " (Set: " << tsName
                  << ", Target: " << targetDevice
                  << ", ZDiff: " << targetZDiffUm << "um"
                  << ", Reverse: " << reverseSign << ")" << std::endl;
    }
};

// =============================================================================
// PLUGIN REGISTRATION (Static initialization)
// =============================================================================

namespace {
    struct ZOffsetProcessRegistrar {
        ZOffsetProcessRegistrar() {
            auto& registry = ProcessPluginRegistry::Instance();
            registry.RegisterPlugin(std::make_unique<RunZOffsetPlugin>());
            std::cout << "[ZOffsetProcesses] Registered Run Z Offset plugin" << std::endl;
        }
    };

    static ZOffsetProcessRegistrar s_registrar;
}
