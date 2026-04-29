// MotionPositionFileCommands.h
// Family G: position-file editing commands extracted from MotionCommandHandler.
//   - UpdatePositionAxis : update a single axis of a named position from context
//   - UpdatePositionAll  : snapshot current device position into a named position
#pragma once

#include "MotionCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class MotionPositionFileCommands {
public:
  // Returns std::nullopt if methodName is not owned by this family.
  // Otherwise returns true on success, false on handled failure (errorMessage set).
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const MotionCommandDeps& deps);
};
