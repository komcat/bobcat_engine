#include "lighting/LightingManager.h"
#include "lighting/LightingJsonLoader.h"
#include "lighting/CCSPD3Adapter.h"
#include <iostream>
#include <algorithm>

namespace Lighting {

LightingManager::LightingManager() {
    std::cout << "[LightingManager] Created" << std::endl;
}

LightingManager::~LightingManager() {
    DisconnectAll();
    std::cout << "[LightingManager] Destroyed" << std::endl;
}

std::unique_ptr<ILightingController> LightingManager::CreateController(const std::string& type) {
    if (type == "CCS_PD3" || type == "ccs_pd3") {
        return std::make_unique<CCSPD3Adapter>();
    }

    std::cerr << "[LightingManager] Unknown controller type: " << type << std::endl;
    return nullptr;
}

bool LightingManager::AddController(const ControllerConfig& config) {
    if (config.deviceId.empty()) {
        std::cerr << "[LightingManager] Cannot add controller with empty deviceId" << std::endl;
        return false;
    }

    if (m_controllers.find(config.deviceId) != m_controllers.end()) {
        std::cerr << "[LightingManager] Controller already exists: " << config.deviceId << std::endl;
        return false;
    }

    auto controller = CreateController(config.controllerType);
    if (!controller) {
        std::cerr << "[LightingManager] Failed to create controller: " << config.controllerType << std::endl;
        return false;
    }

    if (!controller->Initialize(config)) {
        std::cerr << "[LightingManager] Failed to initialize controller: " << config.deviceId << std::endl;
        return false;
    }

    m_configs[config.deviceId] = config;
    m_controllers[config.deviceId] = std::move(controller);

    std::cout << "[LightingManager] Added controller: " << config.deviceName
              << " (" << config.deviceId << ") - " << config.controllerType << std::endl;
    return true;
}

bool LightingManager::RemoveController(const std::string& deviceId) {
    auto it = m_controllers.find(deviceId);
    if (it == m_controllers.end()) {
        return false;
    }

    it->second->Disconnect();
    m_controllers.erase(it);
    m_configs.erase(deviceId);

    std::cout << "[LightingManager] Removed controller: " << deviceId << std::endl;
    return true;
}

ILightingController* LightingManager::GetController(const std::string& deviceId) {
    auto it = m_controllers.find(deviceId);
    return (it != m_controllers.end()) ? it->second.get() : nullptr;
}

const ILightingController* LightingManager::GetController(const std::string& deviceId) const {
    auto it = m_controllers.find(deviceId);
    return (it != m_controllers.end()) ? it->second.get() : nullptr;
}

std::vector<std::string> LightingManager::GetControllerIds() const {
    std::vector<std::string> ids;
    for (const auto& pair : m_controllers) {
        ids.push_back(pair.first);
    }
    return ids;
}

size_t LightingManager::GetControllerCount() const {
    return m_controllers.size();
}

bool LightingManager::Connect(const std::string& deviceId) {
    auto* controller = GetController(deviceId);
    if (!controller) {
        std::cerr << "[LightingManager] Controller not found: " << deviceId << std::endl;
        return false;
    }
    return controller->Connect();
}

bool LightingManager::Disconnect(const std::string& deviceId) {
    auto* controller = GetController(deviceId);
    if (!controller) {
        return false;
    }
    return controller->Disconnect();
}

int LightingManager::ConnectAll() {
    int count = 0;
    for (auto& pair : m_controllers) {
        if (pair.second->Connect()) {
            count++;
        }
    }
    std::cout << "[LightingManager] Connected " << count << " of "
              << m_controllers.size() << " controllers" << std::endl;
    return count;
}

int LightingManager::ConnectEnabledDevices() {
    int count = 0;
    for (auto& pair : m_controllers) {
        auto it = m_configs.find(pair.first);
        if (it != m_configs.end() && it->second.enabled) {
            if (pair.second->Connect()) {
                count++;
            }
        }
    }
    std::cout << "[LightingManager] Connected " << count << " enabled controller(s)" << std::endl;
    return count;
}

void LightingManager::DisconnectAll() {
    for (auto& pair : m_controllers) {
        pair.second->Disconnect();
    }
    std::cout << "[LightingManager] Disconnected all controllers" << std::endl;
}

bool LightingManager::IsConnected(const std::string& deviceId) const {
    auto* controller = GetController(deviceId);
    return controller ? controller->IsConnected() : false;
}

LightingResult LightingManager::SetIntensity(const std::string& deviceId, int channel, int intensity) {
    auto* controller = GetController(deviceId);
    if (!controller) {
        return LightingResult::DeviceNotFound;
    }
    return controller->SetChannelIntensity(channel, intensity);
}

LightingResult LightingManager::SetIntensityByName(const std::string& deviceId,
                                                    const std::string& channelName,
                                                    int intensity) {
    int channel = GetChannelByName(deviceId, channelName);
    if (channel < 0) {
        return LightingResult::InvalidChannel;
    }
    return SetIntensity(deviceId, channel, intensity);
}

LightingResult LightingManager::SetIntensityPercent(const std::string& deviceId,
                                                     int channel,
                                                     double percent) {
    int intensity = PercentToIntensity(percent);
    return SetIntensity(deviceId, channel, intensity);
}

LightingResult LightingManager::SetOn(const std::string& deviceId, int channel, bool on) {
    auto* controller = GetController(deviceId);
    if (!controller) {
        return LightingResult::DeviceNotFound;
    }
    return controller->SetChannelOn(channel, on);
}

LightingResult LightingManager::SetAllOff(const std::string& deviceId) {
    auto* controller = GetController(deviceId);
    if (!controller) {
        return LightingResult::DeviceNotFound;
    }
    return controller->SetAllChannelsOff();
}

LightingResult LightingManager::SetAllIntensity(const std::string& deviceId, int intensity) {
    auto* controller = GetController(deviceId);
    if (!controller) {
        return LightingResult::DeviceNotFound;
    }
    return controller->SetAllChannelsIntensity(intensity);
}

void LightingManager::AllOff() {
    for (auto& pair : m_controllers) {
        if (pair.second->IsConnected()) {
            pair.second->SetAllChannelsOff();
        }
    }
    std::cout << "[LightingManager] All lights OFF" << std::endl;
}

void LightingManager::AllOn(int intensity) {
    for (auto& pair : m_controllers) {
        if (pair.second->IsConnected()) {
            pair.second->SetAllChannelsIntensity(intensity);
        }
    }
    std::cout << "[LightingManager] All lights ON at intensity " << intensity << std::endl;
}

ChannelStatus LightingManager::GetChannelStatus(const std::string& deviceId, int channel) {
    auto* controller = GetController(deviceId);
    if (!controller) {
        ChannelStatus status;
        status.channelNumber = channel;
        return status;
    }
    return controller->GetChannelStatus(channel);
}

ControllerStatus LightingManager::GetControllerStatus(const std::string& deviceId) {
    auto* controller = GetController(deviceId);
    if (!controller) {
        ControllerStatus status;
        status.deviceId = deviceId;
        status.connected = false;
        return status;
    }
    return controller->GetControllerStatus();
}

std::vector<ControllerStatus> LightingManager::GetAllStatus() {
    std::vector<ControllerStatus> statuses;
    for (auto& pair : m_controllers) {
        statuses.push_back(pair.second->GetControllerStatus());
    }
    return statuses;
}

int LightingManager::GetChannelByName(const std::string& deviceId, const std::string& name) const {
    auto it = m_configs.find(deviceId);
    if (it == m_configs.end()) {
        return -1;
    }

    // Case-insensitive search
    std::string lowerName = name;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

    for (const auto& ch : it->second.channels) {
        std::string lowerChName = ch.name;
        std::transform(lowerChName.begin(), lowerChName.end(), lowerChName.begin(), ::tolower);

        if (lowerChName == lowerName) {
            return ch.channelNumber;
        }

        // Also check color name
        std::string lowerColor = ch.color;
        std::transform(lowerColor.begin(), lowerColor.end(), lowerColor.begin(), ::tolower);
        if (!lowerColor.empty() && lowerColor == lowerName) {
            return ch.channelNumber;
        }
    }

    return -1;
}

std::string LightingManager::GetChannelName(const std::string& deviceId, int channel) const {
    auto it = m_configs.find(deviceId);
    if (it == m_configs.end()) {
        return "";
    }

    for (const auto& ch : it->second.channels) {
        if (ch.channelNumber == channel) {
            return ch.name;
        }
    }

    return "L" + std::to_string(channel + 1);
}

std::vector<ChannelConfig> LightingManager::GetChannelConfigs(const std::string& deviceId) const {
    auto it = m_configs.find(deviceId);
    if (it == m_configs.end()) {
        return {};
    }
    return it->second.channels;
}

const ControllerConfig* LightingManager::GetControllerConfig(const std::string& deviceId) const {
    auto it = m_configs.find(deviceId);
    if (it == m_configs.end()) {
        return nullptr;
    }
    return &it->second;
}

void LightingManager::SetConfigFilePath(const std::string& path) {
    m_configFilePath = path;
    std::cout << "[LightingManager] Config file path set to: " << path << std::endl;
}

void LightingManager::UpdateChannelDefaultIntensity(const std::string& deviceId, int channel, int intensity) {
    auto it = m_configs.find(deviceId);
    if (it == m_configs.end()) {
        std::cerr << "[LightingManager] Device not found for update: " << deviceId << std::endl;
        return;
    }

    for (auto& ch : it->second.channels) {
        if (ch.channelNumber == channel) {
            ch.defaultIntensity = intensity;
            return;
        }
    }
}

void LightingManager::ApplyDefaultIntensities(const std::string& deviceId) {
    auto* controller = GetController(deviceId);
    if (!controller || !controller->IsConnected()) {
        std::cerr << "[LightingManager] Cannot apply defaults - not connected: " << deviceId << std::endl;
        return;
    }

    auto it = m_configs.find(deviceId);
    if (it == m_configs.end()) {
        return;
    }

    std::cout << "[LightingManager] Applying saved intensities for: " << deviceId << std::endl;
    for (const auto& ch : it->second.channels) {
        if (ch.enabled && ch.defaultIntensity > 0) {
            controller->SetChannelIntensity(ch.channelNumber, ch.defaultIntensity);
            controller->SetChannelOn(ch.channelNumber, true);
            std::cout << "  Channel " << ch.channelNumber << " (" << ch.name
                      << "): " << ch.defaultIntensity << std::endl;
        }
    }
}

bool LightingManager::SaveConfig() {
    if (m_configFilePath.empty()) {
        std::cerr << "[LightingManager] Cannot save - no config file path set" << std::endl;
        return false;
    }

    return LightingJsonLoader::SaveToFile(*this, m_configFilePath);
}

} // namespace Lighting
