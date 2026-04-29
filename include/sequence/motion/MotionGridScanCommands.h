// MotionGridScanCommands.h
// Family D: 2D grid-scan commands extracted from MotionCommandHandler.
//   - GridScan            : preset modes (coarse/fine/ultrafine)
//   - GridScanExtended    : full-control 2D grid scan
//   - GridScanDualChannel : two-channel scan for PIC waveguide alignment
//
// Owns its own JSON+text result-file output via private static helpers in the .cpp.
#pragma once

#include "MotionCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class MotionGridScanCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const MotionCommandDeps& deps);
};
