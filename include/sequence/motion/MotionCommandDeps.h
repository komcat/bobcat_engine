// MotionCommandDeps.h
// Shared dependency bundle passed to per-family motion command helpers.
// Helpers ignore fields they do not need; the orchestrator builds this
// once at the top of MotionCommandHandler::ExecuteCommand and reuses it
// for every helper TryExecute call.
#pragma once

class UnifiedMotionManager;
class ProductPositionManager;
class DeviceLinkManager;
class DataStore;
class ScanActivityTracker;
class LogManager;
class MotionCommandHandler;

struct MotionCommandDeps {
  UnifiedMotionManager*  motion       = nullptr;
  ProductPositionManager* posMgr      = nullptr;
  DeviceLinkManager*     linkMgr      = nullptr;
  DataStore*             dataStore    = nullptr;
  ScanActivityTracker*   scanAct      = nullptr;
  LogManager*            logger       = nullptr;
  // Callback handle for shared helpers that still live on the orchestrator
  // (Wait*WithStopCheck, ResolveNumericValue, InvokeRetraceEditCallback).
  MotionCommandHandler*  parent       = nullptr;
};
