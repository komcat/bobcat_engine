// IoInputCommands.cpp — see header for description.
#include "IoInputCommands.h"

#include "EziIOManager.h"
#include "LogManager.h"

#include <cstdint>

std::optional<bool> IoInputCommands::TryExecute(
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
  // GET INPUT (by pin name)
  // ============================================================================
  if (methodName == "GetInput") {
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
  if (methodName == "ReadInputs") {
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
  if (methodName == "ReadOutputs") {
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
  // CLEAR LATCH
  // ============================================================================
  if (methodName == "ClearLatch") {
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

  return std::nullopt;  // not our family
}
