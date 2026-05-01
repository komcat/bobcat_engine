// IOCommandHandler.cpp
// Thin dispatch ladder. Per-family helpers under include/sequence/io/ own the
// actual command logic; this file pre-casts the manager pointer, builds the
// deps struct, walks each family's TryExecute in order, and falls through to
// "unknown method".
#include "IOCommandHandler.h"

#include "EziIOManager.h"

#include "io/IoCommandDeps.h"
#include "io/IoInputCommands.h"
#include "io/IoOutputCommands.h"
#include "io/IoTimingCommands.h"

bool IOCommandHandler::ExecuteCommand(
  void* manager,
  const std::string& deviceName,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context)
{
  IoCommandDeps deps{};
  deps.logger    = m_logger;   // protected member from ICommandHandler base
  deps.ioManager = static_cast<EziIOManager*>(manager);

  if (auto r = IoOutputCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) {
    return *r;
  }
  if (auto r = IoInputCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) {
    return *r;
  }
  if (auto r = IoTimingCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) {
    return *r;
  }

  errorMessage = "Unknown IO method: " + methodName;
  return false;
}
