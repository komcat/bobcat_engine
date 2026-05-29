// SysSnapshotCommands.h
// CaptureSnapshot — invoke StateSnapshotManager from a recipe/script.
//   params:
//     label            (string, required if no default) — short name for this snapshot
//     tags             (string, optional)              — comma-separated tags
//     note             (string, optional)              — free-form note
//     source           (string, optional, default "recipe") — "recipe"/"lua"/"ui"
//     capture_image    (string, optional, default "true")   — "true"/"false"/"1"/"0"
//
//   Captures recipe step metadata from the supplied SequenceContext (step
//   index / nickname / recipe name) when available, so the snapshot can be
//   linked back to the run that produced it.
//
//   On success, writes the snapshot path back to context under:
//     snapshot.path, snapshot.label, snapshot.images_count, snapshot.timestamp
#pragma once

#include "SysCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class SysSnapshotCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const SysCommandDeps& deps);
};
