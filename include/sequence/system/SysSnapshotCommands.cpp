// SysSnapshotCommands.cpp — see header for description.
#include "SysSnapshotCommands.h"

#include "SystemCommandHandler.h"
#include "StateSnapshotManager.h"
#include "SequenceContext.h"
#include "LogManager.h"

#include <sstream>

namespace {

// Split a comma-separated tag string into trimmed non-empty tokens.
std::vector<std::string> SplitTags(const std::string& raw) {
  std::vector<std::string> out;
  std::stringstream ss(raw);
  std::string item;
  while (std::getline(ss, item, ',')) {
    // trim whitespace
    size_t b = item.find_first_not_of(" \t\r\n");
    size_t e = item.find_last_not_of(" \t\r\n");
    if (b == std::string::npos) continue;
    out.push_back(item.substr(b, e - b + 1));
  }
  return out;
}

bool ParseBool(const std::string& s, bool defaultVal) {
  if (s.empty()) return defaultVal;
  return (s == "true" || s == "1" || s == "TRUE" || s == "yes");
}

std::string GetParam(const std::map<std::string, std::string>& p,
                     const std::string& key, const std::string& def = "") {
  auto it = p.find(key);
  return (it == p.end()) ? def : it->second;
}

} // namespace

std::optional<bool> SysSnapshotCommands::TryExecute(
    const std::string& /*deviceName*/,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const SysCommandDeps& deps)
{
  if (methodName != "CaptureSnapshot") {
    return std::nullopt;
  }

  if (!deps.parent) {
    errorMessage = "CaptureSnapshot: missing SystemCommandHandler parent";
    return false;
  }
  StateSnapshotManager* mgr = deps.parent->GetStateSnapshotManager();
  if (!mgr) {
    errorMessage = "CaptureSnapshot: StateSnapshotManager not wired";
    return false;
  }

  SnapshotOptions opts;
  opts.label         = GetParam(params, "label");
  opts.tags          = SplitTags(GetParam(params, "tags"));
  opts.note          = GetParam(params, "note");
  opts.source        = GetParam(params, "source", "recipe");
  opts.capture_image = ParseBool(GetParam(params, "capture_image", "true"), true);

  // Auto-populate recipe metadata from the live SequenceContext when present.
  // Callers from the UI / a free-running script will pass nullptr and skip
  // these fields — that's fine; they just won't appear in the snapshot.
  if (context) {
    opts.recipe_name         = context->GetSequenceName();
    opts.recipe_step_index   = context->GetCurrentStepIndex();
    opts.recipe_step_nickname = context->GetCurrentStepName();
  }

  auto result = mgr->Capture(opts);
  if (!result || !result->success) {
    errorMessage = "CaptureSnapshot: " +
      (result ? result->error : std::string("manager returned no result"));
    if (deps.logger) {
      deps.logger->Error(errorMessage, "Snapshot");
    }
    return false;
  }

  // Publish the saved path back to the context so downstream steps (e.g.
  // a UserPrompt showing "snapshot saved at ...") can read it.
  if (context) {
    context->SetValue("snapshot.path",          result->snapshot_path);
    context->SetValue("snapshot.label",         result->label);
    context->SetValue("snapshot.timestamp",     result->timestamp_iso);
    context->SetValue("snapshot.images_count",
                      static_cast<float>(result->image_paths.size()));
  }

  if (deps.logger) {
    deps.logger->Info("Snapshot saved: " + result->snapshot_path, "Snapshot");
  }
  return true;
}
