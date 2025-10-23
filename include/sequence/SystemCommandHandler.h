// SystemCommandHandler.h
#pragma once

#include "ICommandHandler.h"
#include <functional>

// Callback type for user prompts - returns true if user confirmed, false if cancelled
using UserPromptCallback = std::function<bool(const std::string& title, const std::string& message)>;

// Callback type for non-blocking notifications - just shows, doesn't wait for response
using UserNotificationCallback = std::function<void(const std::string& title, const std::string& message)>;

class SystemCommandHandler : public ICommandHandler
{
public:
  bool ExecuteCommand(
    void* manager,
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage
  ) override;

  std::string GetManagerType() const override { return "system"; }

  // Set the UI callback for blocking user prompts (must be called from main thread)
  void SetUserPromptCallback(UserPromptCallback callback) {
    m_userPromptCallback = callback;
  }

  // Set the UI callback for non-blocking notifications (must be called from main thread)
  void SetUserNotificationCallback(UserNotificationCallback callback) {
    m_userNotificationCallback = callback;
  }

private:
  UserPromptCallback m_userPromptCallback;
  UserNotificationCallback m_userNotificationCallback;
};