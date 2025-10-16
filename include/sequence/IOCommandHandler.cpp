// IOCommandHandler.cpp
#include "IOCommandHandler.h"
#include "EziIOManager.h"
#include "LogManager.h"
#include <thread>
#include <chrono>

bool IOCommandHandler::ExecuteCommand(
  void* manager,
  const std::string& deviceName,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage)
{
  EziIOManager* ioManager = static_cast<EziIOManager*>(manager);

  // ============================================================================
  // SET OUTPUT (by pin name)
  // ============================================================================
  if (methodName == "SetOutput") {
    auto pinIt = params.find("pinName");
    auto stateIt = params.find("state");

    if (pinIt == params.end() || stateIt == params.end()) {
      errorMessage = "Missing required parameters: pinName, state";
      return false;
    }

    std::string pinName = pinIt->second;
    bool state = (stateIt->second == "true");

    if (m_logger) {
      m_logger->Info(
        "Setting " + deviceName + "." + pinName + " to " + (state ? "ON" : "OFF"),
        "IOHandler"
      );
    }

    EziIOError result = ioManager->SetOutputByName(deviceName, pinName, state);

    if (result != EziIOError::SUCCESS) {
      errorMessage = "SetOutput failed: " + GetErrorString(result);
      return false;
    }

    return true;
  }

  // ============================================================================
  // SET OUTPUT (by pin number)
  // ============================================================================
  else if (methodName == "SetOutputPin") {
    auto pinIt = params.find("pin");
    auto stateIt = params.find("state");

    if (pinIt == params.end() || stateIt == params.end()) {
      errorMessage = "Missing required parameters: pin, state";
      return false;
    }

    int pin = std::stoi(pinIt->second);
    bool state = (stateIt->second == "true");

    if (m_logger) {
      m_logger->Info(
        "Setting " + deviceName + " pin " + std::to_string(pin) +
        " to " + (state ? "ON" : "OFF"),
        "IOHandler"
      );
    }

    EziIOError result = ioManager->SetOutput(deviceName, pin, state);

    if (result != EziIOError::SUCCESS) {
      errorMessage = "SetOutputPin failed: " + GetErrorString(result);
      return false;
    }

    return true;
  }

  // ============================================================================
  // GET INPUT (by pin name)
  // ============================================================================
  else if (methodName == "GetInput") {
    auto pinIt = params.find("pinName");

    if (pinIt == params.end()) {
      errorMessage = "Missing required parameter: pinName";
      return false;
    }

    std::string pinName = pinIt->second;
    bool state = false;

    if (!ioManager->GetInputByName(deviceName, pinName, state)) {
      errorMessage = "GetInput failed for pin: " + pinName;
      return false;
    }

    if (m_logger) {
      m_logger->Info(
        deviceName + "." + pinName + " is " + (state ? "ON" : "OFF"),
        "IOHandler"
      );
    }

    return true;
  }

  // ============================================================================
  // READ ALL INPUTS
  // ============================================================================
  else if (methodName == "ReadInputs") {
    uint32_t inputs = 0, latch = 0;

    EziIOError result = ioManager->ReadInputs(deviceName, inputs, latch);

    if (result != EziIOError::SUCCESS) {
      errorMessage = "ReadInputs failed: " + GetErrorString(result);
      return false;
    }

    if (m_logger) {
      m_logger->Info(
        "Read inputs from " + deviceName + ": 0x" +
        std::to_string(inputs) + " (Latch: 0x" + std::to_string(latch) + ")",
        "IOHandler"
      );
    }

    return true;
  }

  // ============================================================================
  // READ ALL OUTPUTS
  // ============================================================================
  else if (methodName == "ReadOutputs") {
    uint32_t outputs = 0, status = 0;

    EziIOError result = ioManager->ReadOutputs(deviceName, outputs, status);

    if (result != EziIOError::SUCCESS) {
      errorMessage = "ReadOutputs failed: " + GetErrorString(result);
      return false;
    }

    if (m_logger) {
      m_logger->Info(
        "Read outputs from " + deviceName + ": 0x" +
        std::to_string(outputs) + " (Status: 0x" + std::to_string(status) + ")",
        "IOHandler"
      );
    }

    return true;
  }

  // ============================================================================
  // SET MULTIPLE OUTPUTS (bulk operation)
  // ============================================================================
  else if (methodName == "SetOutputs") {
    auto setMaskIt = params.find("setMask");
    auto clearMaskIt = params.find("clearMask");

    if (setMaskIt == params.end() || clearMaskIt == params.end()) {
      errorMessage = "Missing required parameters: setMask, clearMask";
      return false;
    }

    uint32_t setMask = std::stoul(setMaskIt->second);
    uint32_t clearMask = std::stoul(clearMaskIt->second);

    if (m_logger) {
      m_logger->Info(
        "Setting outputs on " + deviceName +
        " (Set: 0x" + std::to_string(setMask) +
        ", Clear: 0x" + std::to_string(clearMask) + ")",
        "IOHandler"
      );
    }

    EziIOError result = ioManager->SetOutputs(deviceName, setMask, clearMask);

    if (result != EziIOError::SUCCESS) {
      errorMessage = "SetOutputs failed: " + GetErrorString(result);
      return false;
    }

    return true;
  }

  // ============================================================================
  // CLEAR LATCH
  // ============================================================================
  else if (methodName == "ClearLatch") {
    auto maskIt = params.find("latchMask");

    if (maskIt == params.end()) {
      errorMessage = "Missing required parameter: latchMask";
      return false;
    }

    uint32_t latchMask = std::stoul(maskIt->second);

    if (m_logger) {
      m_logger->Info(
        "Clearing latch on " + deviceName + " (Mask: 0x" +
        std::to_string(latchMask) + ")",
        "IOHandler"
      );
    }

    EziIOError result = ioManager->ClearLatch(deviceName, latchMask);

    if (result != EziIOError::SUCCESS) {
      errorMessage = "ClearLatch failed: " + GetErrorString(result);
      return false;
    }

    return true;
  }

  // ============================================================================
  // PULSE OUTPUT (turn on, wait, turn off)
  // ============================================================================
  else if (methodName == "Pulse") {
    auto pinIt = params.find("pinName");
    auto durationIt = params.find("milliseconds");

    if (pinIt == params.end() || durationIt == params.end()) {
      errorMessage = "Missing required parameters: pinName, milliseconds";
      return false;
    }

    std::string pinName = pinIt->second;
    int ms = std::stoi(durationIt->second);

    if (m_logger) {
      m_logger->Info(
        "Pulsing " + deviceName + "." + pinName + " for " + std::to_string(ms) + "ms",
        "IOHandler"
      );
    }

    // Turn ON
    EziIOError result = ioManager->SetOutputByName(deviceName, pinName, true);
    if (result != EziIOError::SUCCESS) {
      errorMessage = "Pulse failed (ON): " + GetErrorString(result);
      return false;
    }

    // Wait
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));

    // Turn OFF
    result = ioManager->SetOutputByName(deviceName, pinName, false);
    if (result != EziIOError::SUCCESS) {
      errorMessage = "Pulse failed (OFF): " + GetErrorString(result);
      return false;
    }

    return true;
  }

  // ============================================================================
  // DELAY (simple wait)
  // ============================================================================
  else if (methodName == "Delay") {
    auto msIt = params.find("milliseconds");

    if (msIt == params.end()) {
      errorMessage = "Missing required parameter: milliseconds";
      return false;
    }

    int ms = std::stoi(msIt->second);

    if (m_logger) {
      m_logger->Info("Delaying for " + std::to_string(ms) + "ms", "IOHandler");
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    return true;
  }

  // ============================================================================
  // WAIT FOR INPUT (wait until input becomes specific state)
  // ============================================================================
  else if (methodName == "WaitForInput") {
    auto pinIt = params.find("pinName");
    auto stateIt = params.find("state");
    auto timeoutIt = params.find("timeout");

    if (pinIt == params.end() || stateIt == params.end()) {
      errorMessage = "Missing required parameters: pinName, state";
      return false;
    }

    std::string pinName = pinIt->second;
    bool targetState = (stateIt->second == "true");
    int timeoutMs = 5000;  // Default 5 seconds

    if (timeoutIt != params.end()) {
      timeoutMs = std::stoi(timeoutIt->second);
    }

    if (m_logger) {
      m_logger->Info(
        "Waiting for " + deviceName + "." + pinName +
        " to become " + (targetState ? "ON" : "OFF") +
        " (timeout: " + std::to_string(timeoutMs) + "ms)",
        "IOHandler"
      );
    }

    auto startTime = std::chrono::steady_clock::now();
    bool currentState = false;

    while (true) {
      // Check if timeout
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime
      ).count();

      if (elapsed >= timeoutMs) {
        errorMessage = "WaitForInput timeout waiting for " + pinName;
        return false;
      }

      // Read input state
      if (!ioManager->GetInputByName(deviceName, pinName, currentState)) {
        errorMessage = "WaitForInput failed to read pin: " + pinName;
        return false;
      }

      // Check if we reached target state
      if (currentState == targetState) {
        if (m_logger) {
          m_logger->Info(
            pinName + " reached target state after " + std::to_string(elapsed) + "ms",
            "IOHandler"
          );
        }
        return true;
      }

      // Small delay before next check
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  // ============================================================================
  // UNKNOWN METHOD
  // ============================================================================
  else {
    errorMessage = "Unknown IO method: " + methodName;
    return false;
  }
}