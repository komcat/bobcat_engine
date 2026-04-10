// LightingProcesses.cpp - Lighting control process plugins for recipe system

#include "../IProcessPlugin.h"
#include "../ProcessPluginRegistry.h"
#include "../../sequence/SequenceBuilder.h"
#include "../../AppUI/RecipePage/ProcessConfigWidget.h"
#include "../../AppUI/RecipePage/DeviceSelectorWidgets.h"
#include "../RecipeManager.h"

#include <iostream>
#include <memory>
#include <map>
#include <string>

namespace {

//=============================================================================
// HELPERS
//=============================================================================

static std::string GetParam(const std::map<std::string, std::string>& params,
                            const std::string& key, const std::string& defaultVal) {
    auto it = params.find(key);
    return (it != params.end()) ? it->second : defaultVal;
}

static void AddDelayAction(SequenceBuilder* sequence, const std::string& msStr) {
    if (msStr.empty() || msStr == "0") return;
    try {
        int ms = static_cast<int>(std::stod(msStr));
        if (ms <= 0) return;
        sequence->AddAction("system", "Wait", {{"milliseconds", std::to_string(ms)}});
    } catch (...) {}
}

static void AddDelayUI(ProcessConfigWidget* widget,
                       const std::map<std::string, std::string>& params) {
    widget->addNumericParameter("delay_before_ms", "Delay Before (ms)",
        GetParam(params, "delay_before_ms", "0"), 0, 60000, 100, 0, "ms");
    widget->addNumericParameter("delay_after_ms", "Delay After (ms)",
        GetParam(params, "delay_after_ms", "0"), 0, 60000, 100, 0, "ms");
}

//=============================================================================
// LIGHT SET INTENSITY PLUGIN
//=============================================================================

class LightSetIntensityPlugin : public IProcessPlugin
{
public:
    std::string GetName() const override {
        return "Light: Set Intensity";
    }

    std::string GetCategory() const override {
        return "Vision";
    }

    std::string GetDescription() const override {
        return "Set lighting channel intensity (0-100%)";
    }

    std::map<std::string, std::string> GetDefaultParameters() const override {
        return {
            {"device_name", "light-main"},
            {"group", ""},
            {"channel", "Red"},
            {"intensity", "50"},
            {"delay_before_ms", "0"},
            {"delay_after_ms", "0"}
        };
    }

    void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
        const auto& params = instance->parameters;

        widget->addDeviceSelector("device_name", "Lighting Device", params.at("device_name"),
                                  static_cast<int>(DeviceComboBox::DeviceType::Lighting));

        widget->addParameterField("group", "Group (e.g. light1, empty=any)",
                                  GetParam(params, "group", ""));

        widget->addParameterField("channel", "Channel Name", params.at("channel"));
        widget->addParameterField("intensity", "Intensity (0-100%)", params.at("intensity"));

        AddDelayUI(widget, params);
    }

    void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
        const auto& params = step->parameters;

        AddDelayAction(sequence, GetParam(params, "delay_before_ms", "0"));

        std::map<std::string, std::string> cmdParams;
        cmdParams["channel"] = GetParam(params, "channel", "Red");
        cmdParams["intensity"] = GetParam(params, "intensity", "50");

        sequence->AddAction(GetParam(params, "device_name", "light-main"),
                           "SetIntensity", cmdParams);

        AddDelayAction(sequence, GetParam(params, "delay_after_ms", "0"));
    }
};

//=============================================================================
// LIGHT TURN ON PLUGIN
//=============================================================================

class LightTurnOnPlugin : public IProcessPlugin
{
public:
    std::string GetName() const override {
        return "Light: Turn On";
    }

    std::string GetCategory() const override {
        return "Vision";
    }

    std::string GetDescription() const override {
        return "Turn on a lighting channel";
    }

    std::map<std::string, std::string> GetDefaultParameters() const override {
        return {
            {"device_name", "light-main"},
            {"group", ""},
            {"channel", "Red"},
            {"delay_before_ms", "0"},
            {"delay_after_ms", "0"}
        };
    }

    void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
        const auto& params = instance->parameters;

        widget->addDeviceSelector("device_name", "Lighting Device", params.at("device_name"),
                                  static_cast<int>(DeviceComboBox::DeviceType::Lighting));

        widget->addParameterField("group", "Group (e.g. light1, empty=any)",
                                  GetParam(params, "group", ""));

        widget->addParameterField("channel", "Channel Name", params.at("channel"));

        AddDelayUI(widget, params);
    }

    void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
        const auto& params = step->parameters;

        AddDelayAction(sequence, GetParam(params, "delay_before_ms", "0"));

        std::map<std::string, std::string> cmdParams;
        cmdParams["channel"] = GetParam(params, "channel", "Red");

        sequence->AddAction(GetParam(params, "device_name", "light-main"),
                           "TurnOn", cmdParams);

        AddDelayAction(sequence, GetParam(params, "delay_after_ms", "0"));
    }
};

//=============================================================================
// LIGHT TURN OFF PLUGIN
//=============================================================================

class LightTurnOffPlugin : public IProcessPlugin
{
public:
    std::string GetName() const override {
        return "Light: Turn Off";
    }

    std::string GetCategory() const override {
        return "Vision";
    }

    std::string GetDescription() const override {
        return "Turn off a lighting channel";
    }

    std::map<std::string, std::string> GetDefaultParameters() const override {
        return {
            {"device_name", "light-main"},
            {"group", ""},
            {"channel", "Red"},
            {"delay_before_ms", "0"},
            {"delay_after_ms", "0"}
        };
    }

    void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
        const auto& params = instance->parameters;

        widget->addDeviceSelector("device_name", "Lighting Device", params.at("device_name"),
                                  static_cast<int>(DeviceComboBox::DeviceType::Lighting));

        widget->addParameterField("group", "Group (e.g. light1, empty=any)",
                                  GetParam(params, "group", ""));

        widget->addParameterField("channel", "Channel Name", params.at("channel"));

        AddDelayUI(widget, params);
    }

    void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
        const auto& params = step->parameters;

        AddDelayAction(sequence, GetParam(params, "delay_before_ms", "0"));

        std::map<std::string, std::string> cmdParams;
        cmdParams["channel"] = GetParam(params, "channel", "Red");

        sequence->AddAction(GetParam(params, "device_name", "light-main"),
                           "TurnOff", cmdParams);

        AddDelayAction(sequence, GetParam(params, "delay_after_ms", "0"));
    }
};

//=============================================================================
// LIGHT ALL OFF PLUGIN
//=============================================================================

class LightAllOffPlugin : public IProcessPlugin
{
public:
    std::string GetName() const override {
        return "Light: All Off";
    }

    std::string GetCategory() const override {
        return "Vision";
    }

    std::string GetDescription() const override {
        return "Turn off all lighting channels";
    }

    std::map<std::string, std::string> GetDefaultParameters() const override {
        return {
            {"device_name", "light-main"},
            {"delay_before_ms", "0"},
            {"delay_after_ms", "0"}
        };
    }

    void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
        const auto& params = instance->parameters;

        widget->addDeviceSelector("device_name", "Lighting Device", params.at("device_name"),
                                  static_cast<int>(DeviceComboBox::DeviceType::Lighting));

        AddDelayUI(widget, params);
    }

    void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
        const auto& params = step->parameters;

        AddDelayAction(sequence, GetParam(params, "delay_before_ms", "0"));

        sequence->AddAction(GetParam(params, "device_name", "light-main"),
                           "AllOff", {});

        AddDelayAction(sequence, GetParam(params, "delay_after_ms", "0"));
    }
};

//=============================================================================
// LIGHT ALL ON PLUGIN
//=============================================================================

class LightAllOnPlugin : public IProcessPlugin
{
public:
    std::string GetName() const override {
        return "Light: All On";
    }

    std::string GetCategory() const override {
        return "Vision";
    }

    std::string GetDescription() const override {
        return "Turn on all lighting channels at specified intensity";
    }

    std::map<std::string, std::string> GetDefaultParameters() const override {
        return {
            {"device_name", "light-main"},
            {"intensity", "50"},
            {"delay_before_ms", "0"},
            {"delay_after_ms", "0"}
        };
    }

    void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
        const auto& params = instance->parameters;

        widget->addDeviceSelector("device_name", "Lighting Device", params.at("device_name"),
                                  static_cast<int>(DeviceComboBox::DeviceType::Lighting));

        widget->addParameterField("intensity", "Intensity (0-100%)", params.at("intensity"));

        AddDelayUI(widget, params);
    }

    void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
        const auto& params = step->parameters;

        AddDelayAction(sequence, GetParam(params, "delay_before_ms", "0"));

        std::map<std::string, std::string> cmdParams;
        cmdParams["intensity"] = GetParam(params, "intensity", "50");

        sequence->AddAction(GetParam(params, "device_name", "light-main"),
                           "AllOn", cmdParams);

        AddDelayAction(sequence, GetParam(params, "delay_after_ms", "0"));
    }
};

//=============================================================================
// LIGHT CONFIGURE PLUGIN
// Set per-color intensities for a specific light group in one step.
//=============================================================================

class LightConfigurePlugin : public IProcessPlugin
{
public:
    std::string GetName() const override {
        return "Light: Configure";
    }

    std::string GetCategory() const override {
        return "Vision";
    }

    std::string GetDescription() const override {
        return "Configure per-color intensities for a light group (channels not listed are turned off)";
    }

    std::map<std::string, std::string> GetDefaultParameters() const override {
        return {
            {"device_name", "light-main"},
            {"group", "light1"},
            {"white_intensity", "0"},
            {"red_intensity", "0"},
            {"green_intensity", "0"},
            {"blue_intensity", "0"},
            {"delay_before_ms", "0"},
            {"delay_after_ms", "0"}
        };
    }

    void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
        const auto& params = instance->parameters;

        widget->addDeviceSelector("device_name", "Lighting Device",
                                  GetParam(params, "device_name", "light-main"),
                                  static_cast<int>(DeviceComboBox::DeviceType::Lighting));

        widget->addParameterField("group", "Light Group (e.g. light1)",
                                  GetParam(params, "group", "light1"));

        widget->addNumericParameter("white_intensity", "White / Collimate (%)",
                                    GetParam(params, "white_intensity", "0"),
                                    0.0, 100.0, 5.0, 1, "%");

        widget->addNumericParameter("red_intensity", "Red (%)",
                                    GetParam(params, "red_intensity", "0"),
                                    0.0, 100.0, 5.0, 1, "%");

        widget->addNumericParameter("green_intensity", "Green (%)",
                                    GetParam(params, "green_intensity", "0"),
                                    0.0, 100.0, 5.0, 1, "%");

        widget->addNumericParameter("blue_intensity", "Blue (%)",
                                    GetParam(params, "blue_intensity", "0"),
                                    0.0, 100.0, 5.0, 1, "%");

        AddDelayUI(widget, params);
    }

    void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
        const auto& params = step->parameters;

        AddDelayAction(sequence, GetParam(params, "delay_before_ms", "0"));

        std::map<std::string, std::string> cmdParams;
        cmdParams["group"] = GetParam(params, "group", "light1");
        cmdParams["white_intensity"] = GetParam(params, "white_intensity", "0");
        cmdParams["red_intensity"] = GetParam(params, "red_intensity", "0");
        cmdParams["green_intensity"] = GetParam(params, "green_intensity", "0");
        cmdParams["blue_intensity"] = GetParam(params, "blue_intensity", "0");

        sequence->AddAction(GetParam(params, "device_name", "light-main"),
                           "Configure", cmdParams);

        AddDelayAction(sequence, GetParam(params, "delay_after_ms", "0"));
    }
};

//=============================================================================
// STATIC REGISTRATION
//=============================================================================

struct LightingProcessesRegistrar {
    LightingProcessesRegistrar() {
        auto& registry = ProcessPluginRegistry::Instance();

        registry.RegisterPlugin(std::make_unique<LightSetIntensityPlugin>());
        std::cout << "[ProcessPluginRegistry] Registered plugin: Light: Set Intensity (Category: Vision)" << std::endl;

        registry.RegisterPlugin(std::make_unique<LightTurnOnPlugin>());
        std::cout << "[ProcessPluginRegistry] Registered plugin: Light: Turn On (Category: Vision)" << std::endl;

        registry.RegisterPlugin(std::make_unique<LightTurnOffPlugin>());
        std::cout << "[ProcessPluginRegistry] Registered plugin: Light: Turn Off (Category: Vision)" << std::endl;

        registry.RegisterPlugin(std::make_unique<LightAllOffPlugin>());
        std::cout << "[ProcessPluginRegistry] Registered plugin: Light: All Off (Category: Vision)" << std::endl;

        registry.RegisterPlugin(std::make_unique<LightAllOnPlugin>());
        std::cout << "[ProcessPluginRegistry] Registered plugin: Light: All On (Category: Vision)" << std::endl;

        registry.RegisterPlugin(std::make_unique<LightConfigurePlugin>());
        std::cout << "[ProcessPluginRegistry] Registered plugin: Light: Configure (Category: Vision)" << std::endl;

        std::cout << "[LightingProcesses] Registered 6 lighting process plugins" << std::endl;
    }
};

static LightingProcessesRegistrar s_registrar;

} // anonymous namespace
