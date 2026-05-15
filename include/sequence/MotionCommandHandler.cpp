// MotionCommandHandler.cpp
//
// Orchestrator for motion commands. Per-family logic lives in
// include/sequence/motion/Motion*Commands.{h,cpp}; this file holds the
// dispatch ladder in ExecuteCommand() plus the shared Wait* / ResolveNumericValue
// / InvokeRetraceEditCallback helpers that those split families call back
// into via deps.parent->.
#include "MotionCommandHandler.h"

// Per-family helpers (dispatch order — each TryExecute returns std::nullopt
// when the methodName isn't theirs, otherwise true/false handled).
#include "motion/MotionPositionFileCommands.h"  // Family G: position-file edits
#include "motion/MotionLinkedDeviceCommands.h"  // Family H: linked-device commands
#include "motion/MotionVelocityCommands.h"      // Family B: velocity commands
#include "motion/MotionFastAlignCommands.h"     // Family I: PI hexapod fast-align
#include "motion/MotionGridScanCommands.h"      // Family D: grid scans
#include "motion/MotionScanCommands.h"          // Family C: 1D adaptive scans
#include "motion/MotionGraphContextCommands.h"  // Family E: graph + context-driven moves
#include "motion/MotionParallelCommands.h"      // Family F: parallel moves
#include "motion/MotionBasicCommands.h"         // Family A: basic motion primitives

#include "unified_motion_manager.h"   // UnifiedMotionManager + PositionStruct
#include "LogManager.h"               // m_logger
#include "SequenceContext.h"          // context-> in shared Wait* helpers

#include <algorithm>                  // std::max in WaitForMotionWithStopCheck
#include <chrono>                     // timeout bookkeeping in Wait* helpers
#include <cmath>                      // std::abs / std::sqrt in position verification
#include <thread>                     // std::this_thread::sleep_for in poll loops
#include <vector>                     // WaitForMultipleRelativeMotionsWithStopCheck signature

// Helper to resolve context: references or parse direct values
double MotionCommandHandler::ResolveNumericValue(const std::string& valueStr, SequenceContext* context)
{
  // Check if value is a context reference (e.g., "context:needle_target_x")
  if (valueStr.find("context:") == 0) {
    std::string refKey = valueStr.substr(8);  // Remove "context:" prefix
    if (context) {
      double value = static_cast<double>(context->GetNumeric(refKey, 0.0f));
      if (m_logger) {
        m_logger->Debug("Resolved " + valueStr + " = " + std::to_string(value), "MotionHandler");
      }
      return value;
    }
    if (m_logger) {
      m_logger->Warning("No context available to resolve " + valueStr + ", using 0.0", "MotionHandler");
    }
    return 0.0;  // Fallback if no context
  } else {
    // Parse as direct numeric value
    double value = std::stod(valueStr);
    if (m_logger) {
      m_logger->Debug("Parsed direct value " + valueStr + " = " + std::to_string(value), "MotionHandler");
    }
    return value;
  }
}

bool MotionCommandHandler::ExecuteCommand(
  void* manager,
  const std::string& deviceName,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context)
{
  UnifiedMotionManager* motionManager = static_cast<UnifiedMotionManager*>(manager);

  // Context can be used for capturing positions, velocities, etc.
  // Example: if (context && params.find("captureAs") != params.end()) {
  //   context->SetValue(params.at("captureAs"), value, deviceName, methodName);
  // }

  // Shared dependency bundle for split helper families (under include/sequence/motion/).
  // Built once here and reused across every helper TryExecute call below.
  MotionCommandDeps deps{};
  deps.motion     = motionManager;
  deps.posMgr     = m_positionManager;
  deps.linkMgr    = m_linkManager;
  deps.dataStore  = m_dataStore;
  deps.scanAct    = m_scanActivityTracker;
  deps.logger     = m_logger;
  deps.parent     = this;

  // Family G: position-file editing (UpdatePositionAxis, UpdatePositionAll)
  if (auto r = MotionPositionFileCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) {
    return *r;
  }
  // Family H: linked-device commands (RegisterDeviceLink, CompensateDeviceLink)
  if (auto r = MotionLinkedDeviceCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) {
    return *r;
  }
  // Family B: velocity commands (SetVelocity, SetAllAxisVelocity, SetSystemVelocity, GetSystemVelocity)
  if (auto r = MotionVelocityCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) {
    return *r;
  }
  // Family I: PI hexapod fast-align (FastAlignXY, FastAlignZXY)
  if (auto r = MotionFastAlignCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) {
    return *r;
  }
  // Family D: grid scans (GridScan, GridScanExtended, GridScanDualChannel)
  if (auto r = MotionGridScanCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) {
    return *r;
  }
  // Family C: 1D adaptive scans (XCAN, XCANExtended, AxisScan)
  if (auto r = MotionScanCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) {
    return *r;
  }
  // Family E: graph + context-driven moves (MoveToGraphNode, MoveToContextPosition,
  // SavePositionToContext, MoveToAbsFromContext, MoveToContextXY)
  if (auto r = MotionGraphContextCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) {
    return *r;
  }
  // Family F: parallel moves (ParallelMove, ParallelMoveRelative)
  if (auto r = MotionParallelCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) {
    return *r;
  }
  // Family A: basic motion primitives (MoveToPosition, MoveToAbs, MoveRelative,
  // MoveToNamedPosition, StopAxis, EmergencyStop, HomeAxis, EnableServo, WaitForMotionComplete)
  if (auto r = MotionBasicCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) {
    return *r;
  }

  // No helper claimed the methodName.
  errorMessage = "Unknown motion method: " + methodName;
  return false;
}

// =============================================================================
// HELPER: Wait for motion complete with safety stop checking and position verification
// =============================================================================
bool MotionCommandHandler::WaitForMotionWithStopCheck(
  void* manager,
  const std::string& deviceName,
  SequenceContext* context,
  int timeoutMs,
  const TargetPosition* targetPos)
{
  UnifiedMotionManager* motionManager = static_cast<UnifiedMotionManager*>(manager);
  auto startTime = std::chrono::steady_clock::now();

  // ✅ Get pending target from motion manager if no explicit target provided
  // This ensures we verify position even for MoveToNamedPosition commands
  std::string pendingTargetName = motionManager->GetPendingTarget(deviceName);
  TargetPosition pendingTarget;
  bool usePendingTarget = false;

  if (!targetPos && !pendingTargetName.empty()) {
    // Get the target position coordinates
    PositionStruct targetPosStruct;
    if (motionManager->GetPositionWithCalculation(deviceName, pendingTargetName, targetPosStruct)) {
      pendingTarget.x = targetPosStruct.x;
      pendingTarget.y = targetPosStruct.y;
      pendingTarget.z = targetPosStruct.z;
      pendingTarget.hasX = true;
      pendingTarget.hasY = true;
      pendingTarget.hasZ = true;
      usePendingTarget = true;

      if (m_logger) {
        m_logger->Debug("WaitForMotion: Using pending target '" + pendingTargetName +
                        "' at [" + std::to_string(pendingTarget.x) + "," +
                        std::to_string(pendingTarget.y) + "," +
                        std::to_string(pendingTarget.z) + "]", "MotionHandler");
      }
    }
  }

  // Use pending target if no explicit target provided
  const TargetPosition* effectiveTarget = targetPos ? targetPos : (usePendingTarget ? &pendingTarget : nullptr);

  // ✅ Record starting position to detect actual movement
  PositionStruct startingPos = motionManager->GetCurrentPosition(deviceName);
  bool hasMovedFromStart = false;

  // ✅ FIX: Track "stuck" state - controller reports done but position is wrong
  std::chrono::steady_clock::time_point stuckDetectedTime;
  PositionStruct stuckPosition;
  bool isStuckDetected = false;
  const int STUCK_TIMEOUT_MS = 2000;  // Fail after 2 seconds of no position change

  // ✅ FIX: Scale timeout based on configured velocity and initial distance to target.
  // At slow velocities (e.g. 0.5 mm/s) a long move can legitimately exceed the default
  // 30s timeout. Stuck detection above already guards against actually-frozen motion,
  // so the outer timeout only needs to bound a worst-case-but-progressing move.
  int graceExtensionMs = 0;  // 5% one-shot extension applied if controller still moving at timeout
  bool graceUsed = false;
  if (effectiveTarget) {
    struct AxisInfo { const char* name; double delta; bool present; };
    AxisInfo axes[6] = {
      {"X", effectiveTarget->hasX ? effectiveTarget->x - startingPos.x : 0.0, effectiveTarget->hasX},
      {"Y", effectiveTarget->hasY ? effectiveTarget->y - startingPos.y : 0.0, effectiveTarget->hasY},
      {"Z", effectiveTarget->hasZ ? effectiveTarget->z - startingPos.z : 0.0, effectiveTarget->hasZ},
      {"U", effectiveTarget->hasU ? effectiveTarget->u - startingPos.u : 0.0, effectiveTarget->hasU},
      {"V", effectiveTarget->hasV ? effectiveTarget->v - startingPos.v : 0.0, effectiveTarget->hasV},
      {"W", effectiveTarget->hasW ? effectiveTarget->w - startingPos.w : 0.0, effectiveTarget->hasW},
    };

    double worstAxisSeconds = 0.0;
    bool anyVelocityKnown = false;
    for (const auto& a : axes) {
      if (!a.present) continue;
      double absDelta = std::abs(a.delta);
      if (absDelta < 0.05) continue;  // already inside completion threshold

      double velocity = 0.0;
      if (motionManager->GetVelocity(deviceName, a.name, velocity) && velocity > 1e-6) {
        anyVelocityKnown = true;
        double axisSeconds = absDelta / velocity;
        if (axisSeconds > worstAxisSeconds) worstAxisSeconds = axisSeconds;
      }
    }

    if (anyVelocityKnown && worstAxisSeconds > 0.0) {
      // 1.5x margin for accel/decel + waypoint hops, plus a 5s floor for short moves.
      int estimatedMs = static_cast<int>(worstAxisSeconds * 1500.0) + 5000;
      // Cap at 10 minutes to keep a hard upper bound on truly runaway waits.
      const int MAX_DYNAMIC_TIMEOUT_MS = 600000;
      if (estimatedMs > MAX_DYNAMIC_TIMEOUT_MS) estimatedMs = MAX_DYNAMIC_TIMEOUT_MS;
      if (estimatedMs > timeoutMs) {
        if (m_logger) {
          m_logger->Debug("WaitForMotion: extending timeout to " + std::to_string(estimatedMs) +
                          "ms based on distance/velocity (was " + std::to_string(timeoutMs) + "ms)",
                          "MotionHandler");
        }
        timeoutMs = estimatedMs;
      }
      // Reserve a 5% grace window to grant once if the controller is still moving at timeout.
      graceExtensionMs = static_cast<int>(estimatedMs * 0.05);
    }
  }

  // ✅ FIX: Small delay to ensure motion actually starts (avoid race condition)
  // When non-blocking motion is initiated, there's a brief window where the command
  // has been sent but the motor hasn't started moving yet
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Safety bound: timeoutMs / 50ms poll * 2x margin, minimum 400 iterations
  const int maxIterations = std::max(400, timeoutMs / 50 * 2);
  for (int iteration = 0; iteration < maxIterations; ++iteration) {
    // ✅ CRITICAL: Update waypoint state machine before checking status
    // This allows the waypoint system to transition from MovingToFinalTarget to Idle
    motionManager->Update();

    // ✅ SAFETY-CRITICAL: Check stop flags FIRST (before motion status)
    if (context && context->IsStopRequestedAny()) {
      if (m_logger) {
        m_logger->Warning("SAFETY STOP: Stop requested during motion - triggering emergency stop", "MotionHandler");
      }

      // Emergency stop the device immediately
      if (!motionManager->EmergencyStop(deviceName)) {
        if (m_logger) {
          m_logger->Error("Emergency stop failed for device: " + deviceName, "MotionHandler");
        }
      }

      // Clear pending target on stop
      motionManager->ClearPendingTarget(deviceName);
      return false;  // Stop sequence execution
    }

    // Check motion status
    bool isMoving = motionManager->IsDeviceInMotion(deviceName);
    bool waypointActive = motionManager->IsWaypointSystemActive(deviceName);

    // ✅ Check if device has moved from starting position
    PositionStruct currentPos = motionManager->GetCurrentPosition(deviceName);
    if (!hasMovedFromStart) {
      double dx = currentPos.x - startingPos.x;
      double dy = currentPos.y - startingPos.y;
      double dz = currentPos.z - startingPos.z;
      double distFromStart = std::sqrt(dx*dx + dy*dy + dz*dz);
      if (distFromStart > 0.1) {  // Consider moved if > 0.1mm from start
        hasMovedFromStart = true;
      }
    }

    if (!isMoving && !waypointActive) {
      // ✅ If we have a target and haven't moved yet, keep waiting (motor startup delay)
      if (effectiveTarget && !hasMovedFromStart) {
        // Check if we're already at target (maybe no move needed)
        double distToTarget = 0.0;
        if (effectiveTarget->hasX) distToTarget += (currentPos.x - effectiveTarget->x) * (currentPos.x - effectiveTarget->x);
        if (effectiveTarget->hasY) distToTarget += (currentPos.y - effectiveTarget->y) * (currentPos.y - effectiveTarget->y);
        if (effectiveTarget->hasZ) distToTarget += (currentPos.z - effectiveTarget->z) * (currentPos.z - effectiveTarget->z);
        distToTarget = std::sqrt(distToTarget);

        if (distToTarget > 0.5) {  // More than 0.5mm from target but haven't started moving
          // Check elapsed time to avoid infinite wait
          auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime
          ).count();

          if (elapsed < 500) {  // Wait up to 500ms for motion to start
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;  // Keep waiting for motion to start
          } else {
            if (m_logger) {
              m_logger->Warning("WaitForMotion: Motion did not start within 500ms, position still " +
                              std::to_string(distToTarget) + "mm from target", "MotionHandler");
            }
          }
        }
      }

      // ✅ Controller reports motion complete

      if (effectiveTarget) {
        double distanceFromTarget = 0.0;

        if (effectiveTarget->hasX) {
          double dx = currentPos.x - effectiveTarget->x;
          distanceFromTarget += dx * dx;
        }
        if (effectiveTarget->hasY) {
          double dy = currentPos.y - effectiveTarget->y;
          distanceFromTarget += dy * dy;
        }
        if (effectiveTarget->hasZ) {
          double dz = currentPos.z - effectiveTarget->z;
          distanceFromTarget += dz * dz;
        }
        if (effectiveTarget->hasU) {
          double du = currentPos.u - effectiveTarget->u;
          distanceFromTarget += du * du;
        }
        if (effectiveTarget->hasV) {
          double dv = currentPos.v - effectiveTarget->v;
          distanceFromTarget += dv * dv;
        }
        if (effectiveTarget->hasW) {
          double dw = currentPos.w - effectiveTarget->w;
          distanceFromTarget += dw * dw;
        }

        distanceFromTarget = std::sqrt(distanceFromTarget);

        const double TARGET_THRESHOLD = 0.05;  // Must be within 0.05mm of target
        if (distanceFromTarget > TARGET_THRESHOLD) {
          // ✅ FIX: Track stuck state to detect when controller is done but position won't change
          if (!isStuckDetected) {
            // First time detecting this stuck state
            isStuckDetected = true;
            stuckDetectedTime = std::chrono::steady_clock::now();
            stuckPosition = currentPos;
            if (m_logger) {
              m_logger->Warning("WaitForMotion: Controller reports done but still " + std::to_string(distanceFromTarget) +
                               "mm from target - monitoring for stuck condition", "MotionHandler");
            }
          } else {
            // Check if position has changed since we detected stuck state
            double dx = currentPos.x - stuckPosition.x;
            double dy = currentPos.y - stuckPosition.y;
            double dz = currentPos.z - stuckPosition.z;
            double positionChange = std::sqrt(dx*dx + dy*dy + dz*dz);

            if (positionChange > 0.01) {  // Position changed > 10 microns - reset stuck timer
              stuckPosition = currentPos;
              stuckDetectedTime = std::chrono::steady_clock::now();
            } else {
              // Position hasn't changed - check if we've been stuck too long
              auto stuckDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - stuckDetectedTime
              ).count();

              if (stuckDuration > STUCK_TIMEOUT_MS) {
                if (m_logger) {
                  m_logger->Error("WaitForMotion: STUCK - Controller reports done, position unchanged for " +
                                 std::to_string(stuckDuration) + "ms, still " + std::to_string(distanceFromTarget) +
                                 "mm from target. Motion may have hit a limit or encountered an error.", "MotionHandler");
                }
                motionManager->ClearPendingTarget(deviceName);
                return false;  // Fail - motion is stuck
              }
            }
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
          continue;  // Keep waiting briefly
        } else {
          // Position is now within threshold - clear stuck state
          isStuckDetected = false;
        }
      }

      if (m_logger) {
        std::string stableStr = "WaitForMotion: Motion complete at [" +
                               std::to_string(currentPos.x) + "," + std::to_string(currentPos.y) + "," + std::to_string(currentPos.z) + "]";
        m_logger->Debug(stableStr, "MotionHandler");
      }

      // ✅ Clear pending target now that move is confirmed complete
      motionManager->ClearPendingTarget(deviceName);

      // Motion stopped and position is stable - now verify position if target was provided
      if (effectiveTarget) {
        const double TOLERANCE = 0.05;  // ±0.05mm tolerance

        bool positionOK = true;
        std::string errorDetail;

        // Verify X axis if specified
        if (effectiveTarget->hasX) {
          double actualX = 0.0;
          if (motionManager->GetPosition(deviceName, "X", actualX)) {
            double errorX = std::abs(actualX - effectiveTarget->x);
            if (errorX > TOLERANCE) {
              positionOK = false;
              errorDetail += "X: target=" + std::to_string(effectiveTarget->x) +
                             ", actual=" + std::to_string(actualX) +
                             ", error=" + std::to_string(errorX) + "mm; ";
            }
          }
        }

        // Verify Y axis if specified
        if (effectiveTarget->hasY) {
          double actualY = 0.0;
          if (motionManager->GetPosition(deviceName, "Y", actualY)) {
            double errorY = std::abs(actualY - effectiveTarget->y);
            if (errorY > TOLERANCE) {
              positionOK = false;
              errorDetail += "Y: target=" + std::to_string(effectiveTarget->y) +
                             ", actual=" + std::to_string(actualY) +
                             ", error=" + std::to_string(errorY) + "mm; ";
            }
          }
        }

        // Verify Z axis if specified
        if (effectiveTarget->hasZ) {
          double actualZ = 0.0;
          if (motionManager->GetPosition(deviceName, "Z", actualZ)) {
            double errorZ = std::abs(actualZ - effectiveTarget->z);
            if (errorZ > TOLERANCE) {
              positionOK = false;
              errorDetail += "Z: target=" + std::to_string(effectiveTarget->z) +
                             ", actual=" + std::to_string(actualZ) +
                             ", error=" + std::to_string(errorZ) + "mm; ";
            }
          }
        }

        // Verify U axis if specified (rotational, degrees)
        if (effectiveTarget->hasU) {
          double actualU = 0.0;
          if (motionManager->GetPosition(deviceName, "U", actualU)) {
            double errorU = std::abs(actualU - effectiveTarget->u);
            if (errorU > TOLERANCE) {
              positionOK = false;
              errorDetail += "U: target=" + std::to_string(effectiveTarget->u) +
                             ", actual=" + std::to_string(actualU) +
                             ", error=" + std::to_string(errorU) + "deg; ";
            }
          }
        }

        // Verify V axis if specified (rotational, degrees)
        if (effectiveTarget->hasV) {
          double actualV = 0.0;
          if (motionManager->GetPosition(deviceName, "V", actualV)) {
            double errorV = std::abs(actualV - effectiveTarget->v);
            if (errorV > TOLERANCE) {
              positionOK = false;
              errorDetail += "V: target=" + std::to_string(effectiveTarget->v) +
                             ", actual=" + std::to_string(actualV) +
                             ", error=" + std::to_string(errorV) + "deg; ";
            }
          }
        }

        // Verify W axis if specified (rotational, degrees)
        if (effectiveTarget->hasW) {
          double actualW = 0.0;
          if (motionManager->GetPosition(deviceName, "W", actualW)) {
            double errorW = std::abs(actualW - effectiveTarget->w);
            if (errorW > TOLERANCE) {
              positionOK = false;
              errorDetail += "W: target=" + std::to_string(effectiveTarget->w) +
                             ", actual=" + std::to_string(actualW) +
                             ", error=" + std::to_string(errorW) + "deg; ";
            }
          }
        }

        if (!positionOK) {
          if (m_logger) {
            m_logger->Error("Position verification FAILED for " + deviceName + ": " + errorDetail, "MotionHandler");
          }
          return false;  // Position error exceeds tolerance
        }

        if (m_logger) {
          m_logger->Debug("Position verification OK for " + deviceName + " (tolerance ±" +
                          std::to_string(TOLERANCE) + "mm)", "MotionHandler");
        }
      }

      // Motion completed successfully (and position verified if required)
      return true;
    }

    // Check timeout
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - startTime
    ).count();

    if (elapsed > timeoutMs) {
      // ✅ One-shot 5% grace: if the estimated time has elapsed but the controller
      // is still actively moving, extend the deadline once before declaring failure.
      bool granted = false;
      if (!graceUsed && graceExtensionMs > 0) {
        bool stillMoving = motionManager->IsDeviceInMotion(deviceName) ||
                           motionManager->IsWaypointSystemActive(deviceName);
        if (stillMoving) {
          timeoutMs += graceExtensionMs;
          graceUsed = true;
          granted = true;
          if (m_logger) {
            m_logger->Warning("WaitForMotion: estimated time elapsed but controller still moving - granting 5% grace (+" +
                              std::to_string(graceExtensionMs) + "ms) on " + deviceName, "MotionHandler");
          }
        }
      }

      if (!granted) {
        if (m_logger) {
          m_logger->Error("Timeout waiting for motion complete on: " + deviceName, "MotionHandler");
        }
        return false;
      }
    }

    // Sleep briefly to avoid busy-waiting (50ms polling rate)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // Safety bound exhausted
  if (m_logger) {
    m_logger->Error("Loop safety bound reached in WaitForMotionWithStopCheck for " + deviceName, "MotionHandler");
  }
  return false;
}

// =============================================================================
// HELPER: Wait for multiple relative motions to complete with safety stop checking
// =============================================================================
bool MotionCommandHandler::WaitForMultipleRelativeMotionsWithStopCheck(
  void* manager,
  const std::vector<std::string>& deviceNames,
  SequenceContext* context,
  int timeoutMs)
{
  UnifiedMotionManager* motionManager = static_cast<UnifiedMotionManager*>(manager);
  auto startTime = std::chrono::steady_clock::now();

  // Track completion status for each device
  std::vector<bool> completed(deviceNames.size(), false);
  int completedCount = 0;

  // Small delay to ensure all motions actually start
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  while (completedCount < static_cast<int>(deviceNames.size())) {
    // ✅ CRITICAL: Update waypoint state machines for all devices
    motionManager->Update();

    // ✅ SAFETY-CRITICAL: Check stop flags FIRST
    if (context && context->IsStopRequestedAny()) {
      if (m_logger) {
        m_logger->Warning("SAFETY STOP: Stop requested during parallel relative motion - stopping all devices", "MotionHandler");
      }

      // Emergency stop ALL devices
      for (const auto& deviceName : deviceNames) {
        (void)motionManager->EmergencyStop(deviceName);
      }

      return false;
    }

    // Check each device's motion status
    for (size_t i = 0; i < deviceNames.size(); ++i) {
      if (completed[i]) continue;  // Already done

      const auto& deviceName = deviceNames[i];
      bool isMoving = motionManager->IsDeviceInMotion(deviceName);

      if (!isMoving) {
        // Device completed
        completed[i] = true;
        completedCount++;

        if (m_logger) {
          m_logger->Debug("ParallelMoveRelative: " + deviceName + " completed (" +
                         std::to_string(completedCount) + "/" + std::to_string(deviceNames.size()) + ")", "MotionHandler");
        }
      }
    }

    // Check timeout
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - startTime
    ).count();

    if (elapsed > timeoutMs) {
      if (m_logger) {
        std::string pendingDevices;
        for (size_t i = 0; i < deviceNames.size(); ++i) {
          if (!completed[i]) {
            if (!pendingDevices.empty()) pendingDevices += ", ";
            pendingDevices += deviceNames[i];
          }
        }
        m_logger->Error("ParallelMoveRelative: Timeout waiting for devices: " + pendingDevices, "MotionHandler");
      }

      // Emergency stop all devices that are still moving
      for (size_t i = 0; i < deviceNames.size(); ++i) {
        if (!completed[i]) {
          (void)motionManager->EmergencyStop(deviceNames[i]);
        }
      }

      return false;
    }

    // Sleep briefly to avoid busy-waiting
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  return true;
}

// =============================================================================
// HELPER: Invoke retrace edit callback after motion completes
// =============================================================================
bool MotionCommandHandler::InvokeRetraceEditCallback(
  void* motionManager,
  const std::string& deviceName,
  const std::string& positionName,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  double originalX, double originalY, double originalZ,
  double originalU, double originalV, double originalW)
{
  if (!m_retraceEditCallback) {
    return true;  // No callback, continue
  }

  UnifiedMotionManager* manager = static_cast<UnifiedMotionManager*>(motionManager);

  // Build motion info
  RetraceMotionInfo info;
  info.deviceName = deviceName;
  info.positionName = positionName;
  info.methodName = methodName;
  info.originalX = originalX;
  info.originalY = originalY;
  info.originalZ = originalZ;
  info.originalU = originalU;
  info.originalV = originalV;
  info.originalW = originalW;
  info.params = params;

  // Get current position
  PositionStruct currentPos = manager->GetCurrentPosition(deviceName);
  RetraceCurrentPosition current;
  current.x = currentPos.x;
  current.y = currentPos.y;
  current.z = currentPos.z;
  current.u = currentPos.u;
  current.v = currentPos.v;
  current.w = currentPos.w;

  // Track statistics
  m_retracePositionsVisited++;

  if (m_logger) {
    m_logger->Info("Retrace: Invoking callback for " + deviceName + "/" + positionName +
                   " (position " + std::to_string(m_retracePositionsVisited) + ")", "MotionHandler");
  }

  // Invoke callback - this blocks until user responds
  bool saveRequested = false;
  bool continueExecution = m_retraceEditCallback(info, current, saveRequested);

  if (saveRequested) {
    // User wants to save the adjusted position
    m_retracePositionsSaved++;

    // Get updated current position (user may have jogged)
    PositionStruct updatedPos = manager->GetCurrentPosition(deviceName);

    // Save to file
    if (manager->SaveNamedPositionToFile(deviceName, positionName, updatedPos, true)) {
      if (m_logger) {
        m_logger->Info("Retrace: Saved position '" + positionName + "' for device " + deviceName +
                       " -> (" + std::to_string(updatedPos.x) + ", " + std::to_string(updatedPos.y) +
                       ", " + std::to_string(updatedPos.z) + ")", "MotionHandler");
      }
    } else {
      if (m_logger) {
        m_logger->Error("Retrace: Failed to save position '" + positionName + "'", "MotionHandler");
      }
    }
  }

  return continueExecution;
}