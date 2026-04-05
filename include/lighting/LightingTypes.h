#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace Lighting {

// Channel configuration
struct ChannelConfig {
    int channelNumber = 0;          // 0-7 (L1-L8)
    std::string name;               // e.g., "Collimate", "Red", "Green", "Blue"
    std::string color;              // Optional: color identifier for grouping (e.g., "red", "white")
    std::string group;              // Optional: physical light group (e.g., "light1", "light2")
    int defaultIntensity = 0;       // Default intensity 0-255
    bool enabled = true;            // Channel enabled/disabled
};

// Controller device configuration
struct ControllerConfig {
    std::string deviceId;           // Unique device identifier
    std::string deviceName;         // Display name for the device
    std::string controllerType;     // "CCS_PD3" etc.
    std::string ipAddress;          // IP address
    int port = 40001;               // TCP port (default 40001)
    bool enabled = true;            // Device enabled/disabled
    std::vector<ChannelConfig> channels;
};

// Channel status
struct ChannelStatus {
    int channelNumber = 0;
    std::string name;
    int intensity = 0;              // Current intensity 0-255
    int mode = 0;                   // Lighting mode (0=continuous, 1-10=strobe)
    bool isOn = false;              // ON/OFF state
};

// Controller status
struct ControllerStatus {
    std::string deviceId;
    std::string deviceName;
    bool connected = false;
    bool hasError = false;
    int errorCode = 0;
    std::vector<ChannelStatus> channelStatuses;
};

// Operation result
enum class LightingResult {
    Success,
    NotConnected,
    InvalidChannel,
    InvalidIntensity,
    CommunicationError,
    DeviceNotFound,
    Timeout,
    ChecksumError,
    CommandError
};

// Convert result to string
inline std::string LightingResultToString(LightingResult result) {
    switch (result) {
        case LightingResult::Success: return "Success";
        case LightingResult::NotConnected: return "Not Connected";
        case LightingResult::InvalidChannel: return "Invalid Channel";
        case LightingResult::InvalidIntensity: return "Invalid Intensity";
        case LightingResult::CommunicationError: return "Communication Error";
        case LightingResult::DeviceNotFound: return "Device Not Found";
        case LightingResult::Timeout: return "Timeout";
        case LightingResult::ChecksumError: return "Checksum Error";
        case LightingResult::CommandError: return "Command Error";
        default: return "Unknown";
    }
}

// Intensity conversion helpers
inline int PercentToIntensity(double percent) {
    if (percent < 0.0) percent = 0.0;
    if (percent > 100.0) percent = 100.0;
    return static_cast<int>(percent / 100.0 * 255.0);
}

inline double IntensityToPercent(int intensity) {
    if (intensity < 0) intensity = 0;
    if (intensity > 255) intensity = 255;
    return static_cast<double>(intensity) / 255.0 * 100.0;
}

} // namespace Lighting
