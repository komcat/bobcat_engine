// SysControlCommands.cpp — see header for description.
#include "SysControlCommands.h"

#include "LogManager.h"
#include "SequenceContext.h"

#include <chrono>
#include <iostream>
#include <thread>

std::optional<bool> SysControlCommands::TryExecute(
    const std::string& /*deviceName*/,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const SysCommandDeps& deps)
{
  LogManager* logger = deps.logger;

  // ============================================================================
  // WAIT / DELAY
  // ============================================================================
  if (methodName == "Wait" || methodName == "Delay") {
    auto msIt = params.find("milliseconds");

    if (msIt == params.end()) {
      errorMessage = "Missing required parameter: milliseconds";
      return false;
    }

    int totalMs = std::stoi(msIt->second);

    if (logger) {
      logger->Debug("Waiting for " + std::to_string(totalMs) + "ms", "SystemHandler");
    }

    // Use interruptible wait with progress updates
    const int updateIntervalMs = 100;  // Update UI every 100ms
    int remainingMs = totalMs;

    auto startTime = std::chrono::steady_clock::now();

    while (remainingMs > 0) {
      // Check stop flag - allow immediate cancellation
      if (context && context->IsStopRequestedAny()) {
        if (logger) {
          logger->Warning("Wait interrupted by user stop request", "SystemHandler");
        }
        return false;  // Stop execution
      }

      // Notify progress callback for UI update
      if (context) {
        context->NotifyWaitProgress(remainingMs, totalMs);
      }

      // Sleep for the smaller of: remaining time or update interval
      int sleepMs = (remainingMs < updateIntervalMs) ? remainingMs : updateIntervalMs;
      std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));

      // Calculate actual remaining time (more accurate than just subtracting)
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime
      ).count();
      remainingMs = totalMs - static_cast<int>(elapsed);
    }

    // Final notification with 0 remaining
    if (context) {
      context->NotifyWaitProgress(0, totalMs);
    }

    if (logger) {
      logger->Debug("Wait completed", "SystemHandler");
    }

    return true;
  }

  // ============================================================================
  // LOG MESSAGE
  // ============================================================================
  if (methodName == "LogMessage" || methodName == "Log") {
    auto messageIt = params.find("message");
    auto logLevelIt = params.find("log_level");

    if (messageIt == params.end()) {
      errorMessage = "Missing required parameter: message";
      return false;
    }

    std::string message = messageIt->second;
    std::string logLevel = (logLevelIt != params.end()) ? logLevelIt->second : "Info";

    if (logger) {
      if (logLevel == "Debug") {
        logger->Debug(message, "Recipe");
      }
      else if (logLevel == "Warning") {
        logger->Warning(message, "Recipe");
      }
      else if (logLevel == "Error") {
        logger->Error(message, "Recipe");
      }
      else {
        logger->Info(message, "Recipe");
      }
    }

    return true;
  }

  // ============================================================================
  // STOP SEQUENCE
  // ============================================================================
  if (methodName == "StopSequence") {
    // Request sequence to stop at next checkpoint
    if (context) {
      context->RequestStop();

      if (logger) {
        logger->Warning("Sequence stop requested by recipe command", "Recipe");
      }

      std::cout << "[SystemCommandHandler] Sequence stop requested" << std::endl;
    }
    else {
      errorMessage = "No execution context available for StopSequence";
      return false;
    }

    return true;
  }

  return std::nullopt;  // not our family
}
