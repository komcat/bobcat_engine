#pragma once
#include "ILightingController.h"
#include <mutex>
#include <iostream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

namespace Lighting {

class CCSPD3Adapter : public ILightingController {
public:
    CCSPD3Adapter();
    ~CCSPD3Adapter() override;

    // ILightingController interface
    bool Initialize(const ControllerConfig& config) override;
    bool Connect() override;
    bool Disconnect() override;
    bool IsConnected() const override;

    LightingResult SetChannelIntensity(int channel, int intensity) override;
    LightingResult SetChannelOn(int channel, bool on) override;
    LightingResult SetAllChannelsIntensity(int intensity) override;
    LightingResult SetAllChannelsOff() override;

    ChannelStatus GetChannelStatus(int channel) override;
    ControllerStatus GetControllerStatus() override;
    LightingResult CheckError() override;

    std::string GetDeviceId() const override;
    std::string GetDeviceName() const override;
    std::string GetControllerType() const override { return "CCS_PD3"; }
    std::string GetLastError() const override;
    const ControllerConfig& GetConfig() const override { return m_config; }

private:
    // Protocol helpers
    std::string CalculateChecksum(const std::string& data);
    std::string BuildCommand(const std::string& channel,
                             const std::string& instruction,
                             const std::string& data = "");
    std::string SendCommand(const std::string& command);
    std::string SendCommandInternal(const std::string& command);  // No mutex lock
    bool ParseStatusResponse(const std::string& response, ChannelStatus& status);
    bool ParseErrorResponse(const std::string& response, int& errorCode);

    // Configuration
    ControllerConfig m_config;

    // Connection
    SOCKET m_socket;
    bool m_connected;
    bool m_initialized;
    std::string m_lastError;
    mutable std::mutex m_mutex;

    // Constants
    static constexpr int DEFAULT_PORT = 40001;
    static constexpr int TIMEOUT_MS = 5000;
    static constexpr int RECV_BUFFER_SIZE = 1024;
};

} // namespace Lighting
