#pragma once
#include "ILightingController.h"
#include "LightingTypes.h"
#include <map>
#include <memory>
#include <vector>
#include <string>

namespace Lighting {

class LightingManager {
public:
    LightingManager();
    ~LightingManager();

    // Device Management
    bool AddController(const ControllerConfig& config);
    bool RemoveController(const std::string& deviceId);
    ILightingController* GetController(const std::string& deviceId);
    const ILightingController* GetController(const std::string& deviceId) const;
    std::vector<std::string> GetControllerIds() const;
    size_t GetControllerCount() const;

    // Connection Management
    bool Connect(const std::string& deviceId);
    bool Disconnect(const std::string& deviceId);
    int ConnectAll();
    int ConnectEnabledDevices();
    void DisconnectAll();
    bool IsConnected(const std::string& deviceId) const;

    // Channel Control by Device ID
    LightingResult SetIntensity(const std::string& deviceId, int channel, int intensity);
    LightingResult SetIntensityByName(const std::string& deviceId,
                                       const std::string& channelName,
                                       int intensity);
    LightingResult SetIntensityPercent(const std::string& deviceId, int channel, double percent);
    LightingResult SetOn(const std::string& deviceId, int channel, bool on);
    LightingResult SetAllOff(const std::string& deviceId);
    LightingResult SetAllIntensity(const std::string& deviceId, int intensity);

    // Global Control (all controllers)
    void AllOff();
    void AllOn(int intensity = 128);

    // Status
    ChannelStatus GetChannelStatus(const std::string& deviceId, int channel);
    ControllerStatus GetControllerStatus(const std::string& deviceId);
    std::vector<ControllerStatus> GetAllStatus();

    // Channel Name Resolution
    int GetChannelByName(const std::string& deviceId, const std::string& name) const;
    std::string GetChannelName(const std::string& deviceId, int channel) const;
    std::vector<ChannelConfig> GetChannelConfigs(const std::string& deviceId) const;

    // Configuration Access
    const ControllerConfig* GetControllerConfig(const std::string& deviceId) const;

    // Persistence
    void SetConfigFilePath(const std::string& path);
    void UpdateChannelDefaultIntensity(const std::string& deviceId, int channel, int intensity);
    void ApplyDefaultIntensities(const std::string& deviceId);
    bool SaveConfig();

private:
    std::unique_ptr<ILightingController> CreateController(const std::string& type);

    std::map<std::string, std::unique_ptr<ILightingController>> m_controllers;
    std::map<std::string, ControllerConfig> m_configs;
    std::string m_configFilePath;
};

} // namespace Lighting
