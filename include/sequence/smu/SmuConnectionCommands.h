// SmuConnectionCommands.h
// SMU connection lifecycle:
//   - Connect    : open the device's transport (VISA/serial/etc.)
//   - Disconnect : close the transport and release any device-side state
#pragma once

#include "SmuCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class SmuConnectionCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const SmuCommandDeps& deps);
};
