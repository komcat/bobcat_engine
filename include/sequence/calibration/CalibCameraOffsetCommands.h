// CalibCameraOffsetCommands.h
// Family C: camera-to-needle offset commands.
//   - LoadCameraToNeedleOffset : read needlexyoffset.json (.local override)
//   - SaveCameraToNeedleOffset : write needlexyoffset.local.json
// Owns the `config/SYSTEM/needlexyoffset[.local].json` paths + the local-first
// override pattern (read prefers .local, write always targets .local so the
// factory default is preserved).
#pragma once

#include "CalibCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class CalibCameraOffsetCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const CalibCommandDeps& deps);
};
