// CoordAlignLocalPointCommands.h
// Family C: TaughtLocalPoint lifecycle.
//   - TeachLocalPoint     : capture current device position, convert to local
//   - TransformLocalPoint : load stored point + live system → world coords
//   - ListLocalPoints     : list all stored local points
//   - ClearLocalPoint     : remove a local point by uid
// Owns the local-point map operations + disk persistence.
#pragma once

#include "CoordAlignCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class CoordAlignLocalPointCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const CoordAlignCommandDeps& deps);
};
