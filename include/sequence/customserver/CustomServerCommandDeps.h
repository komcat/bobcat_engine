// CustomServerCommandDeps.h
// Shared dependency bundle passed to per-family CustomServer command helpers.
// Mirrors the vision/VisionCommandDeps.h pattern — single ApplicationContext*
// covers everything the helpers need (BeamServerClient, CameraManager,
// UnifiedMotionManager, LogManager) reachable via deps.app->member.
#pragma once

struct ApplicationContext;
class CustomServerCommandHandler;

struct CustomServerCommandDeps {
  // Reach managers via deps.app->beamServerClient etc.
  ApplicationContext*           app    = nullptr;

  // Callback handle in case any helper needs to invoke a sibling shim
  // (none currently — kept for symmetry with the established pattern).
  CustomServerCommandHandler*   parent = nullptr;
};
