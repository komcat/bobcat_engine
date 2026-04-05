// LightingCommandHandler.h
#pragma once
#include "ICommandHandler.h"

/**
 * @brief Command handler for Lighting devices in sequence engine
 *
 * Supported commands:
 *   - SetOn: Turn channel on/off by name
 *   - SetIntensity: Set channel intensity by name (0-100%)
 *   - AllOff: Turn all channels off
 *   - AllOn: Turn all channels on at specified intensity
 *
 * Example usage in sequence:
 *   device: "light-main"
 *   method: "SetIntensity"
 *   params: { "channel": "Red", "intensity": "50" }
 */
class LightingCommandHandler : public ICommandHandler {
public:
    LightingCommandHandler() = default;
    ~LightingCommandHandler() override = default;

    /**
     * @brief Execute Lighting command
     * @param manager Pointer to LightingManager (cast from void*)
     * @param deviceName Name of lighting controller (device_name from config)
     * @param methodName Method/action to execute
     * @param params Parameters map
     * @param errorMessage Output error message
     * @param context Sequence context for variable storage
     * @return true if command successful
     */
    bool ExecuteCommand(
        void* manager,
        const std::string& deviceName,
        const std::string& methodName,
        const std::map<std::string, std::string>& params,
        std::string& errorMessage,
        SequenceContext* context
    ) override;

    std::string GetManagerType() const override { return "lighting"; }

private:
    /**
     * @brief Find device ID by device name
     */
    std::string FindDeviceIdByName(void* manager, const std::string& deviceName);

    /**
     * @brief Resolve channel param to channel number
     * Tries name lookup first, falls back to parsing as integer (0-7)
     * @return channel number, or -1 if not found
     */
    int ResolveChannel(void* manager, const std::string& deviceId, const std::string& channelParam);

    /**
     * @brief Parse intensity value (0-100%) from parameters
     */
    bool ParseIntensity(const std::map<std::string, std::string>& params, double& outIntensity);

    /**
     * @brief Parse on/off state from parameters
     */
    bool ParseOnOff(const std::map<std::string, std::string>& params, bool& outOn);
};
