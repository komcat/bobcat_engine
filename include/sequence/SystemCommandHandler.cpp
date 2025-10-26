// SystemCommandHandler.cpp
#include "SystemCommandHandler.h"
#include "LogManager.h"
#include <thread>
#include <chrono>

bool SystemCommandHandler::ExecuteCommand(
  void* manager,
  const std::string& deviceName,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context)
{
  // manager is unused for system commands (can be nullptr)
  (void)manager;
  (void)deviceName;

  // Context can be used for capturing user responses, timing data, etc.
  // Example: UserPrompt can store user's choice in context
  auto captureAsIt = params.find("captureAs");

  // ============================================================================
  // WAIT / DELAY
  // ============================================================================
  if (methodName == "Wait" || methodName == "Delay") {
    auto msIt = params.find("milliseconds");

    if (msIt == params.end()) {
      errorMessage = "Missing required parameter: milliseconds";
      return false;
    }

    int ms = std::stoi(msIt->second);

    if (m_logger) {
      m_logger->Info("Waiting for " + std::to_string(ms) + "ms", "SystemHandler");
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    return true;
  }

  // ============================================================================
  // USER PROMPT (configurable blocking/non-blocking)
  // ============================================================================
  else if (methodName == "UserPrompt") {
    auto titleIt = params.find("title");
    auto messageIt = params.find("message");
    auto blockingIt = params.find("blocking");

    if (titleIt == params.end() || messageIt == params.end()) {
      errorMessage = "Missing required parameters: title, message";
      return false;
    }

    std::string title = titleIt->second;
    std::string message = messageIt->second;
    bool blocking = (blockingIt != params.end() && blockingIt->second == "true");

    if (!m_userPromptCallback) {
      errorMessage = "UserPrompt callback not configured";
      return false;
    }

    if (blocking) {
      // Blocking mode - wait for user response
      if (m_logger) {
        m_logger->Info("Showing blocking user prompt: " + title, "SystemHandler");
      }

      // Call the callback - this will be handled by Qt on the main thread
      bool userConfirmed = m_userPromptCallback(title, message);

      if (!userConfirmed) {
        errorMessage = "User cancelled operation";
        if (m_logger) {
          m_logger->Warning("User cancelled prompt: " + title, "SystemHandler");
        }
        return false;
      }

      if (m_logger) {
        m_logger->Info("User confirmed prompt: " + title, "SystemHandler");
      }
    }
    else {
      // Non-blocking mode - just show message and continue
      if (m_logger) {
        m_logger->Info("Showing non-blocking notification: " + title, "SystemHandler");
      }

      // Still call the callback but ignore the result
      m_userPromptCallback(title, message);

      if (m_logger) {
        m_logger->Info("Non-blocking notification shown", "SystemHandler");
      }
    }

    return true;
  }

  // ============================================================================
  // UNKNOWN METHOD
  // ============================================================================
  else {
    errorMessage = "Unknown system method: " + methodName;
    return false;
  }
}