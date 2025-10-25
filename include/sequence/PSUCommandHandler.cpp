// PSUCommandHandler.cpp
#include "PSUCommandHandler.h"
#include "PowerSupplyManager.h"
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
  std::string& errorMessage)
{
  PowerSupplyManager* psuManager = static_cast<PowerSupplyManager*>(manager);

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

  // ============================================================================
  // SET OUTPUT ON
  // ============================================================================
  if (methodName == "SetOutputOn" || methodName == "OutputOn") {
    if (m_logger) {
      m_logger->Info("Turning PSU output ON for " + deviceName, "PSUHandler");
    }

    if (!psu->setOutputOn()) {
      errorMessage = "SetOutputOn failed: " + psu->getLastError();
      return false;
    }

    return true;
  }

  // ============================================================================
  // SET OUTPUT OFF
  // ============================================================================
  else if (methodName == "SetOutputOff" || methodName == "OutputOff") {
    if (m_logger) {
      m_logger->Info("Turning PSU output OFF for " + deviceName, "PSUHandler");
    }

    if (!psu->setOutputOff()) {
      errorMessage = "SetOutputOff failed: " + psu->getLastError();
      return false;
    }

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
        "Setting " + deviceName + " voltage to " + std::to_string(voltage) + " V",
        "PSUHandler"
      );
    }

    if (!psu->setVoltage(voltage)) {
      errorMessage = "SetVoltage failed: " + psu->getLastError();
      return false;
    }

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
        std::to_string(currentInAmps) + " A)",
        "PSUHandler"
      );
    }

    if (!psu->setCurrent(currentInAmps)) {
      errorMessage = "SetCurrent failed: " + psu->getLastError();
      return false;
    }

    return true;
  }

  // ============================================================================
  // READ VOLTAGE
  // ============================================================================
  else if (methodName == "ReadVoltage") {
    double voltage = psu->readVoltage();

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
    double current = psu->readCurrent();

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
    for (int i = 0; i < readCount; i++) {
      double voltage = psu->readVoltage();

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
    for (int i = 0; i < readCount; i++) {
      double current = psu->readCurrent();

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
