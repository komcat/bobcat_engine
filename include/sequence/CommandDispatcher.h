// CommandDispatcher.h
#pragma once

#include <string>
#include <map>
#include <memory>
#include "ICommandHandler.h"

class SequenceContext;

class CommandDispatcher
{
public:
  CommandDispatcher();
  ~CommandDispatcher();

  // Register a command handler for a manager type
  void RegisterHandler(const std::string& managerType, std::unique_ptr<ICommandHandler> handler);

  // Execute a command with execution context
  bool ExecuteCommand(
    const std::string& managerType,
    void* manager,
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context = nullptr
  );

  // Check if a handler is registered for a manager type
  bool HasHandler(const std::string& managerType) const;

  // Set logger for all handlers
  void SetLogger(LogManager* logger);

private:
  std::map<std::string, std::unique_ptr<ICommandHandler>> m_handlers;
};