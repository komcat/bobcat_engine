// LightingCommandHandler.cpp
#include "LightingCommandHandler.h"
#include "lighting/LightingManager.h"
#include "lighting/LightingTypes.h"
#include "SequenceContext.h"
#include "LogManager.h"
#include <sstream>
#include <algorithm>
#include <cctype>

bool LightingCommandHandler::ExecuteCommand(
    void* manager,
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context)
{
    Lighting::LightingManager* lightingManager = static_cast<Lighting::LightingManager*>(manager);

    // Find device ID by device name
    std::string deviceId = FindDeviceIdByName(lightingManager, deviceName);
    if (deviceId.empty()) {
        errorMessage = "Lighting device '" + deviceName + "' not found";
        return false;
    }

    // Check if device is connected
    if (!lightingManager->IsConnected(deviceId)) {
        errorMessage = "Lighting device '" + deviceName + "' is not connected";
        return false;
    }

    // ============================================================================
    // SET ON/OFF - Turn channel on or off by name
    // Params: channel (name), state (on/off/true/false/1/0)
    // ============================================================================
    if (methodName == "SetOn" || methodName == "SetState" || methodName == "TurnOn" || methodName == "TurnOff") {
        auto channelIt = params.find("channel");
        if (channelIt == params.end()) {
            errorMessage = "Missing required parameter: channel";
            return false;
        }

        std::string channelName = channelIt->second;
        bool turnOn = true;

        // For TurnOn/TurnOff, state is implicit
        if (methodName == "TurnOff") {
            turnOn = false;
        } else if (methodName == "TurnOn") {
            turnOn = true;
        } else {
            // Parse state from params
            if (!ParseOnOff(params, turnOn)) {
                errorMessage = "Missing or invalid parameter: state (use on/off/true/false/1/0)";
                return false;
            }
        }

        if (m_logger) {
            m_logger->Info(
                "Lighting " + deviceName + ": Setting " + channelName + " " + (turnOn ? "ON" : "OFF"),
                "LightingHandler"
            );
        }

        auto result = lightingManager->SetIntensityByName(deviceId, channelName, turnOn ? 128 : 0);
        if (result != Lighting::LightingResult::Success) {
            errorMessage = "SetOn failed for channel '" + channelName + "': " +
                          Lighting::LightingResultToString(result);
            return false;
        }

        return true;
    }

    // ============================================================================
    // SET INTENSITY - Set channel intensity by name (percentage)
    // Params: channel (name), intensity (0-100%)
    // ============================================================================
    else if (methodName == "SetIntensity") {
        auto channelIt = params.find("channel");
        if (channelIt == params.end()) {
            errorMessage = "Missing required parameter: channel";
            return false;
        }

        std::string channelName = channelIt->second;
        double intensityPercent;

        if (!ParseIntensity(params, intensityPercent)) {
            errorMessage = "Missing or invalid parameter: intensity (0-100)";
            return false;
        }

        int intensityValue = Lighting::PercentToIntensity(intensityPercent);

        if (m_logger) {
            m_logger->Info(
                "Lighting " + deviceName + ": Setting " + channelName + " intensity to " +
                std::to_string(intensityPercent) + "% (" + std::to_string(intensityValue) + ")",
                "LightingHandler"
            );
        }

        auto result = lightingManager->SetIntensityByName(deviceId, channelName, intensityValue);
        if (result != Lighting::LightingResult::Success) {
            errorMessage = "SetIntensity failed for channel '" + channelName + "': " +
                          Lighting::LightingResultToString(result);
            return false;
        }

        return true;
    }

    // ============================================================================
    // ALL OFF - Turn all channels off
    // ============================================================================
    else if (methodName == "AllOff") {
        if (m_logger) {
            m_logger->Info("Lighting " + deviceName + ": All channels OFF", "LightingHandler");
        }

        auto result = lightingManager->SetAllOff(deviceId);
        if (result != Lighting::LightingResult::Success) {
            errorMessage = "AllOff failed: " + Lighting::LightingResultToString(result);
            return false;
        }

        return true;
    }

    // ============================================================================
    // ALL ON - Turn all channels on at specified intensity
    // Params: intensity (0-100%, default 50%)
    // ============================================================================
    else if (methodName == "AllOn") {
        double intensityPercent = 50.0;  // Default 50%
        ParseIntensity(params, intensityPercent);  // Optional

        int intensityValue = Lighting::PercentToIntensity(intensityPercent);

        if (m_logger) {
            m_logger->Info(
                "Lighting " + deviceName + ": All channels ON at " +
                std::to_string(intensityPercent) + "%",
                "LightingHandler"
            );
        }

        auto result = lightingManager->SetAllIntensity(deviceId, intensityValue);
        if (result != Lighting::LightingResult::Success) {
            errorMessage = "AllOn failed: " + Lighting::LightingResultToString(result);
            return false;
        }

        return true;
    }

    // ============================================================================
    // GET STATUS - Log current status of all channels
    // ============================================================================
    else if (methodName == "GetStatus") {
        auto status = lightingManager->GetControllerStatus(deviceId);

        if (m_logger) {
            std::ostringstream oss;
            oss << deviceName << " Status:\n";
            oss << "  Connected: " << (status.connected ? "Yes" : "No") << "\n";

            for (const auto& ch : status.channelStatuses) {
                double percent = Lighting::IntensityToPercent(ch.intensity);
                oss << "  " << ch.name << " (L" << (ch.channelNumber + 1) << "): "
                    << (ch.isOn ? "ON" : "OFF")
                    << " @ " << percent << "% (" << ch.intensity << ")\n";
            }

            m_logger->Info(oss.str(), "LightingHandler");
        }

        return true;
    }

    // ============================================================================
    // UNKNOWN METHOD
    // ============================================================================
    else {
        errorMessage = "Unknown Lighting method: " + methodName;
        return false;
    }
}

std::string LightingCommandHandler::FindDeviceIdByName(void* manager, const std::string& deviceName)
{
    Lighting::LightingManager* lightingManager = static_cast<Lighting::LightingManager*>(manager);

    for (const auto& deviceId : lightingManager->GetControllerIds()) {
        const auto* config = lightingManager->GetControllerConfig(deviceId);
        if (config && config->deviceName == deviceName) {
            return deviceId;
        }
        // Also check if deviceName matches deviceId directly
        if (deviceId == deviceName) {
            return deviceId;
        }
    }

    return "";
}

bool LightingCommandHandler::ParseIntensity(
    const std::map<std::string, std::string>& params,
    double& outIntensity)
{
    auto it = params.find("intensity");
    if (it == params.end()) {
        return false;
    }

    try {
        outIntensity = std::stod(it->second);

        // Clamp to valid range
        if (outIntensity < 0.0) outIntensity = 0.0;
        if (outIntensity > 100.0) outIntensity = 100.0;

        return true;
    }
    catch (...) {
        return false;
    }
}

bool LightingCommandHandler::ParseOnOff(
    const std::map<std::string, std::string>& params,
    bool& outOn)
{
    auto it = params.find("state");
    if (it == params.end()) {
        return false;
    }

    std::string value = it->second;
    // Convert to lowercase for comparison
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (value == "on" || value == "true" || value == "1" || value == "yes") {
        outOn = true;
        return true;
    }
    else if (value == "off" || value == "false" || value == "0" || value == "no") {
        outOn = false;
        return true;
    }

    return false;
}
