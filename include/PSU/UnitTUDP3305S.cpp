// UnitTUDP3305S.cpp - UNI-T UDP3305S-E 3-channel programmable DC PSU driver (USB/VISA)
#include "UnitTUDP3305S.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>

namespace {

// Trim trailing whitespace / line terminators from a SCPI response.
void trimTrailingWhitespace(std::string& s)
{
  while (!s.empty()) {
    const char c = s.back();
    if (c == '\n' || c == '\r' || c == ' ' || c == '\t') {
      s.pop_back();
    } else {
      break;
    }
  }
}

// One-line description for common VISA errors we are likely to see on USB.
std::string describeVisaError(ViStatus status)
{
  switch (status) {
    case VI_SUCCESS:               return "Success";
    case VI_ERROR_SYSTEM_ERROR:    return "System error";
    case VI_ERROR_INV_OBJECT:      return "Invalid session/object reference";
    case VI_ERROR_RSRC_LOCKED:     return "Resource locked";
    case VI_ERROR_TMO:             return "Timeout";
    case VI_ERROR_INV_RSRC_NAME:   return "Invalid resource name";
    case VI_ERROR_RSRC_NFOUND:     return "Resource not found";
    case VI_ERROR_NSUP_OPER:       return "Operation not supported";
    case VI_ERROR_CLOSING_FAILED:  return "Failed to close session";
    default: {
      std::ostringstream oss;
      oss << "VISA error 0x" << std::hex << static_cast<unsigned long>(status);
      return oss.str();
    }
  }
}

// Parse a numeric SCPI response, returning fallback on failure.
double parseDouble(const std::string& s, double fallback)
{
  try {
    return std::stod(s);
  } catch (...) {
    return fallback;
  }
}

} // namespace

// ============================================================================
// Construction / destruction
// ============================================================================

UnitTUDP3305S::UnitTUDP3305S(const std::string& visaResource,
                             const std::string& deviceName)
  : m_visaResource(visaResource)
  , m_deviceName(deviceName)
{
  std::cout << "[UnitTUDP3305S] Created '" << m_deviceName
            << "' for VISA resource: " << m_visaResource << std::endl;
}

UnitTUDP3305S::~UnitTUDP3305S()
{
  // RAII: ensure outputs are off and VISA sessions are closed.
  disconnect();
}

// ============================================================================
// Connection management
// ============================================================================

bool UnitTUDP3305S::connect()
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_connected) {
    m_lastError = "Already connected";
    return true;
  }

  if (m_visaResource.empty()) {
    m_lastError = "VISA resource string is empty";
    std::cerr << "[UnitTUDP3305S] " << m_lastError << std::endl;
    return false;
  }

  // Open VISA Resource Manager.
  ViStatus status = viOpenDefaultRM(&m_defaultRM);
  if (status < VI_SUCCESS) {
    m_lastError = "viOpenDefaultRM failed: " + describeVisaError(status);
    std::cerr << "[UnitTUDP3305S] " << m_lastError << std::endl;
    m_defaultRM = VI_NULL;
    return false;
  }

  // Open the instrument session.
  status = viOpen(m_defaultRM,
                  const_cast<char*>(m_visaResource.c_str()),
                  VI_NULL, VI_NULL, &m_instrument);
  if (status < VI_SUCCESS) {
    m_lastError = "viOpen('" + m_visaResource + "') failed: " + describeVisaError(status);
    std::cerr << "[UnitTUDP3305S] " << m_lastError << std::endl;
    viClose(m_defaultRM);
    m_defaultRM = VI_NULL;
    m_instrument = VI_NULL;
    return false;
  }

  // Configure session: timeout + LF termination.
  viSetAttribute(m_instrument, VI_ATTR_TMO_VALUE, TIMEOUT_MS);
  viSetAttribute(m_instrument, VI_ATTR_TERMCHAR_EN, VI_TRUE);
  viSetAttribute(m_instrument, VI_ATTR_TERMCHAR, '\n');
  viFlush(m_instrument, VI_READ_BUF_DISCARD | VI_WRITE_BUF_DISCARD);

  // USBTMC devices need a short settle window after the session opens.
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  m_connected = true;

  // Identify the device (best-effort - non-fatal on failure).
  std::string idn;
  if (queryRaw("*IDN?", idn)) {
    m_deviceID = idn;
    std::cout << "[UnitTUDP3305S] '" << m_deviceName << "' connected: " << idn << std::endl;
  } else {
    std::cout << "[UnitTUDP3305S] '" << m_deviceName
              << "' connected (no *IDN? response): " << m_lastError << std::endl;
  }

  return true;
}

void UnitTUDP3305S::disconnect()
{
  // Safety habit: force every output off before tearing the session down.
  // We do this OUTSIDE the lock-guard scope so the helper can take its own lock.
  if (m_connected) {
    forceAllOutputsOff();
  }

  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_instrument != VI_NULL) {
    viClose(m_instrument);
    m_instrument = VI_NULL;
  }
  if (m_defaultRM != VI_NULL) {
    viClose(m_defaultRM);
    m_defaultRM = VI_NULL;
  }

  if (m_connected) {
    m_connected = false;
    std::cout << "[UnitTUDP3305S] '" << m_deviceName << "' disconnected" << std::endl;
  }
}

bool UnitTUDP3305S::isConnected() const
{
  return m_connected;
}

void UnitTUDP3305S::forceAllOutputsOff()
{
  // Best-effort - we ignore failures here because the caller is on the
  // disconnect path and we don't want to mask the real reason for tear-down.
  for (int ch = 1; ch <= NUM_CHANNELS; ++ch) {
    std::ostringstream oss;
    oss << "OUTPut CH" << ch << ",OFF";
    (void)writeLocked(oss.str());
  }
}

// ============================================================================
// Output ON/OFF
// ============================================================================

bool UnitTUDP3305S::setOutputOn(int channel)
{
  if (!validateChannel(channel)) {
    return false;
  }
  std::ostringstream oss;
  oss << "OUTPut CH" << channel << ",ON";
  std::cout << "[UnitTUDP3305S] CH" << channel << " output ON" << std::endl;
  return writeLocked(oss.str());
}

bool UnitTUDP3305S::setOutputOff(int channel)
{
  if (!validateChannel(channel)) {
    return false;
  }
  std::ostringstream oss;
  oss << "OUTPut CH" << channel << ",OFF";
  std::cout << "[UnitTUDP3305S] CH" << channel << " output OFF" << std::endl;
  return writeLocked(oss.str());
}

// ============================================================================
// Voltage / current programming
// ============================================================================

bool UnitTUDP3305S::setVoltage(double voltage, int channel)
{
  if (!validateChannel(channel)) {
    return false;
  }
  // Per HANDOFF: this individual-set form is not yet hardware-verified, but it
  // follows standard SCPI conventions and the verified APPLy/SOURce<n>:VOLTage?
  // pair. Confirm on hardware before using in production recipes.
  std::ostringstream oss;
  oss << "SOURce" << channel << ":VOLTage " << voltage;
  std::cout << "[UnitTUDP3305S] CH" << channel << " set V = " << voltage << " V" << std::endl;
  return writeLocked(oss.str());
}

bool UnitTUDP3305S::setCurrent(double current, int channel)
{
  if (!validateChannel(channel)) {
    return false;
  }
  // Per HANDOFF: see setVoltage() note about individual-set verification.
  std::ostringstream oss;
  oss << "SOURce" << channel << ":CURRent " << current;
  std::cout << "[UnitTUDP3305S] CH" << channel << " set I = " << current << " A" << std::endl;
  return writeLocked(oss.str());
}

// ============================================================================
// Protection (NOT YET HARDWARE-VERIFIED - see HANDOFF section 6)
// ============================================================================

bool UnitTUDP3305S::setOVP(double voltage, int channel)
{
  if (!validateChannel(channel)) {
    return false;
  }
  std::ostringstream oss;
  oss << "SOURce" << channel << ":VOLTage:PROTection " << voltage;
  std::cout << "[UnitTUDP3305S] CH" << channel << " OVP = " << voltage << " V (unverified SCPI)" << std::endl;
  return writeLocked(oss.str());
}

bool UnitTUDP3305S::setOCP(double current, int channel)
{
  if (!validateChannel(channel)) {
    return false;
  }
  std::ostringstream oss;
  oss << "SOURce" << channel << ":CURRent:PROTection " << current;
  std::cout << "[UnitTUDP3305S] CH" << channel << " OCP = " << current << " A (unverified SCPI)" << std::endl;
  return writeLocked(oss.str());
}

// ============================================================================
// Measurements & readbacks
// ============================================================================

double UnitTUDP3305S::readVoltage(int channel)
{
  if (!validateChannel(channel)) {
    return -1.0;
  }
  std::ostringstream q;
  q << "MEASure:VOLTage? CH" << channel;
  std::string response;
  if (!queryLocked(q.str(), response)) {
    return -1.0;
  }
  return parseDouble(response, -1.0);
}

double UnitTUDP3305S::readCurrent(int channel)
{
  if (!validateChannel(channel)) {
    return -1.0;
  }
  std::ostringstream q;
  q << "MEASure:CURRent? CH" << channel;
  std::string response;
  if (!queryLocked(q.str(), response)) {
    return -1.0;
  }
  return parseDouble(response, -1.0);
}

double UnitTUDP3305S::readSetVoltage(int channel)
{
  if (!validateChannel(channel)) {
    return -1.0;
  }
  std::ostringstream q;
  q << "SOURce" << channel << ":VOLTage?";
  std::string response;
  if (!queryLocked(q.str(), response)) {
    return -1.0;
  }
  return parseDouble(response, -1.0);
}

double UnitTUDP3305S::readSetCurrent(int channel)
{
  if (!validateChannel(channel)) {
    return -1.0;
  }
  std::ostringstream q;
  q << "SOURce" << channel << ":CURRent?";
  std::string response;
  if (!queryLocked(q.str(), response)) {
    return -1.0;
  }
  return parseDouble(response, -1.0);
}

bool UnitTUDP3305S::readOutputState(int channel)
{
  if (!validateChannel(channel)) {
    return false;
  }
  std::ostringstream q;
  q << "OUTPut? CH" << channel;
  std::string response;
  if (!queryLocked(q.str(), response)) {
    return false;
  }

  // UDP3305S returns "ON"/"OFF" as a string; defensively also accept "1"/"0"
  // in case firmware variants return numeric form (per HANDOFF section 2 quirk).
  std::string upper;
  upper.reserve(response.size());
  for (char c : response) {
    upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
  }
  return (upper == "ON" || upper == "1");
}

// ============================================================================
// SCPI primitives
// ============================================================================

bool UnitTUDP3305S::validateChannel(int channel) const
{
  if (channel < 1 || channel > NUM_CHANNELS) {
    m_lastError = "Invalid channel " + std::to_string(channel)
                + " (valid: 1.." + std::to_string(NUM_CHANNELS) + ")";
    std::cerr << "[UnitTUDP3305S] " << m_lastError << std::endl;
    return false;
  }
  return true;
}

bool UnitTUDP3305S::writeRaw(const std::string& command)
{
  // ASSUMES m_mutex IS HELD.
  if (!m_connected || m_instrument == VI_NULL) {
    m_lastError = "Not connected";
    return false;
  }

  const std::string framed = command + "\n";
  ViUInt32 retCount = 0;
  const ViStatus status = viWrite(m_instrument,
                                  (ViBuf)framed.c_str(),
                                  static_cast<ViUInt32>(framed.length()),
                                  &retCount);
  if (status < VI_SUCCESS) {
    m_lastError = "viWrite('" + command + "') failed: " + describeVisaError(status);
    std::cerr << "[UnitTUDP3305S] " << m_lastError << std::endl;
    return false;
  }
  return true;
}

bool UnitTUDP3305S::queryRaw(const std::string& query, std::string& response)
{
  // ASSUMES m_mutex IS HELD.
  response.clear();
  if (!writeRaw(query)) {
    return false;
  }

  char buffer[READ_BUFFER_SIZE];
  ViUInt32 retCount = 0;
  const ViStatus status = viRead(m_instrument,
                                 (ViBuf)buffer,
                                 static_cast<ViUInt32>(READ_BUFFER_SIZE - 1U),
                                 &retCount);
  if (status < VI_SUCCESS) {
    m_lastError = "viRead('" + query + "') failed: " + describeVisaError(status);
    std::cerr << "[UnitTUDP3305S] " << m_lastError << std::endl;
    return false;
  }

  if (retCount >= READ_BUFFER_SIZE) {
    retCount = static_cast<ViUInt32>(READ_BUFFER_SIZE - 1U);
  }
  buffer[retCount] = '\0';
  response.assign(buffer, static_cast<size_t>(retCount));
  trimTrailingWhitespace(response);
  return true;
}

bool UnitTUDP3305S::writeLocked(const std::string& command)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return writeRaw(command);
}

bool UnitTUDP3305S::queryLocked(const std::string& query, std::string& response)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return queryRaw(query, response);
}

std::string UnitTUDP3305S::query(const std::string& scpi)
{
  std::string response;
  if (!queryLocked(scpi, response)) {
    return std::string();
  }
  return response;
}
