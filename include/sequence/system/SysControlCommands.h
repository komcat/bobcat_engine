// SysControlCommands.h
// Sequence flow & diagnostics primitives:
//   - Wait / Delay     : interruptible sleep with progress notifications
//   - StopSequence     : request the engine to stop at the next checkpoint
//   - LogMessage / Log : emit a line to the LogManager at a chosen level
#pragma once

#include "SysCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class SysControlCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const SysCommandDeps& deps);
};
