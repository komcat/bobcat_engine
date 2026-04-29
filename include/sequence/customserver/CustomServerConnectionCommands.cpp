// CustomServerConnectionCommands.cpp
// Connection family. See CustomServerConnectionCommands.h.
#include "CustomServerConnectionCommands.h"

#include "ApplicationContext.h"
#include "../../customserver/BeamServerClient.h"
#include "LogManager.h"

#include <cstdlib>  // strtol
#include <string>

namespace {

// Exception-free numeric conversion (std::stoi throws on empty/invalid).
// Local copy — same as the file-statics in the orchestrator pre-split. Each
// helper that needs them keeps its own private copy to avoid coupling helpers
// through a shared utility header.
int safeStoi(const std::string& str, int defaultVal = 0) {
  if (str.empty()) return defaultVal;
  char* end = nullptr;
  long val = std::strtol(str.c_str(), &end, 10);
  if (end == str.c_str()) return defaultVal;
  return static_cast<int>(val);
}

}  // namespace

std::optional<bool> CustomServerConnectionCommands::TryExecute(
  const std::string& /*deviceName*/,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* /*context*/,
  const CustomServerCommandDeps& deps)
{
  if (methodName == "Connect") {
    std::string host = "127.0.0.1";
    int port = 5555;

    auto hostIt = params.find("host");
    if (hostIt != params.end() && !hostIt->second.empty()) {
      host = hostIt->second;
    }

    auto portIt = params.find("port");
    if (portIt != params.end() && !portIt->second.empty()) {
      port = safeStoi(portIt->second);
    }

    if (deps.app->logManager) {
      deps.app->logManager->Info("Connecting to beam server at " + host + ":" + std::to_string(port),
                                 "CustomServer");
    }

    if (!deps.app->beamServerClient->Connect(host, port)) {
      errorMessage = "Failed to connect to beam server: " +
                     deps.app->beamServerClient->GetLastError();
      if (deps.app->logManager) deps.app->logManager->Error(errorMessage, "CustomServer");
      return false;
    }

    if (deps.app->logManager) deps.app->logManager->Info("Connected to beam server", "CustomServer");
    return true;
  }

  if (methodName == "Disconnect") {
    deps.app->beamServerClient->Disconnect();
    if (deps.app->logManager) deps.app->logManager->Info("Disconnected from beam server", "CustomServer");
    return true;
  }

  return std::nullopt;
}
