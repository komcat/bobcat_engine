// MotionGraphContextCommands.h
// Family E: graph- and context-driven motion commands extracted from MotionCommandHandler.
//   - MoveToGraphNode       : graph-based path planning
//   - MoveToContextPosition : move to world coords from vision context
//   - SavePositionToContext : capture current device position into context
//   - MoveToAbsFromContext  : move to absolute position stored in context
//   - MoveToContextXY       : XY-only move from context, preserves Z
#pragma once

#include "MotionCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class MotionGraphContextCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const MotionCommandDeps& deps);
};
