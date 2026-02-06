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
            {"channel", "Red"},
            {"intensity", "50"}
        };
    }

    void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
        const auto& params = instance->parameters;

        // Device selector for lighting controller
        widget->addDeviceSelector("device_name", "Lighting Device", params.at("device_name"),
                                  static_cast<int>(DeviceComboBox::DeviceType::Lighting));

        // Channel name (text field - user types channel name from config)
        widget->addParameterField("channel", "Channel Name", params.at("channel"));

        // Intensity percentage
        widget->addParameterField("intensity", "Intensity (0-100%)", params.at("intensity"));
    }

    void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
        const auto& params = step->parameters;

        std::string deviceName = "light-main";
        std::string channel = "Red";
        std::string intensity = "50";

        auto it = params.find("device_name");
        if (it != params.end()) deviceName = it->second;

        it = params.find("channel");
        if (it != params.end()) channel = it->second;

        it = params.find("intensity");
        if (it != params.end()) intensity = it->second;

        std::map<std::string, std::string> cmdParams;
        cmdParams["channel"] = channel;
        cmdParams["intensity"] = intensity;

        sequence->AddAction(deviceName, "SetIntensity", cmdParams);
        (void)step;
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
            {"channel", "Red"}
        };
    }

    void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
        const auto& params = instance->parameters;

        widget->addDeviceSelector("device_name", "Lighting Device", params.at("device_name"),
                                  static_cast<int>(DeviceComboBox::DeviceType::Lighting));

        widget->addParameterField("channel", "Channel Name", params.at("channel"));
    }

    void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
        const auto& params = step->parameters;

        std::string deviceName = "light-main";
        std::string channel = "Red";

        auto it = params.find("device_name");
        if (it != params.end()) deviceName = it->second;

        it = params.find("channel");
        if (it != params.end()) channel = it->second;

        std::map<std::string, std::string> cmdParams;
        cmdParams["channel"] = channel;

        sequence->AddAction(deviceName, "TurnOn", cmdParams);
        (void)step;
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
            {"channel", "Red"}
        };
    }

    void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
        const auto& params = instance->parameters;

        widget->addDeviceSelector("device_name", "Lighting Device", params.at("device_name"),
                                  static_cast<int>(DeviceComboBox::DeviceType::Lighting));

        widget->addParameterField("channel", "Channel Name", params.at("channel"));
    }

    void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
        const auto& params = step->parameters;

        std::string deviceName = "light-main";
        std::string channel = "Red";

        auto it = params.find("device_name");
        if (it != params.end()) deviceName = it->second;

        it = params.find("channel");
        if (it != params.end()) channel = it->second;

        std::map<std::string, std::string> cmdParams;
        cmdParams["channel"] = channel;

        sequence->AddAction(deviceName, "TurnOff", cmdParams);
        (void)step;
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
            {"device_name", "light-main"}
        };
    }

    void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
        const auto& params = instance->parameters;

        widget->addDeviceSelector("device_name", "Lighting Device", params.at("device_name"),
                                  static_cast<int>(DeviceComboBox::DeviceType::Lighting));
    }

    void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
        const auto& params = step->parameters;

        std::string deviceName = "light-main";

        auto it = params.find("device_name");
        if (it != params.end()) deviceName = it->second;

        sequence->AddAction(deviceName, "AllOff", {});
        (void)step;
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
            {"intensity", "50"}
        };
    }

    void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
        const auto& params = instance->parameters;

        widget->addDeviceSelector("device_name", "Lighting Device", params.at("device_name"),
                                  static_cast<int>(DeviceComboBox::DeviceType::Lighting));

        widget->addParameterField("intensity", "Intensity (0-100%)", params.at("intensity"));
    }

    void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
        const auto& params = step->parameters;

        std::string deviceName = "light-main";
        std::string intensity = "50";

        auto it = params.find("device_name");
        if (it != params.end()) deviceName = it->second;

        it = params.find("intensity");
        if (it != params.end()) intensity = it->second;

        std::map<std::string, std::string> cmdParams;
        cmdParams["intensity"] = intensity;

        sequence->AddAction(deviceName, "AllOn", cmdParams);
        (void)step;
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

        std::cout << "[LightingProcesses] Registered 5 lighting process plugins" << std::endl;
    }
};

static LightingProcessesRegistrar s_registrar;

} // anonymous namespace
