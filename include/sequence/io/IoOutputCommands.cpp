// IoOutputCommands.cpp — see header for description.
#include "IoOutputCommands.h"

#include "EziIOManager.h"
#include "LogManager.h"

#include <cctype>
#include <chrono>
#include <cstdint>
#include <thread>

namespace {

// Trim leading/trailing whitespace and report whether the result parses as
// a positive decimal integer. Returns isNumeric=true and trimmed.empty()=true
// for an all-whitespace input — callers must combine the two checks
// (isNumeric && !trimmed.empty()) to gate the numeric path, mirroring the
// original inline logic in SetOutput / SetOutputPin.
struct PinParseResult { std::string trimmed; bool isNumeric; };

PinParseResult ParsePinName(const std::string& pinValue)
{
  std::string trimmed = pinValue;
  size_t start = trimmed.find_first_not_of(" \t\n\r");
  size_t end = trimmed.find_last_not_of(" \t\n\r");
  if (start != std::string::npos && end != std::string::npos) {
    trimmed = trimmed.substr(start, end - start + 1);
  }

  bool isNumeric = true;
  for (char c : trimmed) {
    if (!std::isdigit(static_cast<unsigned char>(c))) {
      isNumeric = false;
      break;
    }
  }
  return {trimmed, isNumeric};
}

}  // namespace

std::optional<bool> IoOutputCommands::TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* /*context*/,
    const IoCommandDeps& deps)
{
  LogManager*   m_logger  = deps.logger;
  EziIOManager* ioManager = deps.ioManager;

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
      auto [trimmed, isNumeric] = ParsePinName(pinValue);

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
  if (methodName == "SetOutputName") {
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
  if (methodName == "ClearOutputName") {
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
  if (methodName == "SetOutputPin") {
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
    auto [trimmed, isNumeric] = ParsePinName(pinValue);

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
  // SET MULTIPLE OUTPUTS (bulk operation)
  // ============================================================================
  if (methodName == "SetOutputs") {
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
  // PULSE OUTPUT (turn on, wait, turn off)
  // ============================================================================
  if (methodName == "Pulse") {
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

  return std::nullopt;  // not our family
}
