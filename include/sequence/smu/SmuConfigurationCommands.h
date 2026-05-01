// SmuConfigurationCommands.h
// Pre-output configuration of an SMU (does not arm/disarm the source):
//   - SetVoltage             : program source voltage (per channel)
//   - SetCurrent             : program source current (per channel)
//   - SetCurrentCompliance   : over-current protection limit
//   - SetVoltageCompliance   : over-voltage protection limit (placeholder —
//                              ISMUDevice has no setter yet, value is stored
//                              in SequenceContext only)
//   - SetCompliance          : backward-compatible alias defaulting to current
//   - SetMode                : Keithley6482 measurement mode
//                              (ACCURATE / FAST / ULTRA_FAST)
#pragma once

#include "SmuCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class SmuConfigurationCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const SmuCommandDeps& deps);
};
