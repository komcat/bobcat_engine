// ScriptCommandHandler.cpp - Handles RunFile and RunString methods
#include "ScriptCommandHandler.h"
#include "ScriptEngine.h"
#include "LogManager.h"

bool ScriptCommandHandler::ExecuteCommand(
    void* /*manager*/,
    const std::string& /*deviceName*/,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* /*context*/)
{
    if (!m_scriptEngine) {
        errorMessage = "Script engine not initialized";
        return false;
    }

    if (m_scriptEngine->IsRunning()) {
        errorMessage = "Script engine is busy — another script is already running";
        return false;
    }

    if (methodName == "RunFile") {
        auto it = params.find("path");
        if (it == params.end() || it->second.empty()) {
            errorMessage = "RunFile requires 'path' parameter";
            return false;
        }

        if (m_logger) {
            m_logger->Info("[ScriptCmd] Running script: " + it->second, "ScriptCmd");
        }

        return m_scriptEngine->ExecuteFile(it->second, errorMessage);
    }

    if (methodName == "RunString") {
        auto it = params.find("code");
        if (it == params.end() || it->second.empty()) {
            errorMessage = "RunString requires 'code' parameter";
            return false;
        }

        if (m_logger) {
            m_logger->Debug("[ScriptCmd] Running inline script", "ScriptCmd");
        }

        return m_scriptEngine->ExecuteString(it->second, errorMessage);
    }

    errorMessage = "Unknown scripting method: " + methodName;
    return false;
}
