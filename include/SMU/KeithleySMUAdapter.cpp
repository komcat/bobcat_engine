// KeithleySMUAdapter.cpp
#include "KeithleySMUAdapter.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>
#include <cctype>

KeithleySMUAdapter::KeithleySMUAdapter(const std::string& visaResource, const SMUDeviceConfig& config)
  : QObject(nullptr)
  , m_defaultRM(VI_NULL)
  , m_instrument(VI_NULL)
  , m_config(config)
  , m_visaResource(visaResource)
  , m_modelFamily(ModelFamily::UNKNOWN)
  , m_channelCount(1)
  , m_connected(false)
  , m_outputOn(false)
  , m_sourceMode(SourceMode::VOLTAGE)
  , m_currentChannel(1)
{
  std::cout << "[KeithleySMUAdapter] Created for " << m_config.Name
            << " at " << visaResource << std::endl;
}

KeithleySMUAdapter::~KeithleySMUAdapter()
{
  Disconnect();
}

bool KeithleySMUAdapter::Connect()
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_connected) {
    std::cout << "[KeithleySMUAdapter] Already connected" << std::endl;
    return true;
  }

  std::cout << "[KeithleySMUAdapter] Opening VISA resource manager..." << std::endl;

  // Open VISA resource manager
  ViStatus status = viOpenDefaultRM(&m_defaultRM);
  if (status < VI_SUCCESS) {
    SetLastError("Failed to open VISA resource manager: " + GetVISAErrorDescription(status));
    std::cerr << "[KeithleySMUAdapter] " << m_lastError << std::endl;
    return false;
  }

  std::cout << "[KeithleySMUAdapter] Connecting to: " << m_visaResource << std::endl;

  // Open instrument connection with timeout (5 seconds)
  constexpr unsigned long CONNECTION_TIMEOUT_MS = 5000;
  status = viOpen(m_defaultRM, m_visaResource.c_str(), VI_NULL, CONNECTION_TIMEOUT_MS, &m_instrument);
  if (status < VI_SUCCESS) {
    SetLastError("Failed to open device (timeout=" + std::to_string(CONNECTION_TIMEOUT_MS) + "ms): " + GetVISAErrorDescription(status));
    std::cerr << "[KeithleySMUAdapter] " << m_lastError << std::endl;
    viClose(m_defaultRM);
    m_defaultRM = VI_NULL;
    return false;
  }

  std::cout << "[KeithleySMUAdapter] VISA connection opened successfully" << std::endl;

  // Set timeout
  viSetAttribute(m_instrument, VI_ATTR_TMO_VALUE, TIMEOUT_MS);
  std::cout << "[KeithleySMUAdapter] Timeout set to " << TIMEOUT_MS << "ms" << std::endl;

  // Configure termination character for SOCKET connections
  // Keithley instruments use newline (\n = 0x0A) as terminator
  viSetAttribute(m_instrument, VI_ATTR_TERMCHAR, '\n');
  viSetAttribute(m_instrument, VI_ATTR_TERMCHAR_EN, VI_TRUE);
  std::cout << "[KeithleySMUAdapter] Termination character set to LF (0x0A)" << std::endl;

  // Query device ID
  std::string idn;
  if (!QueryCommandInternal("*IDN?", idn)) {
    SetLastError("Failed to query device ID");
    std::cerr << "[KeithleySMUAdapter] " << m_lastError << std::endl;

    // Clean up without recursive lock (don't call Disconnect())
    if (m_instrument != VI_NULL) {
      viClose(m_instrument);
      m_instrument = VI_NULL;
    }
    if (m_defaultRM != VI_NULL) {
      viClose(m_defaultRM);
      m_defaultRM = VI_NULL;
    }
    return false;
  }

  m_deviceID = idn;
  std::cout << "[KeithleySMUAdapter] Connected: " << idn << std::endl;

  // Detect model and configure
  if (!DetectModelFromIDN(idn)) {
    SetLastError("Failed to detect Keithley model from IDN: " + idn);
    std::cerr << "[KeithleySMUAdapter] " << m_lastError << std::endl;

    // Clean up without recursive lock (don't call Disconnect())
    if (m_instrument != VI_NULL) {
      viClose(m_instrument);
      m_instrument = VI_NULL;
    }
    if (m_defaultRM != VI_NULL) {
      viClose(m_defaultRM);
      m_defaultRM = VI_NULL;
    }
    return false;
  }

  std::cout << "[KeithleySMUAdapter] Detected model: " << m_modelString
            << " (Family: " << (m_modelFamily == ModelFamily::SERIES_2400 ? "2400" : "2600")
            << ", Channels: " << m_channelCount << ")" << std::endl;

  // Clear error queue
  WriteCommandInternal("*CLS");

  // Reset to known state
  WriteCommandInternal("*RST");

  // Turn output off by default
  if (m_modelFamily == ModelFamily::SERIES_2600) {
    // TSP commands for 2600 series - turn off both channels
    WriteCommandInternal("smua.source.output = smua.OUTPUT_OFF");
    if (m_channelCount > 1) {
      WriteCommandInternal("smub.source.output = smub.OUTPUT_OFF");
    }
  } else {
    // SCPI command for 2400 series
    WriteCommandInternal(":OUTP OFF");
  }
  m_outputOn = false;

  m_connected = true;
  ClearError();

  return true;
}

void KeithleySMUAdapter::Disconnect()
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_connected) {
    // Turn output off before disconnecting
    if (m_modelFamily == ModelFamily::SERIES_2600) {
      // TSP commands for 2600 series - turn off both channels
      WriteCommandInternal("smua.source.output = smua.OUTPUT_OFF");
      if (m_channelCount > 1) {
        WriteCommandInternal("smub.source.output = smub.OUTPUT_OFF");
      }
    } else {
      // SCPI command for 2400 series
      WriteCommandInternal(":OUTP OFF");
    }
  }

  if (m_instrument != VI_NULL) {
    viClose(m_instrument);
    m_instrument = VI_NULL;
  }

  if (m_defaultRM != VI_NULL) {
    viClose(m_defaultRM);
    m_defaultRM = VI_NULL;
  }

  m_connected = false;
  std::cout << "[KeithleySMUAdapter] Disconnected from " << m_config.Name << std::endl;
}

std::string KeithleySMUAdapter::GetDeviceID() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_deviceID;
}

SMUMode KeithleySMUAdapter::GetSupportedMode() const
{
  // Keithley SMUs support both sourcing and measuring
  if (m_sourceMode == SourceMode::VOLTAGE) {
    return SMUMode::VOLTAGE_SOURCE;
  } else {
    return SMUMode::CURRENT_SOURCE;
  }
}

bool KeithleySMUAdapter::SetOutputOn()
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_connected) {
    SetLastError("Device not connected");
    return false;
  }

  std::string cmd;
  if (m_modelFamily == ModelFamily::SERIES_2600) {
    // TSP command for 2600 series
    cmd = GetTSPChannelName() + ".source.output = " + GetTSPChannelName() + ".OUTPUT_ON";
  } else {
    // SCPI command for 2400 series
    cmd = ":OUTP ON";
  }

  if (!WriteCommandInternal(cmd)) {
    SetLastError("Failed to turn output on");
    return false;
  }

  m_outputOn = true;
  return true;
}

bool KeithleySMUAdapter::SetOutputOff()
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_connected) {
    SetLastError("Device not connected");
    return false;
  }

  std::string cmd;
  if (m_modelFamily == ModelFamily::SERIES_2600) {
    // TSP command for 2600 series
    cmd = GetTSPChannelName() + ".source.output = " + GetTSPChannelName() + ".OUTPUT_OFF";
  } else {
    // SCPI command for 2400 series
    cmd = ":OUTP OFF";
  }

  if (!WriteCommandInternal(cmd)) {
    SetLastError("Failed to turn output off");
    return false;
  }

  m_outputOn = false;
  return true;
}

bool KeithleySMUAdapter::IsOutputOn()
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_connected) {
    return false;
  }

  std::string response;
  std::string cmd;

  if (m_modelFamily == ModelFamily::SERIES_2600) {
    // TSP command for 2600 series - use print() to get response
    cmd = "print(" + GetTSPChannelName() + ".source.output)";
  } else {
    // SCPI command for 2400 series
    cmd = ":OUTP?";
  }

  if (!QueryCommandInternal(cmd, response)) {
    return false;
  }

  // 2600 series returns "1.00000e+00" for ON, "0.00000e+00" for OFF
  // 2400 series returns "1" or "ON" for ON, "0" or "OFF" for OFF
  if (m_modelFamily == ModelFamily::SERIES_2600) {
    try {
      double val = std::stod(response);
      m_outputOn = (val > 0.5);
    } catch (...) {
      m_outputOn = false;
    }
  } else {
    m_outputOn = (response == "1" || response == "ON");
  }

  return m_outputOn;
}

bool KeithleySMUAdapter::SetVoltage(double voltage)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_connected) {
    SetLastError("Device not connected");
    return false;
  }

  if (m_modelFamily == ModelFamily::SERIES_2600) {
    // TSP commands for 2600 series
    std::string ch = GetTSPChannelName();

    // Set source function to voltage
    std::string funcCmd = ch + ".source.func = " + ch + ".OUTPUT_DCVOLTS";
    if (!WriteCommandInternal(funcCmd)) {
      SetLastError("Failed to set source function to voltage");
      return false;
    }

    // Set voltage level
    std::ostringstream oss;
    oss << ch << ".source.levelv = " << std::scientific << std::setprecision(6) << voltage;
    if (!WriteCommandInternal(oss.str())) {
      SetLastError("Failed to set voltage");
      return false;
    }
  } else {
    // SCPI commands for 2400 series
    if (!WriteCommandInternal(":SOUR:FUNC VOLT")) {
      SetLastError("Failed to set source function to voltage");
      return false;
    }

    std::ostringstream oss;
    oss << ":SOUR:VOLT " << std::scientific << std::setprecision(6) << voltage;
    if (!WriteCommandInternal(oss.str())) {
      SetLastError("Failed to set voltage");
      return false;
    }
  }

  m_sourceMode = SourceMode::VOLTAGE;
  return true;
}

bool KeithleySMUAdapter::SetCurrent(double current)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_connected) {
    SetLastError("Device not connected");
    return false;
  }

  if (m_modelFamily == ModelFamily::SERIES_2600) {
    // TSP commands for 2600 series
    std::string ch = GetTSPChannelName();

    // Set source function to current
    std::string funcCmd = ch + ".source.func = " + ch + ".OUTPUT_DCAMPS";
    if (!WriteCommandInternal(funcCmd)) {
      SetLastError("Failed to set source function to current");
      return false;
    }

    // Set current level
    std::ostringstream oss;
    oss << ch << ".source.leveli = " << std::scientific << std::setprecision(6) << current;
    if (!WriteCommandInternal(oss.str())) {
      SetLastError("Failed to set current");
      return false;
    }
  } else {
    // SCPI commands for 2400 series
    if (!WriteCommandInternal(":SOUR:FUNC CURR")) {
      SetLastError("Failed to set source function to current");
      return false;
    }

    std::ostringstream oss;
    oss << ":SOUR:CURR " << std::scientific << std::setprecision(6) << current;
    if (!WriteCommandInternal(oss.str())) {
      SetLastError("Failed to set current");
      return false;
    }
  }

  m_sourceMode = SourceMode::CURRENT;
  return true;
}

bool KeithleySMUAdapter::SetCompliance(double limit)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_connected) {
    SetLastError("Device not connected");
    return false;
  }

  std::ostringstream oss;

  if (m_modelFamily == ModelFamily::SERIES_2600) {
    // TSP commands for 2600 series
    std::string ch = GetTSPChannelName();

    // Set compliance based on source mode
    if (m_sourceMode == SourceMode::VOLTAGE) {
      // Sourcing voltage -> set current limit
      oss << ch << ".source.limiti = " << std::scientific << std::setprecision(6) << limit;
    } else {
      // Sourcing current -> set voltage limit
      oss << ch << ".source.limitv = " << std::scientific << std::setprecision(6) << limit;
    }
  } else {
    // SCPI commands for 2400 series
    if (m_sourceMode == SourceMode::VOLTAGE) {
      // Sourcing voltage -> set current compliance
      oss << ":SENS:CURR:PROT " << std::scientific << std::setprecision(6) << limit;
    } else {
      // Sourcing current -> set voltage compliance
      oss << ":SENS:VOLT:PROT " << std::scientific << std::setprecision(6) << limit;
    }
  }

  if (!WriteCommandInternal(oss.str())) {
    SetLastError("Failed to set compliance");
    return false;
  }

  return true;
}

double KeithleySMUAdapter::ReadVoltage()
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_connected) {
    SetLastError("Device not connected");
    return -999.0;
  }

  std::string response;
  std::string cmd;

  if (m_modelFamily == ModelFamily::SERIES_2600) {
    // TSP command for 2600 series
    cmd = "print(" + GetTSPChannelName() + ".measure.v())";
  } else {
    // SCPI command for 2400 series
    cmd = ":MEAS:VOLT?";
  }

  if (!QueryCommandInternal(cmd, response)) {
    SetLastError("Failed to read voltage measurement");
    return -999.0;
  }

  // Parse response
  try {
    double voltage = std::stod(response);

    // Check for overflow (Keithley returns 9.91E+37 for overflow)
    if (voltage > 1e30 || voltage < -1e30) {
      SetLastError("Voltage measurement overflow (out of range)");
      return -999.0;
    }

    return voltage;
  } catch (...) {
    SetLastError("Failed to parse voltage reading: " + response);
    return -999.0;
  }
}

double KeithleySMUAdapter::ReadCurrent()
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_connected) {
    SetLastError("Device not connected");
    return -999.0;
  }

  std::string response;
  std::string cmd;

  if (m_modelFamily == ModelFamily::SERIES_2600) {
    // TSP command for 2600 series
    cmd = "print(" + GetTSPChannelName() + ".measure.i())";
  } else {
    // SCPI command for 2400 series
    cmd = ":MEAS:CURR?";
  }

  if (!QueryCommandInternal(cmd, response)) {
    SetLastError("Failed to read current measurement");
    return -999.0;
  }

  // Parse response
  try {
    double current = std::stod(response);

    // Check for overflow (Keithley returns 9.91E+37 for overflow)
    if (current > 1e30 || current < -1e30) {
      SetLastError("Current measurement overflow (out of range)");
      return -999.0;
    }

    return current;
  } catch (...) {
    SetLastError("Failed to parse current reading: " + response);
    return -999.0;
  }
}

SMUMeasurement KeithleySMUAdapter::MeasureCurrent()
{
  SMUMeasurement result;
  result.value = ReadCurrent();
  result.success = (result.value != -999.0);
  if (!result.success) {
    result.errorMessage = GetLastError();
  }

  // Generate timestamp
  std::time_t now = std::time(nullptr);
  std::stringstream ss;
  ss << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S");
  result.timestamp = ss.str();

  return result;
}

SMUMeasurement KeithleySMUAdapter::MeasureVoltage()
{
  SMUMeasurement result;
  result.value = ReadVoltage();
  result.success = (result.value != -999.0);
  if (!result.success) {
    result.errorMessage = GetLastError();
  }

  // Generate timestamp
  std::time_t now = std::time(nullptr);
  std::stringstream ss;
  ss << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S");
  result.timestamp = ss.str();

  return result;
}

int KeithleySMUAdapter::GetChannelCount() const
{
  return m_channelCount;
}

std::vector<int> KeithleySMUAdapter::GetEnabledChannels() const
{
  std::vector<int> channels;
  for (int i = 1; i <= m_channelCount; ++i) {
    channels.push_back(i);
  }
  return channels;
}

double KeithleySMUAdapter::ReadChannel(int channel)
{
  if (channel < 1 || channel > m_channelCount) {
    SetLastError("Invalid channel: " + std::to_string(channel));
    return -999.0;
  }

  // For dual-channel models, select channel first
  if (m_channelCount > 1 && channel != m_currentChannel) {
    if (!SelectChannel(channel)) {
      return -999.0;
    }
  }

  // Read based on source mode (if sourcing voltage, measure current and vice versa)
  if (m_sourceMode == SourceMode::VOLTAGE) {
    return ReadCurrent();
  } else {
    return ReadVoltage();
  }
}

std::vector<double> KeithleySMUAdapter::ReadAllChannels()
{
  std::vector<double> results;

  for (int ch = 1; ch <= m_channelCount; ++ch) {
    results.push_back(ReadChannel(ch));
  }

  return results;
}

bool KeithleySMUAdapter::SetMeasurementRange(double range, bool autoRange)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_connected) {
    SetLastError("Device not connected");
    return false;
  }

  std::ostringstream oss;

  if (autoRange) {
    // Enable auto-range for current measurement (most common case)
    if (!WriteCommandInternal(":SENS:CURR:RANG:AUTO ON")) {
      SetLastError("Failed to enable auto-range");
      return false;
    }
  } else {
    // Set fixed range
    oss << ":SENS:CURR:RANG " << std::scientific << std::setprecision(6) << range;
    if (!WriteCommandInternal(oss.str())) {
      SetLastError("Failed to set measurement range");
      return false;
    }
  }

  return true;
}

bool KeithleySMUAdapter::SetIntegrationTime(double nplc)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_connected) {
    SetLastError("Device not connected");
    return false;
  }

  std::ostringstream oss;
  oss << ":SENS:CURR:NPLC " << std::fixed << std::setprecision(2) << nplc;

  if (!WriteCommandInternal(oss.str())) {
    SetLastError("Failed to set integration time");
    return false;
  }

  return true;
}

bool KeithleySMUAdapter::SetZeroCheck(bool enabled)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_connected) {
    SetLastError("Device not connected");
    return false;
  }

  std::string cmd = enabled ? ":SYST:ZCH ON" : ":SYST:ZCH OFF";
  if (!WriteCommandInternal(cmd)) {
    SetLastError("Failed to set zero check");
    return false;
  }

  return true;
}

bool KeithleySMUAdapter::PerformZeroCorrection()
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_connected) {
    SetLastError("Device not connected");
    return false;
  }

  if (!WriteCommandInternal(":SYST:ZCOR:ACQ")) {
    SetLastError("Failed to perform zero correction");
    return false;
  }

  return true;
}

bool KeithleySMUAdapter::SetSourceMode(SourceMode mode)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_connected) {
    SetLastError("Device not connected");
    return false;
  }

  std::string cmd;
  if (m_modelFamily == ModelFamily::SERIES_2600) {
    // TSP commands for 2600 series
    std::string ch = GetTSPChannelName();
    if (mode == SourceMode::VOLTAGE) {
      cmd = ch + ".source.func = " + ch + ".OUTPUT_DCVOLTS";
    } else {
      cmd = ch + ".source.func = " + ch + ".OUTPUT_DCAMPS";
    }
  } else {
    // SCPI commands for 2400 series
    if (mode == SourceMode::VOLTAGE) {
      cmd = ":SOUR:FUNC VOLT";
    } else {
      cmd = ":SOUR:FUNC CURR";
    }
  }

  if (!WriteCommandInternal(cmd)) {
    SetLastError("Failed to set source mode");
    return false;
  }

  m_sourceMode = mode;
  return true;
}

bool KeithleySMUAdapter::SelectChannel(int channel)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_connected) {
    SetLastError("Device not connected");
    return false;
  }

  if (channel < 1 || channel > m_channelCount) {
    SetLastError("Invalid channel: " + std::to_string(channel));
    return false;
  }

  // For 2600 series, we just update the internal channel variable
  // The TSP commands use smua/smub based on this selection
  // No command needed - channel is used in GetTSPChannelName()
  m_currentChannel = channel;
  return true;
}

bool KeithleySMUAdapter::Reset()
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_connected) {
    SetLastError("Device not connected");
    return false;
  }

  if (!WriteCommandInternal("*RST")) {
    SetLastError("Failed to reset device");
    return false;
  }

  // Reset state variables
  m_outputOn = false;
  m_sourceMode = SourceMode::VOLTAGE;
  m_currentChannel = 1;

  return true;
}

bool KeithleySMUAdapter::QueryCommand(const std::string& command, std::string& response)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return QueryCommandInternal(command, response);
}

bool KeithleySMUAdapter::WriteCommand(const std::string& command)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return WriteCommandInternal(command);
}

// ========== Private Helper Methods ==========

bool KeithleySMUAdapter::DetectModelFromIDN(const std::string& idn)
{
  // IDN format: "KEITHLEY INSTRUMENTS INC.,MODEL 2400,4112268,C33   Mar 31 2015 09:32:39/A02  /U/K"
  // Extract model number

  if (idn.find("2400") != std::string::npos || idn.find("2401") != std::string::npos ||
      idn.find("2410") != std::string::npos || idn.find("2420") != std::string::npos ||
      idn.find("2425") != std::string::npos || idn.find("2430") != std::string::npos ||
      idn.find("2440") != std::string::npos || idn.find("2450") != std::string::npos) {
    m_modelFamily = ModelFamily::SERIES_2400;
    m_channelCount = 1;

    // Extract exact model
    size_t pos = idn.find("24");
    if (pos != std::string::npos) {
      m_modelString = idn.substr(pos, 4);
    } else {
      m_modelString = "2400";
    }
    return true;
  }
  else if (idn.find("2601") != std::string::npos || idn.find("2602") != std::string::npos ||
           idn.find("2604") != std::string::npos || idn.find("2611") != std::string::npos ||
           idn.find("2612") != std::string::npos || idn.find("2614") != std::string::npos ||
           idn.find("2634") != std::string::npos || idn.find("2635") != std::string::npos ||
           idn.find("2636") != std::string::npos) {
    m_modelFamily = ModelFamily::SERIES_2600;

    // Determine channel count from model
    if (idn.find("2601") != std::string::npos || idn.find("2611") != std::string::npos ||
        idn.find("2635") != std::string::npos) {
      m_channelCount = 1;
    } else {
      m_channelCount = 2;  // 2602, 2604, 2612, 2614, 2634, 2636 are dual-channel
    }

    // Extract exact model
    size_t pos = idn.find("26");
    if (pos != std::string::npos) {
      m_modelString = idn.substr(pos, 5);  // Include 'B' suffix if present
    } else {
      m_modelString = "2600";
    }
    return true;
  }

  // Unknown model
  m_modelFamily = ModelFamily::UNKNOWN;
  m_channelCount = 1;
  m_modelString = "UNKNOWN";
  return false;
}

bool KeithleySMUAdapter::QueryCommandInternal(const std::string& command, std::string& response)
{
  if (m_instrument == VI_NULL) {
    std::cerr << "[KeithleySMUAdapter] QueryCommandInternal: instrument is NULL" << std::endl;
    return false;
  }

  // Write command
  ViStatus status = viPrintf(m_instrument, "%s\n", command.c_str());
  if (status < VI_SUCCESS) {
    SetLastError("Query write failed: " + GetVISAErrorDescription(status));
    std::cerr << "[KeithleySMUAdapter] " << m_lastError << std::endl;
    return false;
  }

  // Read response
  char buffer[BUFFER_SIZE] = {0};
  ViUInt32 retCount = 0;

  status = viScanf(m_instrument, "%t", buffer);
  if (status < VI_SUCCESS) {
    SetLastError("Query read failed: " + GetVISAErrorDescription(status));
    std::cerr << "[KeithleySMUAdapter] " << m_lastError << std::endl;
    return false;
  }

  response = TrimString(std::string(buffer));
  return true;
}

bool KeithleySMUAdapter::WriteCommandInternal(const std::string& command)
{
  if (!m_connected || m_instrument == VI_NULL) {
    return false;
  }

  ViStatus status = viPrintf(m_instrument, "%s\n", command.c_str());
  if (status < VI_SUCCESS) {
    SetLastError("Write failed: " + GetVISAErrorDescription(status));
    return false;
  }

  return true;
}

std::string KeithleySMUAdapter::GetVISAErrorDescription(ViStatus status) const
{
  char errorMsg[256];
  viStatusDesc(m_instrument, status, errorMsg);
  return std::string(errorMsg);
}

void KeithleySMUAdapter::SetLastError(const std::string& error)
{
  m_lastError = error;
}

std::string KeithleySMUAdapter::TrimString(const std::string& str) const
{
  auto start = str.begin();
  while (start != str.end() && std::isspace(*start)) {
    start++;
  }

  auto end = str.end();
  do {
    end--;
  } while (std::distance(start, end) > 0 && std::isspace(*end));

  return std::string(start, end + 1);
}

std::string KeithleySMUAdapter::GetTSPChannelName() const
{
  // For 2600 series: channel 1 = "smua", channel 2 = "smub"
  return (m_currentChannel == 1) ? "smua" : "smub";
}
