// IoOutputCommands.h
// IO output writes (single-pin and bulk):
//   - SetOutput        : write a pin by name, falling back to numeric pin if
//                        the name is unknown and the value parses as digits
//   - SetOutputName    : turn a named output ON
//   - ClearOutputName  : turn a named output OFF
//   - SetOutputPin     : write a pin given either a numeric or named target
//   - SetOutputs       : bulk set/clear via setMask + clearMask
//   - Pulse            : turn a named output ON, sleep N ms, turn it OFF
#pragma once

#include "IoCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class IoOutputCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const IoCommandDeps& deps);
};
