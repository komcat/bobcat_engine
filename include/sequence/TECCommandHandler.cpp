// TECCommandHandler.cpp
#include "TECCommandHandler.h"
#include "TECManager.h"
#include "SequenceContext.h"
#include "LogManager.h"
#include <sstream>
#include <thread>
#include <chrono>
#include <iomanip>

bool TECCommandHandler::ExecuteCommand(
  void* manager,
  const std::string& deviceName,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context)
{
  TECManager* tecManager = static_cast<TECManager*>(manager);

  // Check if device exists
  if (!tecManager->HasDevice(deviceName)) {
    errorMessage = "TEC device '" + deviceName + "' not found";
    return false;
  }

  // Context can be used for capturing temperature, current, power, etc.
  // Example: GetTemperature, GetCurrent methods can capture values
  auto captureAsIt = params.find("captureAs");

  // ============================================================================
  // SET TEMPERATURE
  // ============================================================================
  if (methodName == "SetTemperature") {
    double temperature;
    if (!ParseTemperature(params, temperature)) {
      errorMessage = "Missing or invalid parameter: temperature";
      return false;
    }

    if (m_logger) {
      m_logger->Info(
        "Setting " + deviceName + " temperature to " + std::to_string(temperature) + " �C",
        "TECHandler"
      );
    }

    auto result = tecManager->SetTemperature(deviceName, temperature);

    if (!result.Success) {
      errorMessage = "SetTemperature failed: " + result.Message;
      return false;
    }

    return true;
  }

  // ============================================================================
  // TEC ON
  // ============================================================================
  else if (methodName == "TEC_On" || methodName == "TECOn") {
    if (m_logger) {
      m_logger->Info("Turning TEC ON for " + deviceName, "TECHandler");
    }

    auto result = tecManager->TEC_On(deviceName);

    if (!result.Success) {
      errorMessage = "TEC_On failed: " + result.Message;
      return false;
    }

    return true;
  }

  // ============================================================================
  // TEC OFF
  // ============================================================================
  else if (methodName == "TEC_Off" || methodName == "TECOff") {
    if (m_logger) {
      m_logger->Info("Turning TEC OFF for " + deviceName, "TECHandler");
    }

    auto result = tecManager->TEC_Off(deviceName);

    if (!result.Success) {
      errorMessage = "TEC_Off failed: " + result.Message;
      return false;
    }

    return true;
  }

  // ============================================================================
  // READ TEMPERATURE
  // ============================================================================
  else if (methodName == "ReadTemperature") {
    // Get optional log level parameter
    auto logLevelIt = params.find("logLevel");
    std::string logLevel = (logLevelIt != params.end()) ? logLevelIt->second : "Info";

    auto result = tecManager->ReadTemperature(deviceName);

    if (!result.Success) {
      errorMessage = "ReadTemperature failed: " + result.Message;
      return false;
    }

    // Auto-capture to context if requested
    auto captureAsIt = params.find("captureAs");
    if (context && captureAsIt != params.end() && !captureAsIt->second.empty()) {
      context->SetValue(captureAsIt->second, static_cast<float>(result.Value), deviceName, methodName);

      if (m_logger) {
        m_logger->Info("Temperature " + std::to_string(result.Value) + " �C stored as '" +
                      captureAsIt->second + "'", "TECHandler");
      }
    }

    // Format message with proper unit
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);
    oss << deviceName << " Temperature = " << result.Value << " �C";
    std::string message = oss.str();

    // Log based on level
    if (m_logger) {
      if (logLevel == "Debug") {
        m_logger->Debug(message, "TEC");
      }
      else if (logLevel == "Warning") {
        m_logger->Warning(message, "TEC");
      }
      else {
        m_logger->Info(message, "TEC");
      }
    }

    return true;
  }

  // ============================================================================
  // SET LASER CURRENT
  // ============================================================================
  else if (methodName == "SetLaserCurrent") {
    double current;
    if (!ParseCurrent(params, current)) {
      errorMessage = "Missing or invalid parameter: current";
      return false;
    }

    if (m_logger) {
      m_logger->Info(
        "Setting " + deviceName + " laser current to " + std::to_string(current) + " mA",
        "TECHandler"
      );
    }

    auto result = tecManager->SetLaserCurrent(deviceName, current);

    if (!result.Success) {
      errorMessage = "SetLaserCurrent failed: " + result.Message;
      return false;
    }

    return true;
  }

  // ============================================================================
  // LASER ON
  // ============================================================================
  else if (methodName == "Laser_On" || methodName == "LaserOn") {
    if (m_logger) {
      m_logger->Info("Turning Laser ON for " + deviceName, "TECHandler");
    }

    auto result = tecManager->Laser_On(deviceName);

    if (!result.Success) {
      errorMessage = "Laser_On failed: " + result.Message;
      return false;
    }

    return true;
  }

  // ============================================================================
  // LASER OFF
  // ============================================================================
  else if (methodName == "Laser_Off" || methodName == "LaserOff") {
    if (m_logger) {
      m_logger->Info("Turning Laser OFF for " + deviceName, "TECHandler");
    }

    auto result = tecManager->Laser_Off(deviceName);

    if (!result.Success) {
      errorMessage = "Laser_Off failed: " + result.Message;
      return false;
    }

    return true;
  }

  // ============================================================================
  // READ LASER CURRENT
  // ============================================================================
  else if (methodName == "ReadLaserCurrent") {
    // Get optional log level parameter
    auto logLevelIt = params.find("logLevel");
    std::string logLevel = (logLevelIt != params.end()) ? logLevelIt->second : "Info";

    auto result = tecManager->ReadLaserCurrent(deviceName);

    if (!result.Success) {
      errorMessage = "ReadLaserCurrent failed: " + result.Message;
      return false;
    }

    // Auto-capture to context if requested
    auto captureAsIt = params.find("captureAs");
    if (context && captureAsIt != params.end() && !captureAsIt->second.empty()) {
      context->SetValue(captureAsIt->second, static_cast<float>(result.Value), deviceName, methodName);

      if (m_logger) {
        m_logger->Info("Laser Current " + std::to_string(result.Value) + " mA stored as '" +
                      captureAsIt->second + "'", "TECHandler");
      }
    }

    // Format message with proper unit
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);
    oss << deviceName << " Laser Current = " << result.Value << " mA";
    std::string message = oss.str();

    // Log based on level
    if (m_logger) {
      if (logLevel == "Debug") {
        m_logger->Debug(message, "TEC");
      }
      else if (logLevel == "Warning") {
        m_logger->Warning(message, "TEC");
      }
      else {
        m_logger->Info(message, "TEC");
      }
    }

    return true;
  }

  // ============================================================================
  // GET STATUS
  // ============================================================================
  else if (methodName == "GetStatus") {
    TECStatus status = tecManager->GetDeviceStatus(deviceName);

    if (m_logger) {
      std::ostringstream oss;
      oss << deviceName << " Status:\n";
      oss << "  Connected: " << (status.Connected ? "Yes" : "No") << "\n";
      oss << "  TEC: " << (status.TECOn ? "ON" : "OFF") << "\n";
      oss << "  Laser: " << (status.LaserOn ? "ON" : "OFF") << "\n";
      oss << "  Temperature: " << status.CurrentTemperature << " �C (SP: "
        << status.SetpointTemperature << ")\n";
      oss << "  Laser Current: " << status.CurrentLaserCurrent << " mA (SP: "
        << status.SetpointLaserCurrent << ")";
      m_logger->Info(oss.str(), "TECHandler");
    }

    return true;
  }

  // ============================================================================
  // WAIT FOR TEMPERATURE (wait until temperature reaches setpoint)
  // ============================================================================
  else if (methodName == "WaitForTemperature") {
    auto toleranceIt = params.find("tolerance");
    auto timeoutIt = params.find("timeout");

    double tolerance = 0.5;  // Default �0.5�C
    int timeoutMs = 30000;   // Default 30 seconds

    if (toleranceIt != params.end()) {
      tolerance = std::stod(toleranceIt->second);
    }

    if (timeoutIt != params.end()) {
      timeoutMs = std::stoi(timeoutIt->second);
    }

    if (m_logger) {
      m_logger->Info(
        "Waiting for " + deviceName + " temperature to stabilize (tolerance: �" +
        std::to_string(tolerance) + " �C, timeout: " + std::to_string(timeoutMs) + "ms)",
        "TECHandler"
      );
    }

    auto startTime = std::chrono::steady_clock::now();

    while (true) {
      // Check timeout
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime
      ).count();

      if (elapsed >= timeoutMs) {
        errorMessage = "WaitForTemperature timeout";
        return false;
      }

      // Read current temperature
      auto tempResult = tecManager->ReadTemperature(deviceName);
      if (!tempResult.Success) {
        errorMessage = "Failed to read temperature: " + tempResult.Message;
        return false;
      }

      // Get setpoint
      TECStatus status = tecManager->GetDeviceStatus(deviceName);
      double currentTemp = tempResult.Value;
      double setpointTemp = status.SetpointTemperature;

      // Check if within tolerance
      double difference = std::abs(currentTemp - setpointTemp);
      if (difference <= tolerance) {
        if (m_logger) {
          m_logger->Info(
            "Temperature stabilized at " + std::to_string(currentTemp) +
            " �C after " + std::to_string(elapsed) + "ms",
            "TECHandler"
          );
        }
        return true;
      }

      // Wait before next check
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
  }

  // ============================================================================
  // DELAY
  // ============================================================================
  else if (methodName == "Delay") {
    auto msIt = params.find("milliseconds");

    if (msIt == params.end()) {
      errorMessage = "Missing required parameter: milliseconds";
      return false;
    }

    int ms = std::stoi(msIt->second);

    if (m_logger) {
      m_logger->Info("Delaying for " + std::to_string(ms) + "ms", "TECHandler");
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    return true;
  }

  // ============================================================================
  // UNKNOWN METHOD
  // ============================================================================
  else {
    errorMessage = "Unknown TEC method: " + methodName;
    return false;
  }
}

bool TECCommandHandler::ParseTemperature(
  const std::map<std::string, std::string>& params,
  double& outTemp)
{
  auto it = params.find("temperature");
  if (it == params.end()) {
    return false;
  }

  try {
    outTemp = std::stod(it->second);

    // Sanity check: typical range -50 to 100�C
    if (outTemp < -50.0 || outTemp > 100.0) {
      return false;
    }

    return true;
  }
  catch (...) {
    return false;
  }
}

bool TECCommandHandler::ParseCurrent(
  const std::map<std::string, std::string>& params,
  double& outCurrent)
{
  auto it = params.find("current");
  if (it == params.end()) {
    return false;
  }

  try {
    outCurrent = std::stod(it->second);

    // Sanity check: typical range 0-1000 mA
    if (outCurrent < 0.0 || outCurrent > 1000.0) {
      return false;
    }

    return true;
  }
  catch (...) {
    return false;
  }
}