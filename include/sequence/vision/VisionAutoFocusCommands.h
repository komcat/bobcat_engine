// VisionAutoFocusCommands.h
// Family: Z-axis auto-focus via Laplacian variance focus measure.
//   - auto_focus : sweep Z, capture frames, pick the Z that maximises focus, move there.
#pragma once

#include "VisionCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class VisionAutoFocusCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const VisionCommandDeps& deps);

private:
  static bool ExecuteAutoFocus(
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const VisionCommandDeps& deps);
};
