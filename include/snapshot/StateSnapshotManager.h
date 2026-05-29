// StateSnapshotManager.h
// Captures a labeled, append-only snapshot of the machine's current state
// (motion positions, TEC/PSU/SMU/lighting readings, IO bits, pneumatic
// positions, DataStore values, active DUT) plus optional camera frames.
//
// Output layout (per snapshot):
//   data/<product>/snapshots/<timestamp>_<label>.json
//   data/<product>/snapshots/<timestamp>_<label>_<cameraId>.png   (one per camera)
//   data/<product>/snapshots/index.jsonl                          (one summary line each)
//
// If no product is loaded, the fallback root is data/_no_product/snapshots/.
//
// All subsystem dumps are best-effort: a failure in one family is logged into
// the snapshot under "skipped"/"errors" and never aborts the snapshot.
#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

struct ApplicationContext;

struct SnapshotOptions {
  // Free-form user label. Sanitized (alnum + "-_") for filesystem use.
  std::string label;

  // Optional tags (e.g. {"debug", "post-glue"}) — stored in JSON + index.
  std::vector<std::string> tags;

  // Optional free-form note.
  std::string note;

  // Where the capture was triggered from: "recipe", "lua", "ui", or custom.
  std::string source = "ui";

  // If true, attempt to save a frame from every connected camera alongside
  // the JSON. Cameras that are not grabbing or fail to capture are listed
  // in the snapshot under "skipped.cameras" rather than failing the call.
  bool capture_image = true;

  // Optional recipe metadata when invoked from a running sequence.
  std::string recipe_name;
  int         recipe_step_index = -1;
  std::string recipe_step_nickname;
};

struct SnapshotResult {
  bool                     success = false;
  std::string              snapshot_path;   // absolute path to JSON
  std::vector<std::string> image_paths;     // absolute paths to saved frames
  std::string              label;
  std::string              timestamp_iso;
  std::string              error;           // populated on failure
};

class StateSnapshotManager {
public:
  explicit StateSnapshotManager(ApplicationContext* ctx);
  ~StateSnapshotManager() = default;

  // Synchronous capture. Returns std::nullopt only on a hard precondition
  // failure (no ApplicationContext); subsystem read failures are captured
  // inline and reported via SnapshotResult.error / .success.
  std::optional<SnapshotResult> Capture(const SnapshotOptions& opts);

  // For diagnostics — returns the root directory the snapshot would write to
  // given the currently-loaded product (or the no-product fallback).
  std::string GetSnapshotRoot() const;

private:
  ApplicationContext* m_ctx;
  std::mutex          m_mutex;       // serialize concurrent captures
  std::string         m_lastDirCreated;  // small optimization across calls

  // Filename helpers
  static std::string SanitizeLabel(const std::string& raw);
  static std::string NowFilenameStamp();
  static std::string NowIso8601();

  // Resolves "<product>": active DUT product first, then ProductPositionManager,
  // then "_no_product".
  std::string ResolveProductName() const;

  // Per-subsystem dumpers. Each writes into the supplied root JSON object
  // and silently records skips/errors under "skipped" / "errors".
  void DumpMeta      (nlohmann::json& root, const SnapshotOptions& opts) const;
  void DumpDut       (nlohmann::json& root) const;
  void DumpMotion    (nlohmann::json& root) const;
  void DumpTec       (nlohmann::json& root) const;
  void DumpPsu       (nlohmann::json& root) const;
  void DumpSmu       (nlohmann::json& root) const;
  void DumpLighting  (nlohmann::json& root) const;
  void DumpIo        (nlohmann::json& root) const;
  void DumpPneumatic (nlohmann::json& root) const;
  void DumpDataStore (nlohmann::json& root) const;

  // Saves a frame from each grabbing camera into the snapshot folder.
  // Returns saved absolute paths; appends skipped camera ids into root["skipped"]["cameras"].
  std::vector<std::string> CaptureCameras(nlohmann::json& root,
                                          const std::string& snapshotDir,
                                          const std::string& baseName);

  // Appends a one-line summary to data/<product>/snapshots/index.jsonl.
  // Never throws; logs to the snapshot's "errors" array on failure.
  void AppendIndexLine(nlohmann::json& root,
                       const std::string& snapshotDir,
                       const SnapshotResult& result,
                       const SnapshotOptions& opts) const;
};
