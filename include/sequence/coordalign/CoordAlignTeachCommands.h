// CoordAlignTeachCommands.h
// Family A: TaughtCoordinateSystem lifecycle.
//   - TeachCoordinateSystem  : read 3 fids from context → compute axes → store
//   - ClearCoordinateSystem  : remove taught + its derived live system by uid
//   - ListCoordinateSystems  : list all stored taught systems
// Owns config/<...>/taught_systems_*.json (file path + load/save).
#pragma once

#include "CoordAlignCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class CoordAlignTeachCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const CoordAlignCommandDeps& deps);
};
