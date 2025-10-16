// CommandDispatcher.cpp
#include "CommandDispatcher.h"
#include "LogManager.h"

CommandDispatcher::CommandDispatcher()
{
}

CommandDispatcher::~CommandDispatcher()
{
}

void CommandDispatcher::RegisterHandler(const std::string& managerType, std::unique_ptr<ICommandHandler> handler)
{
  m_handlers[managerType] = std::move(handler);
}

bool CommandDispatcher::ExecuteCommand(
  const std::string& managerType,
  void* manager,
  const std::string& deviceName,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage)
{
  auto it = m_handlers.find(managerType);
  if (it == m_handlers.end()) {
    errorMessage = "No handler registered for manager type: " + managerType;
    return false;
  }

  return it->second->ExecuteCommand(manager, deviceName, methodName, params, errorMessage);
}

void CommandDispatcher::SetLogger(LogManager* logger)
{
  for (auto& pair : m_handlers) {
    pair.second->SetLogger(logger);
  }
}