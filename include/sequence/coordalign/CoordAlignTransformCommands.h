// CoordAlignTransformCommands.h
// Family D: apply transformations.
//   - TransformPoint  : raw local x/y → world coords (no point storage)
//   - MoveToLocalXY   : transform local x/y → world → motion-manager move
// Verb-on-existing-data; doesn't own a data type, just consumes the live
// system + math.
#pragma once

#include "CoordAlignCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class CoordAlignTransformCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const CoordAlignCommandDeps& deps);
};
