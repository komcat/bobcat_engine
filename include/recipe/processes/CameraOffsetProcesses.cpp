// CameraOffsetProcesses.cpp
// Recipe plugins for camera offset move operations

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
// MOVE TO HIGH-MAG CAMERA (relative offset)
// =============================================================================

class MoveToHighMagCameraPlugin : public IProcessPlugin
{
public:
    std::string GetName() const override {
        return "Move to High-Mag Camera";
    }

    std::string GetCategory() const override {
        return "Calibration";
    }

    std::string GetDescription() const override {
        return "Apply the calibrated camera offset to move from the current position "
               "to the high-magnification camera view. Adds the offset (dX, dY, dZ) "
               "to the current gantry position.";
    }

    std::map<std::string, std::string> GetDefaultParameters() const override {
        return {
            {"calibration_set_name", ""},
            {"velocity", "100"}
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

        widget->addCameraOffsetSetSelector("calibration_set_name", "Calibration Set",
                                           getParam("calibration_set_name"));
        widget->addParameterField("velocity", "Velocity", getParam("velocity"));
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

        std::string calName = getParam("calibration_set_name");
        std::string velocity = getParam("velocity");

        sequence->AddAction("cameraoffset", "MoveToHighMag", {
            {"calibration_set_name", calName},
            {"velocity", velocity}
        });

        std::cout << "[CameraOffsetProcesses] Built MoveToHighMag: "
                  << step->userNickname << " (set: " << calName << ")" << std::endl;
    }
};

// =============================================================================
// MOVE TO LOW-MAG CAMERA (relative offset)
// =============================================================================

class MoveToLowMagCameraPlugin : public IProcessPlugin
{
public:
    std::string GetName() const override {
        return "Move to Low-Mag Camera";
    }

    std::string GetCategory() const override {
        return "Calibration";
    }

    std::string GetDescription() const override {
        return "Apply the calibrated camera offset to move from the current position "
               "to the low-magnification camera view. Subtracts the offset (dX, dY, dZ) "
               "from the current gantry position.";
    }

    std::map<std::string, std::string> GetDefaultParameters() const override {
        return {
            {"calibration_set_name", ""},
            {"velocity", "100"}
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

        widget->addCameraOffsetSetSelector("calibration_set_name", "Calibration Set",
                                           getParam("calibration_set_name"));
        widget->addParameterField("velocity", "Velocity", getParam("velocity"));
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

        std::string calName = getParam("calibration_set_name");
        std::string velocity = getParam("velocity");

        sequence->AddAction("cameraoffset", "MoveToLowMag", {
            {"calibration_set_name", calName},
            {"velocity", velocity}
        });

        std::cout << "[CameraOffsetProcesses] Built MoveToLowMag: "
                  << step->userNickname << " (set: " << calName << ")" << std::endl;
    }
};

// =============================================================================
// MOVE TO HIGH-MAG CAMERA (absolute position)
// =============================================================================

class MoveToHighMagAbsPlugin : public IProcessPlugin
{
public:
    std::string GetName() const override {
        return "Move to High-Mag Camera (Absolute)";
    }

    std::string GetCategory() const override {
        return "Calibration";
    }

    std::string GetDescription() const override {
        return "Move the gantry to the absolute calibrated high-magnification camera position. "
               "Uses the saved XYZ coordinates from the calibration set.";
    }

    std::map<std::string, std::string> GetDefaultParameters() const override {
        return {
            {"calibration_set_name", ""},
            {"velocity", "100"}
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

        widget->addCameraOffsetSetSelector("calibration_set_name", "Calibration Set",
                                           getParam("calibration_set_name"));
        widget->addParameterField("velocity", "Velocity", getParam("velocity"));
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

        std::string calName = getParam("calibration_set_name");
        std::string velocity = getParam("velocity");

        sequence->AddAction("cameraoffset", "MoveToHighMagAbs", {
            {"calibration_set_name", calName},
            {"velocity", velocity}
        });

        std::cout << "[CameraOffsetProcesses] Built MoveToHighMagAbs: "
                  << step->userNickname << " (set: " << calName << ")" << std::endl;
    }
};

// =============================================================================
// MOVE TO LOW-MAG CAMERA (absolute position)
// =============================================================================

class MoveToLowMagAbsPlugin : public IProcessPlugin
{
public:
    std::string GetName() const override {
        return "Move to Low-Mag Camera (Absolute)";
    }

    std::string GetCategory() const override {
        return "Calibration";
    }

    std::string GetDescription() const override {
        return "Move the gantry to the absolute calibrated low-magnification camera position. "
               "Uses the saved XYZ coordinates from the calibration set.";
    }

    std::map<std::string, std::string> GetDefaultParameters() const override {
        return {
            {"calibration_set_name", ""},
            {"velocity", "100"}
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

        widget->addCameraOffsetSetSelector("calibration_set_name", "Calibration Set",
                                           getParam("calibration_set_name"));
        widget->addParameterField("velocity", "Velocity", getParam("velocity"));
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

        std::string calName = getParam("calibration_set_name");
        std::string velocity = getParam("velocity");

        sequence->AddAction("cameraoffset", "MoveToLowMagAbs", {
            {"calibration_set_name", calName},
            {"velocity", velocity}
        });

        std::cout << "[CameraOffsetProcesses] Built MoveToLowMagAbs: "
                  << step->userNickname << " (set: " << calName << ")" << std::endl;
    }
};

// =============================================================================
// PLUGIN REGISTRATION (Static initialization)
// =============================================================================

namespace {
    struct CameraOffsetProcessRegistrar {
        CameraOffsetProcessRegistrar() {
            auto& registry = ProcessPluginRegistry::Instance();
            registry.RegisterPlugin(std::make_unique<MoveToHighMagCameraPlugin>());
            registry.RegisterPlugin(std::make_unique<MoveToLowMagCameraPlugin>());
            registry.RegisterPlugin(std::make_unique<MoveToHighMagAbsPlugin>());
            registry.RegisterPlugin(std::make_unique<MoveToLowMagAbsPlugin>());
            std::cout << "[CameraOffsetProcesses] Registered 4 camera offset plugins" << std::endl;
        }
    };

    static CameraOffsetProcessRegistrar s_registrar;
}
