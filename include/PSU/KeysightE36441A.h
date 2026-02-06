// KeysightE36441A.h - Keysight E36441A 4-Channel Power Supply Driver (TCP/IP)
#pragma once

#include "IPowerSupply.h"
#include <string>
#include <mutex>

#include "SocketGuard.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <netinet/in.h>
#include <arpa/inet.h>
#define SOCKET_ERROR -1
#endif

/**
 * @brief Keysight E36441A 4-Channel Power Supply Driver
 *
 * Communicates via TCP/IP using SCPI commands
 *
 * Features:
 * - 4 independent output channels
 * - Thread-safe operations
 * - Query and Write commands
 * - Auto-reconnect capability
 * - Error handling
 * - Channel-specific control
 *
 * Channel Specifications:
 * - Channels 1-4: Independent control
 * - Each channel has independent voltage/current settings
 *
 * Default Port: 5025 (SCPI standard)
 */
class KeysightE36441A : public IPowerSupply
{
public:
    /**
     * @param ip IP address of the power supply (e.g., "192.168.0.32")
     * @param port TCP port (default 5025 for SCPI)
     * @param deviceName Friendly name for the device
     */
    explicit KeysightE36441A(const std::string& ip,
                             int port = 5025,
                             const std::string& deviceName = "PSU");
    ~KeysightE36441A() override;

    // IPowerSupply interface implementation
    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;

    std::string getDeviceName() const override { return m_deviceName; }
    std::string getDeviceID() const override;

    // Multi-channel output control (channel: 1-4)
    bool setOutputOn(int channel = 1) override;
    bool setOutputOff(int channel = 1) override;

    // Multi-channel voltage/current control (channel: 1-4)
    bool setVoltage(double voltage, int channel = 1) override;
    bool setCurrent(double current, int channel = 1) override;

    // Multi-channel protection settings (channel: 1-4)
    bool setOVP(double voltage, int channel = 1) override;
    bool setOCP(double current, int channel = 1) override;

    // Multi-channel measurements (channel: 1-4)
    double readVoltage(int channel = 1) override;
    double readCurrent(int channel = 1) override;
    bool readOutputState(int channel = 1) override;

    std::string getLastError() const override { return m_lastError; }

    // Additional query methods
    std::string query(const std::string& command);
    bool write(const std::string& command);

    // Channel-specific query with automatic channel selection
    std::string queryChannel(const std::string& command, int channel);
    bool writeChannel(const std::string& command, int channel);

    // Utility methods
    bool selectChannel(int channel);
    int getCurrentChannel() const { return m_currentChannel; }

private:
    bool initializeWSA();
    void cleanupWSA();
    std::string receiveResponse();

    // Internal methods without locking (for use when already locked)
    std::string queryInternal(const std::string& command);
    bool writeInternal(const std::string& command);

    SocketGuard m_socket;
    std::string m_ip;
    int m_port;
    std::string m_deviceName;
    std::string m_deviceID;
    int m_currentChannel;  // Currently selected channel (1-4)

    mutable std::mutex m_socketMutex;
    bool m_connected;
    mutable std::string m_lastError;

    static bool s_wsaInitialized;

    static constexpr int RECV_TIMEOUT_MS = 5000;  // 5 seconds
    static constexpr int SEND_TIMEOUT_MS = 5000;  // 5 seconds
    static constexpr size_t BUFFER_SIZE = 4096;
    static constexpr int NUM_CHANNELS = 4;
};
