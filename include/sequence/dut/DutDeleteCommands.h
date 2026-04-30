// DutDeleteCommands.h
// DUT data removal:
//   - DeleteEntry        : remove a single entry by its timestamp string
//   - DeleteEntriesByKey : remove every entry whose top-level key matches
#pragma once

#include "DutCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class DutDeleteCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const DutCommandDeps& deps);
};
