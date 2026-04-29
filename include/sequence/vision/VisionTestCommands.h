// VisionTestCommands.h
// Family: test/scaffolding vision commands extracted from VisionCommandHandler.
//   - load_image_from_file : load an image from disk into the vision pipeline
#pragma once

#include "VisionCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class VisionTestCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const VisionCommandDeps& deps);
};
