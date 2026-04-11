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
  std::string& errorMessage,
  SequenceContext* /*context*/)
{
  EziIOManager* ioManager = static_cast<EziIOManager*>(manager);

  // Context can be used for capturing input states
  // Example: if (context && params.find("captureAs") != params.end()) {
  //   context->SetValue(params.at("captureAs"), inputState, deviceName, methodName);
  // }

  // ============================================================================
  // SET OUTPUT (by pin name, with fallback to numeric pin)
  // ============================================================================
  if (methodName == "SetOutput") {
    auto pinIt = params.find("pinName");
    auto stateIt = params.find("state");

    if (pinIt == params.end() || stateIt == params.end()) {
      errorMessage = "Missing required parameters: pinName, state";
      return false;
    }

    std::string pinValue = pinIt->second;
    bool state = (stateIt->second == "true");

    // ✅ First try by name
    EziIOError result = ioManager->SetOutputByName(deviceName, pinValue, state);

    // ✅ If failed and pinValue is numeric, try as numeric pin (for backward compatibility)
    if (result != EziIOError::SUCCESS) {
      // Check if pinValue is numeric
      bool isNumeric = true;
      std::string trimmed = pinValue;
      size_t start = trimmed.find_first_not_of(" \t\n\r");
      size_t end = trimmed.find_last_not_of(" \t\n\r");
      if (start != std::string::npos && end != std::string::npos) {
        trimmed = trimmed.substr(start, end - start + 1);
      }

      for (char c : trimmed) {
        if (!std::isdigit(c)) {
          isNumeric = false;
          break;
        }
      }

      if (isNumeric && !trimmed.empty()) {
        // Try as numeric pin
        int pin = std::stoi(trimmed);

        if (m_logger) {
          m_logger->Debug(
            "Pin name '" + pinValue + "' not found, trying as numeric pin " + std::to_string(pin),
            "IOHandler"
          );
        }

        result = ioManager->SetOutput(deviceName, pin, state);
      }
    }

    if (result != EziIOError::SUCCESS) {
      errorMessage = "SetOutput failed: " + GetErrorString(result);
      return false;
    }

    if (m_logger) {
      m_logger->Info(
        "Setting " + deviceName + "." + pinValue + " to " + (state ? "ON" : "OFF"),
        "IOHandler"
      );
    }

    return true;
  }

  // ============================================================================
  // SET OUTPUT NAME (turn ON by name)
  // ============================================================================
  else if (methodName == "SetOutputName") {
    auto pinIt = params.find("pinName");

    if (pinIt == params.end()) {
      errorMessage = "Missing required parameter: pinName";
      return false;
    }

    std::string pinName = pinIt->second;

    if (m_logger) {
      m_logger->Info(
        "Setting " + deviceName + "." + pinName + " to ON",
        "IOHandler"
      );
    }

    EziIOError result = ioManager->SetOutputByName(deviceName, pinName, true);

    if (result != EziIOError::SUCCESS) {
      errorMessage = "SetOutputName failed: " + GetErrorString(result);
      return false;
    }

    return true;
  }

  // ============================================================================
  // CLEAR OUTPUT NAME (turn OFF by name)
  // ============================================================================
  else if (methodName == "ClearOutputName") {
    auto pinIt = params.find("pinName");

    if (pinIt == params.end()) {
      errorMessage = "Missing required parameter: pinName";
      return false;
    }

    std::string pinName = pinIt->second;

    if (m_logger) {
      m_logger->Info(
        "Setting " + deviceName + "." + pinName + " to OFF",
        "IOHandler"
      );
    }

    EziIOError result = ioManager->SetOutputByName(deviceName, pinName, false);

    if (result != EziIOError::SUCCESS) {
      errorMessage = "ClearOutputName failed: " + GetErrorString(result);
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

    bool state = (stateIt->second == "true");
    std::string pinValue = pinIt->second;

    // ✅ Check if pin is numeric or a name
    // If it contains non-digit characters (except leading/trailing spaces), treat as name
    bool isNumeric = true;
    std::string trimmed = pinValue;
    // Simple trim
    size_t start = trimmed.find_first_not_of(" \t\n\r");
    size_t end = trimmed.find_last_not_of(" \t\n\r");
    if (start != std::string::npos && end != std::string::npos) {
      trimmed = trimmed.substr(start, end - start + 1);
    }

    for (char c : trimmed) {
      if (!std::isdigit(c)) {
        isNumeric = false;
        break;
      }
    }

    EziIOError result;

    if (isNumeric && !trimmed.empty()) {
      // Numeric pin - use SetOutput(deviceName, pin, state)
      int pin = std::stoi(trimmed);

      if (m_logger) {
        m_logger->Info(
          "Setting " + deviceName + " pin " + std::to_string(pin) +
          " to " + (state ? "ON" : "OFF"),
          "IOHandler"
        );
      }

      result = ioManager->SetOutput(deviceName, pin, state);
    } else {
      // Named pin - use SetOutputByName(deviceName, pinName, state)
      if (m_logger) {
        m_logger->Info(
          "Setting " + deviceName + "." + pinValue +
          " to " + (state ? "ON" : "OFF"),
          "IOHandler"
        );
      }

      result = ioManager->SetOutputByName(deviceName, pinValue, state);
    }

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
    uint64_t inputs = 0, latch = 0;

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
    uint64_t outputs = 0, status = 0;

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
      m_logger->Debug("Delaying for " + std::to_string(ms) + "ms", "IOHandler");
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

    constexpr int MAX_ITERATIONS = 1000; // Safety bound: 5s timeout / 10ms poll * 2x margin
    for (int iteration = 0; iteration < MAX_ITERATIONS; ++iteration) {
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

    // Safety bound exhausted
    if (m_logger) {
      m_logger->Error("Loop safety bound reached in WaitForInput", "IOHandler");
    }
    errorMessage = "WaitForInput safety bound reached for " + pinName;
    return false;
  }

  // ============================================================================
  // UNKNOWN METHOD
  // ============================================================================
  else {
    errorMessage = "Unknown IO method: " + methodName;
    return false;
  }
}