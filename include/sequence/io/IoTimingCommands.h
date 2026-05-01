// IoTimingCommands.h
// Sleep / poll primitives in the IO domain:
//   - Delay        : sleep for N milliseconds (no IO touched)
//   - WaitForInput : poll an input pin's state in a bounded for-loop until it
//                    matches the target state or a timeout elapses
#pragma once

#include "IoCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class IoTimingCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const IoCommandDeps& deps);
};
