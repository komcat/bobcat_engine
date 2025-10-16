// ICommandHandler.h
#pragma once

#include <string>
#include <map>

class LogManager;

// Base interface for all command handlers
class ICommandHandler
{
public:
  virtual ~ICommandHandler() = default;

  // Execute a command for a specific device
  // Returns true on success, false on failure
  virtual bool ExecuteCommand(
    void* manager,                              // Manager pointer (cast to specific type in handler)
    const std::string& deviceName,              // Device name (e.g., "hex-left", "IOBottom")
    const std::string& methodName,              // Method name (e.g., "MoveAbsolute", "SetOutput")
    const std::map<std::string, std::string>& params,  // Parameters
    std::string& errorMessage                   // Output: error message if failed
  ) = 0;

  // Get the manager type this handler handles (e.g., "motion", "io", "pneumatic")
  virtual std::string GetManagerType() const = 0;

  // Set logger for this handler
  virtual void SetLogger(LogManager* logger) { m_logger = logger; }

protected:
  LogManager* m_logger = nullptr;
};