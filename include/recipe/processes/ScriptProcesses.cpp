// ScriptProcesses.cpp - Recipe process plugin for Lua script execution
#include "../IProcessPlugin.h"
#include "../ProcessPluginRegistry.h"
#include "../RecipeManager.h"
#include "../../sequence/SequenceBuilder.h"
#include "../../AppUI/RecipePage/ProcessConfigWidget.h"
#include <iostream>
#include <memory>
#include <map>
#include <string>

class RunScriptPlugin : public IProcessPlugin
{
public:
    std::string GetName() const override { return "Run Script"; }
    std::string GetCategory() const override { return "System"; }
    std::string GetDescription() const override {
        return "Execute a Lua script file";
    }

    std::map<std::string, std::string> GetDefaultParameters() const override {
        return {
            {"script_path", "scripts/example_basic.lua"}
        };
    }

    void CreateConfigUI(ProcessConfigWidget* widget,
                        const ProcessInstance* instance) override {
        const auto& params = instance->parameters;
        widget->addParameterField("script_path", "Script Path",
                                  params.at("script_path"));
    }

    void BuildSequence(SequenceBuilder* sequence,
                       const ProcessInstance* step) override {
        const auto& params = step->parameters;
        std::map<std::string, std::string> scriptParams;
        scriptParams["path"] = params.at("script_path");
        sequence->AddAction("scripting", "RunFile", scriptParams);
    }
};

class RunScriptInlinePlugin : public IProcessPlugin
{
public:
    std::string GetName() const override { return "Run Script (Inline)"; }
    std::string GetCategory() const override { return "System"; }
    std::string GetDescription() const override {
        return "Execute inline Lua code";
    }

    std::map<std::string, std::string> GetDefaultParameters() const override {
        return {
            {"code", "log.info(\"Hello from Lua\")"}
        };
    }

    void CreateConfigUI(ProcessConfigWidget* widget,
                        const ProcessInstance* instance) override {
        const auto& params = instance->parameters;
        widget->addParameterField("code", "Lua Code", params.at("code"));
    }

    void BuildSequence(SequenceBuilder* sequence,
                       const ProcessInstance* step) override {
        const auto& params = step->parameters;
        std::map<std::string, std::string> scriptParams;
        scriptParams["code"] = params.at("code");
        sequence->AddAction("scripting", "RunString", scriptParams);
    }
};

//=============================================================================
// AUTO-REGISTRATION
//=============================================================================

namespace {
    struct RegisterScriptProcesses {
        RegisterScriptProcesses() {
            auto& registry = ProcessPluginRegistry::Instance();
            registry.RegisterPlugin(std::make_unique<RunScriptPlugin>());
            registry.RegisterPlugin(std::make_unique<RunScriptInlinePlugin>());
            std::cout << "[ScriptProcesses] Registered 2 script process plugins" << std::endl;
        }
    };
    static RegisterScriptProcesses g_registerScriptProcesses;
}
