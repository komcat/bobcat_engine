// CLD101xDirectAdapter.cpp
#include "CLD101xDirectAdapter.h"
#include <iostream>
#include <sstream>
#include <cstring>

CLD101xDirectAdapter::CLD101xDirectAdapter(const TECDevice& config)
  : m_config(config)
  , m_defaultRM(0)
  , m_instrument(0)
  , m_connected(false)
{
  std::cout << "[INFO] CLD101xDirectAdapter: Created for device '"
    << m_config.Name << "'" << std::endl;

  // Initialize status
  m_status.Connected = false;
  m_status.TECOn = false;
  m_status.LaserOn = false;
}

CLD101xDirectAdapter::~CLD101xDirectAdapter() {
  Disconnect();
}

// ============================================================================
// CONNECTION MANAGEMENT
// ============================================================================

bool CLD101xDirectAdapter::Connect() {
  if (m_connected) {
    std::cout << "[WARN] " << m_config.Name << ": Already connected" << std::endl;
    return true;
  }

  std::cout << "[INFO] " << m_config.Name << ": Connecting to VISA resource: "
    << m_config.VISAResource << std::endl;

  ViStatus status;

  // Open VISA Resource Manager
  status = viOpenDefaultRM(&m_defaultRM);
  if (status < VI_SUCCESS) {
    std::cerr << "[ERROR] " << m_config.Name << ": Failed to open VISA Resource Manager: "
      << GetVISAErrorDescription(status) << std::endl;
    return false;
  }

  // Open instrument session
  status = viOpen(m_defaultRM,
                  const_cast<char*>(m_config.VISAResource.c_str()),
                  VI_NULL,
                  VI_NULL,
                  &m_instrument);
  if (status < VI_SUCCESS) {
    std::cerr << "[ERROR] " << m_config.Name << ": Failed to open instrument: "
      << GetVISAErrorDescription(status) << std::endl;
    viClose(m_defaultRM);
    m_defaultRM = 0;
    return false;
  }

  // Set timeout
  status = viSetAttribute(m_instrument, VI_ATTR_TMO_VALUE, TIMEOUT_MS);
  if (status < VI_SUCCESS) {
    std::cerr << "[WARN] " << m_config.Name << ": Failed to set timeout: "
      << GetVISAErrorDescription(status) << std::endl;
  }

  m_connected = true;
  m_status.Connected = true;

  // Query device identification
  std::string idn;
  if (QueryCommand("*IDN?", idn)) {
    std::cout << "[INFO] " << m_config.Name << ": Connected to: " << idn << std::endl;
  }

  // Query initial states
  QueryLaserState();
  QueryTECState();

  std::cout << "[INFO] " << m_config.Name << ": Connected successfully" << std::endl;
  return true;
}

void CLD101xDirectAdapter::Disconnect() {
  if (!m_connected) {
    return;
  }

  std::cout << "[INFO] " << m_config.Name << ": Disconnecting..." << std::endl;

  // Close instrument session
  if (m_instrument != 0) {
    viClose(m_instrument);
    m_instrument = 0;
  }

  // Close resource manager
  if (m_defaultRM != 0) {
    viClose(m_defaultRM);
    m_defaultRM = 0;
  }

  m_connected = false;
  m_status.Connected = false;

  std::cout << "[INFO] " << m_config.Name << ": Disconnected" << std::endl;
}

bool CLD101xDirectAdapter::IsConnected() const {
  return m_connected;
}

bool CLD101xDirectAdapter::Reconnect() {
  std::cout << "[INFO] " << m_config.Name << ": Reconnecting..." << std::endl;
  Disconnect();
  return Connect();
}

// ============================================================================
// TEC CONTROL
// ============================================================================

TECCommandResult CLD101xDirectAdapter::SetTemperature(double temperature) {
  TECCommandResult result;

  if (!m_connected) {
    result.Success = false;
    result.Message = "Not connected";
    return result;
  }

  // Build SCPI command
  std::ostringstream cmd;
  cmd << "source2:temperature:spoint " << temperature;

  if (WriteCommand(cmd.str())) {
    // Verify by reading back
    std::string response;
    if (QueryCommand("source2:temperature:spoint?", response)) {
      double readback = ParseNumericResponse(response);
      result.Success = true;
      result.Message = "Set TEC temperature to " + std::to_string(readback) + " °C";
      result.Value = readback;
      m_status.SetpointTemperature = readback;
      std::cout << "[INFO] " << m_config.Name << ": " << result.Message << std::endl;
    } else {
      result.Success = false;
      result.Message = "Failed to verify setpoint";
    }
  } else {
    result.Success = false;
    result.Message = "Communication failed";
  }

  return result;
}

TECCommandResult CLD101xDirectAdapter::TEC_On() {
  TECCommandResult result;

  if (!m_connected) {
    result.Success = false;
    result.Message = "Not connected";
    return result;
  }

  if (WriteCommand("output2:state on")) {
    // Verify state
    if (QueryTECState()) {
      result.Success = true;
      result.Message = "TEC turned ON";
      std::cout << "[INFO] " << m_config.Name << ": TEC turned ON" << std::endl;
    } else {
      result.Success = false;
      result.Message = "Failed to verify TEC state";
    }
  } else {
    result.Success = false;
    result.Message = "Communication failed";
  }

  return result;
}

TECCommandResult CLD101xDirectAdapter::TEC_Off() {
  TECCommandResult result;

  if (!m_connected) {
    result.Success = false;
    result.Message = "Not connected";
    return result;
  }

  if (WriteCommand("output2:state off")) {
    // Verify state
    if (QueryTECState()) {
      result.Success = true;
      result.Message = "TEC turned OFF";
      std::cout << "[INFO] " << m_config.Name << ": TEC turned OFF" << std::endl;
    } else {
      result.Success = false;
      result.Message = "Failed to verify TEC state";
    }
  } else {
    result.Success = false;
    result.Message = "Communication failed";
  }

  return result;
}

TECCommandResult CLD101xDirectAdapter::ReadTemperature() {
  TECCommandResult result;

  if (!m_connected) {
    result.Success = false;
    result.Message = "Not connected";
    return result;
  }

  std::string response;
  if (QueryCommand("SENSe2:temperature:data?", response)) {
    double temp = ParseNumericResponse(response);
    result.Success = true;
    result.Message = "Current TEC temperature: " + std::to_string(temp) + " °C";
    result.Value = temp;
    m_status.CurrentTemperature = temp;
  } else {
    result.Success = false;
    result.Message = "Communication failed";
  }

  return result;
}

TECCommandResult CLD101xDirectAdapter::ReadTECCurrent() {
  TECCommandResult result;

  if (!m_connected) {
    result.Success = false;
    result.Message = "Not connected";
    return result;
  }

  std::string response;
  // CLD101x uses SENSe2:current:dc:data? for TEC current (matching laser pattern sense3:current:dc:data?)
  if (QueryCommand("SENSe2:current:dc:data?", response)) {
    double current = ParseNumericResponse(response);  // Current in Amps
    result.Success = true;
    result.Message = "Current TEC current: " + std::to_string(current) + " A";
    result.Value = current;
    m_status.CurrentTECCurrent = current;
  } else {
    result.Success = false;
    result.Message = "Communication failed";
  }

  return result;
}

// ============================================================================
// LASER CONTROL
// ============================================================================

TECCommandResult CLD101xDirectAdapter::SetLaserCurrent(double current) {
  TECCommandResult result;

  if (!m_connected) {
    result.Success = false;
    result.Message = "Not connected";
    return result;
  }

  // Convert mA to A for the device
  double currentInAmps = current / 1000.0;

  // Build SCPI command
  std::ostringstream cmd;
  cmd << "source1:current:level:amplitude " << currentInAmps;

  if (WriteCommand(cmd.str())) {
    // Verify by reading back
    std::string response;
    if (QueryCommand("source1:current:level:amplitude?", response)) {
      double readbackAmps = ParseNumericResponse(response);
      double readbackMA = readbackAmps * 1000.0;
      result.Success = true;
      result.Message = "Set laser current to " + std::to_string(readbackMA) + " mA";
      result.Value = readbackMA;
      m_status.SetpointLaserCurrent = readbackMA;
      std::cout << "[INFO] " << m_config.Name << ": " << result.Message << std::endl;
    } else {
      result.Success = false;
      result.Message = "Failed to verify setpoint";
    }
  } else {
    result.Success = false;
    result.Message = "Communication failed";
  }

  return result;
}

TECCommandResult CLD101xDirectAdapter::Laser_On() {
  TECCommandResult result;

  if (!m_connected) {
    result.Success = false;
    result.Message = "Not connected";
    return result;
  }

  if (WriteCommand("output1:state on")) {
    // Verify state
    if (QueryLaserState()) {
      result.Success = true;
      result.Message = "Laser turned ON";
      std::cout << "[INFO] " << m_config.Name << ": Laser turned ON" << std::endl;
    } else {
      result.Success = false;
      result.Message = "Failed to verify laser state";
    }
  } else {
    result.Success = false;
    result.Message = "Communication failed";
  }

  return result;
}

TECCommandResult CLD101xDirectAdapter::Laser_Off() {
  TECCommandResult result;

  if (!m_connected) {
    result.Success = false;
    result.Message = "Not connected";
    return result;
  }

  if (WriteCommand("output1:state off")) {
    // Verify state
    if (QueryLaserState()) {
      result.Success = true;
      result.Message = "Laser turned OFF";
      std::cout << "[INFO] " << m_config.Name << ": Laser turned OFF" << std::endl;
    } else {
      result.Success = false;
      result.Message = "Failed to verify laser state";
    }
  } else {
    result.Success = false;
    result.Message = "Communication failed";
  }

  return result;
}

TECCommandResult CLD101xDirectAdapter::ReadLaserCurrent() {
  TECCommandResult result;

  if (!m_connected) {
    result.Success = false;
    result.Message = "Not connected";
    return result;
  }

  std::string response;
  if (QueryCommand("sense3:current:dc:data?", response)) {
    double currentInAmps = ParseNumericResponse(response);
    double currentInMA = currentInAmps * 1000.0;  // Convert A to mA

    result.Success = true;
    result.Message = "Current laser current: " + std::to_string(currentInMA) + " mA";
    result.Value = currentInMA;
    m_status.CurrentLaserCurrent = currentInMA;
  } else {
    result.Success = false;
    result.Message = "Communication failed";
  }

  return result;
}

// ============================================================================
// STATUS QUERIES
// ============================================================================

TECStatus CLD101xDirectAdapter::GetStatus() const {
  return m_status;
}

std::string CLD101xDirectAdapter::GetDeviceName() const {
  return m_config.Name;
}

std::string CLD101xDirectAdapter::GetDeviceType() const {
  return m_config.TypeController;
}

// ============================================================================
// PRIVATE METHODS
// ============================================================================

bool CLD101xDirectAdapter::WriteCommand(const std::string& command) {
  if (!m_connected || m_instrument == 0) {
    std::cerr << "[ERROR] " << m_config.Name << ": Not connected" << std::endl;
    return false;
  }

  ViStatus status;
  ViUInt32 retCount;

  // Add newline to command
  std::string cmdWithNewline = command + "\n";

  // Write command to device
  status = viWrite(m_instrument,
                   (ViBuf)cmdWithNewline.c_str(),
                   cmdWithNewline.length(),
                   &retCount);

  if (status < VI_SUCCESS) {
    std::cerr << "[ERROR] " << m_config.Name << ": Write failed: "
      << GetVISAErrorDescription(status) << std::endl;
    return false;
  }

  return true;
}

bool CLD101xDirectAdapter::QueryCommand(const std::string& query, std::string& response) {
  if (!m_connected || m_instrument == 0) {
    std::cerr << "[ERROR] " << m_config.Name << ": Not connected" << std::endl;
    return false;
  }

  ViStatus status;
  ViUInt32 retCount;
  char buffer[READ_BUFFER_SIZE];

  // Add newline to query
  std::string queryWithNewline = query + "\n";

  // Write query to device
  status = viWrite(m_instrument,
                   (ViBuf)queryWithNewline.c_str(),
                   queryWithNewline.length(),
                   &retCount);

  if (status < VI_SUCCESS) {
    std::cerr << "[ERROR] " << m_config.Name << ": Query write failed: "
      << GetVISAErrorDescription(status) << std::endl;
    return false;
  }

  // Read response
  status = viRead(m_instrument, (ViBuf)buffer, READ_BUFFER_SIZE - 1, &retCount);

  if (status < VI_SUCCESS) {
    std::cerr << "[ERROR] " << m_config.Name << ": Query read failed: "
      << GetVISAErrorDescription(status) << std::endl;
    return false;
  }

  // Null-terminate and convert to string
  buffer[retCount] = '\0';
  response = std::string(buffer);

  // Remove trailing whitespace/newlines
  while (!response.empty() && (response.back() == '\n' || response.back() == '\r' || response.back() == ' ')) {
    response.pop_back();
  }

  return true;
}

double CLD101xDirectAdapter::ParseNumericResponse(const std::string& response) const {
  // SCPI responses are typically just the numeric value
  // e.g., "25.3" or "0.05"
  try {
    return std::stod(response);
  }
  catch (...) {
    std::cerr << "[ERROR] " << m_config.Name
      << ": Failed to parse numeric value from: '" << response << "'" << std::endl;
  }

  return 0.0;
}

bool CLD101xDirectAdapter::QueryLaserState() {
  if (!m_connected) {
    return false;
  }

  std::string response;
  if (QueryCommand("output1:state?", response)) {
    // Response should be "1" (ON) or "0" (OFF), or "ON"/"OFF"
    m_status.LaserOn = (response.find("1") != std::string::npos ||
                        response.find("ON") != std::string::npos);
    return true;
  }

  return false;
}

bool CLD101xDirectAdapter::QueryTECState() {
  if (!m_connected) {
    return false;
  }

  std::string response;
  if (QueryCommand("output2:state?", response)) {
    // Response should be "1" (ON) or "0" (OFF), or "ON"/"OFF"
    m_status.TECOn = (response.find("1") != std::string::npos ||
                      response.find("ON") != std::string::npos);
    return true;
  }

  return false;
}

std::string CLD101xDirectAdapter::GetVISAErrorDescription(ViStatus status) const {
  // Common VISA error codes
  switch (status) {
    case VI_SUCCESS:
      return "Success";
    case VI_ERROR_SYSTEM_ERROR:
      return "System error";
    case VI_ERROR_INV_OBJECT:
      return "Invalid session or object reference";
    case VI_ERROR_RSRC_LOCKED:
      return "Resource locked";
    case VI_ERROR_TMO:
      return "Timeout";
    case VI_ERROR_CLOSING_FAILED:
      return "Failed to close session";
    case VI_ERROR_INV_RSRC_NAME:
      return "Invalid resource name";
    case VI_ERROR_RSRC_NFOUND:
      return "Resource not found";
    case VI_ERROR_NSUP_OPER:
      return "Operation not supported";
    default:
      return "VISA error code: 0x" + std::to_string(status);
  }
}
