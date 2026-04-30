// CalibYawCorrectionCommands.h
// Family E: corner-pair yaw correction commands.
//   - LoadYawCorrectionData         : load corner1/corner2 + previous yaw
//   - CalculateAndSaveYawCorrection : compute yaw from 2 corners and persist
//   - NavigateToSavedCorner         : move gantry to a saved corner XY (Z held)
//   - ApplyYawCorrection            : compute u_correction from current vs prev
//   - ShowYawCorrectionResult       : surface result back to context
//   - SaveYawCorrectionToPosition   : write u_correction back to a named position
// Owns the `config/SYSTEM/yaw_correction_<uid>.json` file + CalculateYawAngle
// math + the writeback path through ProductPositionManager.
#pragma once

#include "CalibCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class CalibYawCorrectionCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const CalibCommandDeps& deps);
};
