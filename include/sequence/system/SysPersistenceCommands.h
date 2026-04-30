// SysPersistenceCommands.h
// Filesystem-side data writers:
//   - SaveLastCaptureToFile : find the most recently captured frame in the
//                             SequenceContext and persist it as a PNG.
//   - CropROI               : center-crop a frame from the context (optionally
//                             saving the crop) and store the result back.
//   - AppendCsv             : append a row (with optional header on first
//                             write) to a CSV file under data/.
#pragma once

#include "SysCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class SysPersistenceCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const SysCommandDeps& deps);
};
