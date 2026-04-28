// EdgeTransferRetrace.h
// Shared types for the Edge Transfer retrace callback flow — kept free of
// Qt / AppUI dependencies so worker threads and the sequence layer can
// reference them without pulling in widget headers.
//
// Wiring pattern (mirrors PlacementRetraceCallback):
//   recipe plugin
//     → EdgeTransferCommandHandler::RunAlignment
//     → EdgeTransferRetraceCallback (set by RecipeWorker / ProgramWorker)
//     → RunProgramPage shows EdgeRetraceWidget embedded in its execution stack
//     → widget emits outcome; callback returns the enum; recipe resumes.
#pragma once

#include <functional>
#include <string>

// Parameters passed from the recipe plugin through the callback.
struct EdgeTransferRetraceParams {
  // Name of the Edge Transfer set to load from the current product's
  // edge_transfer/ directory.
  std::string trainingSetName;
};

// Outcome the user picks on the Review page of the retrace widget.
//   Applied   — corrections were executed on the target hex device
//   Skipped   — no motion; recipe continues (step succeeds)
//   Cancelled — user aborted; recipe step fails
enum class EdgeTransferRetraceOutcome {
  Applied,
  Skipped,
  Cancelled
};

// Blocking callback the sequence handler invokes on the recipe worker
// thread. The implementation must marshal UI onto the Qt main thread and
// block the caller until the user reaches an outcome.
using EdgeTransferRetraceCallback =
  std::function<EdgeTransferRetraceOutcome(const EdgeTransferRetraceParams&)>;
