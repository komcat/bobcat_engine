// DutInsertCommands.h
// DUT data ingest:
//   - InsertString / InsertDouble / InsertInteger : append a typed entry
//   - InsertFromContext       : copy a single context value into a DUT entry
//                               (auto/string/double/integer coercion)
//   - BulkInsertFromContext   : sweep all context keys with a given prefix into
//                               one nlohmann::json object entry
//   - InsertAllFromDataStore  : snapshot all DataStore channels into one
//                               nlohmann::json object entry (optional prefix
//                               filter)
#pragma once

#include "DutCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class DutInsertCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const DutCommandDeps& deps);
};
