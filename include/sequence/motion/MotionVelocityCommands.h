// MotionVelocityCommands.h
// Family B: velocity-related commands extracted from MotionCommandHandler.
//   - SetVelocity         : per-axis velocity
//   - SetAllAxisVelocity  : ACS — all installed axes at once
//   - SetSystemVelocity   : PI hexapod system velocity
//   - GetSystemVelocity   : PI hexapod system velocity (captures to context)
#pragma once

#include "MotionCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class MotionVelocityCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const MotionCommandDeps& deps);
};
