// SmuPollingCommands.h
// Continuous-measurement orchestration via the centralized SMUPollingManager:
//   - StartPolling : begin polling on one channel or "all" enabled channels
//                    at a given interval (ms) and Keithley6482 measurement
//                    mode (ACCURATE / FAST / ULTRA_FAST)
//   - StopPolling  : stop polling on every channel for the device and publish
//                    the total accumulated sample count to the SequenceContext
//                    under "<deviceName>.sampleCount"
#pragma once

#include "SmuCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class SmuPollingCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const SmuCommandDeps& deps);
};
