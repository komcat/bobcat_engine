// PSUCommandHandler.cpp
#include "PSUCommandHandler.h"
#include "PowerSupplyManager.h"
#include "SequenceContext.h"
#include "LogManager.h"
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>

bool PSUCommandHandler::ExecuteCommand(
  void* manager,
  const std::string& deviceName,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context)
{
  PowerSupplyManager* psuManager = static_cast<PowerSupplyManager*>(manager);

  // Manager-level commands routed through the sentinel "psu" device.
  // Handled here BEFORE the per-device lookup since they don't target a
  // specific PSU.
  if (methodName == "DisableAllOutputs") {
    // Per-device handlers auto-connect lazily on first command; mirror that
    // here, otherwise disableAllOutputs() silently skips any PSU that hasn't
    // been touched yet this session (its isConnected() guard is true).
    int disabled = 0;
    int skipped = 0;
    for (const auto& name : psuManager->getDeviceNames()) {
      auto psu = psuManager->getPowerSupply(name);
      if (!psu) continue;

      if (!psu->isConnected()) {
        if (!psu->connect()) {
          if (m_logger) {
            m_logger->Warning("DisableAllOutputs: could not connect '" + name + "': " + psu->getLastError(),
                              "PSUHandler");
          }
          ++skipped;
          continue;
        }
      }

      int channels = psuManager->getDeviceChannelCount(name);
      for (int ch = 1; ch <= channels; ++ch) {
        psu->setOutputOff(ch);
      }
      ++disabled;
    }

    if (m_logger) {
      m_logger->Info("PSU DisableAllOutputs: disabled outputs on " + std::to_string(disabled) +
                     " PSU(s) (skipped " + std::to_string(skipped) + ")", "PSUHandler");
    }
    return true;
  }

  // Check if device exists
  if (!psuManager->hasDevice(deviceName)) {
    errorMessage = "PSU device '" + deviceName + "' not found";
    return false;
  }

  // Get the power supply device
  auto psu = psuManager->getPowerSupply(deviceName);
  if (!psu) {
    errorMessage = "Failed to get PSU device '" + deviceName + "'";
    return false;
  }

  // Context can be used for capturing voltage, current, power measurements
  // Example: MeasureVoltage, MeasureCurrent methods can capture values
  auto captureAsIt = params.find("captureAs");

  // Parse channel number (defaults to 1 if not specified)
  int channel = ParseChannel(params);

  // ============================================================================
  // AUTO-CONNECT: Check connection status and connect if needed
  // ============================================================================
  if (!psu->isConnected()) {
    if (m_logger) {
      m_logger->Warning("PSU '" + deviceName + "' is not connected. Attempting to connect...", "PSUHandler");
    }

    if (psu->connect()) {
      if (m_logger) {
        m_logger->Info("Successfully connected to PSU '" + deviceName + "'", "PSUHandler");
      }
    } else {
      errorMessage = "Failed to connect to PSU '" + deviceName + "': " + psu->getLastError();
      if (m_logger) {
        m_logger->Error(errorMessage, "PSUHandler");
      }
      return false;
    }
  }

  // ============================================================================
  // SET OUTPUT ON
  // ============================================================================
  if (methodName == "SetOutputOn" || methodName == "OutputOn") {
    if (m_logger) {
      m_logger->Info("Turning PSU output ON for " + deviceName + " (Ch " + std::to_string(channel) + ")", "PSUHandler");
    }

    if (!psu->setOutputOn(channel)) {
      errorMessage = "SetOutputOn failed: " + psu->getLastError();
      return false;
    }

    // Delay to allow PSU output to stabilize
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    return true;
  }

  // ============================================================================
  // SET OUTPUT OFF
  // ============================================================================
  else if (methodName == "SetOutputOff" || methodName == "OutputOff") {
    if (m_logger) {
      m_logger->Info("Turning PSU output OFF for " + deviceName + " (Ch " + std::to_string(channel) + ")", "PSUHandler");
    }

    if (!psu->setOutputOff(channel)) {
      errorMessage = "SetOutputOff failed: " + psu->getLastError();
      return false;
    }

    // Delay to allow PSU output to stabilize
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    return true;
  }

  // ============================================================================
  // SET VOLTAGE (CV Mode)
  // ============================================================================
  else if (methodName == "SetVoltage") {
    double voltage;
    if (!ParseVoltage(params, voltage)) {
      errorMessage = "Missing or invalid parameter: voltage";
      return false;
    }

    if (m_logger) {
      m_logger->Info(
        "Setting " + deviceName + " voltage to " + std::to_string(voltage) + " V (Ch " + std::to_string(channel) + ")",
        "PSUHandler"
      );
    }

    if (!psu->setVoltage(voltage, channel)) {
      errorMessage = "SetVoltage failed: " + psu->getLastError();
      return false;
    }

    // Delay to allow PSU voltage to stabilize
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    return true;
  }

  // ============================================================================
  // SET CURRENT (CC Mode)
  // ============================================================================
  else if (methodName == "SetCurrent") {
    double current;
    if (!ParseCurrent(params, current)) {
      errorMessage = "Missing or invalid parameter: current";
      return false;
    }

    // Convert mA to A for the PSU interface
    double currentInAmps = current / 1000.0;

    if (m_logger) {
      m_logger->Info(
        "Setting " + deviceName + " current to " + std::to_string(current) + " mA (" +
        std::to_string(currentInAmps) + " A) (Ch " + std::to_string(channel) + ")",
        "PSUHandler"
      );
    }

    if (!psu->setCurrent(currentInAmps, channel)) {
      errorMessage = "SetCurrent failed: " + psu->getLastError();
      return false;
    }

    // Delay to allow PSU current to stabilize
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    return true;
  }

  // ============================================================================
  // READ VOLTAGE
  // ============================================================================
  else if (methodName == "ReadVoltage") {
    double voltage = psu->readVoltage(channel);

    // Auto-capture to context if requested
    if (context && captureAsIt != params.end()) {
      context->SetValue(captureAsIt->second, static_cast<float>(voltage), deviceName, methodName);

      if (m_logger) {
        m_logger->Info("Measured " + std::to_string(voltage) + " V, stored as '" +
                      captureAsIt->second + "'", "PSUHandler");
      }
    }

    // Format message
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);
    oss << deviceName << " Voltage = " << voltage << " V";
    std::string message = oss.str();

    if (m_logger) {
      m_logger->Info(message, "PSU");
    }

    return true;
  }

  // ============================================================================
  // READ CURRENT
  // ============================================================================
  else if (methodName == "ReadCurrent") {
    double current = psu->readCurrent(channel);

    // Auto-capture to context if requested (store in Amps)
    if (context && captureAsIt != params.end()) {
      context->SetValue(captureAsIt->second, static_cast<float>(current), deviceName, methodName);

      if (m_logger) {
        m_logger->Info("Measured " + std::to_string(current) + " A, stored as '" +
                      captureAsIt->second + "'", "PSUHandler");
      }
    }

    // Convert A to mA for display
    double currentInMA = current * 1000.0;

    // Format message
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);
    oss << deviceName << " Current = " << currentInMA << " mA (" << current << " A)";
    std::string message = oss.str();

    if (m_logger) {
      m_logger->Info(message, "PSU");
    }

    return true;
  }

  // ============================================================================
  // READ VOLTAGE MULTIPLE TIMES (with delay)
  // ============================================================================
  else if (methodName == "ReadVoltageMultiple") {
    // Parse parameters
    int readCount = 1;
    int delayMs = 0;

    auto countIt = params.find("read_count");
    if (countIt != params.end()) {
      try {
        readCount = std::stoi(countIt->second);
        if (readCount < 1) readCount = 1;
        if (readCount > 100) readCount = 100; // Safety limit
      } catch (...) {
        readCount = 1;
      }
    }

    auto delayIt = params.find("delay_ms");
    if (delayIt != params.end()) {
      try {
        delayMs = std::stoi(delayIt->second);
        if (delayMs < 0) delayMs = 0;
      } catch (...) {
        delayMs = 0;
      }
    }

    if (m_logger) {
      std::ostringstream infoMsg;
      infoMsg << "Reading voltage from " << deviceName << " " << readCount
              << " time(s) with " << delayMs << "ms delay";
      m_logger->Info(infoMsg.str(), "PSU");
    }

    // Perform multiple reads
    double lastVoltage = 0.0;
    for (int i = 0; i < readCount; i++) {
      double voltage = psu->readVoltage(channel);
      lastVoltage = voltage;  // Track last reading for context capture

      // Format message
      std::ostringstream oss;
      oss << std::fixed << std::setprecision(3);
      oss << "[" << (i + 1) << "/" << readCount << "] "
          << deviceName << " Voltage = " << voltage << " V";

      if (m_logger) {
        m_logger->Info(oss.str(), "PSU");
      }

      // Delay before next read (except after last read)
      if (i < readCount - 1 && delayMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
      }
    }

    // Auto-capture the last voltage reading to context if requested
    if (context && captureAsIt != params.end()) {
      context->SetValue(captureAsIt->second, static_cast<float>(lastVoltage), deviceName, methodName);

      if (m_logger) {
        m_logger->Info("Last voltage reading " + std::to_string(lastVoltage) + " V stored as '" +
                      captureAsIt->second + "'", "PSUHandler");
      }
    }

    return true;
  }

  // ============================================================================
  // READ CURRENT MULTIPLE TIMES (with delay)
  // ============================================================================
  else if (methodName == "ReadCurrentMultiple") {
    // Parse parameters
    int readCount = 1;
    int delayMs = 0;

    auto countIt = params.find("read_count");
    if (countIt != params.end()) {
      try {
        readCount = std::stoi(countIt->second);
        if (readCount < 1) readCount = 1;
        if (readCount > 100) readCount = 100; // Safety limit
      } catch (...) {
        readCount = 1;
      }
    }

    auto delayIt = params.find("delay_ms");
    if (delayIt != params.end()) {
      try {
        delayMs = std::stoi(delayIt->second);
        if (delayMs < 0) delayMs = 0;
      } catch (...) {
        delayMs = 0;
      }
    }

    if (m_logger) {
      std::ostringstream infoMsg;
      infoMsg << "Reading current from " << deviceName << " " << readCount
              << " time(s) with " << delayMs << "ms delay";
      m_logger->Info(infoMsg.str(), "PSU");
    }

    // Perform multiple reads
    double lastCurrent = 0.0;
    for (int i = 0; i < readCount; i++) {
      double current = psu->readCurrent(channel);
      lastCurrent = current;  // Track last reading for context capture

      // Convert A to mA for display
      double currentInMA = current * 1000.0;

      // Format message
      std::ostringstream oss;
      oss << std::fixed << std::setprecision(3);
      oss << "[" << (i + 1) << "/" << readCount << "] "
          << deviceName << " Current = " << currentInMA << " mA (" << current << " A)";

      if (m_logger) {
        m_logger->Info(oss.str(), "PSU");
      }

      // Delay before next read (except after last read)
      if (i < readCount - 1 && delayMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
      }
    }

    // Auto-capture the last current reading to context if requested (store in Amps)
    if (context && captureAsIt != params.end()) {
      context->SetValue(captureAsIt->second, static_cast<float>(lastCurrent), deviceName, methodName);

      if (m_logger) {
        m_logger->Info("Last current reading " + std::to_string(lastCurrent) + " A stored as '" +
                      captureAsIt->second + "'", "PSUHandler");
      }
    }

    return true;
  }

  // ============================================================================
  // CONNECT
  // ============================================================================
  else if (methodName == "Connect") {
    if (m_logger) {
      m_logger->Info("Connecting to PSU device: " + deviceName, "PSUHandler");
    }

    bool success = psu->connect();

    if (!success) {
      errorMessage = "Failed to connect to PSU device: " + deviceName;
      return false;
    }

    if (m_logger) {
      m_logger->Info("Successfully connected to PSU device: " + deviceName, "PSUHandler");
    }
    return true;
  }

  // ============================================================================
  // DISCONNECT
  // ============================================================================
  else if (methodName == "Disconnect") {
    if (m_logger) {
      m_logger->Info("Disconnecting from PSU device: " + deviceName, "PSUHandler");
    }

    psu->disconnect();

    if (m_logger) {
      m_logger->Info("Disconnected from PSU device: " + deviceName, "PSUHandler");
    }
    return true;
  }

  // ============================================================================
  // UNKNOWN METHOD
  // ============================================================================
  else {
    errorMessage = "Unknown PSU method: " + methodName;
    return false;
  }
}

bool PSUCommandHandler::ParseVoltage(
  const std::map<std::string, std::string>& params,
  double& outVoltage)
{
  auto it = params.find("voltage");
  if (it == params.end()) {
    return false;
  }

  try {
    outVoltage = std::stod(it->second);

    // Sanity check: typical range 0-30V
    if (outVoltage < 0.0 || outVoltage > 30.0) {
      return false;
    }

    return true;
  }
  catch (...) {
    return false;
  }
}

bool PSUCommandHandler::ParseCurrent(
  const std::map<std::string, std::string>& params,
  double& outCurrent)
{
  auto it = params.find("current");
  if (it == params.end()) {
    return false;
  }

  try {
    outCurrent = std::stod(it->second);

    // Sanity check: typical range 0-3000 mA
    if (outCurrent < 0.0 || outCurrent > 3000.0) {
      return false;
    }

    return true;
  }
  catch (...) {
    return false;
  }
}

int PSUCommandHandler::ParseChannel(const std::map<std::string, std::string>& params)
{
  auto it = params.find("channel");
  if (it == params.end()) {
    return 1; // Default to channel 1
  }

  try {
    int channel = std::stoi(it->second);

    // Sanity check: channel range 1-4 (supports up to 4-channel devices)
    if (channel < 1 || channel > 4) {
      return 1; // Default to channel 1 if out of range
    }

    return channel;
  }
  catch (...) {
    return 1; // Default to channel 1 on parse error
  }
}
