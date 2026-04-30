// SysDialogCommands.h
// Operator dialog & widget callbacks — all of these block the recipe thread
// while the UI thread shows a Qt widget and waits for the user:
//   - UserPrompt                : OK/Cancel modal (with auto_accept / loop skip)
//   - ManualAdjust              : jog-control dialog for live position tweaks
//   - AskForInput               : text input dialog, stored to a context key
//   - ManualAlignPoint          : crosshair alignment widget
//   - PlacementCorrectionRetrace: PlacementRetraceWidget driven by a training
//                                 set; auto-skipped in loop execution mode.
#pragma once

#include "SysCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class SysDialogCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const SysCommandDeps& deps);
};
