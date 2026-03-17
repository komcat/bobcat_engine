// KeysightE36441A.cpp - Keysight E36441A 4-Channel Power Supply Driver Implementation
#include "KeysightE36441A.h"
#include <iostream>
#include <sstream>
#include <cstring>

std::once_flag KeysightE36441A::s_wsaInitFlag;
bool KeysightE36441A::s_wsaReady = false;

KeysightE36441A::KeysightE36441A(const std::string& ip, int port, const std::string& deviceName)
    : m_socket()
    , m_ip(ip)
    , m_port(port)
    , m_deviceName(deviceName)
    , m_currentChannel(1)
    , m_connected(false)
{
    initializeWSA();
}

KeysightE36441A::~KeysightE36441A()
{
    disconnect();
}

bool KeysightE36441A::initializeWSA()
{
#ifdef _WIN32
    std::call_once(s_wsaInitFlag, []() {
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            std::cerr << "[KeysightE36441A] WSAStartup failed with error: " << result << std::endl;
            s_wsaReady = false;
        } else {
            s_wsaReady = true;
            std::cout << "[KeysightE36441A] WSA initialized" << std::endl;
        }
    });

    if (!s_wsaReady) {
        m_lastError = "WSAStartup failed during initialization";
        return false;
    }
#endif
    return true;
}

void KeysightE36441A::cleanupWSA()
{
#ifdef _WIN32
    WSACleanup();
#endif
}

bool KeysightE36441A::connect()
{
    std::lock_guard<std::mutex> lock(m_socketMutex);

    if (m_connected) {
        m_lastError = "Already connected";
        return true;
    }

    // Create socket
    m_socket.reset(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (!m_socket.valid()) {
#ifdef _WIN32
        m_lastError = "Socket creation failed with error: " + std::to_string(WSAGetLastError());
#else
        m_lastError = "Socket creation failed";
#endif
        std::cerr << "[KeysightE36441A] " << m_lastError << std::endl;
        return false;
    }

    // Set socket timeouts
#ifdef _WIN32
    DWORD timeout = RECV_TIMEOUT_MS;
    setsockopt(m_socket.get(), SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    timeout = SEND_TIMEOUT_MS;
    setsockopt(m_socket.get(), SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = RECV_TIMEOUT_MS / 1000;
    tv.tv_usec = (RECV_TIMEOUT_MS % 1000) * 1000;
    setsockopt(m_socket.get(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    tv.tv_sec = SEND_TIMEOUT_MS / 1000;
    tv.tv_usec = (SEND_TIMEOUT_MS % 1000) * 1000;
    setsockopt(m_socket.get(), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
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

    if (::connect(m_socket.get(), (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
#ifdef _WIN32
        m_lastError = "Connection failed with error: " + std::to_string(WSAGetLastError());
#else
        m_lastError = "Connection failed";
#endif
        std::cerr << "[KeysightE36441A] " << m_lastError << std::endl;
        m_socket.reset();
        return false;
    }

    m_connected = true;
    std::cout << "[KeysightE36441A] Connected to " << m_ip << ":" << m_port << std::endl;

    // Query device ID (use internal method since mutex is already locked)
    m_deviceID = queryInternal("*IDN?");

    return true;
}

void KeysightE36441A::disconnect()
{
    std::lock_guard<std::mutex> lock(m_socketMutex);

    if (m_socket.valid()) {
        m_socket.reset();
        m_connected = false;
        std::cout << "[KeysightE36441A] Disconnected from " << m_ip << ":" << m_port << std::endl;
    }
}

bool KeysightE36441A::isConnected() const
{
    return m_connected;
}

std::string KeysightE36441A::getDeviceID() const
{
    return m_deviceID;
}

std::string KeysightE36441A::queryInternal(const std::string& command)
{
    // ASSUME MUTEX IS ALREADY LOCKED BY CALLER

    if (!m_connected || !m_socket.valid()) {
        m_lastError = "Not connected";
        return "";
    }

    // Prepare command (append newline if needed)
    std::string cmd = command;
    if (!cmd.empty() && cmd.back() != '\n') {
        cmd += '\n';
    }

    // Send command
    int bytesSent = send(m_socket.get(), cmd.c_str(), static_cast<int>(cmd.length()), 0);
    if (bytesSent == SOCKET_ERROR) {
#ifdef _WIN32
        m_lastError = "Send failed with error: " + std::to_string(WSAGetLastError());
#else
        m_lastError = "Send failed";
#endif
        std::cerr << "[KeysightE36441A] " << m_lastError << std::endl;
        return "";
    }

    // Receive response
    return receiveResponse();
}

bool KeysightE36441A::writeInternal(const std::string& command)
{
    // ASSUME MUTEX IS ALREADY LOCKED BY CALLER

    if (!m_connected || !m_socket.valid()) {
        m_lastError = "Not connected";
        std::cerr << "[KeysightE36441A] Write failed - " << m_lastError << std::endl;
        return false;
    }

    // Prepare command (append newline if needed)
    std::string cmd = command;
    if (!cmd.empty() && cmd.back() != '\n') {
        cmd += '\n';
    }

    // Send command
    int bytesSent = send(m_socket.get(), cmd.c_str(), static_cast<int>(cmd.length()), 0);
    if (bytesSent == SOCKET_ERROR) {
#ifdef _WIN32
        m_lastError = "Send failed with error: " + std::to_string(WSAGetLastError());
#else
        m_lastError = "Send failed";
#endif
        std::cerr << "[KeysightE36441A] " << m_lastError << std::endl;
        return false;
    }

    return true;
}

std::string KeysightE36441A::query(const std::string& command)
{
    std::lock_guard<std::mutex> lock(m_socketMutex);
    return queryInternal(command);
}

bool KeysightE36441A::write(const std::string& command)
{
    std::lock_guard<std::mutex> lock(m_socketMutex);
    return writeInternal(command);
}

std::string KeysightE36441A::receiveResponse()
{
    char buffer[BUFFER_SIZE];
    int bytesReceived = recv(m_socket.get(), buffer, BUFFER_SIZE - 1, 0);

    if (bytesReceived == SOCKET_ERROR) {
#ifdef _WIN32
        m_lastError = "Receive failed with error: " + std::to_string(WSAGetLastError());
#else
        m_lastError = "Receive failed";
#endif
        std::cerr << "[KeysightE36441A] " << m_lastError << std::endl;
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

bool KeysightE36441A::selectChannel(int channel)
{
    if (channel < 1 || channel > NUM_CHANNELS) {
        m_lastError = "Invalid channel: " + std::to_string(channel) + " (valid: 1-4)";
        std::cerr << "[KeysightE36441A] " << m_lastError << std::endl;
        return false;
    }

    std::lock_guard<std::mutex> lock(m_socketMutex);

    // Use INST CH<n> format
    std::ostringstream oss;
    oss << "INST CH" << channel;

    if (writeInternal(oss.str())) {
        m_currentChannel = channel;
        return true;
    }

    return false;
}

std::string KeysightE36441A::queryChannel(const std::string& command, int channel)
{
    if (channel < 1 || channel > NUM_CHANNELS) {
        m_lastError = "Invalid channel: " + std::to_string(channel) + " (valid: 1-4)";
        std::cerr << "[KeysightE36441A] " << m_lastError << std::endl;
        return "";
    }

    std::lock_guard<std::mutex> lock(m_socketMutex);

    // Select channel first using INST CH<n> format
    std::ostringstream selectCmd;
    selectCmd << "INST CH" << channel;
    if (!writeInternal(selectCmd.str())) {
        return "";
    }
    m_currentChannel = channel;

    // Execute query
    return queryInternal(command);
}

bool KeysightE36441A::writeChannel(const std::string& command, int channel)
{
    if (channel < 1 || channel > NUM_CHANNELS) {
        m_lastError = "Invalid channel: " + std::to_string(channel) + " (valid: 1-4)";
        std::cerr << "[KeysightE36441A] " << m_lastError << std::endl;
        return false;
    }

    std::lock_guard<std::mutex> lock(m_socketMutex);

    // Select channel first using INST CH<n> format
    std::ostringstream selectCmd;
    selectCmd << "INST CH" << channel;
    if (!writeInternal(selectCmd.str())) {
        return false;
    }
    m_currentChannel = channel;

    // Execute command
    return writeInternal(command);
}

bool KeysightE36441A::setOutputOn(int channel)
{
    if (channel < 1 || channel > NUM_CHANNELS) {
        m_lastError = "Invalid channel: " + std::to_string(channel);
        return false;
    }

    std::cout << "[KeysightE36441A] Setting output ON, channel: " << channel << std::endl;
    return writeChannel("OUTP ON", channel);
}

bool KeysightE36441A::setOutputOff(int channel)
{
    if (channel < 1 || channel > NUM_CHANNELS) {
        m_lastError = "Invalid channel: " + std::to_string(channel);
        return false;
    }

    std::cout << "[KeysightE36441A] Setting output OFF, channel: " << channel << std::endl;
    return writeChannel("OUTP OFF", channel);
}

bool KeysightE36441A::setVoltage(double voltage, int channel)
{
    if (channel < 1 || channel > NUM_CHANNELS) {
        m_lastError = "Invalid channel: " + std::to_string(channel);
        return false;
    }

    std::ostringstream oss;
    oss << "VOLT " << voltage;

    std::cout << "[KeysightE36441A] Setting voltage to " << voltage
              << "V on channel " << channel << std::endl;

    return writeChannel(oss.str(), channel);
}

bool KeysightE36441A::setCurrent(double current, int channel)
{
    if (channel < 1 || channel > NUM_CHANNELS) {
        m_lastError = "Invalid channel: " + std::to_string(channel);
        return false;
    }

    std::ostringstream oss;
    oss << "CURR " << current;

    std::cout << "[KeysightE36441A] Setting current to " << current
              << "A on channel " << channel << std::endl;

    return writeChannel(oss.str(), channel);
}

bool KeysightE36441A::setOVP(double voltage, int channel)
{
    if (channel < 1 || channel > NUM_CHANNELS) {
        m_lastError = "Invalid channel: " + std::to_string(channel);
        return false;
    }

    std::lock_guard<std::mutex> lock(m_socketMutex);

    // Select channel first using INST CH<n> format
    std::ostringstream selectCmd;
    selectCmd << "INST CH" << channel;
    if (!writeInternal(selectCmd.str())) {
        return false;
    }

    // Set OVP value
    std::ostringstream oss;
    oss << "VOLT:PROT " << voltage;
    if (!writeInternal(oss.str())) {
        return false;
    }

    // Enable OVP
    return writeInternal("VOLT:PROT:STAT ON");
}

bool KeysightE36441A::setOCP(double current, int channel)
{
    if (channel < 1 || channel > NUM_CHANNELS) {
        m_lastError = "Invalid channel: " + std::to_string(channel);
        return false;
    }

    std::lock_guard<std::mutex> lock(m_socketMutex);

    // Select channel first using INST CH<n> format
    std::ostringstream selectCmd;
    selectCmd << "INST CH" << channel;
    if (!writeInternal(selectCmd.str())) {
        return false;
    }

    // Set OCP value
    std::ostringstream oss;
    oss << "CURR:PROT " << current;
    if (!writeInternal(oss.str())) {
        return false;
    }

    // Enable OCP
    return writeInternal("CURR:PROT:STAT ON");
}

double KeysightE36441A::readVoltage(int channel)
{
    if (channel < 1 || channel > NUM_CHANNELS) {
        m_lastError = "Invalid channel: " + std::to_string(channel);
        return -1.0;
    }

    // Use INST:NSEL to select channel, then query
    std::string response = queryChannel("MEAS:VOLT?", channel);

    if (response.empty()) {
        return -1.0;
    }

    try {
        return std::stod(response);
    }
    catch (const std::exception& e) {
        m_lastError = "Failed to parse voltage: " + std::string(e.what());
        std::cerr << "[KeysightE36441A] " << m_lastError << std::endl;
        return -1.0;
    }
}

double KeysightE36441A::readCurrent(int channel)
{
    if (channel < 1 || channel > NUM_CHANNELS) {
        m_lastError = "Invalid channel: " + std::to_string(channel);
        return -1.0;
    }

    // Use INST:NSEL to select channel, then query
    std::string response = queryChannel("MEAS:CURR?", channel);

    if (response.empty()) {
        return -1.0;
    }

    try {
        return std::stod(response);
    }
    catch (const std::exception& e) {
        m_lastError = "Failed to parse current: " + std::string(e.what());
        std::cerr << "[KeysightE36441A] " << m_lastError << std::endl;
        return -1.0;
    }
}

double KeysightE36441A::readSetCurrent(int channel)
{
    if (channel < 1 || channel > NUM_CHANNELS) {
        m_lastError = "Invalid channel: " + std::to_string(channel);
        return -1.0;
    }

    // CURR? returns the programmed current level (not measured)
    std::string response = queryChannel("CURR?", channel);

    if (response.empty()) {
        return -1.0;
    }

    try {
        return std::stod(response);
    }
    catch (const std::exception& e) {
        m_lastError = "Failed to parse set current: " + std::string(e.what());
        std::cerr << "[KeysightE36441A] " << m_lastError << std::endl;
        return -1.0;
    }
}

bool KeysightE36441A::readOutputState(int channel)
{
    if (channel < 1 || channel > NUM_CHANNELS) {
        m_lastError = "Invalid channel: " + std::to_string(channel);
        return false;
    }

    // Use INST:NSEL to select channel, then query
    std::string response = queryChannel("OUTP?", channel);

    if (response.empty()) {
        return false;
    }

    // Response is "0" or "1" (or "OFF"/"ON")
    return (response == "1" || response == "ON");
}
