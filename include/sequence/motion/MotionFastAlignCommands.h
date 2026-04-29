// MotionFastAlignCommands.h
// Family I: PI hexapod fast-align commands extracted from MotionCommandHandler.
//   - FastAlignXY  : 2D FSM scan via direct PIController access
//   - FastAlignZXY : Z-iterated FSM with best-Z tracking
//
// Both commands talk directly to PIController (bypassing UnifiedMotionManager
// abstraction) because they invoke FSM/GetFastScanStatus/GetAnalogVoltage,
// which are PI-only primitives.
#pragma once

#include "MotionCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class MotionFastAlignCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const MotionCommandDeps& deps);
};
