// CalibNavigationCommands.h
// Family A: navigation calibration commands extracted from
// CalibrationCommandHandler.
//   - LoadExistingCalibration : load camera-position-only navigation record
//   - CapturePosition         : read current motion position into context
//   - SaveNavigationData      : persist the camera position record to disk
// Owns GetNavigationFilePath / Save / Load implementations for the
// `config/SYSTEM/navigation_<uid>.json` files.
#pragma once

#include "CalibCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class CalibNavigationCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const CalibCommandDeps& deps);
};
