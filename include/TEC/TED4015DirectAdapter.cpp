// TED4015DirectAdapter.cpp
#include "TED4015DirectAdapter.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <thread>
#include <chrono>

TED4015DirectAdapter::TED4015DirectAdapter(const TECDevice& config)
  : m_config(config)
  , m_defaultRM(0)
  , m_instrument(0)
  , m_connected(false)
{
  std::cout << "[INFO] TED4015DirectAdapter: Created for device '"
    << m_config.Name << "'" << std::endl;

  // Initialize status
  m_status.Connected = false;
  m_status.TECOn = false;
  m_status.LaserOn = false;  // TED4015 doesn't have laser, always false
}

TED4015DirectAdapter::~TED4015DirectAdapter() {
  Disconnect();
}

// ============================================================================
// CONNECTION MANAGEMENT
// ============================================================================

bool TED4015DirectAdapter::Connect() {
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

  // Configure termination character (newline)
  viSetAttribute(m_instrument, VI_ATTR_TERMCHAR_EN, VI_TRUE);
  viSetAttribute(m_instrument, VI_ATTR_TERMCHAR, '\n');

  // Clear I/O buffers to remove any stale data
  viFlush(m_instrument, VI_READ_BUF_DISCARD | VI_WRITE_BUF_DISCARD);

  // USB devices need time to initialize after session opens
  // Give the device 200ms to settle before sending commands
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  m_connected = true;
  m_status.Connected = true;

  // Clear any errors in the device (optional but recommended)
  WriteCommand("*CLS");

  // Query device identification
  std::string idn;
  if (QueryCommand("*IDN?", idn)) {
    std::cout << "[INFO] " << m_config.Name << ": Connected to: " << idn << std::endl;
  }

  // Query initial TEC state
  QueryTECState();

  // Read initial temperature
  ReadTemperature();

  std::cout << "[INFO] " << m_config.Name << ": Connected successfully" << std::endl;
  return true;
}

void TED4015DirectAdapter::Disconnect() {
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

bool TED4015DirectAdapter::IsConnected() const {
  return m_connected;
}

bool TED4015DirectAdapter::Reconnect() {
  std::cout << "[INFO] " << m_config.Name << ": Reconnecting..." << std::endl;
  Disconnect();
  return Connect();
}

// ============================================================================
// TEC CONTROL
// ============================================================================

TECCommandResult TED4015DirectAdapter::SetTemperature(double temperature) {
  TECCommandResult result;

  if (!m_connected) {
    result.Success = false;
    result.Message = "Not connected";
    return result;
  }

  // Build SCPI command
  std::ostringstream cmd;
  cmd << "SOUR:TEMP " << temperature;

  if (WriteCommand(cmd.str())) {
    // Verify by reading back
    std::string response;
    if (QueryCommand("SOUR:TEMP?", response)) {
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

TECCommandResult TED4015DirectAdapter::TEC_On() {
  TECCommandResult result;

  if (!m_connected) {
    result.Success = false;
    result.Message = "Not connected";
    return result;
  }

  if (WriteCommand("OUTP:STAT 1")) {
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

TECCommandResult TED4015DirectAdapter::TEC_Off() {
  TECCommandResult result;

  if (!m_connected) {
    result.Success = false;
    result.Message = "Not connected";
    return result;
  }

  if (WriteCommand("OUTP:STAT 0")) {
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

TECCommandResult TED4015DirectAdapter::ReadTemperature() {
  TECCommandResult result;

  if (!m_connected) {
    result.Success = false;
    result.Message = "Not connected";
    return result;
  }

  std::string response;
  if (QueryCommand("MEAS:TEMP?", response)) {
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

TECCommandResult TED4015DirectAdapter::ReadTECCurrent() {
  TECCommandResult result;

  if (!m_connected) {
    result.Success = false;
    result.Message = "Not connected";
    return result;
  }

  // Check if TEC output is ON - measurements only work when output is enabled
  if (!m_status.TECOn) {
    result.Success = true;
    result.Message = "TEC output is OFF";
    result.Value = 0.0;
    m_status.CurrentTECCurrent = 0.0;
    m_status.CurrentTECVoltage = 0.0;
    return result;
  }

  std::string response;
  if (QueryCommand("MEAS:CURR?", response)) {
    double current = ParseNumericResponse(response);  // Current in Amps
    result.Success = true;
    result.Message = "Current TEC current: " + std::to_string(current) + " A";
    result.Value = current;
    m_status.CurrentTECCurrent = current;

    // Also read TEC voltage while we're at it
    std::string voltResponse;
    if (QueryCommand("MEAS:VOLT?", voltResponse)) {
      double voltage = ParseNumericResponse(voltResponse);
      m_status.CurrentTECVoltage = voltage;
    }
  } else {
    result.Success = false;
    result.Message = "Communication failed";
  }

  return result;
}

// ============================================================================
// LASER CONTROL (NOT SUPPORTED - TED4015 is TEC-only)
// ============================================================================

TECCommandResult TED4015DirectAdapter::SetLaserCurrent(double /*current*/) {
  TECCommandResult result;
  result.Success = false;
  result.Message = "Laser control not supported on TED4015 (TEC-only controller)";
  // No warning - this is expected for TEC-only controllers
  return result;
}

TECCommandResult TED4015DirectAdapter::Laser_On() {
  TECCommandResult result;
  result.Success = false;
  result.Message = "Laser control not supported on TED4015 (TEC-only controller)";
  // No warning - this is expected for TEC-only controllers
  return result;
}

TECCommandResult TED4015DirectAdapter::Laser_Off() {
  TECCommandResult result;
  result.Success = false;
  result.Message = "Laser control not supported on TED4015 (TEC-only controller)";
  // No warning - this is expected for TEC-only controllers
  return result;
}

TECCommandResult TED4015DirectAdapter::ReadLaserCurrent() {
  TECCommandResult result;
  result.Success = false;
  result.Message = "Laser control not supported on TED4015 (TEC-only controller)";
  // No warning - this is expected for TEC-only controllers
  return result;
}

// ============================================================================
// STATUS QUERIES
// ============================================================================

TECStatus TED4015DirectAdapter::GetStatus() const {
  return m_status;
}

std::string TED4015DirectAdapter::GetDeviceName() const {
  return m_config.Name;
}

std::string TED4015DirectAdapter::GetDeviceType() const {
  return m_config.TypeController;
}

// ============================================================================
// PRIVATE METHODS
// ============================================================================

bool TED4015DirectAdapter::WriteCommand(const std::string& command) {
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
                   static_cast<ViUInt32>(cmdWithNewline.length()),
                   &retCount);

  if (status < VI_SUCCESS) {
    std::cerr << "[ERROR] " << m_config.Name << ": Write failed: "
      << GetVISAErrorDescription(status) << std::endl;
    return false;
  }

  return true;
}

bool TED4015DirectAdapter::QueryCommand(const std::string& query, std::string& response) {
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
                   static_cast<ViUInt32>(queryWithNewline.length()),
                   &retCount);

  if (status < VI_SUCCESS) {
    std::cerr << "[ERROR] " << m_config.Name << ": Query write failed for '" << query << "': "
      << GetVISAErrorDescription(status) << std::endl;
    return false;
  }

  // Read response
  status = viRead(m_instrument, (ViBuf)buffer, READ_BUFFER_SIZE - 1, &retCount);

  if (status < VI_SUCCESS) {
    std::cerr << "[ERROR] " << m_config.Name << ": Query read failed for '" << query << "': "
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

double TED4015DirectAdapter::ParseNumericResponse(const std::string& response) const {
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

bool TED4015DirectAdapter::QueryTECState() {
  if (!m_connected) {
    return false;
  }

  std::string response;
  if (QueryCommand("OUTP:STAT?", response)) {
    // Response should be "1" (ON) or "0" (OFF)
    m_status.TECOn = (response.find("1") != std::string::npos ||
                      response.find("ON") != std::string::npos);
    return true;
  }

  return false;
}

std::string TED4015DirectAdapter::GetVISAErrorDescription(ViStatus status) const {
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
