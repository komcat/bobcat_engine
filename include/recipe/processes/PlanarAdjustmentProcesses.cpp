// PlanarAdjustmentProcesses.cpp
// Recipe plugin: Run Planar Adjustment

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
// RUN PLANAR ADJUSTMENT PLUGIN
// =============================================================================

class RunPlanarAdjustmentPlugin : public IProcessPlugin
{
public:
    std::string GetName() const override {
        return "Run Planar Adjustment";
    }

    std::string GetCategory() const override {
        return "Calibration";
    }

    std::string GetDescription() const override {
        return "Apply a planar tilt correction to a hexapod axis using a previously "
               "taught two-point planar adjustment training set.";
    }

    std::map<std::string, std::string> GetDefaultParameters() const override {
        return {
            {"training_set_name", ""},
            {"source_angle", "angle_x"},
            {"target_device", "hex-right"},
            {"target_axis", "U"},
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

        // Training set selector (searches planar_adjustment/ folder)
        widget->addPlanarAdjustmentSetSelector("training_set_name", "Training Set",
                                               getParam("training_set_name"));

        // Source angle
        widget->addParameterField("source_angle", "Source Angle (angle_x or angle_y)",
                                  getParam("source_angle"));

        // Target device
        widget->addDeviceSelector("target_device", "Target Device",
                                  getParam("target_device"), 0);

        // Target axis
        widget->addParameterField("target_axis", "Target Axis (U, V, or W)",
                                  getParam("target_axis"));

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
        std::string sourceAngle = getParam("source_angle");
        std::string targetDevice = getParam("target_device");
        std::string targetAxis = getParam("target_axis");
        std::string reverseSign = getParam("reverse_sign");

        sequence->AddAction("planaradjustment", "RunPlanarAdjustment", {
            {"training_set_name", tsName},
            {"source_angle", sourceAngle},
            {"target_device", targetDevice},
            {"target_axis", targetAxis},
            {"reverse_sign", reverseSign}
        });

        std::cout << "[PlanarAdjustmentProcesses] Built sequence for: "
                  << step->userNickname << " (Set: " << tsName
                  << ", Source: " << sourceAngle
                  << ", Target: " << targetDevice << "/" << targetAxis
                  << ", Reverse: " << reverseSign << ")" << std::endl;
    }
};

// =============================================================================
// PLUGIN REGISTRATION (Static initialization)
// =============================================================================

namespace {
    struct PlanarAdjustmentProcessRegistrar {
        PlanarAdjustmentProcessRegistrar() {
            auto& registry = ProcessPluginRegistry::Instance();
            registry.RegisterPlugin(std::make_unique<RunPlanarAdjustmentPlugin>());
            std::cout << "[PlanarAdjustmentProcesses] Registered Run Planar Adjustment plugin" << std::endl;
        }
    };

    static PlanarAdjustmentProcessRegistrar s_registrar;
}
