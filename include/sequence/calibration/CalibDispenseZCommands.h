// CalibDispenseZCommands.h
// Family D: per-DUT dispense Z height commands.
//   - LoadDispenseZHeight : read dispense_z_<uid>.json
//   - SaveDispenseZHeight : write the same
// Owns the `config/SYSTEM/dispense_z_<uid>.json` file path + load/save impls.
#pragma once

#include "CalibCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class CalibDispenseZCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const CalibCommandDeps& deps);
};
