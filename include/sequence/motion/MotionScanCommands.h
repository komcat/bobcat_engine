// MotionScanCommands.h
// Family C: 1D adaptive-scan commands extracted from MotionCommandHandler.
//   - XCAN          : preset-mode hexapod adaptive scanning
//   - XCANExtended  : flexible adaptive scanning with custom parameters
//   - AxisScan      : 1D scan on a single axis with peak/valley detection
#pragma once

#include "MotionCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class MotionScanCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const MotionCommandDeps& deps);
};
