// SmuOutputMeasureCommands.h
// Runtime ops on a connected SMU:
//   - OutputOn / OutputOff : enable/disable the source output (per channel)
//   - ReadCurrent          : single-shot current measurement on a channel
//   - ReadVoltage          : single-shot voltage measurement on a channel
// Read* both publish to the SequenceContext under
// "<deviceName>.current_chN" / "<deviceName>.voltage_chN" and honour an
// optional "captureAs" parameter for an additional alias.
#pragma once

#include "SmuCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class SmuOutputMeasureCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const SmuCommandDeps& deps);
};
