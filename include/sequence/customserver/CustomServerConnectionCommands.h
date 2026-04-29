// CustomServerConnectionCommands.h
// Family: BeamServer connection management.
//   - Connect    : open TCP connection to beam server
//   - Disconnect : close it
#pragma once

#include "CustomServerCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class CustomServerConnectionCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const CustomServerCommandDeps& deps);
};
