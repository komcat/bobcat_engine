// VisionCommandDeps.h
// Shared dependency bundle passed to per-family vision command helpers.
// Mirrors the motion/MotionCommandDeps.h pattern but compresses the manager
// pointers down to a single ApplicationContext* — vision touches a wider
// loosely-coupled set of subsystems (camera, lighting, motion, sequence,
// camera windows, GDI, log) than motion did, and the orchestrator already
// kept everything via m_context, so we honour that.
#pragma once

struct ApplicationContext;
class VisionCommandHandler;

struct VisionCommandDeps {
  // Reach managers via deps.app->cameraManager etc. — same structure as the
  // orchestrator's m_context.
  ApplicationContext*   app    = nullptr;

  // Callback handle for the LoadProfile / Execute / CaptureAndExecute shims
  // that stay on the orchestrator (mirrors Motion's deps.parent->Wait*).
  // The execute_profile pipeline inside VisionProfileCommands.cpp re-enters
  // the orchestrator via this pointer so the inter-method composition keeps
  // working post-split.
  VisionCommandHandler* parent = nullptr;
};
