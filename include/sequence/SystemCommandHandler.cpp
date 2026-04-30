// SystemCommandHandler.cpp
// Thin dispatch ladder. Per-family helpers under include/sequence/system/
// own the actual command logic; this file only wires the deps struct, walks
// each family's TryExecute in order, and falls through to "unknown method".
#include "SystemCommandHandler.h"

#include "system/SysCommandDeps.h"
#include "system/SysControlCommands.h"
#include "system/SysDialogCommands.h"
#include "system/SysPersistenceCommands.h"
#include "system/SysSwirCommands.h"

bool SystemCommandHandler::ExecuteCommand(
  void* /*manager*/,
  const std::string& deviceName,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context)
{
  // manager is unused for system commands (can be nullptr).
  // Context can be used for capturing user responses, timing data, etc.

  SysCommandDeps deps{};
  deps.logger = m_logger;   // protected member from ICommandHandler base
  deps.parent = this;

  if (auto r = SysControlCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) {
    return *r;
  }
  if (auto r = SysDialogCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) {
    return *r;
  }
  if (auto r = SysSwirCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) {
    return *r;
  }
  if (auto r = SysPersistenceCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) {
    return *r;
  }

  errorMessage = "Unknown system method: " + methodName;
  return false;
}
