// MotionBasicCommands.h
// Family A: basic motion primitives extracted from MotionCommandHandler.
//   - MoveToPosition         : single-axis absolute move
//   - MoveToAbs              : multi-axis absolute move (XYZUVW)
//   - MoveRelative           : single-axis relative move
//   - MoveToNamedPosition    : move to a named position
//   - StopAxis               : decelerated stop
//   - EmergencyStop          : immediate stop
//   - HomeAxis               : homing routine
//   - EnableServo            : enable/disable servo loop
//   - WaitForMotionComplete  : block until motion settles
//
// Last family extracted: shares retrace edit mode + InvokeRetraceEditCallback
// + WaitForMotionWithStopCheck + ResolveNumericValue with the orchestrator.
// All shared helpers are reached via deps.parent->.
#pragma once

#include "MotionCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class MotionBasicCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const MotionCommandDeps& deps);
};
