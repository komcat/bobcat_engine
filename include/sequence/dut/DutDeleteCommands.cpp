// DutDeleteCommands.cpp — see header for description.
#include "DutDeleteCommands.h"

#include "../DUTCommandHandler.h"
#include "ApplicationContext.h"
#include "LogManager.h"
#include "dut/DeviceUnderTest.h"

#include <string>

std::optional<bool> DutDeleteCommands::TryExecute(
    const std::string& /*deviceName*/,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* /*context*/,
    const DutCommandDeps& deps)
{
  LogManager* m_logger = deps.app ? deps.app->logManager : nullptr;

  // ============================================================================
  // DELETE ENTRY
  // ============================================================================
  if (methodName == "DeleteEntry") {
    if (!deps.parent->EnsureDUTActive(errorMessage)) {
      return false;
    }

    std::string timestamp = deps.parent->GetParam(params, "timestamp");

    if (timestamp.empty()) {
      errorMessage = "Missing required parameter: timestamp";
      return false;
    }

    bool deleted = deps.app->currentDUT->deleteEntry(timestamp);

    if (!deleted) {
      errorMessage = "Entry not found with timestamp: " + timestamp;
      if (m_logger) {
        m_logger->Warning(errorMessage, "DUT");
      }
      return false;
    }

    if (m_logger) {
      m_logger->Debug("DUT Deleted entry: " + timestamp, "DUT");
    }
    return true;
  }

  // ============================================================================
  // DELETE ENTRIES BY KEY
  // ============================================================================
  if (methodName == "DeleteEntriesByKey") {
    if (!deps.parent->EnsureDUTActive(errorMessage)) {
      return false;
    }

    std::string key = deps.parent->GetParam(params, "key");

    if (key.empty()) {
      errorMessage = "Missing required parameter: key";
      return false;
    }

    int deletedCount = deps.app->currentDUT->deleteEntriesByKey(key);

    if (deletedCount == -1) {
      errorMessage = "Failed to delete entries by key (save error): " + key;
      if (m_logger) {
        m_logger->Error(errorMessage, "DUT");
      }
      return false;
    }

    if (deletedCount == 0) {
      errorMessage = "No entries found with key: " + key;
      if (m_logger) {
        m_logger->Warning(errorMessage, "DUT");
      }
      return false;
    }

    if (m_logger) {
      m_logger->Debug("DUT Deleted " + std::to_string(deletedCount) + " entries with key: " + key, "DUT");
    }
    return true;
  }

  return std::nullopt;  // not our family
}
