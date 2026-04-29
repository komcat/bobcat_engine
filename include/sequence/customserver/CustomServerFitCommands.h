// CustomServerFitCommands.h
// Family: TCP-based fit operations + state management.
//   - Fit   : run optimization fit
//   - Save  : persist fit results
//   - Reset : clear fit state on the server
#pragma once

#include "CustomServerCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class CustomServerFitCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const CustomServerCommandDeps& deps);

private:
  static bool HandleFit(const std::map<std::string, std::string>& params,
                        std::string& errorMessage,
                        SequenceContext* context,
                        const CustomServerCommandDeps& deps);
  static bool HandleSave(const std::map<std::string, std::string>& params,
                         std::string& errorMessage,
                         SequenceContext* context,
                         const CustomServerCommandDeps& deps);
  static bool HandleReset(std::string& errorMessage,
                          const CustomServerCommandDeps& deps);
};
