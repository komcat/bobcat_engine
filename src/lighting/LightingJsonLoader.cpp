#include "lighting/LightingJsonLoader.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

using json = nlohmann::json;

namespace Lighting {

static ChannelConfig ParseChannelConfig(const json& j) {
    ChannelConfig config;

    config.channelNumber = j.value("channel", 0);
    config.name = j.value("name", "L" + std::to_string(config.channelNumber + 1));
    config.color = j.value("color", "");
    config.defaultIntensity = j.value("default_intensity", 0);
    config.enabled = j.value("enabled", true);

    return config;
}

static ControllerConfig ParseControllerConfig(const json& j) {
    ControllerConfig config;

    config.deviceId = j.value("device_id", "");
    config.deviceName = j.value("device_name", config.deviceId);
    config.controllerType = j.value("controller_type", "CCS_PD3");
    config.ipAddress = j.value("ip_address", "");
    config.port = j.value("port", 40001);
    config.enabled = j.value("enabled", true);

    // Parse channels
    if (j.contains("channels") && j["channels"].is_array()) {
        for (const auto& chJson : j["channels"]) {
            config.channels.push_back(ParseChannelConfig(chJson));
        }
    }

    // If no channels defined, create default 8 channels
    if (config.channels.empty()) {
        for (int i = 0; i < 8; ++i) {
            ChannelConfig ch;
            ch.channelNumber = i;
            ch.name = "L" + std::to_string(i + 1);
            ch.defaultIntensity = 0;
            ch.enabled = true;
            config.channels.push_back(ch);
        }
    }

    return config;
}

int LightingJsonLoader::LoadFromFile(LightingManager& manager, const std::string& filepath) {
    std::cout << "[LightingJsonLoader] Loading from: " << filepath << std::endl;

    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[LightingJsonLoader] Cannot open file: " << filepath << std::endl;
        return 0;
    }

    json j;
    try {
        file >> j;
    } catch (const json::parse_error& e) {
        std::cerr << "[LightingJsonLoader] JSON parse error: " << e.what() << std::endl;
        return 0;
    }

    // Look for lighting_controllers array
    if (!j.contains("lighting_controllers") || !j["lighting_controllers"].is_array()) {
        std::cerr << "[LightingJsonLoader] Missing 'lighting_controllers' array in JSON" << std::endl;
        return 0;
    }

    int addedCount = 0;
    for (const auto& controllerJson : j["lighting_controllers"]) {
        try {
            ControllerConfig config = ParseControllerConfig(controllerJson);

            if (config.deviceId.empty()) {
                std::cerr << "[LightingJsonLoader] Skipping controller with empty device_id" << std::endl;
                continue;
            }

            if (config.ipAddress.empty()) {
                std::cerr << "[LightingJsonLoader] Skipping controller with empty ip_address: "
                          << config.deviceId << std::endl;
                continue;
            }

            if (manager.AddController(config)) {
                addedCount++;
                std::cout << "[LightingJsonLoader] Loaded controller: " << config.deviceName
                          << " (" << config.deviceId << ") at " << config.ipAddress
                          << ":" << config.port << std::endl;
            }

        } catch (const json::exception& e) {
            std::cerr << "[LightingJsonLoader] Error parsing controller: " << e.what() << std::endl;
            continue;
        }
    }

    std::cout << "[LightingJsonLoader] Loaded " << addedCount << " controller(s)" << std::endl;
    return addedCount;
}

bool LightingJsonLoader::SaveToFile(const LightingManager& manager, const std::string& filepath) {
    json j;
    json controllersArray = json::array();

    for (const auto& deviceId : manager.GetControllerIds()) {
        const ControllerConfig* config = manager.GetControllerConfig(deviceId);
        if (!config) continue;

        json controllerJson;
        controllerJson["device_id"] = config->deviceId;
        controllerJson["device_name"] = config->deviceName;
        controllerJson["controller_type"] = config->controllerType;
        controllerJson["ip_address"] = config->ipAddress;
        controllerJson["port"] = config->port;
        controllerJson["enabled"] = config->enabled;

        json channelsArray = json::array();
        for (const auto& ch : config->channels) {
            json chJson;
            chJson["channel"] = ch.channelNumber;
            chJson["name"] = ch.name;
            chJson["color"] = ch.color;
            chJson["default_intensity"] = ch.defaultIntensity;
            chJson["enabled"] = ch.enabled;
            channelsArray.push_back(chJson);
        }
        controllerJson["channels"] = channelsArray;

        controllersArray.push_back(controllerJson);
    }

    j["lighting_controllers"] = controllersArray;

    std::ofstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[LightingJsonLoader] Cannot open file for writing: " << filepath << std::endl;
        return false;
    }

    file << j.dump(2);
    std::cout << "[LightingJsonLoader] Saved configuration to: " << filepath << std::endl;
    return true;
}

} // namespace Lighting
