// DUTCommandHandler.cpp
// Thin dispatch ladder. Per-family helpers under include/sequence/dut/ own the
// actual command logic; this file builds the deps struct, walks each family's
// TryExecute in order, and falls through to "unknown method".
#include "DUTCommandHandler.h"

#include "LogManager.h"

#include "dut/DutCommandDeps.h"
#include "dut/DutLifecycleCommands.h"
#include "dut/DutInsertCommands.h"
#include "dut/DutDeleteCommands.h"

#include <iostream>

DUTCommandHandler::DUTCommandHandler(ApplicationContext* context)
  : m_context(context)
{
  if (!m_context) {
    std::cerr << "[DUTCommandHandler] Error: ApplicationContext cannot be null" << std::endl;
    m_valid = false;
    return;
  }
  std::cout << "[DUTCommandHandler] Created" << std::endl;
}

bool DUTCommandHandler::ExecuteCommand(
  void* /*manager*/,
  const std::string& deviceName,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context)
{
  DutCommandDeps deps{};
  deps.app    = m_context;
  deps.parent = this;

  if (auto r = DutLifecycleCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) {
    return *r;
  }
  if (auto r = DutInsertCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) {
    return *r;
  }
  if (auto r = DutDeleteCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) {
    return *r;
  }

  errorMessage = "Unknown DUT method: " + methodName;
  return false;
}

bool DUTCommandHandler::EnsureDUTActive(std::string& errorMessage) const
{
  if (m_context->currentDUT == nullptr) {
    errorMessage = "No active DUT. Create or load a DUT first.";
    if (m_logger) {
      m_logger->Error(errorMessage, "DUT");
    }
    return false;
  }
  return true;
}

std::string DUTCommandHandler::GetParam(const std::map<std::string, std::string>& params, const std::string& key) const
{
  auto it = params.find(key);
  if (it != params.end()) {
    return it->second;
  }
  return "";
}
