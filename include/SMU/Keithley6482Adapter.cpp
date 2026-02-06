// Keithley6482Adapter.cpp - Implementation of Keithley 6482 Dual-Channel Picoammeter with Voltage Source
#include "Keithley6482Adapter.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <utility>  // for std::pair

Keithley6482Adapter::Keithley6482Adapter(const std::string& visaResource, const SMUDeviceConfig& config)
  : QObject(nullptr)
  , m_defaultRM(VI_NULL)
  , m_instrument(VI_NULL)
  , m_config(config)
  , m_visaResource(visaResource)
  , m_deviceID("")
  , m_connected(false)
  , m_currentChannel(1)
  , m_currentMode(MeasurementMode::ACCURATE)
  , m_pollTimer(nullptr)
  , m_isPolling(false)
  , m_sampleCount(0)
  , m_readCount(0)
  , m_lastError("")
{
  // Create polling timer
  m_pollTimer = new QTimer(this);
  connect(m_pollTimer, &QTimer::timeout, this, &Keithley6482Adapter::onPollTimeout);

  std::cout << "[Keithley6482] Adapter created for: " << m_config.Name
    << " (Resource: " << m_visaResource << ")" << std::endl;
}

Keithley6482Adapter::~Keithley6482Adapter()
{
  StopPolling();  // Stop polling before disconnecting
  Disconnect();
  std::cout << "[Keithley6482] Adapter destroyed: " << m_config.Name << std::endl;
}

// ========== Connection Management ==========

bool Keithley6482Adapter::Connect()
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_connected) {
    std::cout << "[Keithley6482] Already connected: " << m_config.Name << std::endl;
    return true;
  }

  std::cout << "[Keithley6482] Connecting to: " << m_visaResource << std::endl;

  // Open VISA Resource Manager
  ViStatus status = viOpenDefaultRM(&m_defaultRM);
  if (status < VI_SUCCESS) {
    SetLastError("Failed to open VISA Resource Manager: " + GetVISAErrorDescription(status));
    std::cerr << "[Keithley6482] " << m_lastError << std::endl;
    return false;
  }

  // Open instrument session with timeout (5 seconds)
  constexpr unsigned long CONNECTION_TIMEOUT_MS = 5000;
  status = viOpen(m_defaultRM, m_visaResource.c_str(), VI_NULL, CONNECTION_TIMEOUT_MS, &m_instrument);
  if (status < VI_SUCCESS) {
    SetLastError("Failed to open instrument (timeout=" + std::to_string(CONNECTION_TIMEOUT_MS) + "ms): " + GetVISAErrorDescription(status));
    std::cerr << "[Keithley6482] " << m_lastError << std::endl;
    viClose(m_defaultRM);
    m_defaultRM = VI_NULL;
    return false;
  }

  // Set timeout
  viSetAttribute(m_instrument, VI_ATTR_TMO_VALUE, TIMEOUT_MS);

  // Query device identification - use direct VISA calls (not QueryCommand which checks m_connected)
  char buffer[BUFFER_SIZE];
  status = viPrintf(m_instrument, "*IDN?\n");
  if (status < VI_SUCCESS) {
    SetLastError("Failed to write *IDN? command: " + GetVISAErrorDescription(status));
    std::cerr << "[Keithley6482] " << m_lastError << std::endl;
    // Clean up without calling Disconnect() to avoid deadlock
    viClose(m_instrument);
    viClose(m_defaultRM);
    m_instrument = VI_NULL;
    m_defaultRM = VI_NULL;
    return false;
  }

  status = viScanf(m_instrument, "%t", buffer);
  if (status < VI_SUCCESS) {
    SetLastError("Failed to read *IDN? response: " + GetVISAErrorDescription(status));
    std::cerr << "[Keithley6482] " << m_lastError << std::endl;
    // Clean up without calling Disconnect() to avoid deadlock
    viClose(m_instrument);
    viClose(m_defaultRM);
    m_instrument = VI_NULL;
    m_defaultRM = VI_NULL;
    return false;
  }

  m_deviceID = TrimString(std::string(buffer));
  m_connected = true;  // Set connected flag BEFORE calling other methods

  std::cout << "[Keithley6482] Connected to: " << m_deviceID << std::endl;

  // Initialize device settings - use Internal methods since mutex is already locked
  WriteCommandInternal("*RST");                           // Reset to defaults
  WriteCommandInternal("*CLS");                           // Clear status
  WriteCommandInternal(":SYST:ZCH OFF");                 // Disable zero check
  WriteCommandInternal(":FORM:ELEM READ");               // Set data format

  // Set default integration time
  if (m_config.MeasurementNPLC > 0) {
    SetIntegrationTimeInternal(m_config.MeasurementNPLC);
  }

  // Set measurement range
  if (m_config.AutoRange) {
    WriteCommandInternal(":SENS:CURR:RANG:AUTO ON");
  }
  else if (m_config.FixedRange > 0) {
    SetMeasurementRangeInternal(m_config.FixedRange, false);
  }

  return true;
}

void Keithley6482Adapter::Disconnect()
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_connected) {
    return;
  }

  std::cout << "[Keithley6482] Disconnecting: " << m_config.Name << std::endl;

  // Turn off zero check before closing (safety)
  WriteCommandInternal(":SYST:ZCH ON");

  // Close VISA sessions
  if (m_instrument != VI_NULL) {
    viClose(m_instrument);
    m_instrument = VI_NULL;
  }

  if (m_defaultRM != VI_NULL) {
    viClose(m_defaultRM);
    m_defaultRM = VI_NULL;
  }

  m_connected = false;
  std::cout << "[Keithley6482] Disconnected: " << m_config.Name << std::endl;
}

std::string Keithley6482Adapter::GetDeviceID() const
{
  return m_deviceID;
}

// ========== Output Control ==========

bool Keithley6482Adapter::SetOutputOn()
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_connected) {
    SetLastError("Device not connected");
    return false;
  }

  std::cout << "[Keithley6482] Turning output ON" << std::endl;

  if (!WriteCommandInternal(":OUTP ON")) {
    std::cerr << "[Keithley6482] Failed to turn output ON" << std::endl;
    return false;
  }

  std::cout << "[Keithley6482] Output ON successful" << std::endl;
  return true;
}

bool Keithley6482Adapter::SetOutputOff()
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_connected) {
    SetLastError("Device not connected");
    return false;
  }

  std::cout << "[Keithley6482] Turning output OFF" << std::endl;

  if (!WriteCommandInternal(":OUTP OFF")) {
    std::cerr << "[Keithley6482] Failed to turn output OFF" << std::endl;
    return false;
  }

  std::cout << "[Keithley6482] Output OFF successful" << std::endl;
  return true;
}

bool Keithley6482Adapter::IsOutputOn()
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_connected) {
    return false;
  }

  std::string response;
  if (!QueryCommandInternal(":OUTP?", response)) {
    return false;
  }

  // Response is "1" for ON, "0" for OFF
  return (response == "1" || response == "ON");
}

// ========== Source Control ==========

bool Keithley6482Adapter::SetVoltage(double voltage)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_connected) {
    SetLastError("Device not connected");
    return false;
  }

  std::cout << "[Keithley6482] Setting voltage to " << voltage << " V" << std::endl;

  std::ostringstream cmd;
  cmd << ":SOUR:VOLT " << voltage;

  if (!WriteCommandInternal(cmd.str())) {
    std::cerr << "[Keithley6482] Failed to set voltage" << std::endl;
    return false;
  }

  std::cout << "[Keithley6482] Voltage set successfully" << std::endl;
  return true;
}

bool Keithley6482Adapter::SetCurrent(double /*current*/)
{
  SetLastError("SetCurrent: Not applicable for this device (voltage source only)");
  return false;
}

bool Keithley6482Adapter::SetCompliance(double limit)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_connected) {
    SetLastError("Device not connected");
    return false;
  }

  // NOTE: Keithley 6482 is primarily a picoammeter with voltage source capability
  // for biasing. Unlike true SMU devices (e.g., Keithley 2400), the 6482 does NOT
  // support user-configurable current compliance limits via SCPI commands.
  //
  // The device has built-in hardware current limits for the voltage source, but
  // these are not user-adjustable. The current measurement range itself provides
  // the protection mechanism.
  //
  // Commands like :SOUR:VOLT:ILIM are NOT supported and will generate SCPI error -113
  // (Undefined header).
  //
  // For compatibility with the ISMUDevice interface, we accept the compliance
  // value but do not send any SCPI commands to the device.

  std::cout << "[Keithley6482] Compliance setting requested (" << limit
            << " A) - NOTE: This device does not support user-configurable compliance. "
            << "Built-in hardware limits apply." << std::endl;

  return true;  // Return success since this is a no-op for this device
}

// ========== Measurement ==========

double Keithley6482Adapter::ReadVoltage()
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_connected) {
    SetLastError("Device not connected");
    return -999.0;
  }

  std::string response;
  if (!QueryCommandInternal(":SOUR:VOLT?", response)) {
    std::cerr << "[Keithley6482] Failed to read voltage" << std::endl;
    return -999.0;
  }

  try {
    double voltage = std::stod(response);
    return voltage;
  } catch (const std::exception& /*e*/) {
    SetLastError("Failed to parse voltage response: " + response);
    std::cerr << "[Keithley6482] " << m_lastError << std::endl;
    return -999.0;
  }
}

double Keithley6482Adapter::ReadCurrent()
{
  // Backward compatible: reads from current selected channel
  return ReadChannel(m_currentChannel);
}

// ========== Multi-Channel Measurement ==========

int Keithley6482Adapter::GetChannelCount() const
{
  return m_config.ChannelCount;
}

std::vector<int> Keithley6482Adapter::GetEnabledChannels() const
{
  return m_config.EnabledChannels;
}

double Keithley6482Adapter::ReadChannel(int channel)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_connected) {
    SetLastError("Device not connected");
    return -999.0;
  }

  // Validate channel number
  if (channel < 1 || channel > m_config.ChannelCount) {
    SetLastError("Invalid channel number: " + std::to_string(channel) +
                 " (valid: 1-" + std::to_string(m_config.ChannelCount) + ")");
    return -999.0;
  }

  // Check if channel is enabled
  auto& enabled = m_config.EnabledChannels;
  if (std::find(enabled.begin(), enabled.end(), channel) == enabled.end()) {
    SetLastError("Channel " + std::to_string(channel) + " is not enabled");
    return -999.0;
  }

  // For 6482, use :READ? which returns comma-separated values for all channels
  // Then extract the requested channel from the response
  std::string response;
  if (!QueryCommandInternal(":READ?", response)) {
    SetLastError("Failed to read current from channel " + std::to_string(channel));
    return -999.0;
  }

  m_currentChannel = channel;

  try {
    // Parse comma-separated response: "1.234e-6,5.678e-7" (ch1,ch2)
    std::vector<double> values;
    std::stringstream ss(response);
    std::string value;
    while (std::getline(ss, value, ',')) {
      values.push_back(std::stod(value));
    }

    // Get the requested channel (1-indexed, array is 0-indexed)
    int idx = channel - 1;
    if (idx >= 0 && idx < static_cast<int>(values.size())) {
      return values[idx];
    } else if (!values.empty()) {
      // If channel index out of range, return first value
      return values[0];
    } else {
      SetLastError("No values in response");
      return -999.0;
    }
  }
  catch (const std::exception& e) {
    SetLastError(std::string("Failed to parse current value: ") + e.what());
    return -999.0;
  }
}

std::vector<double> Keithley6482Adapter::ReadAllChannels()
{
  std::lock_guard<std::mutex> lock(m_mutex);

  std::vector<double> results;

  if (!m_connected) {
    SetLastError("Device not connected");
    // Return -999.0 for all enabled channels
    for (size_t i = 0; i < m_config.EnabledChannels.size(); ++i) {
      results.push_back(-999.0);
    }
    return results;
  }

  // Periodic VISA buffer clear for stability (every 500 reads, like Python implementation)
  m_readCount++;
  if (m_readCount % 500 == 0) {
    ClearVISABuffersInternal();
  }

  // Read all enabled channels
  if (m_config.EnabledChannels.size() > 1) {
    // Read multiple channels simultaneously
    // NOTE: FORM:ELEM is set once during ConfigureMeasurementMode(), not per-read
    // This reduces SCPI overhead and improves polling performance

    std::string response;
    if (!QueryCommandInternal(":READ?", response)) {
      SetLastError("Failed to read all channels");
      for (size_t i = 0; i < m_config.EnabledChannels.size(); ++i) {
        results.push_back(-999.0);
      }
      return results;
    }

    // Parse comma-separated response: "1.234e-6,5.678e-7"
    std::stringstream ss(response);
    std::string value;
    while (std::getline(ss, value, ',')) {
      try {
        results.push_back(std::stod(value));
      }
      catch (const std::exception&) {
        results.push_back(-999.0);
      }
    }
  }
  else if (m_config.EnabledChannels.size() == 1) {
    // Single channel - still use :READ? and parse first value
    int channel = m_config.EnabledChannels[0];
    m_currentChannel = channel;

    std::string response;
    if (!QueryCommandInternal(":READ?", response)) {
      results.push_back(-999.0);
      return results;
    }

    // Parse comma-separated response and get the requested channel
    std::stringstream ss(response);
    std::string value;
    int idx = 0;
    int targetIdx = channel - 1;
    while (std::getline(ss, value, ',')) {
      if (idx == targetIdx) {
        try {
          results.push_back(std::stod(value));
        }
        catch (const std::exception&) {
          results.push_back(-999.0);
        }
        break;
      }
      idx++;
    }
    // If we didn't find the channel, try first value
    if (results.empty()) {
      try {
        results.push_back(std::stod(response));
      }
      catch (const std::exception&) {
        results.push_back(-999.0);
      }
    }
  }

  return results;
}

SMUMeasurement Keithley6482Adapter::MeasureCurrent()
{
  SMUMeasurement result;

  double current = ReadCurrent();

  if (current != -999.0) {
    result.value = current;
    result.success = true;
    result.errorMessage = "";
  }
  else {
    result.value = 0.0;
    result.success = false;
    result.errorMessage = GetLastError();
  }

  // Add timestamp
  std::time_t now = std::time(nullptr);
  std::tm tm_buf{};
  localtime_s(&tm_buf, &now);
  std::stringstream ss;
  ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
  result.timestamp = ss.str();

  return result;
}

SMUMeasurement Keithley6482Adapter::MeasureVoltage()
{
  SMUMeasurement result;
  result.value = 0.0;
  result.success = false;
  result.errorMessage = "MeasureVoltage: Not applicable for picoammeter (current measurement only)";

  return result;
}

// ========== Configuration ==========

bool Keithley6482Adapter::SetMeasurementRange(double range, bool autoRange)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return SetMeasurementRangeInternal(range, autoRange);
}

bool Keithley6482Adapter::SetChannelMeasurementRange(int channel, double range, bool autoRange)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_connected) {
    SetLastError("Device not connected");
    return false;
  }

  if (channel < 1 || channel > 2) {
    SetLastError("Invalid channel number (must be 1 or 2)");
    return false;
  }

  std::ostringstream cmd;
  if (autoRange) {
    cmd << ":SENS" << channel << ":CURR:RANG:AUTO ON";
    std::cout << "[Keithley6482] Setting channel " << channel << " to auto-range" << std::endl;
  }
  else {
    cmd << ":SENS" << channel << ":CURR:RANG " << std::scientific << range;
    std::cout << "[Keithley6482] Setting channel " << channel << " range to " << range << " A" << std::endl;
  }

  if (!WriteCommandInternal(cmd.str())) {
    SetLastError("Failed to set measurement range for channel " + std::to_string(channel));
    return false;
  }

  std::cout << "[Keithley6482] Channel " << channel << " range set successfully" << std::endl;
  return true;
}

bool Keithley6482Adapter::GetChannelMeasurementRange(int channel, double& range)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_connected) {
    SetLastError("Device not connected");
    return false;
  }

  if (channel < 1 || channel > 2) {
    SetLastError("Invalid channel number (must be 1 or 2)");
    return false;
  }

  std::ostringstream cmd;
  cmd << ":SENS" << channel << ":CURR:RANG?";

  std::string response;
  if (!QueryCommandInternal(cmd.str(), response)) {
    SetLastError("Failed to query measurement range for channel " + std::to_string(channel));
    return false;
  }

  try {
    range = std::stod(response);
    return true;
  }
  catch (const std::exception& e) {
    SetLastError("Failed to parse range response: " + std::string(e.what()));
    return false;
  }
}

bool Keithley6482Adapter::SetIntegrationTime(double nplc)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return SetIntegrationTimeInternal(nplc);
}

bool Keithley6482Adapter::SetZeroCheck(bool enabled)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_connected) {
    SetLastError("Device not connected");
    return false;
  }

  std::string cmd = enabled ? ":SYST:ZCH ON" : ":SYST:ZCH OFF";
  return WriteCommandInternal(cmd);
}

bool Keithley6482Adapter::PerformZeroCorrection()
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_connected) {
    SetLastError("Device not connected");
    return false;
  }

  // Enable zero check first
  if (!WriteCommandInternal(":SYST:ZCH ON")) {
    return false;
  }

  // Perform zero correction
  if (!WriteCommandInternal(":SYST:ZCOR:ACQ")) {
    return false;
  }

  // Disable zero check
  if (!WriteCommandInternal(":SYST:ZCH OFF")) {
    return false;
  }

  std::cout << "[Keithley6482] Zero correction completed" << std::endl;
  return true;
}

bool Keithley6482Adapter::ConfigureMeasurementMode(MeasurementMode mode)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return ConfigureMeasurementModeInternal(mode);
}

// ========== Fast Mode Configuration ==========

bool Keithley6482Adapter::ConfigureMeasurementModeInternal(MeasurementMode mode)
{
  // ASSUME MUTEX IS ALREADY LOCKED BY CALLER

  if (!m_connected) {
    SetLastError("Device not connected");
    return false;
  }

  std::string modeName;

  switch (mode) {
    case MeasurementMode::ACCURATE:
      modeName = "ACCURATE";
      std::cout << "[Keithley6482] Configuring ACCURATE mode (NPLC=1.0, ~10 Hz)..." << std::endl;

      // ===== Channel 1 configuration =====
      // NPLC = 1.0 for high accuracy
      WriteCommandInternal(":SENS1:CURR:NPLC 1.0");
      // Auto-range ON
      WriteCommandInternal(":SENS1:CURR:RANG:AUTO ON");
      // Averaging enabled for accuracy
      WriteCommandInternal(":SENS1:CURR:AVER:STAT ON");
      WriteCommandInternal(":SENS1:CURR:AVER:COUN 5");
      WriteCommandInternal(":SENS1:CURR:AVER:TCON REP");

      // ===== Channel 2 configuration =====
      WriteCommandInternal(":SENS2:CURR:NPLC 1.0");
      WriteCommandInternal(":SENS2:CURR:RANG:AUTO ON");
      WriteCommandInternal(":SENS2:CURR:AVER:STAT ON");
      WriteCommandInternal(":SENS2:CURR:AVER:COUN 5");
      WriteCommandInternal(":SENS2:CURR:AVER:TCON REP");

      // ===== System-wide settings =====
      // Auto-zero ON for accuracy
      WriteCommandInternal(":SYST:AZER ON");

      // Display ON
      WriteCommandInternal(":DISP:ENAB ON");

      // Single channel reading (current channel)
      WriteCommandInternal(":FORM:ELEM READ");

      break;

    case MeasurementMode::FAST:
      modeName = "FAST (COARSE)";
      std::cout << "[Keithley6482] Configuring FAST mode (NPLC=0.1, ~80 Hz)..." << std::endl;

      // ===== Channel 1 configuration =====
      // NPLC = 0.1 (10x faster than ACCURATE)
      WriteCommandInternal(":SENS1:CURR:NPLC 0.1");
      // Auto-range ON (for wide dynamic range during alignment)
      WriteCommandInternal(":SENS1:CURR:RANG:AUTO ON");
      // Averaging: 3 count, repeating filter (noise reduction)
      WriteCommandInternal(":SENS1:CURR:AVER:STAT ON");
      WriteCommandInternal(":SENS1:CURR:AVER:COUN 3");
      WriteCommandInternal(":SENS1:CURR:AVER:TCON REP");

      // ===== Channel 2 configuration =====
      WriteCommandInternal(":SENS2:CURR:NPLC 0.1");
      WriteCommandInternal(":SENS2:CURR:RANG:AUTO ON");
      WriteCommandInternal(":SENS2:CURR:AVER:STAT ON");
      WriteCommandInternal(":SENS2:CURR:AVER:COUN 3");
      WriteCommandInternal(":SENS2:CURR:AVER:TCON REP");

      // ===== System-wide settings =====
      // Auto-zero ON (maintain accuracy)
      WriteCommandInternal(":SYST:AZER ON");

      // Display ON
      WriteCommandInternal(":DISP:ENAB ON");

      // Read both channels simultaneously for faster dual-channel operation
      WriteCommandInternal(":FORM:ELEM CURR1,CURR2");

      break;

    case MeasurementMode::ULTRA_FAST:
      modeName = "ULTRA-FAST (MONITOR)";
      std::cout << "[Keithley6482] Configuring ULTRA-FAST mode (NPLC=0.01, ~150 Hz)..." << std::endl;

      // ===== Channel 1 configuration =====
      // Minimum NPLC (0.01 is minimum for Keithley 6482)
      WriteCommandInternal(":SENS1:CURR:NPLC 0.01");
      // Fixed range (20mA) - no auto-ranging overhead
      WriteCommandInternal(":SENS1:CURR:RANG:AUTO OFF");
      WriteCommandInternal(":SENS1:CURR:RANG 0.02");  // 20mA
      // Disable averaging for speed
      WriteCommandInternal(":SENS1:CURR:AVER:STAT OFF");
      // Disable median filter for speed
      WriteCommandInternal(":SENS1:CURR:MED:STAT OFF");

      // ===== Channel 2 configuration =====
      WriteCommandInternal(":SENS2:CURR:NPLC 0.01");
      WriteCommandInternal(":SENS2:CURR:RANG:AUTO OFF");
      WriteCommandInternal(":SENS2:CURR:RANG 0.02");  // 20mA
      WriteCommandInternal(":SENS2:CURR:AVER:STAT OFF");
      WriteCommandInternal(":SENS2:CURR:MED:STAT OFF");

      // ===== System-wide settings =====
      // Disable auto-zero for speed
      WriteCommandInternal(":SYST:AZER OFF");

      // Display OFF for speed (if supported)
      try {
        WriteCommandInternal(":DISP:ENAB OFF");
      }
      catch (...) {
        // Some units don't support this
      }

      // Continuous measurement mode
      WriteCommandInternal(":INIT:CONT ON");

      // Read both channels simultaneously (set once here, not per-read)
      WriteCommandInternal(":FORM:ELEM CURR1,CURR2");
      WriteCommandInternal(":FORM:DATA ASC");

      break;

    default:
      SetLastError("Invalid measurement mode");
      return false;
  }

  // Clear errors
  WriteCommandInternal("*CLS");

  m_currentMode = mode;
  std::cout << "[Keithley6482] " << modeName << " mode configured successfully" << std::endl;

  return true;
}

std::pair<double, double> Keithley6482Adapter::ReadBothChannels()
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_connected) {
    SetLastError("Device not connected");
    return { -999.0, -999.0 };
  }

  // Ensure format is set to read both channels
  WriteCommandInternal(":FORM:ELEM CURR1,CURR2");

  std::string response;
  if (!QueryCommandInternal(":READ?", response)) {
    SetLastError("Failed to read both channels");
    return { -999.0, -999.0 };
  }

  // Parse response: "1.234e-6,5.678e-7"
  size_t comma = response.find(',');
  if (comma != std::string::npos) {
    try {
      double ch1 = std::stod(response.substr(0, comma));
      double ch2 = std::stod(response.substr(comma + 1));
      return { ch1, ch2 };
    }
    catch (const std::exception& e) {
      SetLastError(std::string("Failed to parse dual-channel response: ") + e.what());
      return { -999.0, -999.0 };
    }
  }
  else {
    SetLastError("Invalid dual-channel response format (missing comma)");
    return { -999.0, -999.0 };
  }
}

// ========== Additional Methods ==========

bool Keithley6482Adapter::SelectChannel(int channel)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_connected) {
    SetLastError("Device not connected");
    return false;
  }

  if (channel < 1 || channel > 2) {
    SetLastError("Invalid channel (must be 1 or 2)");
    return false;
  }

  // For 6482, channel selection is just tracking which channel to read from
  // The actual reading is done via :READ? which returns all channels
  m_currentChannel = channel;
  std::cout << "[Keithley6482] Selected channel " << channel << std::endl;
  return true;
}

bool Keithley6482Adapter::QueryCommand(const std::string& command, std::string& response)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return QueryCommandInternal(command, response);
}

bool Keithley6482Adapter::WriteCommand(const std::string& command)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return WriteCommandInternal(command);
}

std::vector<std::string> Keithley6482Adapter::ReadErrorQueue()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<std::string> errors;

  if (!m_connected) {
    return errors;
  }

  // Read up to 10 errors from the queue
  for (int i = 0; i < 10; ++i) {
    std::string response;
    if (!QueryCommandInternal(":SYST:ERR?", response)) {
      break;
    }

    // Response format: "-113,Undefined header"
    // If no errors, response is "0,No error"
    if (response.find("0,") == 0 || response.find("+0,") == 0) {
      break;  // No more errors
    }

    errors.push_back(response);
    std::cerr << "[Keithley6482] SCPI Error: " << response << std::endl;
  }

  return errors;
}

// ========== Internal Methods (Assume Mutex Already Locked) ==========

bool Keithley6482Adapter::QueryCommandInternal(const std::string& command, std::string& response)
{
  // ASSUME MUTEX IS ALREADY LOCKED BY CALLER

  if (!m_connected || m_instrument == VI_NULL) {
    SetLastError("Device not connected");
    return false;
  }

  // Write command
  ViStatus status = viPrintf(m_instrument, "%s\n", command.c_str());
  if (status < VI_SUCCESS) {
    SetLastError("Write failed: " + GetVISAErrorDescription(status));
    return false;
  }

  // Read response
  char buffer[BUFFER_SIZE];
  status = viScanf(m_instrument, "%t", buffer);
  if (status < VI_SUCCESS) {
    SetLastError("Read failed: " + GetVISAErrorDescription(status));
    return false;
  }

  response = TrimString(std::string(buffer));
  return true;
}

bool Keithley6482Adapter::WriteCommandInternal(const std::string& command)
{
  // ASSUME MUTEX IS ALREADY LOCKED BY CALLER

  if (!m_connected || m_instrument == VI_NULL) {
    SetLastError("Device not connected");
    return false;
  }

  ViStatus status = viPrintf(m_instrument, "%s\n", command.c_str());
  if (status < VI_SUCCESS) {
    SetLastError("Write failed: " + GetVISAErrorDescription(status));
    return false;
  }

  return true;
}

bool Keithley6482Adapter::SetIntegrationTimeInternal(double nplc)
{
  // ASSUME MUTEX IS ALREADY LOCKED BY CALLER

  if (!m_connected) {
    SetLastError("Device not connected");
    return false;
  }

  if (nplc < 0.01 || nplc > 10.0) {
    SetLastError("NPLC out of range (must be 0.01 to 10.0)");
    return false;
  }

  std::ostringstream cmd;
  cmd << ":SENS:CURR:NPLC " << nplc;
  return WriteCommandInternal(cmd.str());
}

bool Keithley6482Adapter::SetMeasurementRangeInternal(double range, bool autoRange)
{
  // ASSUME MUTEX IS ALREADY LOCKED BY CALLER

  if (!m_connected) {
    SetLastError("Device not connected");
    return false;
  }

  if (autoRange) {
    return WriteCommandInternal(":SENS:CURR:RANG:AUTO ON");
  }
  else {
    std::ostringstream cmd;
    cmd << ":SENS:CURR:RANG " << std::scientific << range;
    return WriteCommandInternal(cmd.str());
  }
}

// ========== Helper Methods ==========

std::string Keithley6482Adapter::GetVISAErrorDescription(ViStatus status) const
{
  char desc[256];
  if (m_defaultRM != VI_NULL) {
    viStatusDesc(m_defaultRM, status, desc);
    return std::string(desc);
  }

  std::ostringstream oss;
  oss << "VISA Error 0x" << std::hex << status;
  return oss.str();
}

void Keithley6482Adapter::SetLastError(const std::string& error)
{
  m_lastError = error;
}

std::string Keithley6482Adapter::TrimString(const std::string& str) const
{
  size_t first = str.find_first_not_of(" \t\n\r");
  if (first == std::string::npos) return "";

  size_t last = str.find_last_not_of(" \t\n\r");
  return str.substr(first, (last - first + 1));
}

void Keithley6482Adapter::ClearVISABuffersInternal()
{
  // ASSUME MUTEX IS ALREADY LOCKED BY CALLER
  // Clear VISA buffers periodically to prevent overflow (like Python implementation)
  // This improves stability during high-speed polling

  if (m_instrument != VI_NULL) {
    // Clear the instrument's input/output buffers
    viClear(m_instrument);
    // Clear status registers and error queue
    WriteCommandInternal("*CLS");
  }
}

// ========== Polling Methods ==========

bool Keithley6482Adapter::StartPolling(int intervalMs)
{
  if (m_isPolling) {
    std::cout << "[Keithley6482] Polling already active: " << m_config.Name << std::endl;
    return false;
  }

  if (!m_connected) {
    SetLastError("Cannot start polling: device not connected");
    std::cerr << "[Keithley6482] " << m_lastError << std::endl;
    return false;
  }

  if (intervalMs < 1) {
    SetLastError("Invalid polling interval (must be >= 1ms)");
    std::cerr << "[Keithley6482] " << m_lastError << std::endl;
    return false;
  }

  std::cout << "[Keithley6482] Starting polling: " << m_config.Name
    << " (Interval: " << intervalMs << "ms, Channel: " << m_currentChannel << ")" << std::endl;

  m_sampleCount = 0;
  m_readCount = 0;  // Reset read counter for periodic VISA buffer clear
  m_isPolling = true;
  m_pollTimer->start(intervalMs);

  return true;
}

void Keithley6482Adapter::StopPolling()
{
  if (!m_isPolling) {
    return;
  }

  std::cout << "[Keithley6482] Stopping polling: " << m_config.Name
    << " (Total samples: " << m_sampleCount.load() << ")" << std::endl;

  if (m_pollTimer->isActive()) {
    m_pollTimer->stop();
  }

  m_isPolling = false;
}

void Keithley6482Adapter::onPollTimeout()
{
  // This runs in Qt event loop thread - safe for UI updates via signals
  if (!m_isPolling || !m_connected) {
    return;
  }

  // Read current measurement
  double current = ReadCurrent();

  if (current != -999.0) {
    // Increment sample count
    m_sampleCount++;

    // Create timestamp
    std::time_t now = std::time(nullptr);
    std::tm tm_buf{};
    localtime_s(&tm_buf, &now);
    std::stringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");

    // Emit signal with measurement data
    emit measurementReady(
      QString::fromStdString(m_config.Name),
      m_currentChannel,
      current,
      QString::fromStdString(ss.str())
    );
  }
  else {
    // Emit error signal
    emit pollingError(
      QString::fromStdString(m_config.Name),
      QString::fromStdString(GetLastError())
    );
  }
}
