// IoCommandDeps.h
// Shared dependency bundle passed to per-family IO command helpers. The
// orchestrator pre-casts the void* manager to EziIOManager* and copies its
// inherited m_logger pointer once per dispatch so helpers don't need to redo
// either piece of plumbing.
#pragma once

class LogManager;
class EziIOManager;

struct IoCommandDeps {
  // Copy of the orchestrator's m_logger (inherited from ICommandHandler) at
  // dispatch time. Re-bound on every ExecuteCommand call so SetLogger() still
  // works mid-session.
  LogManager*   logger    = nullptr;

  // Pre-cast manager pointer. Not null-checked by the orchestrator — the
  // original IOCommandHandler unconditionally trusted this pointer, and
  // verbatim move preserves that contract.
  EziIOManager* ioManager = nullptr;
};
