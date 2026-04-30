// CalibNeedleZCommands.h
// Family B: global needle-Z calibration commands.
//   - SaveNeedleZCalibration : save needle Z height (system-global)
//   - LoadNeedleZCalibration : load it back
// Owns the `config/SYSTEM/needle_z_calibration.json` file path + load/save
// implementations + delta-vs-previous reporting.
#pragma once

#include "CalibCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class CalibNeedleZCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const CalibCommandDeps& deps);
};
