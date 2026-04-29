// MotionParallelCommands.h
// Family F: parallel-move commands extracted from MotionCommandHandler.
//   - ParallelMove          : multiple devices to named/graph positions in parallel
//   - ParallelMoveRelative  : multiple devices by relative distances in parallel
//
// The two ParallelMove[Relative]Spec data types live here (not in the
// orchestrator header) because they have no orchestrator-side consumers since
// the now-removed WaitForMultipleMotionsWithStopCheck function was deleted in
// the Step 10 cleanup.
#pragma once

#include "MotionCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

// Structure for parallel move specification
struct ParallelMoveSpec {
  std::string deviceName;
  std::string moveType;      // "position" or "graphNode"
  std::string targetName;    // Named position or node name
  std::string graphName;     // Only for graphNode type
};

// Structure for parallel relative move specification
struct ParallelRelativeMoveSpec {
  std::string deviceName;
  std::string axis;          // "X", "Y", "Z", "U", "V", "W"
  double distance;           // Distance to move
};

class MotionParallelCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const MotionCommandDeps& deps);
};
