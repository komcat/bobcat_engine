// DutCommandDeps.h
// Shared dependency bundle passed to per-family DUT command helpers.
// Mirrors the calibration/CalibCommandDeps.h pattern: the orchestrator carries
// an ApplicationContext* (which exposes currentDUT, productPositionManager,
// and logManager), and helpers reach the cross-family utilities
// (EnsureDUTActive, GetParam) via the parent pointer back to the orchestrator.
#pragma once

struct ApplicationContext;
class DUTCommandHandler;

struct DutCommandDeps {
  // Reach managers via deps.app->currentDUT, deps.app->productPositionManager,
  // deps.app->logManager — same instance the orchestrator carries as m_context.
  ApplicationContext* app    = nullptr;

  // Callback handle for cross-family utilities that stay on the orchestrator:
  // EnsureDUTActive(errorMessage) and GetParam(params, key).
  DUTCommandHandler*  parent = nullptr;
};
