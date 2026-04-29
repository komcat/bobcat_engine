// MotionLinkedDeviceCommands.h
// Family H: linked-device commands extracted from MotionCommandHandler.
//   - RegisterDeviceLink   : register a primary↔linked device offset
//   - CompensateDeviceLink : move linked device to restore the registered offset
#pragma once

#include "MotionCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class MotionLinkedDeviceCommands {
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
