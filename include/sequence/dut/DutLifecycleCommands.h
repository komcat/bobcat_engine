// DutLifecycleCommands.h
// DUT lifecycle: open / close the active DeviceUnderTest stored on
// ApplicationContext::currentDUT.
//   - Create        : new DUT (fails if file already exists for product/SN)
//   - Load          : load existing DUT
//   - LoadOrCreate  : load if exists, else create
//   - Close         : delete the active DUT (in-memory; data is already on disk)
#pragma once

#include "DutCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class DutLifecycleCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const DutCommandDeps& deps);
};
