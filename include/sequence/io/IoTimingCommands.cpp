// IoTimingCommands.cpp — see header for description.
#include "IoTimingCommands.h"

#include "EziIOManager.h"
#include "LogManager.h"

#include <chrono>
#include <thread>

std::optional<bool> IoTimingCommands::TryExecute(
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
  // DELAY (simple wait)
  // ============================================================================
  if (methodName == "Delay") {
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
  if (methodName == "WaitForInput") {
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

  return std::nullopt;  // not our family
}
