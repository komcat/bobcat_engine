// ScriptCommandHandler.h - Command handler for Lua script execution from recipes
#pragma once

#include "ICommandHandler.h"

class ScriptEngine;

class ScriptCommandHandler : public ICommandHandler
{
public:
    bool ExecuteCommand(
        void* manager,
        const std::string& deviceName,
        const std::string& methodName,
        const std::map<std::string, std::string>& params,
        std::string& errorMessage,
        SequenceContext* context) override;

    std::string GetManagerType() const override { return "scripting"; }

    void SetScriptEngine(ScriptEngine* engine) { m_scriptEngine = engine; }

private:
    ScriptEngine* m_scriptEngine = nullptr;
};
