// CustomServerCommandHandler.cpp
//
// Orchestrator for BeamServer / beam-detection commands. Per-family logic
// lives in include/sequence/customserver/Custom*.{h,cpp}; this file only
// holds the dispatch ladder + the top-level guards (m_context + the TCP
// beamServerClient null check).
#include "CustomServerCommandHandler.h"

// Per-family helpers (under include/sequence/customserver/)
#include "customserver/CustomServerAnalysisCommands.h"   // native handlers (no TCP)
#include "customserver/CustomServerConnectionCommands.h" // Connect / Disconnect
#include "customserver/CustomServerFitCommands.h"        // Fit / Save / Reset
#include "customserver/CustomServerDetectionCommands.h"  // Detect / DetectN / DetectFile / DetectFolder

#include "ApplicationContext.h"        // m_context->beamServerClient null check
#include "customserver/BeamServerClient.h"

bool CustomServerCommandHandler::ExecuteCommand(
  void* /*manager*/,
  const std::string& deviceName,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context)
{
  if (!m_context) {
    errorMessage = "ApplicationContext not available";
    return false;
  }

  // Shared dependency bundle for split helper families.
  CustomServerCommandDeps deps{};
  deps.app    = m_context;
  deps.parent = this;

  // ------------------------------------------------------------------
  // Native handlers (no TCP server needed) — checked FIRST so callers
  // can run analysis even when the beam server isn't connected.
  // ------------------------------------------------------------------
  if (auto r = CustomServerAnalysisCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) {
    return *r;
  }

  // ------------------------------------------------------------------
  // TCP-based handlers require beam server client
  // ------------------------------------------------------------------
  if (!m_context->beamServerClient) {
    errorMessage = "BeamServerClient not available";
    return false;
  }

  // Connection family (Connect, Disconnect)
  if (auto r = CustomServerConnectionCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) {
    return *r;
  }
  // Fit family (Fit, Save, Reset)
  if (auto r = CustomServerFitCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) {
    return *r;
  }
  // Detection family (Detect, DetectN, DetectFile, DetectFolder)
  if (auto r = CustomServerDetectionCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) {
    return *r;
  }

  errorMessage = "Unknown method: " + methodName;
  return false;
}
