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
    // SET ON/OFF - Turn channel on or off by name or number
    // Uses the L (on/off) command, preserving current intensity
    // Params: channel (name or number), state (on/off/true/false/1/0)
    // ============================================================================
    if (methodName == "SetOn" || methodName == "SetState" || methodName == "TurnOn" || methodName == "TurnOff") {
        auto channelIt = params.find("channel");
        if (channelIt == params.end()) {
            errorMessage = "Missing required parameter: channel";
            return false;
        }

        int channelNumber = ResolveChannel(lightingManager, deviceId, channelIt->second);
        if (channelNumber < 0) {
            errorMessage = "Channel not found: '" + channelIt->second + "'";
            return false;
        }

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
            std::string channelName = lightingManager->GetChannelName(deviceId, channelNumber);
            m_logger->Info(
                "Lighting " + deviceName + ": " + channelName + " (L" +
                std::to_string(channelNumber + 1) + ") " + (turnOn ? "ON" : "OFF"),
                "LightingHandler"
            );
        }

        // Use SetOn (L command) to preserve current intensity
        auto result = lightingManager->SetOn(deviceId, channelNumber, turnOn);
        if (result != Lighting::LightingResult::Success) {
            errorMessage = "SetOn failed for channel " + std::to_string(channelNumber) + ": " +
                          Lighting::LightingResultToString(result);
            return false;
        }

        return true;
    }

    // ============================================================================
    // SET INTENSITY - Set channel intensity by name or number (percentage)
    // Params: channel (name or number), intensity (0-100%)
    // ============================================================================
    else if (methodName == "SetIntensity") {
        auto channelIt = params.find("channel");
        if (channelIt == params.end()) {
            errorMessage = "Missing required parameter: channel";
            return false;
        }

        int channelNumber = ResolveChannel(lightingManager, deviceId, channelIt->second);
        if (channelNumber < 0) {
            errorMessage = "Channel not found: '" + channelIt->second + "'";
            return false;
        }

        double intensityPercent;
        if (!ParseIntensity(params, intensityPercent)) {
            errorMessage = "Missing or invalid parameter: intensity (0-100)";
            return false;
        }

        int intensityValue = Lighting::PercentToIntensity(intensityPercent);

        if (m_logger) {
            std::string channelName = lightingManager->GetChannelName(deviceId, channelNumber);
            m_logger->Info(
                "Lighting " + deviceName + ": " + channelName + " (L" +
                std::to_string(channelNumber + 1) + ") intensity " +
                std::to_string(intensityPercent) + "% (" + std::to_string(intensityValue) + ")",
                "LightingHandler"
            );
        }

        auto result = lightingManager->SetIntensity(deviceId, channelNumber, intensityValue);
        if (result != Lighting::LightingResult::Success) {
            errorMessage = "SetIntensity failed for channel " + std::to_string(channelNumber) + ": " +
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
    // GET STATUS - Read and store current status of all channels
    // Params (optional): captureAs (prefix for context variables)
    // Context output:
    //   {deviceName}.connected → bool
    //   {deviceName}.{channelName}.intensity → float (0-100%)
    //   {deviceName}.{channelName}.on → bool
    //   If captureAs provided, also stores under {captureAs}.* prefix
    // ============================================================================
    else if (methodName == "GetStatus") {
        auto status = lightingManager->GetControllerStatus(deviceId);

        // Log status
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

        // Store results in SequenceContext
        if (context) {
            context->SetValue(deviceName + ".connected", status.connected,
                             deviceName, methodName);

            for (const auto& ch : status.channelStatuses) {
                std::string chName = ch.name.empty()
                    ? "L" + std::to_string(ch.channelNumber + 1)
                    : ch.name;

                float percent = static_cast<float>(Lighting::IntensityToPercent(ch.intensity));
                context->SetValue(deviceName + "." + chName + ".intensity", percent,
                                 deviceName, methodName);
                context->SetValue(deviceName + "." + chName + ".on", ch.isOn,
                                 deviceName, methodName);
            }

            // If captureAs provided, duplicate under that prefix
            auto captureAsIt = params.find("captureAs");
            if (captureAsIt != params.end() && !captureAsIt->second.empty()) {
                std::string prefix = captureAsIt->second;

                context->SetValue(prefix + ".connected", status.connected,
                                 deviceName, methodName);

                for (const auto& ch : status.channelStatuses) {
                    std::string chName = ch.name.empty()
                        ? "L" + std::to_string(ch.channelNumber + 1)
                        : ch.name;

                    float percent = static_cast<float>(Lighting::IntensityToPercent(ch.intensity));
                    context->SetValue(prefix + "." + chName + ".intensity", percent,
                                     deviceName, methodName);
                    context->SetValue(prefix + "." + chName + ".on", ch.isOn,
                                     deviceName, methodName);
                }

                if (m_logger) {
                    m_logger->Info("Status stored with prefix '" + prefix + "'",
                                 "LightingHandler");
                }
            }
        }

        return true;
    }

    // ============================================================================
    // CONFIGURE - Set multiple channels at once by color
    // Params: group (optional, e.g. "light1"), then per-color intensity:
    //   white_intensity, red_intensity, green_intensity, blue_intensity (0-100%)
    //   Channels not listed are turned off (intensity=0)
    // ============================================================================
    else if (methodName == "Configure") {
        // Optional group filter
        std::string group;
        auto groupIt = params.find("group");
        if (groupIt != params.end()) {
            group = groupIt->second;
        }

        auto channels = lightingManager->GetChannelConfigs(deviceId);

        if (m_logger) {
            m_logger->Info("Lighting " + deviceName + ": Configure" +
                          (group.empty() ? "" : " (group: " + group + ")"),
                          "LightingHandler");
        }

        for (const auto& ch : channels) {
            if (!ch.enabled) continue;

            // Filter by group if specified
            std::string chGroup = ch.group.empty() ? "default" : ch.group;
            if (!group.empty() && chGroup != group) continue;

            // Look for a matching intensity param by color (e.g., "red_intensity")
            std::string colorKey = ch.color;
            if (colorKey.empty()) {
                // Fall back to channel name
                colorKey = ch.name;
            }
            // Lowercase for param key
            std::transform(colorKey.begin(), colorKey.end(), colorKey.begin(),
                           [](unsigned char c) { return std::tolower(c); });

            std::string paramKey = colorKey + "_intensity";
            auto intensityIt = params.find(paramKey);

            int intensityValue = 0;  // Default: off
            if (intensityIt != params.end()) {
                try {
                    double percent = std::stod(intensityIt->second);
                    if (percent < 0.0) percent = 0.0;
                    if (percent > 100.0) percent = 100.0;
                    intensityValue = Lighting::PercentToIntensity(percent);
                }
                catch (...) {
                    // Invalid value, leave at 0
                }
            }

            auto result = lightingManager->SetIntensity(deviceId, ch.channelNumber, intensityValue);
            if (result != Lighting::LightingResult::Success) {
                if (m_logger) {
                    m_logger->Warning("Configure: failed to set channel " + ch.name +
                                     ": " + Lighting::LightingResultToString(result),
                                     "LightingHandler");
                }
            } else if (m_logger) {
                double percent = Lighting::IntensityToPercent(intensityValue);
                m_logger->Info("  " + ch.name + " (" + colorKey + "): " +
                             std::to_string(percent) + "%", "LightingHandler");
            }
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

int LightingCommandHandler::ResolveChannel(void* manager, const std::string& deviceId,
                                            const std::string& channelParam)
{
    Lighting::LightingManager* lightingManager = static_cast<Lighting::LightingManager*>(manager);

    // Try name lookup first (case-insensitive)
    int channel = lightingManager->GetChannelByName(deviceId, channelParam);
    if (channel >= 0) {
        return channel;
    }

    // Fall back to parsing as integer (0-7)
    try {
        int num = std::stoi(channelParam);
        if (num >= 0 && num <= 7) {
            return num;
        }
    }
    catch (...) {
        // Not a valid number either
    }

    return -1;
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
