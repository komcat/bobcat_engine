// CalibCommandDeps.h
// Shared dependency bundle passed to per-family calibration command helpers.
// Mirrors the vision/VisionCommandDeps.h pattern — single ApplicationContext*
// covers everything the helpers need (motionManager, productPositionManager,
// logManager, etc.) reachable via deps.app->member. The parent pointer is the
// callback handle for the two cross-family utilities that stay on the
// orchestrator: GetCurrentDateTime + EnsureCalibrationDirectoryExists.
#pragma once

struct ApplicationContext;
class CalibrationCommandHandler;

struct CalibCommandDeps {
  // Reach managers via deps.app->motionManager etc. — same instance the
  // orchestrator carries as m_context.
  ApplicationContext*           app    = nullptr;

  // Callback handle for cross-family utilities (GetCurrentDateTime,
  // EnsureCalibrationDirectoryExists, m_logger access via inherited
  // ICommandHandler member). Mirrors Motion's deps.parent->Wait*.
  CalibrationCommandHandler*    parent = nullptr;
};
