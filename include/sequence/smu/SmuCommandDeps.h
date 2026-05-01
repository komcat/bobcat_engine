// SmuCommandDeps.h
// Shared dependency bundle passed to per-family SMU command helpers. Differs
// from the calibration/system patterns in that the orchestrator pre-flights
// the manager pointer and resolves the device once per ExecuteCommand call,
// so helpers receive ready-to-use SMUManager* and shared_ptr<ISMUDevice>.
#pragma once

#include <memory>

struct ApplicationContext;
class LogManager;
class SMUManager;
class ISMUDevice;

struct SmuCommandDeps {
  // Reach managers via deps.app->smuPollingManager etc. — same instance the
  // orchestrator carries as m_context.
  ApplicationContext*         app        = nullptr;

  // Copy of the orchestrator's m_logger (inherited from ICommandHandler) at
  // dispatch time. Re-bound on every ExecuteCommand call so SetLogger() still
  // works mid-session.
  LogManager*                 logger     = nullptr;

  // Pre-cast manager pointer — already null-checked by the orchestrator.
  SMUManager*                 smuManager = nullptr;

  // Pre-resolved device for `deviceName` — already non-null. Held by
  // shared_ptr so helpers don't need to re-look up; ownership stays with
  // SMUManager.
  std::shared_ptr<ISMUDevice> device;
};
