// CoordAlignAlignCommands.h
// Family B: LiveCoordinateSystem creation + transform retrieval.
//   - AlignLiveCoordinateSystem : re-execute vision at taught positions →
//                                 compute live frame → store + persist
//   - GetTransform              : retrieve rotation/translate/scale for a uid
// Largest helper — owns ExecuteVisionAtPosition (re-vision pipeline, only
// used here) and the live-system disk persistence.
#pragma once

#include "CoordAlignCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class CoordAlignAlignCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const CoordAlignCommandDeps& deps);
};
