// DataStoreCommandHandler.cpp
#include "DataStoreCommandHandler.h"
#include "LogManager.h"
#include <iostream>

DataStoreCommandHandler::DataStoreCommandHandler(DataStore* dataStore)
  : m_dataStore(dataStore)
{
  if (!m_dataStore) {
    std::cerr << "[DataStoreCommandHandler] Error: DataStore cannot be null" << std::endl;
    m_valid = false;
    return;
  }
  std::cout << "[DataStoreCommandHandler] Created" << std::endl;
}

bool DataStoreCommandHandler::ExecuteCommand(
  void* /*manager*/,
  const std::string& /*deviceName*/,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* /*context*/)
{
  // Context is available but DataStore is the primary target
  // Could use context to track which values were logged, etc.
  if (methodName == "ReadAndLog") {
    auto channelIt = params.find("channelName");
    auto logLevelIt = params.find("logLevel");

    if (channelIt == params.end()) {
      errorMessage = "Missing required parameter: channelName";
      return false;
    }

    std::string channelName = channelIt->second;
    std::string logLevel = (logLevelIt != params.end()) ? logLevelIt->second : "Info";

    // Read value from DataStore
    float value = 0.0f;
    bool hasValue = m_dataStore->TryGetValue(channelName, value);

    if (!hasValue) {
      errorMessage = "Channel not found in DataStore: " + channelName;
      if (m_logger) {
        m_logger->Warning(errorMessage, "DataStoreHandler");
      }
      return false;
    }

    // Detect unit and format value with appropriate prefix
    std::string unit = DetectUnit(channelName);
    std::string formattedValue = FormatValueWithPrefix(value, unit);

    // Build message
    std::string message = "DataStore[" + channelName + "] = " + formattedValue;

    if (m_logger) {
      if (logLevel == "Debug") {
        m_logger->Debug(message, "DataStore");
      }
      else if (logLevel == "Info") {
        m_logger->Info(message, "DataStore");
      }
      else if (logLevel == "Warning") {
        m_logger->Warning(message, "DataStore");
      }
      else {
        m_logger->Info(message, "DataStore");
      }
    }
    else {
      std::cout << "[DataStoreHandler] " << message << std::endl;
    }

    return true;
  }

  errorMessage = "Unknown method: " + methodName;
  return false;
}

std::string DataStoreCommandHandler::DetectUnit(const std::string& channelName)
{
  std::string lowerName = channelName;
  std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

  // Check for current
  if (lowerName.find("current") != std::string::npos ||
    lowerName.find("amp") != std::string::npos ||
    lowerName.find("amps") != std::string::npos) {
    return "A";
  }

  // Check for voltage
  if (lowerName.find("voltage") != std::string::npos ||
    lowerName.find("volt") != std::string::npos ||
    lowerName.find("volts") != std::string::npos) {
    return "V";
  }

  // Check for temperature
  if (lowerName.find("temp") != std::string::npos ||
    lowerName.find("temperature") != std::string::npos) {
    return "°C";
  }

  // Check for power
  if (lowerName.find("power") != std::string::npos ||
    lowerName.find("watt") != std::string::npos) {
    return "W";
  }

  // Check for resistance
  if (lowerName.find("resistance") != std::string::npos ||
    lowerName.find("ohm") != std::string::npos) {
    return "Ω";
  }

  // No unit detected
  return "";
}

std::string DataStoreCommandHandler::FormatValueWithPrefix(float value, const std::string& unit)
{
  float absValue = std::abs(value);
  std::ostringstream ss;
  ss << std::fixed << std::setprecision(3);

  if (absValue == 0.0f) {
    ss << "0";
    if (!unit.empty()) {
      ss << " " << unit;
    }
  }
  else if (absValue < 1e-9) {  // Less than 1 pA
    ss << (value * 1e12) << " p" << unit;  // pico
  }
  else if (absValue < 1e-6) {  // Less than 1 nA
    ss << (value * 1e9) << " n" << unit;   // nano
  }
  else if (absValue < 1e-3) {  // Less than 1 µA
    ss << (value * 1e6) << " µ" << unit;   // micro
  }
  else if (absValue < 1.0) {   // Less than 1 A
    ss << (value * 1e3) << " m" << unit;   // milli
  }
  else if (absValue < 1e3) {   // Less than 1 kA
    ss << value << " " << unit;             // base unit
  }
  else if (absValue < 1e6) {   // Less than 1 MA
    ss << (value / 1e3) << " k" << unit;   // kilo
  }
  else if (absValue < 1e9) {   // Less than 1 GA
    ss << (value / 1e6) << " M" << unit;   // mega
  }
  else {
    ss << (value / 1e9) << " G" << unit;   // giga
  }

  return ss.str();
}