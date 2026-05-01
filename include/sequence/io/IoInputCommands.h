// IoInputCommands.h
// IO read/state-query commands plus input-latch reset:
//   - GetInput     : read a single named input pin
//   - ReadInputs   : read all input bits as a uint64_t bitmask (with latch
//                    bitmask)
//   - ReadOutputs  : read all output bits as a uint64_t bitmask (with status
//                    bitmask)
//   - ClearLatch   : clear the input rising-edge latch bits selected by mask
#pragma once

#include "IoCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class IoInputCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const IoCommandDeps& deps);
};
