// VisionContinuousCommands.h
// Family: continuous-mode + CV-inject vision commands extracted from VisionCommandHandler.
//   - start_continuous   : start GdiCameraWindow continuous vision processing
//   - stop_continuous    : stop the same
//   - enable_cv_inject   : enable cv.* context injection from continuous vision
//   - disable_cv_inject  : disable the same
#pragma once

#include "VisionCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class VisionContinuousCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const VisionCommandDeps& deps);
};
