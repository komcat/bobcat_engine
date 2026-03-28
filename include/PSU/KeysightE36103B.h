// KeysightE36103B.h - Keysight E36103B Power Supply Driver (TCP/IP)
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
 * @brief Keysight E36103B Power Supply Driver
 *
 * Communicates via TCP/IP using SCPI commands
 *
 * Features:
 * - Thread-safe operations
 * - Query and Write commands
 * - Auto-reconnect capability
 * - Error handling
 *
 * Default Port: 5025 (SCPI standard)
 */
class KeysightE36103B : public IPowerSupply
{
public:
    /**
     * @param ip IP address of the power supply (e.g., "192.168.0.60")
     * @param port TCP port (default 5025 for SCPI)
     * @param deviceName Friendly name for the device
     */
    explicit KeysightE36103B(const std::string& ip,
                             int port = 5025,
                             const std::string& deviceName = "PSU");
    ~KeysightE36103B() override;

    // IPowerSupply interface implementation
    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;

    std::string getDeviceName() const override { return m_deviceName; }
    std::string getDeviceID() const override;

    bool setOutputOn(int channel = 1) override;
    bool setOutputOff(int channel = 1) override;

    bool setVoltage(double voltage, int channel = 1) override;
    bool setCurrent(double current, int channel = 1) override;

    bool setOVP(double voltage, int channel = 1) override;
    bool setOCP(double current, int channel = 1) override;

    double readVoltage(int channel = 1) override;
    double readCurrent(int channel = 1) override;
    double readSetCurrent(int channel = 1) override;
    bool readOutputState(int channel = 1) override;

    std::string getLastError() const override { return m_lastError; }

    // Additional query methods
    std::string query(const std::string& command);
    bool write(const std::string& command);

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

    mutable std::mutex m_socketMutex;
    bool m_connected;
    mutable std::string m_lastError;

    static std::once_flag s_wsaInitFlag;
    static bool s_wsaReady;

    static constexpr int RECV_TIMEOUT_MS = 5000;  // 5 seconds
    static constexpr int SEND_TIMEOUT_MS = 5000;  // 5 seconds
    static constexpr size_t BUFFER_SIZE = 4096;
};
