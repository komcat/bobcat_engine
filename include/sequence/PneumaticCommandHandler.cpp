// PneumaticCommandHandler.cpp
#include "PneumaticCommandHandler.h"
#include "PneumaticManager.h"
#include "LogManager.h"
#include <thread>    // ✅ ADD THIS for sleep_for
#include <chrono>    // ✅ ADD THIS for milliseconds

bool PneumaticCommandHandler::ExecuteCommand(
  void* manager,
  const std::string& deviceName,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context)
{
  PneumaticManager* pneumaticManager = static_cast<PneumaticManager*>(manager);

  // Context can be used for capturing pneumatic state
  // Example: if (context && params.find("captureAs") != params.end()) {
  //   context->SetValue(params.at("captureAs"), isExtended, deviceName, methodName);
  // }

  // ============================================================================
  // EXTEND (with optional blocking)
  // ============================================================================
  if (methodName == "Extend") {
    bool blocking = true;  // Default to blocking
    uint32_t pollInterval = 50;  // Default 50ms poll interval

    auto blockingIt = params.find("blocking");
    if (blockingIt != params.end()) {
      blocking = (blockingIt->second == "true");
    }

    auto pollIt = params.find("pollInterval");
    if (pollIt != params.end()) {
      pollInterval = std::stoi(pollIt->second);
    }

    if (m_logger) {
      m_logger->Info(
        "Extending pneumatic slide: " + deviceName +
        (blocking ? " (blocking)" : " (non-blocking)"),
        "PneumaticHandler"
      );
    }

    PneumaticError result;
    if (blocking) {
      result = pneumaticManager->ExtendAndWait(deviceName, pollInterval);
    }
    else {
      result = pneumaticManager->Extend(deviceName);
    }

    if (result != PneumaticError::SUCCESS) {
      errorMessage = "Extend failed: " + GetPneumaticErrorString(result);
      return false;
    }

    if (m_logger) {
      m_logger->Info(deviceName + " extended successfully", "PneumaticHandler");
    }

    return true;
  }

  // ============================================================================
  // RETRACT (with optional blocking)
  // ============================================================================
  else if (methodName == "Retract") {
    bool blocking = true;
    uint32_t pollInterval = 50;

    auto blockingIt = params.find("blocking");
    if (blockingIt != params.end()) {
      blocking = (blockingIt->second == "true");
    }

    auto pollIt = params.find("pollInterval");
    if (pollIt != params.end()) {
      pollInterval = std::stoi(pollIt->second);
    }

    if (m_logger) {
      m_logger->Info(
        "Retracting pneumatic slide: " + deviceName +
        (blocking ? " (blocking)" : " (non-blocking)"),
        "PneumaticHandler"
      );
    }

    PneumaticError result;
    if (blocking) {
      result = pneumaticManager->RetractAndWait(deviceName, pollInterval);
    }
    else {
      result = pneumaticManager->Retract(deviceName);
    }

    if (result != PneumaticError::SUCCESS) {
      errorMessage = "Retract failed: " + GetPneumaticErrorString(result);
      return false;
    }

    if (m_logger) {
      m_logger->Info(deviceName + " retracted successfully", "PneumaticHandler");
    }

    return true;
  }

  // ============================================================================
  // STOP
  // ============================================================================
  else if (methodName == "Stop") {
    if (m_logger) {
      m_logger->Info("Stopping pneumatic slide: " + deviceName, "PneumaticHandler");
    }

    PneumaticError result = pneumaticManager->Stop(deviceName);

    if (result != PneumaticError::SUCCESS) {
      errorMessage = "Stop failed: " + GetPneumaticErrorString(result);
      return false;
    }

    return true;
  }

  // ============================================================================
  // GET POSITION
  // ============================================================================
  else if (methodName == "GetPosition") {
    PneumaticPosition position = pneumaticManager->GetPosition(deviceName);

    if (m_logger) {
      m_logger->Info(
        deviceName + " position: " + GetPositionString(position),
        "PneumaticHandler"
      );
    }

    return true;
  }

  // ============================================================================
  // GET STATUS
  // ============================================================================
  else if (methodName == "GetStatus") {
    PneumaticStatus status = pneumaticManager->GetStatus(deviceName);

    if (!status.Valid) {
      errorMessage = "Failed to get status for: " + deviceName;
      return false;
    }

    if (m_logger) {
      std::string statusMsg = deviceName + " status: " +
        GetPositionString(status.Position) +
        " | Output: " + (status.OutputActive ? "ON" : "OFF") +
        " | Extended: " + (status.ExtendedSensorActive ? "YES" : "NO") +
        " | Retracted: " + (status.RetractedSensorActive ? "YES" : "NO");

      m_logger->Info(statusMsg, "PneumaticHandler");
    }

    return true;
  }

  // ============================================================================
  // WAIT FOR EXTENDED (wait until slide reaches extended position)
  // ============================================================================
  else if (methodName == "WaitForExtended") {
    uint32_t pollInterval = 50;
    uint32_t timeout = 5000;  // Default 5 seconds

    auto pollIt = params.find("pollInterval");
    if (pollIt != params.end()) {
      pollInterval = std::stoi(pollIt->second);
    }

    auto timeoutIt = params.find("timeout");
    if (timeoutIt != params.end()) {
      timeout = std::stoi(timeoutIt->second);
    }

    if (m_logger) {
      m_logger->Info(
        "Waiting for " + deviceName + " to reach extended position (timeout: " +
        std::to_string(timeout) + "ms)",
        "PneumaticHandler"
      );
    }

    // Poll until extended or timeout
    uint32_t elapsed = 0;
    while (elapsed < timeout) {
      PneumaticPosition pos = pneumaticManager->GetPosition(deviceName);

      if (pos == PneumaticPosition::EXTENDED) {
        if (m_logger) {
          m_logger->Info(
            deviceName + " reached extended position after " + std::to_string(elapsed) + "ms",
            "PneumaticHandler"
          );
        }
        return true;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(pollInterval));
      elapsed += pollInterval;
    }

    errorMessage = "Timeout waiting for " + deviceName + " to extend";
    return false;
  }

  // ============================================================================
  // WAIT FOR RETRACTED (wait until slide reaches retracted position)
  // ============================================================================
  else if (methodName == "WaitForRetracted") {
    uint32_t pollInterval = 50;
    uint32_t timeout = 5000;

    auto pollIt = params.find("pollInterval");
    if (pollIt != params.end()) {
      pollInterval = std::stoi(pollIt->second);
    }

    auto timeoutIt = params.find("timeout");
    if (timeoutIt != params.end()) {
      timeout = std::stoi(timeoutIt->second);
    }

    if (m_logger) {
      m_logger->Info(
        "Waiting for " + deviceName + " to reach retracted position (timeout: " +
        std::to_string(timeout) + "ms)",
        "PneumaticHandler"
      );
    }

    // Poll until retracted or timeout
    uint32_t elapsed = 0;
    while (elapsed < timeout) {
      PneumaticPosition pos = pneumaticManager->GetPosition(deviceName);

      if (pos == PneumaticPosition::RETRACTED) {
        if (m_logger) {
          m_logger->Info(
            deviceName + " reached retracted position after " + std::to_string(elapsed) + "ms",
            "PneumaticHandler"
          );
        }
        return true;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(pollInterval));
      elapsed += pollInterval;
    }

    errorMessage = "Timeout waiting for " + deviceName + " to retract";
    return false;
  }

  // ============================================================================
  // UNKNOWN METHOD
  // ============================================================================
  else {
    errorMessage = "Unknown pneumatic method: " + methodName;
    return false;
  }
}