// CLD101xAdapter.cpp
#include "CLD101xAdapter.h"
#include <iostream>
#include <sstream>
#include <cstring>

CLD101xAdapter::CLD101xAdapter(const TECDevice& config)
  : m_config(config)
  , m_socket()
  , m_connected(false)
{
  std::cout << "[INFO] CLD101xAdapter: Created for device '"
    << m_config.Name << "'" << std::endl;

  // Initialize status
  m_status.Connected = false;
  m_status.TECOn = false;
  m_status.LaserOn = false;
}

CLD101xAdapter::~CLD101xAdapter() {
  Disconnect();
}

// ============================================================================
// CONNECTION MANAGEMENT
// ============================================================================

bool CLD101xAdapter::Connect() {
  if (m_connected) {
    std::cout << "[WARN] " << m_config.Name << ": Already connected" << std::endl;
    return true;
  }

  std::cout << "[INFO] " << m_config.Name << ": Connecting to "
    << m_config.IpAddress << ":" << m_config.Port << std::endl;

  // Create socket
  m_socket.reset(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
  if (!m_socket.valid()) {
    std::cerr << "[ERROR] " << m_config.Name << ": Failed to create socket: "
      << WSAGetLastError() << std::endl;
    return false;
  }

  // Set socket timeout
  DWORD timeout = TIMEOUT_MS;
  setsockopt(m_socket.get(), SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
  setsockopt(m_socket.get(), SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

  // Setup server address
  sockaddr_in serverAddr;
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(static_cast<u_short>(m_config.Port));
  inet_pton(AF_INET, m_config.IpAddress.c_str(), &serverAddr.sin_addr);

  // Connect to server
  if (connect(m_socket.get(), (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
    std::cerr << "[ERROR] " << m_config.Name << ": Connection failed: "
      << WSAGetLastError() << std::endl;
    m_socket.reset();
    return false;
  }

  m_connected = true;
  m_status.Connected = true;
  std::cout << "[INFO] " << m_config.Name << ": Connected successfully" << std::endl;

  // Query initial states
  QueryLaserState();
  QueryTECState();

  return true;
}

void CLD101xAdapter::Disconnect() {
  if (!m_connected) {
    return;
  }

  std::cout << "[INFO] " << m_config.Name << ": Disconnecting..." << std::endl;

  if (m_socket.valid()) {
    m_socket.reset();
  }

  m_connected = false;
  m_status.Connected = false;

  std::cout << "[INFO] " << m_config.Name << ": Disconnected" << std::endl;
}

bool CLD101xAdapter::IsConnected() const {
  return m_connected;
}

bool CLD101xAdapter::Reconnect() {
  std::cout << "[INFO] " << m_config.Name << ": Reconnecting..." << std::endl;
  Disconnect();
  return Connect();
}

// ============================================================================
// TEC CONTROL
// ============================================================================

TECCommandResult CLD101xAdapter::SetTemperature(double temperature) {
  TECCommandResult result;

  if (!m_connected) {
    result.Success = false;
    result.Message = "Not connected";
    return result;
  }

  std::ostringstream cmd;
  cmd << "SET_TEC_TEMPERATURE " << temperature;

  std::string response;
  if (SendCommand(cmd.str(), response)) {
    result.Success = true;
    result.Message = response;
    result.Value = temperature;
    m_status.SetpointTemperature = temperature;
    std::cout << "[INFO] " << m_config.Name << ": " << response << std::endl;
  }
  else {
    result.Success = false;
    result.Message = "Communication failed";
  }

  return result;
}

TECCommandResult CLD101xAdapter::TEC_On() {
  TECCommandResult result;

  if (!m_connected) {
    result.Success = false;
    result.Message = "Not connected";
    return result;
  }

  std::string response;
  if (SendCommand("TEC_ON", response)) {
    result.Success = true;
    result.Message = response;
    m_status.TECOn = true;
    std::cout << "[INFO] " << m_config.Name << ": TEC turned ON" << std::endl;
  }
  else {
    result.Success = false;
    result.Message = "Communication failed";
  }

  return result;
}

TECCommandResult CLD101xAdapter::TEC_Off() {
  TECCommandResult result;

  if (!m_connected) {
    result.Success = false;
    result.Message = "Not connected";
    return result;
  }

  std::string response;
  if (SendCommand("TEC_OFF", response)) {
    result.Success = true;
    result.Message = response;
    m_status.TECOn = false;
    std::cout << "[INFO] " << m_config.Name << ": TEC turned OFF" << std::endl;
  }
  else {
    result.Success = false;
    result.Message = "Communication failed";
  }

  return result;
}

TECCommandResult CLD101xAdapter::ReadTemperature() {
  TECCommandResult result;

  if (!m_connected) {
    result.Success = false;
    result.Message = "Not connected";
    return result;
  }

  std::string response;
  if (SendCommand("READ_TEC_TEMPERATURE", response)) {
    double temp = ParseNumericResponse(response);
    result.Success = true;
    result.Message = response;
    result.Value = temp;
    m_status.CurrentTemperature = temp;
  }
  else {
    result.Success = false;
    result.Message = "Communication failed";
  }

  return result;
}

TECCommandResult CLD101xAdapter::ReadTECCurrent() {
  TECCommandResult result;

  if (!m_connected) {
    result.Success = false;
    result.Message = "Not connected";
    return result;
  }

  std::string response;
  if (SendCommand("READ_TEC_CURRENT", response)) {
    double current = ParseNumericResponse(response);
    result.Success = true;
    result.Message = response;
    result.Value = current;
    m_status.CurrentTECCurrent = current;
  }
  else {
    result.Success = false;
    result.Message = "Communication failed";
  }

  return result;
}

// ============================================================================
// LASER CONTROL
// ============================================================================

TECCommandResult CLD101xAdapter::SetLaserCurrent(double current) {
  TECCommandResult result;

  if (!m_connected) {
    result.Success = false;
    result.Message = "Not connected";
    return result;
  }

  // Convert mA to A for the device
  double currentInAmps = current / 1000.0;

  std::ostringstream cmd;
  cmd << "SET_LASER_CURRENT " << currentInAmps;

  std::string response;
  if (SendCommand(cmd.str(), response)) {
    result.Success = true;
    result.Message = response;
    result.Value = current;
    m_status.SetpointLaserCurrent = current;
    std::cout << "[INFO] " << m_config.Name << ": " << response << std::endl;
  }
  else {
    result.Success = false;
    result.Message = "Communication failed";
  }

  return result;
}

TECCommandResult CLD101xAdapter::Laser_On() {
  TECCommandResult result;

  if (!m_connected) {
    result.Success = false;
    result.Message = "Not connected";
    return result;
  }

  std::string response;
  if (SendCommand("LASER_ON", response)) {
    result.Success = true;
    result.Message = response;
    m_status.LaserOn = true;
    std::cout << "[INFO] " << m_config.Name << ": Laser turned ON" << std::endl;
  }
  else {
    result.Success = false;
    result.Message = "Communication failed";
  }

  return result;
}

TECCommandResult CLD101xAdapter::Laser_Off() {
  TECCommandResult result;

  if (!m_connected) {
    result.Success = false;
    result.Message = "Not connected";
    return result;
  }

  std::string response;
  if (SendCommand("LASER_OFF", response)) {
    result.Success = true;
    result.Message = response;
    m_status.LaserOn = false;
    std::cout << "[INFO] " << m_config.Name << ": Laser turned OFF" << std::endl;
  }
  else {
    result.Success = false;
    result.Message = "Communication failed";
  }

  return result;
}

TECCommandResult CLD101xAdapter::ReadLaserCurrent() {
  TECCommandResult result;

  if (!m_connected) {
    result.Success = false;
    result.Message = "Not connected";
    return result;
  }

  std::string response;
  if (SendCommand("READ_LASER_CURRENT", response)) {
    double currentInAmps = ParseNumericResponse(response);
    double currentInMA = currentInAmps * 1000.0;  // Convert A to mA

    result.Success = true;
    result.Message = response;
    result.Value = currentInMA;
    m_status.CurrentLaserCurrent = currentInMA;
  }
  else {
    result.Success = false;
    result.Message = "Communication failed";
  }

  return result;
}

// ============================================================================
// STATUS QUERIES
// ============================================================================

TECStatus CLD101xAdapter::GetStatus() const {
  return m_status;
}

std::string CLD101xAdapter::GetDeviceName() const {
  return m_config.Name;
}

std::string CLD101xAdapter::GetDeviceType() const {
  return m_config.TypeController;
}

// ============================================================================
// PRIVATE METHODS
// ============================================================================

bool CLD101xAdapter::SendCommand(const std::string& command, std::string& response) {
  if (!m_connected || !m_socket.valid()) {
    std::cerr << "[ERROR] " << m_config.Name << ": Not connected" << std::endl;
    return false;
  }

  // Send command
  std::string cmdWithNewline = command + "\n";
  int sendResult = send(m_socket.get(), cmdWithNewline.c_str(), static_cast<int>(cmdWithNewline.length()), 0);
  if (sendResult == SOCKET_ERROR) {
    std::cerr << "[ERROR] " << m_config.Name << ": Send failed: "
      << WSAGetLastError() << std::endl;
    return false;
  }

  // Receive response
  char buffer[1024];
  int bytesReceived = recv(m_socket.get(), buffer, sizeof(buffer) - 1, 0);
  if (bytesReceived > 0) {
    buffer[bytesReceived] = '\0';
    response = std::string(buffer);

    // Remove trailing newline/whitespace
    while (!response.empty() && (response.back() == '\n' || response.back() == '\r')) {
      response.pop_back();
    }

    return true;
  }
  else if (bytesReceived == 0) {
    std::cerr << "[ERROR] " << m_config.Name << ": Connection closed by server" << std::endl;
    m_connected = false;
    m_status.Connected = false;
  }
  else {
    std::cerr << "[ERROR] " << m_config.Name << ": Receive failed: "
      << WSAGetLastError() << std::endl;
  }

  return false;
}

double CLD101xAdapter::ParseNumericResponse(const std::string& response) const {
  // Response format: "Current laser current [A]: 0.05" or "Current TEC temperature [C]: 25.3"
  // Extract the last number from the string

  size_t colonPos = response.find_last_of(':');
  if (colonPos != std::string::npos) {
    std::string valueStr = response.substr(colonPos + 1);
    try {
      return std::stod(valueStr);
    }
    catch (...) {
      std::cerr << "[ERROR] " << m_config.Name
        << ": Failed to parse numeric value from: " << response << std::endl;
    }
  }

  return 0.0;
}

bool CLD101xAdapter::QueryLaserState() {
  if (!m_connected) {
    return false;
  }

  std::string response;
  if (SendCommand("OUTPUT1:STATE?", response)) {
    // Response should be "ON" or "OFF"
    m_status.LaserOn = (response.find("ON") != std::string::npos);
    return true;
  }

  return false;
}

bool CLD101xAdapter::QueryTECState() {
  if (!m_connected) {
    return false;
  }

  std::string response;
  if (SendCommand("OUTPUT2:STATE?", response)) {
    // Response should be "ON" or "OFF"
    m_status.TECOn = (response.find("ON") != std::string::npos);
    return true;
  }

  return false;
}

void CLD101xAdapter::UpdateStatus() const {
  // This can be called periodically to refresh status
  // For now, status is updated when commands are executed
}