// SysSwirCommands.h
// SWIR Beam Window control & telemetry — all dispatch through callbacks set
// by the SwirBeamWindowManager:
//   - SaveSwirPackage   : raw + overlay + per-beam crops + collage in one call
//   - GetSwirBeamState  : snapshot live beam metrics into the SequenceContext
//                         under "swir.{i}.*" / "swir.num_beams"
//   - SetBeamDetect     : programmatic toggle for the window's beam-detect box
//   - SetAutoSat        : programmatic toggle for the auto-saturate box
#pragma once

#include "SysCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class SysSwirCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const SysCommandDeps& deps);
};
