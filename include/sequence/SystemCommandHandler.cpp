// SystemCommandHandler.cpp
#include "SystemCommandHandler.h"
#include "SequenceContext.h"
#include "LogManager.h"
#include <thread>
#include <chrono>
#include <iostream>

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
    auto allowCancelIt = params.find("allow_cancel");

    if (titleIt == params.end() || messageIt == params.end()) {
      errorMessage = "Missing required parameters: title, message";
      return false;
    }

    std::string title = titleIt->second;
    std::string message = messageIt->second;
    bool blocking = (blockingIt != params.end() && blockingIt->second == "true");
    bool allowCancel = (allowCancelIt != params.end() && allowCancelIt->second == "true");

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

      // Store result in context if available
      if (context) {
        std::string result = userConfirmed ? "OK" : "Cancel";
        context->SetValue("user_prompt_result", result, "system", "UserPrompt");
        context->SetValue("user_confirmed", userConfirmed, "system", "UserPrompt");
      }

      if (!userConfirmed) {
        if (allowCancel) {
          // Allow cancel mode - continue sequence even if user cancels
          if (m_logger) {
            m_logger->Info("User cancelled prompt (allowed): " + title, "SystemHandler");
          }
          return true;  // Continue sequence
        } else {
          // Traditional mode - stop sequence if user cancels
          errorMessage = "User cancelled operation";
          if (m_logger) {
            m_logger->Warning("User cancelled prompt: " + title, "SystemHandler");
          }
          return false;  // Stop sequence
        }
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
  // MANUAL ADJUST (Manual position adjustment with jog controls)
  // ============================================================================
  else if (methodName == "ManualAdjust") {
    auto messageIt = params.find("message");
    auto deviceIt = params.find("device_name");
    auto stepSizeIt = params.find("step_size_um");
    auto enableXIt = params.find("enable_x");
    auto enableYIt = params.find("enable_y");
    auto enableZIt = params.find("enable_z");

    if (messageIt == params.end() || deviceIt == params.end() || stepSizeIt == params.end()) {
      errorMessage = "Missing required parameters: message, device_name, step_size_um";
      return false;
    }

    std::string message = messageIt->second;
    std::string deviceName = deviceIt->second;
    double stepSizeUm = std::stod(stepSizeIt->second);
    bool enableX = (enableXIt != params.end() && enableXIt->second == "true");
    bool enableY = (enableYIt != params.end() && enableYIt->second == "true");
    bool enableZ = (enableZIt != params.end() && enableZIt->second == "true");

    if (!m_manualAdjustCallback) {
      errorMessage = "ManualAdjust callback not configured";
      return false;
    }

    if (m_logger) {
      m_logger->Info("Showing manual adjustment for device: " + deviceName, "SystemHandler");
    }

    // Call the callback - this will show the ManualAdjustWidget and wait for user
    bool userCompleted = m_manualAdjustCallback(message, deviceName, stepSizeUm, enableX, enableY, enableZ);

    if (!userCompleted) {
      errorMessage = "User cancelled manual adjustment";
      if (m_logger) {
        m_logger->Warning("User cancelled manual adjustment for: " + deviceName, "SystemHandler");
      }
      return false;
    }

    if (m_logger) {
      m_logger->Info("User completed manual adjustment for: " + deviceName, "SystemHandler");
    }

    return true;
  }

  // ============================================================================
  // ASK FOR INPUT (Text input dialog)
  // ============================================================================
  else if (methodName == "AskForInput") {
    auto titleIt = params.find("title");
    auto promptIt = params.find("prompt");
    auto defaultValueIt = params.find("default_value");
    auto contextKeyIt = params.find("context_key");

    if (titleIt == params.end() || promptIt == params.end()) {
      errorMessage = "Missing required parameters: title, prompt";
      return false;
    }

    std::string title = titleIt->second;
    std::string prompt = promptIt->second;
    std::string defaultValue = (defaultValueIt != params.end()) ? defaultValueIt->second : "";
    std::string contextKey = (contextKeyIt != params.end()) ? contextKeyIt->second : "user_input";

    if (!m_userInputCallback) {
      errorMessage = "UserInput callback not configured";
      return false;
    }

    if (m_logger) {
      m_logger->Info("Showing text input dialog: " + title, "SystemHandler");
    }

    // Call the callback - this will show input dialog and wait for user
    auto result = m_userInputCallback(title, prompt, defaultValue);
    bool accepted = result.first;
    std::string inputText = result.second;

    if (!accepted) {
      errorMessage = "User cancelled input";
      if (m_logger) {
        m_logger->Warning("User cancelled input dialog: " + title, "SystemHandler");
      }
      return false;
    }

    // Store result in context
    if (context) {
      context->SetValue(contextKey, inputText, "system", "AskForInput");

      if (m_logger) {
        m_logger->Info("User input stored in context[" + contextKey + "]: " + inputText, "SystemHandler");
      }
    }

    if (m_logger) {
      m_logger->Info("User provided input: " + inputText, "SystemHandler");
    }

    return true;
  }

  // ============================================================================
  // LOG MESSAGE
  // ============================================================================
  else if (methodName == "LogMessage" || methodName == "Log") {
    auto messageIt = params.find("message");
    auto logLevelIt = params.find("log_level");

    if (messageIt == params.end()) {
      errorMessage = "Missing required parameter: message";
      return false;
    }

    std::string message = messageIt->second;
    std::string logLevel = (logLevelIt != params.end()) ? logLevelIt->second : "Info";

    if (m_logger) {
      if (logLevel == "Debug") {
        m_logger->Debug(message, "Recipe");
      }
      else if (logLevel == "Warning") {
        m_logger->Warning(message, "Recipe");
      }
      else if (logLevel == "Error") {
        m_logger->Error(message, "Recipe");
      }
      else {
        m_logger->Info(message, "Recipe");
      }
    }

    return true;
  }

  // ============================================================================
  // STOP SEQUENCE
  // ============================================================================
  else if (methodName == "StopSequence") {
    // Request sequence to stop at next checkpoint
    if (context) {
      context->RequestStop();

      if (m_logger) {
        m_logger->Warning("Sequence stop requested by recipe command", "Recipe");
      }

      std::cout << "[SystemCommandHandler] Sequence stop requested" << std::endl;
    }
    else {
      errorMessage = "No execution context available for StopSequence";
      return false;
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