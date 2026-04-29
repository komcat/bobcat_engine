// CustomServerDetectionCommands.h
// Family: TCP-based beam detection — live + file + folder pipelines.
//   - Detect       : single-shot detect on current camera frame
//   - DetectN      : N consecutive detections (averaging)
//   - DetectFile   : detect on a single image file
//   - DetectFolder : batch detect across a folder
#pragma once

#include "CustomServerCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class CustomServerDetectionCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const CustomServerCommandDeps& deps);

private:
  static bool HandleDetect(const std::map<std::string, std::string>& params,
                           std::string& errorMessage,
                           SequenceContext* context,
                           const CustomServerCommandDeps& deps);
  static bool HandleDetectN(const std::map<std::string, std::string>& params,
                            std::string& errorMessage,
                            SequenceContext* context,
                            const CustomServerCommandDeps& deps);
  static bool HandleDetectFile(const std::map<std::string, std::string>& params,
                               std::string& errorMessage,
                               SequenceContext* context,
                               const CustomServerCommandDeps& deps);
  static bool HandleDetectFolder(const std::map<std::string, std::string>& params,
                                 std::string& errorMessage,
                                 SequenceContext* context,
                                 const CustomServerCommandDeps& deps);
};
