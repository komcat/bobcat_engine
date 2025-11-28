#pragma once
#include "LightingTypes.h"
#include <memory>
#include <string>

namespace Lighting {

class ILightingController {
public:
    virtual ~ILightingController() = default;

    // Lifecycle
    virtual bool Initialize(const ControllerConfig& config) = 0;
    virtual bool Connect() = 0;
    virtual bool Disconnect() = 0;
    virtual bool IsConnected() const = 0;

    // Channel Control
    virtual LightingResult SetChannelIntensity(int channel, int intensity) = 0;
    virtual LightingResult SetChannelOn(int channel, bool on) = 0;
    virtual LightingResult SetAllChannelsIntensity(int intensity) = 0;
    virtual LightingResult SetAllChannelsOff() = 0;

    // Status
    virtual ChannelStatus GetChannelStatus(int channel) = 0;
    virtual ControllerStatus GetControllerStatus() = 0;
    virtual LightingResult CheckError() = 0;

    // Information
    virtual std::string GetDeviceId() const = 0;
    virtual std::string GetDeviceName() const = 0;
    virtual std::string GetControllerType() const = 0;
    virtual std::string GetLastError() const = 0;
    virtual const ControllerConfig& GetConfig() const = 0;
};

} // namespace Lighting
