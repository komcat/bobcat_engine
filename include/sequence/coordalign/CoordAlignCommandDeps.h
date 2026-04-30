// CoordAlignCommandDeps.h
// Shared dependency bundle passed to per-family CoordinateAlign command
// helpers. Mirrors the calibration/CalibCommandDeps.h pattern: single
// ApplicationContext* covers manager pointers, parent pointer is the
// callback handle for cross-family utilities (GetCurrentTimestamp,
// ResolveNumericValue) and shared state (taught/live/local maps + mutex)
// that stay on the orchestrator until the family migrations complete.
//
// A future cleanup step may extract the maps + persistence into a separate
// CoordAlignStore class; if/when that lands a `CoordAlignStore* store`
// pointer joins this struct.
#pragma once

struct ApplicationContext;
class CoordinateAlignCommandHandler;

struct CoordAlignCommandDeps {
  ApplicationContext*               app    = nullptr;
  CoordinateAlignCommandHandler*    parent = nullptr;
};
