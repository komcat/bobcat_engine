// SMUCommandHandler.cpp
// Thin dispatch ladder. Per-family helpers under include/sequence/smu/ own
// the actual command logic; this file does the manager+device pre-flight,
// builds the deps struct, walks each family's TryExecute in order, and falls
// through to "unknown method".
#include "SMUCommandHandler.h"

#include "ApplicationContext.h"
#include "SMUManager.h"

#include "smu/SmuCommandDeps.h"
#include "smu/SmuConfigurationCommands.h"
#include "smu/SmuConnectionCommands.h"
#include "smu/SmuOutputMeasureCommands.h"
#include "smu/SmuPollingCommands.h"

bool SMUCommandHandler::ExecuteCommand(
  void* manager,
  const std::string& deviceName,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context)
{
  SMUManager* smuManager = static_cast<SMUManager*>(manager);
  if (!smuManager) {
    errorMessage = "SMUManager is null";
    return false;
  }

  // Get the device
  auto device = smuManager->GetDevice(deviceName);
  if (!device) {
    errorMessage = "SMU device not found: " + deviceName;
    return false;
  }

  SmuCommandDeps deps{};
  deps.app        = m_context;
  deps.logger     = m_logger;   // protected member from ICommandHandler base
  deps.smuManager = smuManager;
  deps.device     = device;

  if (auto r = SmuConnectionCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) {
    return *r;
  }
  if (auto r = SmuOutputMeasureCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) {
    return *r;
  }
  if (auto r = SmuConfigurationCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) {
    return *r;
  }
  if (auto r = SmuPollingCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) {
    return *r;
  }

  errorMessage = "Unknown SMU method: " + methodName;
  return false;
}
