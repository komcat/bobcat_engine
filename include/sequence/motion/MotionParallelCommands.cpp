// MotionParallelCommands.cpp
// Family F implementation. See MotionParallelCommands.h.
#include "MotionParallelCommands.h"

#include "MotionCommandHandler.h"        // for parent->Wait* and nested ParallelMove[Relative]Spec types
#include "unified_motion_manager.h"
#include "LogManager.h"
#include "SequenceContext.h"
#include "product_position_manager.h"
#include "GraphMoveWorker.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

std::optional<bool> MotionParallelCommands::TryExecute(
  const std::string& /*deviceName*/,  // unused — ParallelMove[Relative] devices come from JSON
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context,
  const MotionCommandDeps& deps)
{
  UnifiedMotionManager* motionManager = deps.motion;

  // ============================================================================
  // PARALLEL MOVE - Move multiple devices simultaneously
  // ============================================================================
  if (methodName == "ParallelMove") {
    // Parse the moves JSON array from parameters
    // Format: moves = [{"device":"gantry","type":"position","target":"home"},
    //                  {"device":"hex-left","type":"graphNode","graph":"align","target":"probe"}]

    auto movesIt = params.find("moves");
    if (movesIt == params.end()) {
      errorMessage = "ParallelMove: Missing required parameter 'moves'";
      return false;
    }

    // Parse JSON array of moves
    std::vector<ParallelMoveSpec> moveSpecs;
    try {
      nlohmann::json movesJson = nlohmann::json::parse(movesIt->second);

      if (!movesJson.is_array()) {
        errorMessage = "ParallelMove: 'moves' must be a JSON array";
        return false;
      }

      for (const auto& moveObj : movesJson) {
        ParallelMoveSpec spec;

        if (!moveObj.contains("device") || !moveObj.contains("type") || !moveObj.contains("target")) {
          errorMessage = "ParallelMove: Each move must have 'device', 'type', and 'target'";
          return false;
        }

        spec.deviceName = moveObj["device"].get<std::string>();
        spec.moveType = moveObj["type"].get<std::string>();
        spec.targetName = moveObj["target"].get<std::string>();

        if (spec.moveType == "graphNode") {
          if (!moveObj.contains("graph")) {
            errorMessage = "ParallelMove: graphNode type requires 'graph' parameter";
            return false;
          }
          spec.graphName = moveObj["graph"].get<std::string>();
        }

        // Validate device exists
        if (!motionManager->HasDevice(spec.deviceName)) {
          errorMessage = "ParallelMove: Device '" + spec.deviceName + "' not found";
          return false;
        }

        moveSpecs.push_back(spec);
      }
    } catch (const nlohmann::json::exception& e) {
      errorMessage = "ParallelMove: Failed to parse moves JSON: " + std::string(e.what());
      return false;
    }

    if (moveSpecs.empty()) {
      errorMessage = "ParallelMove: No moves specified";
      return false;
    }

    // ✅ Check for debug parameter to enable verbose logging
    bool debugMode = false;
    auto debugIt = params.find("debug");
    if (debugIt != params.end() && (debugIt->second == "true" || debugIt->second == "1")) {
      debugMode = true;
    }
    if (debugMode && motionManager) {
      motionManager->SetDebugLogging(true);
      if (deps.logger) {
        deps.logger->Debug("ParallelMove: Debug logging ENABLED", "MotionHandler");
      }
    }

    if (deps.logger) {
      std::string moveList = "ParallelMove: Starting " + std::to_string(moveSpecs.size()) + " parallel moves:\n";
      for (size_t i = 0; i < moveSpecs.size(); ++i) {
        moveList += "  [" + std::to_string(i+1) + "] " + moveSpecs[i].deviceName +
                    " -> " + moveSpecs[i].targetName +
                    " (type=" + moveSpecs[i].moveType +
                    ", graph=" + moveSpecs[i].graphName + ")\n";
      }
      deps.logger->Info(moveList, "MotionHandler");

      // ✅ Also log current waypoint states before starting
      if (debugMode && motionManager) {
        std::string stateInfo = "ParallelMove: Current waypoint states before move:\n";
        for (const auto& spec : moveSpecs) {
          stateInfo += "  " + spec.deviceName + ": " + motionManager->GetWaypointStateString(spec.deviceName) + "\n";
        }
        deps.logger->Info(stateInfo, "MotionHandler");
      }
    }

    // ✅ PHASE 1: Separate moves by type and validate
    std::vector<ParallelMoveSpec> positionMoves;
    std::vector<ParallelMoveSpec> graphNodeMoves;

    for (const auto& spec : moveSpecs) {
      if (spec.moveType == "position") {
        positionMoves.push_back(spec);
      }
      else if (spec.moveType == "graphNode") {
        graphNodeMoves.push_back(spec);
      }
      else {
        errorMessage = "ParallelMove: Unknown move type '" + spec.moveType + "'";
        return false;
      }
    }

    // Validate graphNode requirements
    if (!graphNodeMoves.empty() && !deps.posMgr) {
      errorMessage = "ParallelMove: ProductPositionManager not available for graphNode moves";
      return false;
    }

    // ✅ PHASE 2: Create and start workers for all graphNode moves (TRUE PARALLEL!)
    std::vector<GraphMoveWorker*> graphWorkers;

    for (const auto& spec : graphNodeMoves) {
      if (deps.logger) {
        deps.logger->Debug("ParallelMove: Creating worker for " + spec.deviceName +
                        " graphNode -> " + spec.targetName + " (graph: " + spec.graphName + ")",
                        "MotionHandler");
      }

      // Create worker (handles all path finding and execution internally)
      auto* worker = new GraphMoveWorker(
        motionManager,
        deps.posMgr,
        spec.deviceName,
        spec.graphName,
        spec.targetName
      );

      // Connect log messages
      // Capture logger pointer by value: GraphMoveWorker outlives this stack frame
      // (it's polled later in PHASE 5), and LogManager lives for the app lifetime
      // so the captured pointer stays valid.
      if (deps.logger) {
        LogManager* logger = deps.logger;
        QObject::connect(worker, &GraphMoveWorker::logMessage,
          [logger](QString msg) {
            logger->Info(msg.toStdString(), "MotionHandler");
          });
      }

      graphWorkers.push_back(worker);

      // Start the worker thread (non-blocking!)
      // NOTE: Do NOT connect deleteLater here — workers are referenced in the poll loop
      // and will be cleaned up manually in PHASE 5
      worker->start();
    }

    if (deps.logger && !graphWorkers.empty()) {
      deps.logger->Debug("ParallelMove: Started " + std::to_string(graphWorkers.size()) +
                      " graphNode workers in parallel", "MotionHandler");
    }

    // ✅ PHASE 3: Start all position moves (non-blocking)
    for (const auto& spec : positionMoves) {
      bool startSuccess = motionManager->MoveToNamedPosition(spec.deviceName, spec.targetName, false);
      if (!startSuccess) {
        errorMessage = "ParallelMove: Failed to start move for " + spec.deviceName + " to " + spec.targetName;
        // Stop all workers
        for (auto* w : graphWorkers) {
          w->requestStop();
        }
        // Emergency stop all devices
        for (const auto& s : moveSpecs) {
          (void)motionManager->EmergencyStop(s.deviceName);
        }
        // Wait for workers to finish and clean up
        for (auto* w : graphWorkers) {
          w->wait();
          delete w;
        }
        graphWorkers.clear();
        return false;
      }

      if (deps.logger) {
        deps.logger->Debug("ParallelMove: Started position move for " + spec.deviceName + " -> " + spec.targetName, "MotionHandler");
      }
    }

    // ✅ PHASE 4: Unified wait loop for ALL moves (position and graphNode)
    auto startTime = std::chrono::steady_clock::now();
    const int OVERALL_TIMEOUT_MS = 60000;  // 60 seconds overall timeout
    const double POSITION_TOLERANCE = 0.5;  // mm - tolerance for position verification

    // Track starting positions and completion state for position moves
    struct PositionMoveState {
      std::string deviceName;
      std::string targetName;
      PositionStruct startPos;
      PositionStruct targetPos;
      bool motionStarted = false;
      bool completed = false;
    };
    std::vector<PositionMoveState> positionMoveStates;

    for (const auto& spec : positionMoves) {
      PositionMoveState state;
      state.deviceName = spec.deviceName;
      state.targetName = spec.targetName;
      state.startPos = motionManager->GetCurrentPosition(spec.deviceName);

      // Get target position
      if (motionManager->GetPositionWithCalculation(spec.deviceName, spec.targetName, state.targetPos)) {
        // Check if already at target (within tolerance)
        double dx = state.startPos.x - state.targetPos.x;
        double dy = state.startPos.y - state.targetPos.y;
        double dz = state.startPos.z - state.targetPos.z;
        double distToTarget = std::sqrt(dx*dx + dy*dy + dz*dz);

        if (distToTarget < POSITION_TOLERANCE) {
          state.motionStarted = true;
          state.completed = true;
          if (deps.logger) {
            deps.logger->Debug("ParallelMove: " + state.deviceName + " already at target '" + state.targetName +
                          "' (dist: " + std::to_string(distToTarget) + "mm)", "MotionHandler");
          }
        }
        positionMoveStates.push_back(state);
      } else {
        if (deps.logger) {
          deps.logger->Warning("ParallelMove: Could not get target position for " + spec.deviceName + " -> " + spec.targetName, "MotionHandler");
        }
        // Still add it but without target verification
        state.targetPos = state.startPos;  // Will rely on IsDeviceInMotion
        positionMoveStates.push_back(state);
      }
    }

    constexpr int MAX_ITERATIONS_PARALLEL = 2400; // Safety bound: 60s timeout / 50ms poll * 2x margin
    for (int iteration = 0; iteration < MAX_ITERATIONS_PARALLEL; ++iteration) {
      // Check stop request
      if (context && context->IsStopRequestedAny()) {
        if (deps.logger) {
          deps.logger->Warning("ParallelMove: Stop requested, aborting all moves", "MotionHandler");
        }
        // Stop all workers
        for (auto* w : graphWorkers) {
          w->requestStop();
        }
        // Emergency stop all devices
        for (const auto& spec : moveSpecs) {
          (void)motionManager->EmergencyStop(spec.deviceName);
        }
        // Wait for workers to finish and clean up
        for (auto* w : graphWorkers) {
          w->wait();
          delete w;
        }
        graphWorkers.clear();
        errorMessage = "ParallelMove: Stopped by user request";
        return false;
      }

      // Check if all workers completed
      bool allWorkersComplete = true;
      for (auto* w : graphWorkers) {
        if (!w->isCompleted()) {
          allWorkersComplete = false;
          break;
        }
      }

      // Check if all position moves completed (with motion start detection)
      bool allPositionsComplete = true;
      for (auto& state : positionMoveStates) {
        if (state.completed) continue;

        PositionStruct currentPos = motionManager->GetCurrentPosition(state.deviceName);
        bool isMoving = motionManager->IsDeviceInMotion(state.deviceName);

        // Detect if motion has started (position changed from start)
        if (!state.motionStarted) {
          double dx = currentPos.x - state.startPos.x;
          double dy = currentPos.y - state.startPos.y;
          double dz = currentPos.z - state.startPos.z;
          double distFromStart = std::sqrt(dx*dx + dy*dy + dz*dz);

          if (distFromStart > 0.01 || isMoving) {  // 0.01mm threshold for start detection
            state.motionStarted = true;
          }
        }

        // Only mark complete if motion started AND device stopped AND at target
        if (state.motionStarted && !isMoving) {
          double dx = currentPos.x - state.targetPos.x;
          double dy = currentPos.y - state.targetPos.y;
          double dz = currentPos.z - state.targetPos.z;
          double distToTarget = std::sqrt(dx*dx + dy*dy + dz*dz);

          if (distToTarget < POSITION_TOLERANCE) {
            state.completed = true;
            if (deps.logger) {
              deps.logger->Debug("ParallelMove: " + state.deviceName + " completed (dist to target: " +
                            std::to_string(distToTarget) + "mm)", "MotionHandler");
            }
            // Clear pending target
            motionManager->ClearPendingTarget(state.deviceName);
          }
        }

        if (!state.completed) {
          allPositionsComplete = false;
        }
      }

      // If all done, break
      if (allWorkersComplete && allPositionsComplete) {
        break;
      }

      // Check timeout
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime).count();
      if (elapsed > OVERALL_TIMEOUT_MS) {
        if (deps.logger) {
          deps.logger->Error("ParallelMove: Overall timeout exceeded", "MotionHandler");
        }
        // Stop all workers
        for (auto* w : graphWorkers) {
          w->requestStop();
        }
        // Emergency stop all devices
        for (const auto& spec : moveSpecs) {
          (void)motionManager->EmergencyStop(spec.deviceName);
        }
        // Wait for workers to finish and clean up
        for (auto* w : graphWorkers) {
          w->wait();
          delete w;
        }
        graphWorkers.clear();
        errorMessage = "ParallelMove: Overall timeout exceeded (" + std::to_string(OVERALL_TIMEOUT_MS) + "ms)";
        return false;
      }

      // Sleep to avoid busy waiting
      std::this_thread::sleep_for(std::chrono::milliseconds(50));

      // Check if loop is about to exhaust — if so, treat as timeout
      if (iteration == MAX_ITERATIONS_PARALLEL - 1) {
        if (deps.logger) {
          deps.logger->Error("Loop safety bound reached in ParallelMove", "MotionHandler");
        }
        for (auto* w : graphWorkers) {
          w->requestStop();
        }
        for (const auto& spec : moveSpecs) {
          (void)motionManager->EmergencyStop(spec.deviceName);
        }
        for (auto* w : graphWorkers) {
          w->wait();
          delete w;
        }
        graphWorkers.clear();
        errorMessage = "ParallelMove: Loop safety bound exhausted";
        return false;
      }
    }

    // ✅ PHASE 5: Check results (reached only via successful break)
    bool allSuccess = true;
    std::string failedDevices;

    // Check worker results
    for (auto* w : graphWorkers) {
      w->wait();  // Ensure thread finished
      if (!w->isSuccess()) {
        allSuccess = false;
        if (!failedDevices.empty()) failedDevices += ", ";
        failedDevices += w->getDeviceName() + " (" + w->getErrorMessage().toStdString() + ")";
      }
    }

    // Clean up graph workers (not auto-deleted — we manage their lifetime)
    for (auto* w : graphWorkers) {
      delete w;
    }
    graphWorkers.clear();

    if (!allSuccess) {
      errorMessage = "ParallelMove: GraphNode moves failed for: " + failedDevices;
      if (deps.logger) {
        deps.logger->Error(errorMessage, "MotionHandler");
      }
      return false;
    }

    if (deps.logger) {
      deps.logger->Info("ParallelMove: All " + std::to_string(moveSpecs.size()) + " moves completed successfully", "MotionHandler");
    }

    return true;
  }

  // ============================================================================
  // PARALLEL MOVE RELATIVE - Move multiple devices relative distances simultaneously
  // ============================================================================
  if (methodName == "ParallelMoveRelative") {
    // Parse the moves JSON array from parameters
    // Format: moves = [{"device":"hex-left","axis":"X","distance":4.0},
    //                  {"device":"hex-right","axis":"Z","distance":-2.0}]

    auto movesIt = params.find("moves");
    if (movesIt == params.end()) {
      errorMessage = "ParallelMoveRelative: Missing required parameter 'moves'";
      return false;
    }

    // Parse JSON array of moves
    std::vector<ParallelRelativeMoveSpec> moveSpecs;
    std::vector<std::string> deviceNames;  // For waiting
    try {
      nlohmann::json movesJson = nlohmann::json::parse(movesIt->second);

      if (!movesJson.is_array()) {
        errorMessage = "ParallelMoveRelative: 'moves' must be a JSON array";
        return false;
      }

      for (const auto& moveObj : movesJson) {
        ParallelRelativeMoveSpec spec;

        if (!moveObj.contains("device") || !moveObj.contains("axis") || !moveObj.contains("distance")) {
          errorMessage = "ParallelMoveRelative: Each move must have 'device', 'axis', and 'distance'";
          return false;
        }

        spec.deviceName = moveObj["device"].get<std::string>();
        spec.axis = moveObj["axis"].get<std::string>();

        // Handle distance as either number or string
        if (moveObj["distance"].is_number()) {
          spec.distance = moveObj["distance"].get<double>();
        } else {
          spec.distance = std::stod(moveObj["distance"].get<std::string>());
        }

        // Validate device exists
        if (!motionManager->HasDevice(spec.deviceName)) {
          errorMessage = "ParallelMoveRelative: Device '" + spec.deviceName + "' not found";
          return false;
        }

        // Validate axis
        std::string axisUpper = spec.axis;
        std::transform(axisUpper.begin(), axisUpper.end(), axisUpper.begin(), ::toupper);
        if (axisUpper != "X" && axisUpper != "Y" && axisUpper != "Z" &&
            axisUpper != "U" && axisUpper != "V" && axisUpper != "W") {
          errorMessage = "ParallelMoveRelative: Invalid axis '" + spec.axis + "' (must be X, Y, Z, U, V, or W)";
          return false;
        }
        spec.axis = axisUpper;

        moveSpecs.push_back(spec);

        // Track unique device names for waiting
        if (std::find(deviceNames.begin(), deviceNames.end(), spec.deviceName) == deviceNames.end()) {
          deviceNames.push_back(spec.deviceName);
        }
      }
    } catch (const nlohmann::json::exception& e) {
      errorMessage = "ParallelMoveRelative: Failed to parse moves JSON: " + std::string(e.what());
      return false;
    }

    if (moveSpecs.empty()) {
      errorMessage = "ParallelMoveRelative: No moves specified";
      return false;
    }

    if (deps.logger) {
      std::string moveList = "ParallelMoveRelative: Starting " + std::to_string(moveSpecs.size()) + " parallel relative moves:\n";
      for (size_t i = 0; i < moveSpecs.size(); ++i) {
        moveList += "  [" + std::to_string(i+1) + "] " + moveSpecs[i].deviceName +
                    " " + moveSpecs[i].axis + " by " + std::to_string(moveSpecs[i].distance) + "mm\n";
      }
      deps.logger->Info(moveList, "MotionHandler");
    }

    // ✅ PHASE 1: Start all relative moves (non-blocking)
    for (const auto& spec : moveSpecs) {
      // Skip zero distance moves
      if (std::abs(spec.distance) < 1e-9) {
        if (deps.logger) {
          deps.logger->Debug("ParallelMoveRelative: Skipping zero-distance move for " + spec.deviceName + " " + spec.axis, "MotionHandler");
        }
        continue;
      }

      if (!motionManager->MoveRelative(spec.deviceName, spec.axis, spec.distance, false)) {
        errorMessage = "ParallelMoveRelative: Failed to start move for " + spec.deviceName + " " + spec.axis;
        // Emergency stop all devices that already started
        for (const auto& stopDevice : deviceNames) {
          (void)motionManager->EmergencyStop(stopDevice);
        }
        return false;
      }

      if (deps.logger) {
        deps.logger->Debug("ParallelMoveRelative: Started move for " + spec.deviceName + " " + spec.axis + " by " + std::to_string(spec.distance), "MotionHandler");
      }
    }

    // ✅ PHASE 2: Wait for all moves to complete
    if (!deps.parent->WaitForMultipleRelativeMotionsWithStopCheck(motionManager, deviceNames, context)) {
      errorMessage = "ParallelMoveRelative: One or more moves failed, timed out, or were stopped";
      return false;
    }

    if (deps.logger) {
      deps.logger->Info("ParallelMoveRelative: All " + std::to_string(moveSpecs.size()) + " moves completed successfully", "MotionHandler");
    }

    return true;
  }

  return std::nullopt;
}
