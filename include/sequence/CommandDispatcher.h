// CommandDispatcher.h
#pragma once

#include <string>
#include <map>
#include <memory>
#include "ICommandHandler.h"

class CommandDispatcher
{
public:
  CommandDispatcher();
  ~CommandDispatcher();

  // Register a command handler for a manager type
  void RegisterHandler(const std::string& managerType, std::unique_ptr<ICommandHandler> handler);

  // Execute a command
  bool ExecuteCommand(
    const std::string& managerType,
    void* manager,
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage
  );

  // Set logger for all handlers
  void SetLogger(LogManager* logger);

private:
  std::map<std::string, std::unique_ptr<ICommandHandler>> m_handlers;
};