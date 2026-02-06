#include "lighting/CCSPD3Adapter.h"
#include <sstream>
#include <iomanip>
#include <cstring>
#include <chrono>
#include <thread>

namespace Lighting {

CCSPD3Adapter::CCSPD3Adapter()
    : m_socket()
    , m_connected(false)
    , m_initialized(false)
{
#ifdef _WIN32
    // Initialize Winsock
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        m_lastError = "WSAStartup failed: " + std::to_string(result);
        std::cerr << "[CCSPD3Adapter] " << m_lastError << std::endl;
    }
#endif
}

CCSPD3Adapter::~CCSPD3Adapter() {
    Disconnect();
#ifdef _WIN32
    WSACleanup();
#endif
}

bool CCSPD3Adapter::Initialize(const ControllerConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config = config;
    m_initialized = true;
    std::cout << "[CCSPD3Adapter] Initialized device: " << config.deviceName
              << " (" << config.deviceId << ") at " << config.ipAddress
              << ":" << config.port << std::endl;
    return true;
}

bool CCSPD3Adapter::Connect() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_initialized) {
        m_lastError = "Adapter not initialized";
        return false;
    }

    if (m_connected) {
        return true;  // Already connected
    }

    std::cout << "[CCSPD3Adapter] Creating socket..." << std::endl;

    // Create socket (same as Python: socket.socket(socket.AF_INET, socket.SOCK_STREAM))
    m_socket.reset(socket(AF_INET, SOCK_STREAM, 0));
    if (!m_socket.valid()) {
#ifdef _WIN32
        int wsaError = WSAGetLastError();
        m_lastError = "Failed to create socket (WSA error: " + std::to_string(wsaError) + ")";
#else
        m_lastError = "Failed to create socket";
#endif
        std::cerr << "[CCSPD3Adapter] " << m_lastError << std::endl;
        return false;
    }

    std::cout << "[CCSPD3Adapter] Socket created, setting timeout..." << std::endl;

    // Set socket timeout (same as Python: socket.settimeout(5.0))
#ifdef _WIN32
    DWORD timeout = TIMEOUT_MS;
    setsockopt(m_socket.get(), SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(m_socket.get(), SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = TIMEOUT_MS / 1000;
    tv.tv_usec = (TIMEOUT_MS % 1000) * 1000;
    setsockopt(m_socket.get(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(m_socket.get(), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

    // Setup address
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(static_cast<u_short>(m_config.port));

    // Convert IP address
    if (inet_pton(AF_INET, m_config.ipAddress.c_str(), &serverAddr.sin_addr) <= 0) {
        m_lastError = "Invalid IP address: " + m_config.ipAddress;
        std::cerr << "[CCSPD3Adapter] " << m_lastError << std::endl;
        m_socket.reset();
        return false;
    }

    // Connect (same as Python: socket.connect((ip, port)))
    std::cout << "[CCSPD3Adapter] Connecting to " << m_config.ipAddress << ":" << m_config.port << "..." << std::endl;
    std::cout << "[CCSPD3Adapter] NOTE: CCS PD3 only allows 1 TCP connection at a time!" << std::endl;

    int result = connect(m_socket.get(), (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    if (result == SOCKET_ERROR) {
#ifdef _WIN32
        int wsaError = WSAGetLastError();
        std::string errorDesc;
        switch (wsaError) {
            case 10060: errorDesc = "TIMEOUT - Check if another app is connected or device is off"; break;
            case 10061: errorDesc = "CONNECTION REFUSED - Port not listening"; break;
            case 10065: errorDesc = "HOST UNREACHABLE - Check network"; break;
            default: errorDesc = "Unknown error"; break;
        }
        m_lastError = "Connection failed (WSA " + std::to_string(wsaError) + ": " + errorDesc + ")";
#else
        m_lastError = "Connection failed to " + m_config.ipAddress + ":" + std::to_string(m_config.port);
#endif
        std::cerr << "[CCSPD3Adapter] " << m_lastError << std::endl;
        m_socket.reset();
        return false;
    }

    m_connected = true;
    std::cout << "[CCSPD3Adapter] [OK] Connected to " << m_config.ipAddress << ":" << m_config.port << std::endl;

    // Test connection by reading channel 0 status (like Python does at startup)
    std::string testCmd = BuildCommand("00", "M", "");
    std::cout << "[CCSPD3Adapter] [TX] " << testCmd << std::endl;
    std::string response = SendCommandInternal(testCmd);
    std::cout << "[CCSPD3Adapter] [RX] " << response << std::endl;

    if (response.empty()) {
        std::cerr << "[CCSPD3Adapter] Connection test failed - no response" << std::endl;
        m_socket.reset();
        m_connected = false;
        return false;
    }

    std::cout << "[CCSPD3Adapter] Connection verified!" << std::endl;
    return true;
}

bool CCSPD3Adapter::Disconnect() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_socket.valid()) {
        m_socket.reset();
    }

    if (m_connected) {
        std::cout << "[CCSPD3Adapter] Disconnected from " << m_config.deviceName << std::endl;
    }

    m_connected = false;
    return true;
}

bool CCSPD3Adapter::IsConnected() const {
    return m_connected;
}

std::string CCSPD3Adapter::CalculateChecksum(const std::string& data) {
    int total = 0;
    for (char c : data) {
        total += static_cast<unsigned char>(c);
    }
    int checksum = total & 0xFF;

    std::ostringstream oss;
    oss << std::uppercase << std::hex << std::setfill('0') << std::setw(2) << checksum;
    return oss.str();
}

std::string CCSPD3Adapter::BuildCommand(const std::string& channel,
                                         const std::string& instruction,
                                         const std::string& data) {
    std::string cmdBody = "@" + channel + instruction + data;
    std::string checksum = CalculateChecksum(cmdBody);
    return cmdBody + checksum;
}

std::string CCSPD3Adapter::SendCommandInternal(const std::string& command) {
    // Internal send without mutex - caller must hold lock
    if (!m_socket.valid()) {
        m_lastError = "Socket not valid";
        return "";
    }

    // Send command with CRLF
    std::string fullCommand = command + "\r\n";
    int bytesSent = send(m_socket.get(), fullCommand.c_str(),
                         static_cast<int>(fullCommand.length()), 0);

    if (bytesSent == SOCKET_ERROR) {
        m_lastError = "Send failed";
        std::cerr << "[CCSPD3Adapter] " << m_lastError << std::endl;
        return "";
    }

    // Receive response with retry
    char buffer[RECV_BUFFER_SIZE];
    memset(buffer, 0, RECV_BUFFER_SIZE);

    std::string response;
    int totalReceived = 0;
    int retries = 3;

    while (retries > 0) {
        int bytesReceived = recv(m_socket.get(), buffer + totalReceived,
                                  RECV_BUFFER_SIZE - 1 - totalReceived, 0);
        if (bytesReceived > 0) {
            totalReceived += bytesReceived;
            buffer[totalReceived] = '\0';

            // Check if we got a complete response (ends with CR or LF)
            if (strchr(buffer, '\r') || strchr(buffer, '\n')) {
                break;
            }
        } else if (bytesReceived == 0) {
            // Connection closed
            break;
        } else {
            // Error or timeout
            retries--;
            if (retries > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
    }

    if (totalReceived == 0) {
        m_lastError = "Receive failed or timeout";
        return "";
    }

    response = std::string(buffer);
    // Trim CRLF
    while (!response.empty() && (response.back() == '\r' || response.back() == '\n')) {
        response.pop_back();
    }

    return response;
}

std::string CCSPD3Adapter::SendCommand(const std::string& command) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_connected || !m_socket.valid()) {
        m_lastError = "Not connected";
        return "";
    }

    return SendCommandInternal(command);
}

LightingResult CCSPD3Adapter::SetChannelIntensity(int channel, int intensity) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_connected) {
        return LightingResult::NotConnected;
    }

    if (channel < 0 || channel > 7) {
        m_lastError = "Invalid channel: " + std::to_string(channel);
        return LightingResult::InvalidChannel;
    }

    if (intensity < 0 || intensity > 255) {
        m_lastError = "Invalid intensity: " + std::to_string(intensity);
        return LightingResult::InvalidIntensity;
    }

    // Build channel string (00-07)
    std::ostringstream chStr;
    chStr << std::setfill('0') << std::setw(2) << channel;

    // Build intensity string (000-255)
    std::ostringstream intStr;
    intStr << std::setfill('0') << std::setw(3) << intensity;

    std::string cmd = BuildCommand(chStr.str(), "F", intStr.str());
    std::string response = SendCommandInternal(cmd);

    if (response.empty()) {
        return LightingResult::CommunicationError;
    }

    // Check for OK response (contains 'O')
    if (response.find('O') != std::string::npos &&
        response.find('N') == std::string::npos) {
        return LightingResult::Success;
    }

    // Check for error
    if (response.find("N01") != std::string::npos) {
        m_lastError = "Command error";
        return LightingResult::CommandError;
    }
    if (response.find("N02") != std::string::npos) {
        m_lastError = "Checksum error";
        return LightingResult::ChecksumError;
    }
    if (response.find("N03") != std::string::npos) {
        m_lastError = "Value out of range";
        return LightingResult::InvalidIntensity;
    }

    return LightingResult::Success;
}

LightingResult CCSPD3Adapter::SetChannelOn(int channel, bool on) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_connected) {
        return LightingResult::NotConnected;
    }

    if (channel < 0 || channel > 7) {
        m_lastError = "Invalid channel: " + std::to_string(channel);
        return LightingResult::InvalidChannel;
    }

    std::ostringstream chStr;
    chStr << std::setfill('0') << std::setw(2) << channel;

    std::string cmd = BuildCommand(chStr.str(), "L", on ? "1" : "0");
    std::string response = SendCommandInternal(cmd);

    if (response.empty()) {
        return LightingResult::CommunicationError;
    }

    if (response.find('O') != std::string::npos &&
        response.find('N') == std::string::npos) {
        return LightingResult::Success;
    }

    return LightingResult::CommandError;
}

LightingResult CCSPD3Adapter::SetAllChannelsIntensity(int intensity) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_connected) {
        return LightingResult::NotConnected;
    }

    if (intensity < 0 || intensity > 255) {
        m_lastError = "Invalid intensity: " + std::to_string(intensity);
        return LightingResult::InvalidIntensity;
    }

    std::ostringstream intStr;
    intStr << std::setfill('0') << std::setw(3) << intensity;

    // Set each enabled channel individually
    // The FF broadcast command may not work reliably on all CCS PD3 units
    bool anyError = false;
    for (const auto& ch : m_config.channels) {
        if (!ch.enabled) {
            continue;
        }

        std::ostringstream chStr;
        chStr << std::setfill('0') << std::setw(2) << ch.channelNumber;

        std::string cmd = BuildCommand(chStr.str(), "F", intStr.str());
        std::string response = SendCommandInternal(cmd);

        if (response.empty()) {
            anyError = true;
            continue;
        }

        // Check for error response
        if (response.find("N01") != std::string::npos ||
            response.find("N02") != std::string::npos ||
            response.find("N03") != std::string::npos) {
            anyError = true;
        }

        // Small delay between commands
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return anyError ? LightingResult::CommandError : LightingResult::Success;
}

LightingResult CCSPD3Adapter::SetAllChannelsOff() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_connected) {
        return LightingResult::NotConnected;
    }

    // Set each enabled channel's intensity to 0 individually
    // The FF broadcast command may not work reliably on all CCS PD3 units
    bool anyError = false;
    for (const auto& ch : m_config.channels) {
        if (!ch.enabled) {
            continue;
        }

        std::ostringstream chStr;
        chStr << std::setfill('0') << std::setw(2) << ch.channelNumber;

        std::string cmd = BuildCommand(chStr.str(), "F", "000");
        std::string response = SendCommandInternal(cmd);

        if (response.empty()) {
            anyError = true;
            continue;
        }

        // Check for error response
        if (response.find("N01") != std::string::npos ||
            response.find("N02") != std::string::npos ||
            response.find("N03") != std::string::npos) {
            anyError = true;
        }

        // Small delay between commands
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return anyError ? LightingResult::CommandError : LightingResult::Success;
}

bool CCSPD3Adapter::ParseStatusResponse(const std::string& response, ChannelStatus& status) {
    // Response format: @[ch]OF[intensity].S[mode].L[state][checksum]
    // Example: @00OF128.S00.L160

    try {
        if (response.length() < 15 || response[0] != '@') {
            return false;
        }

        // Find 'OF' marker
        size_t ofPos = response.find("OF");
        if (ofPos == std::string::npos) {
            return false;
        }

        // Parse channel
        status.channelNumber = std::stoi(response.substr(1, 2));

        // Parse intensity (3 digits after OF)
        status.intensity = std::stoi(response.substr(ofPos + 2, 3));

        // Find and parse mode (after .S)
        size_t sPos = response.find(".S");
        if (sPos != std::string::npos) {
            status.mode = std::stoi(response.substr(sPos + 2, 2));
        }

        // Find and parse on/off state (after .L)
        size_t lPos = response.find(".L");
        if (lPos != std::string::npos) {
            status.isOn = (response[lPos + 2] == '1');
        }

        return true;
    } catch (const std::exception& e) {
        m_lastError = "Parse error: " + std::string(e.what());
        return false;
    }
}

bool CCSPD3Adapter::ParseErrorResponse(const std::string& response, int& errorCode) {
    // Response format: @00O[errorCode][checksum]
    // Example: @00O004F (normal), @00O114F (error)

    try {
        size_t oPos = response.find('O');
        if (oPos == std::string::npos || oPos + 2 >= response.length()) {
            return false;
        }

        errorCode = std::stoi(response.substr(oPos + 1, 2));
        return true;
    } catch (...) {
        return false;
    }
}

ChannelStatus CCSPD3Adapter::GetChannelStatus(int channel) {
    std::lock_guard<std::mutex> lock(m_mutex);

    ChannelStatus status;
    status.channelNumber = channel;

    if (!m_connected || channel < 0 || channel > 7) {
        return status;
    }

    // Find channel name from config
    for (const auto& ch : m_config.channels) {
        if (ch.channelNumber == channel) {
            status.name = ch.name;
            break;
        }
    }

    std::ostringstream chStr;
    chStr << std::setfill('0') << std::setw(2) << channel;

    std::string cmd = BuildCommand(chStr.str(), "M", "");
    std::string response = SendCommandInternal(cmd);

    if (!response.empty()) {
        ParseStatusResponse(response, status);
    }

    return status;
}

ControllerStatus CCSPD3Adapter::GetControllerStatus() {
    std::lock_guard<std::mutex> lock(m_mutex);

    ControllerStatus status;
    status.deviceId = m_config.deviceId;
    status.deviceName = m_config.deviceName;
    status.connected = m_connected;

    if (!m_connected) {
        return status;
    }

    // Check error status (inline to avoid deadlock)
    std::string errorCmd = BuildCommand("00", "C", "");
    std::string errorResponse = SendCommandInternal(errorCmd);
    if (!errorResponse.empty()) {
        int errorCode = 0;
        if (ParseErrorResponse(errorResponse, errorCode)) {
            status.hasError = (errorCode != 0);
            status.errorCode = errorCode;
        }
    }

    // Get all channel statuses
    for (int ch = 0; ch < 8; ++ch) {
        ChannelStatus chStatus;
        chStatus.channelNumber = ch;

        // Find channel name from config
        for (const auto& cfg : m_config.channels) {
            if (cfg.channelNumber == ch) {
                chStatus.name = cfg.name;
                break;
            }
        }

        std::ostringstream chStr;
        chStr << std::setfill('0') << std::setw(2) << ch;

        std::string cmd = BuildCommand(chStr.str(), "M", "");
        std::string response = SendCommandInternal(cmd);

        if (!response.empty()) {
            ParseStatusResponse(response, chStatus);
        }

        status.channelStatuses.push_back(chStatus);

        // Small delay between channel reads
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    return status;
}

LightingResult CCSPD3Adapter::CheckError() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_connected) {
        return LightingResult::NotConnected;
    }

    std::string cmd = BuildCommand("00", "C", "");
    std::string response = SendCommandInternal(cmd);

    if (response.empty()) {
        return LightingResult::CommunicationError;
    }

    int errorCode = 0;
    if (ParseErrorResponse(response, errorCode)) {
        if (errorCode == 0) {
            return LightingResult::Success;
        }
        m_lastError = "Device error code: " + std::to_string(errorCode);
        return LightingResult::CommandError;
    }

    return LightingResult::CommunicationError;
}

std::string CCSPD3Adapter::GetDeviceId() const {
    return m_config.deviceId;
}

std::string CCSPD3Adapter::GetDeviceName() const {
    return m_config.deviceName;
}

std::string CCSPD3Adapter::GetLastError() const {
    return m_lastError;
}

} // namespace Lighting
