// CustomServerAnalysisCommands.h
// Family: "native" analysis handlers — no BeamServerClient/TCP required.
//   - FitSlices            : fit a series of D4-sigma slices (Gaussian beam fit)
//   - D4SigmaZScanFolder   : process a folder of Z-scan images through the
//                            vision pipeline (HAS_OPENCV-gated at runtime)
#pragma once

#include "CustomServerCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class CustomServerAnalysisCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const CustomServerCommandDeps& deps);

private:
  static bool HandleFitSlices(const std::map<std::string, std::string>& params,
                              std::string& errorMessage,
                              SequenceContext* context,
                              const CustomServerCommandDeps& deps);
  static bool HandleD4SigmaZScanFolder(const std::map<std::string, std::string>& params,
                                       std::string& errorMessage,
                                       SequenceContext* context,
                                       const CustomServerCommandDeps& deps);
};
