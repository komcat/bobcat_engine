// SmuConnectionCommands.cpp — see header for description.
#include "SmuConnectionCommands.h"

#include "LogManager.h"
#include "SMUManager.h"

std::optional<bool> SmuConnectionCommands::TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& /*params*/,
    std::string& errorMessage,
    SequenceContext* /*context*/,
    const SmuCommandDeps& deps)
{
  LogManager* m_logger = deps.logger;
  auto& device = deps.device;

  // ============================================================================
  // CONNECT
  // ============================================================================
  if (methodName == "Connect") {
    if (m_logger) {
      m_logger->Info("Connecting to SMU device: " + deviceName, "SMUHandler");
    }

    bool success = device->Connect();

    if (!success) {
      errorMessage = "Failed to connect to SMU device: " + deviceName + " - " + device->GetLastError();
      return false;
    }

    if (m_logger) {
      m_logger->Info("Successfully connected to SMU device: " + deviceName, "SMUHandler");
    }
    return true;
  }

  // ============================================================================
  // DISCONNECT
  // ============================================================================
  if (methodName == "Disconnect") {
    if (m_logger) {
      m_logger->Info("Disconnecting from SMU device: " + deviceName, "SMUHandler");
    }

    device->Disconnect();

    if (m_logger) {
      m_logger->Info("Disconnected from SMU device: " + deviceName, "SMUHandler");
    }
    return true;
  }

  return std::nullopt;  // not our family
}
