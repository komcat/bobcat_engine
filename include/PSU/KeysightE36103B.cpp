// KeysightE36103B.cpp - Keysight E36103B Power Supply Driver Implementation
#include "KeysightE36103B.h"
#include <iostream>
#include <sstream>
#include <cstring>

bool KeysightE36103B::s_wsaInitialized = false;

KeysightE36103B::KeysightE36103B(const std::string& ip, int port, const std::string& deviceName)
    : m_socket(INVALID_SOCKET)
    , m_ip(ip)
    , m_port(port)
    , m_deviceName(deviceName)
    , m_connected(false)
{
    initializeWSA();
}

KeysightE36103B::~KeysightE36103B()
{
    disconnect();
}

bool KeysightE36103B::initializeWSA()
{
#ifdef _WIN32
    if (!s_wsaInitialized) {
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            m_lastError = "WSAStartup failed with error: " + std::to_string(result);
            std::cerr << "[KeysightE36103B] " << m_lastError << std::endl;
            return false;
        }
        s_wsaInitialized = true;
        std::cout << "[KeysightE36103B] WSA initialized" << std::endl;
    }
#endif
    return true;
}

void KeysightE36103B::cleanupWSA()
{
#ifdef _WIN32
    if (s_wsaInitialized) {
        WSACleanup();
        s_wsaInitialized = false;
    }
#endif
}

bool KeysightE36103B::connect()
{
    std::lock_guard<std::mutex> lock(m_socketMutex);

    if (m_connected) {
        m_lastError = "Already connected";
        return true;
    }

    // Create socket
    m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_socket == INVALID_SOCKET) {
#ifdef _WIN32
        m_lastError = "Socket creation failed with error: " + std::to_string(WSAGetLastError());
#else
        m_lastError = "Socket creation failed";
#endif
        std::cerr << "[KeysightE36103B] " << m_lastError << std::endl;
        return false;
    }

    // Set socket timeouts
#ifdef _WIN32
    DWORD timeout = RECV_TIMEOUT_MS;
    setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    timeout = SEND_TIMEOUT_MS;
    setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = RECV_TIMEOUT_MS / 1000;
    tv.tv_usec = (RECV_TIMEOUT_MS % 1000) * 1000;
    setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    tv.tv_sec = SEND_TIMEOUT_MS / 1000;
    tv.tv_usec = (SEND_TIMEOUT_MS % 1000) * 1000;
    setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

    // Connect to server
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(static_cast<u_short>(m_port));

#ifdef _WIN32
    inet_pton(AF_INET, m_ip.c_str(), &serverAddr.sin_addr);
#else
    inet_aton(m_ip.c_str(), &serverAddr.sin_addr);
#endif

    if (::connect(m_socket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
#ifdef _WIN32
        m_lastError = "Connection failed with error: " + std::to_string(WSAGetLastError());
#else
        m_lastError = "Connection failed";
#endif
        std::cerr << "[KeysightE36103B] " << m_lastError << std::endl;
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        return false;
    }

    m_connected = true;
    std::cout << "[KeysightE36103B] Connected to " << m_ip << ":" << m_port << std::endl;

    // Query device ID (use internal method since mutex is already locked)
    m_deviceID = queryInternal("*IDN?");

    return true;
}

void KeysightE36103B::disconnect()
{
    std::lock_guard<std::mutex> lock(m_socketMutex);

    if (m_socket != INVALID_SOCKET) {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        m_connected = false;
        std::cout << "[KeysightE36103B] Disconnected from " << m_ip << ":" << m_port << std::endl;
    }
}

bool KeysightE36103B::isConnected() const
{
    return m_connected;
}

std::string KeysightE36103B::getDeviceID() const
{
    return m_deviceID;
}

std::string KeysightE36103B::queryInternal(const std::string& command)
{
    // ASSUME MUTEX IS ALREADY LOCKED BY CALLER

    if (!m_connected || m_socket == INVALID_SOCKET) {
        m_lastError = "Not connected";
        return "";
    }

    // Prepare command (append newline if needed)
    std::string cmd = command;
    if (!cmd.empty() && cmd.back() != '\n') {
        cmd += '\n';
    }

    // Send command
    int bytesSent = send(m_socket, cmd.c_str(), static_cast<int>(cmd.length()), 0);
    if (bytesSent == SOCKET_ERROR) {
#ifdef _WIN32
        m_lastError = "Send failed with error: " + std::to_string(WSAGetLastError());
#else
        m_lastError = "Send failed";
#endif
        std::cerr << "[KeysightE36103B] " << m_lastError << std::endl;
        return "";
    }

    // Receive response
    return receiveResponse();
}

bool KeysightE36103B::writeInternal(const std::string& command)
{
    // ASSUME MUTEX IS ALREADY LOCKED BY CALLER

    if (!m_connected || m_socket == INVALID_SOCKET) {
        m_lastError = "Not connected";
        std::cerr << "[KeysightE36103B] Write failed - " << m_lastError << std::endl;
        return false;
    }

    // Prepare command (append newline if needed)
    std::string cmd = command;
    if (!cmd.empty() && cmd.back() != '\n') {
        cmd += '\n';
    }

    // Send command
    int bytesSent = send(m_socket, cmd.c_str(), static_cast<int>(cmd.length()), 0);
    if (bytesSent == SOCKET_ERROR) {
#ifdef _WIN32
        m_lastError = "Send failed with error: " + std::to_string(WSAGetLastError());
#else
        m_lastError = "Send failed";
#endif
        std::cerr << "[KeysightE36103B] " << m_lastError << std::endl;
        return false;
    }

    return true;
}

std::string KeysightE36103B::query(const std::string& command)
{
    std::lock_guard<std::mutex> lock(m_socketMutex);
    return queryInternal(command);
}

bool KeysightE36103B::write(const std::string& command)
{
    std::lock_guard<std::mutex> lock(m_socketMutex);
    return writeInternal(command);
}

std::string KeysightE36103B::receiveResponse()
{
    char buffer[BUFFER_SIZE];
    int bytesReceived = recv(m_socket, buffer, BUFFER_SIZE - 1, 0);

    if (bytesReceived == SOCKET_ERROR) {
#ifdef _WIN32
        m_lastError = "Receive failed with error: " + std::to_string(WSAGetLastError());
#else
        m_lastError = "Receive failed";
#endif
        std::cerr << "[KeysightE36103B] " << m_lastError << std::endl;
        return "";
    }

    if (bytesReceived == 0) {
        m_lastError = "Connection closed by remote host";
        return "";
    }

    buffer[bytesReceived] = '\0';
    std::string response(buffer);

    // Strip trailing whitespace/newlines
    while (!response.empty() && (response.back() == '\n' || response.back() == '\r' || response.back() == ' ')) {
        response.pop_back();
    }

    return response;
}

bool KeysightE36103B::setOutputOn(int channel)
{
    // E36103B is single-channel, ignore channel parameter
    std::cout << "[KeysightE36103B] Setting output ON for device: " << m_deviceName << std::endl;
    bool result = write("OUTP ON");
    if (result) {
        std::cout << "[KeysightE36103B] Output ON command sent successfully" << std::endl;
    } else {
        std::cerr << "[KeysightE36103B] Output ON command failed: " << m_lastError << std::endl;
    }
    return result;
}

bool KeysightE36103B::setOutputOff(int channel)
{
    // E36103B is single-channel, ignore channel parameter
    std::cout << "[KeysightE36103B] Setting output OFF for device: " << m_deviceName << std::endl;
    bool result = write("OUTP OFF");
    if (result) {
        std::cout << "[KeysightE36103B] Output OFF command sent successfully" << std::endl;
    } else {
        std::cerr << "[KeysightE36103B] Output OFF command failed: " << m_lastError << std::endl;
    }
    return result;
}

bool KeysightE36103B::setVoltage(double voltage, int channel)
{
    // E36103B is single-channel, ignore channel parameter
    std::ostringstream oss;
    oss << "VOLT " << voltage;
    return write(oss.str());
}

bool KeysightE36103B::setCurrent(double current, int channel)
{
    // E36103B is single-channel, ignore channel parameter
    std::ostringstream oss;
    oss << "CURR " << current;
    return write(oss.str());
}

bool KeysightE36103B::setOVP(double voltage, int channel)
{
    // E36103B is single-channel, ignore channel parameter
    std::lock_guard<std::mutex> lock(m_socketMutex);

    std::ostringstream oss;
    oss << "VOLT:PROT " << voltage;
    if (!writeInternal(oss.str())) {
        return false;
    }
    return writeInternal("VOLT:PROT:STAT ON");
}

bool KeysightE36103B::setOCP(double current, int channel)
{
    // E36103B is single-channel, ignore channel parameter
    std::lock_guard<std::mutex> lock(m_socketMutex);

    std::ostringstream oss;
    oss << "CURR:PROT " << current;
    if (!writeInternal(oss.str())) {
        return false;
    }
    return writeInternal("CURR:PROT:STAT ON");
}

double KeysightE36103B::readVoltage(int channel)
{
    // E36103B is single-channel, ignore channel parameter
    std::string response = query("MEAS:VOLT?");
    if (response.empty()) {
        return -1.0;  // Return negative to indicate error
    }

    try {
        return std::stod(response);
    }
    catch (const std::exception& e) {
        m_lastError = "Failed to parse voltage: " + std::string(e.what());
        std::cerr << "[KeysightE36103B] " << m_lastError << std::endl;
        return -1.0;
    }
}

double KeysightE36103B::readCurrent(int channel)
{
    // E36103B is single-channel, ignore channel parameter
    std::string response = query("MEAS:CURR?");
    if (response.empty()) {
        return -1.0;  // Return negative to indicate error
    }

    try {
        return std::stod(response);
    }
    catch (const std::exception& e) {
        m_lastError = "Failed to parse current: " + std::string(e.what());
        std::cerr << "[KeysightE36103B] " << m_lastError << std::endl;
        return -1.0;
    }
}

bool KeysightE36103B::readOutputState(int channel)
{
    // E36103B is single-channel, ignore channel parameter
    std::string response = query("OUTP?");
    if (response.empty()) {
        return false;
    }

    // Response is "0" or "1" (or "OFF"/"ON")
    return (response == "1" || response == "ON");
}
