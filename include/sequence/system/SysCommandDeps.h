// SysCommandDeps.h
// Shared dependency bundle passed to per-family system command helpers.
// Mirrors the calibration/CalibCommandDeps.h pattern. SystemCommandHandler does
// not own an ApplicationContext — its dependencies are the inherited m_logger
// (ICommandHandler) and a set of UI callbacks that the orchestrator stores as
// member fields. Helpers reach the callbacks via deps.parent->GetXxxCallback().
#pragma once

class LogManager;
class SystemCommandHandler;

struct SysCommandDeps {
  // Copy of the orchestrator's m_logger (inherited from ICommandHandler) at
  // dispatch time. Re-bound on every ExecuteCommand call so SetLogger() still
  // works mid-session.
  LogManager*           logger = nullptr;

  // Callback handle for accessing the orchestrator's UI callback fields via
  // public getters (GetUserPromptCallback, GetSaveSwirPackageCallback, ...).
  SystemCommandHandler* parent = nullptr;
};
