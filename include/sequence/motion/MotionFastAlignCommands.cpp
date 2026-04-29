// MotionFastAlignCommands.cpp
// Family I implementation. See MotionFastAlignCommands.h.
#include "MotionFastAlignCommands.h"

#include "unified_motion_manager.h"
#include "pi_controller.h"
#include "LogManager.h"
#include "SequenceContext.h"

#include <chrono>
#include <thread>
#include <string>

std::optional<bool> MotionFastAlignCommands::TryExecute(
  const std::string& deviceName,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context,
  const MotionCommandDeps& deps)
{
  UnifiedMotionManager* motionManager = deps.motion;

  // ============================================================================
  // FAST ALIGN XY - PI hexapod FSM (Fast Scan Maximum) 2D alignment
  // ============================================================================
  if (methodName == "FastAlignXY") {
    // Validate device is a PI hexapod (hex-* pattern)
    if (deviceName.find("hex-") != 0) {
      errorMessage = "FastAlignXY: Device must be a PI hexapod (hex-*), got: " + deviceName;
      return false;
    }

    // Get PI controller directly for FSM commands
    PIController* piController = motionManager->GetPIController(deviceName);
    if (!piController || !piController->IsConnected()) {
      errorMessage = "FastAlignXY: PI controller not connected for device: " + deviceName;
      return false;
    }

    // ✅ REQUIRED PARAMETER: axis1
    std::string axis1 = "X";
    auto axis1It = params.find("axis1");
    if (axis1It != params.end() && !axis1It->second.empty()) {
      axis1 = axis1It->second;
    }

    // ✅ REQUIRED PARAMETER: length1_um (scan length for axis1 in micrometers)
    double length1Um = 20.0;  // Default 20 um
    auto length1It = params.find("length1_um");
    if (length1It != params.end() && !length1It->second.empty()) {
      try {
        length1Um = std::stod(length1It->second);
      } catch (...) {
        errorMessage = "FastAlignXY: Invalid length1_um value: " + length1It->second;
        return false;
      }
    }

    // ✅ REQUIRED PARAMETER: axis2
    std::string axis2 = "Y";
    auto axis2It = params.find("axis2");
    if (axis2It != params.end() && !axis2It->second.empty()) {
      axis2 = axis2It->second;
    }

    // ✅ REQUIRED PARAMETER: length2_um (scan length for axis2 in micrometers)
    double length2Um = 20.0;  // Default 20 um
    auto length2It = params.find("length2_um");
    if (length2It != params.end() && !length2It->second.empty()) {
      try {
        length2Um = std::stod(length2It->second);
      } catch (...) {
        errorMessage = "FastAlignXY: Invalid length2_um value: " + length2It->second;
        return false;
      }
    }

    // ✅ REQUIRED PARAMETER: threshold (voltage threshold)
    double threshold = 0.01;  // Default 0.01 V
    auto thresholdIt = params.find("threshold");
    if (thresholdIt != params.end() && !thresholdIt->second.empty()) {
      try {
        threshold = std::stod(thresholdIt->second);
      } catch (...) {
        errorMessage = "FastAlignXY: Invalid threshold value: " + thresholdIt->second;
        return false;
      }
    }

    // ✅ REQUIRED PARAMETER: line_distance_um (spacing between scan lines)
    double lineDistanceUm = 1.0;  // Default 1 um
    auto lineDistIt = params.find("line_distance_um");
    if (lineDistIt != params.end() && !lineDistIt->second.empty()) {
      try {
        lineDistanceUm = std::stod(lineDistIt->second);
      } catch (...) {
        errorMessage = "FastAlignXY: Invalid line_distance_um value: " + lineDistIt->second;
        return false;
      }
    }

    // ✅ REQUIRED PARAMETER: analog_channel (ADC channel 1-6)
    int analogChannel = 5;  // Default channel 5
    auto channelIt = params.find("analog_channel");
    if (channelIt != params.end() && !channelIt->second.empty()) {
      try {
        analogChannel = std::stoi(channelIt->second);
        if (analogChannel < 1 || analogChannel > 6) {
          errorMessage = "FastAlignXY: analog_channel must be 1-6, got: " + channelIt->second;
          return false;
        }
      } catch (...) {
        errorMessage = "FastAlignXY: Invalid analog_channel value: " + channelIt->second;
        return false;
      }
    }

    // Convert micrometers to millimeters for PI controller
    double length1Mm = length1Um / 1000.0;
    double length2Mm = length2Um / 1000.0;
    double lineDistanceMm = lineDistanceUm / 1000.0;

    if (deps.logger) {
      deps.logger->Info(
        "FastAlignXY: Starting FSM on " + deviceName +
        " (axis1=" + axis1 + "/" + std::to_string(length1Um) + "um" +
        ", axis2=" + axis2 + "/" + std::to_string(length2Um) + "um" +
        ", threshold=" + std::to_string(threshold) + "V" +
        ", lineDistance=" + std::to_string(lineDistanceUm) + "um" +
        ", channel=" + std::to_string(analogChannel) + ")",
        "MotionHandler"
      );
    }

    // Execute FSM command
    bool fsmStarted = piController->FSM(
      axis1, length1Mm,
      axis2, length2Mm,
      threshold, lineDistanceMm,
      analogChannel);

    if (!fsmStarted) {
      errorMessage = "FastAlignXY: FSM command failed to start - check PI controller error";
      return false;
    }

    // ✅ POLL FOR COMPLETION
    const int TIMEOUT_MS = 60000;  // 60 second timeout
    const int POLL_INTERVAL_MS = 200;
    const int NOT_MOVING_THRESHOLD = 5;  // Consider done if not moving for 5 consecutive polls
    int elapsedMs = 0;
    int lastStatus = -999;
    int notMovingCount = 0;

    constexpr int MAX_ITERATIONS_FASTALIGN = 600; // Safety bound: 60s timeout / 200ms poll * 2x margin
    for (int iteration = 0; iteration < MAX_ITERATIONS_FASTALIGN; ++iteration) {
      // Check stop flags
      if (context && context->IsStopRequestedAny()) {
        errorMessage = "FastAlignXY: Cancelled by user";
        return false;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
      elapsedMs += POLL_INTERVAL_MS;

      // Check timeout
      if (elapsedMs >= TIMEOUT_MS) {
        errorMessage = "FastAlignXY: Timeout after " + std::to_string(TIMEOUT_MS/1000) + " seconds";
        return false;
      }

      // Query scan status
      int status = 0;
      if (!piController->GetFastScanStatus(status)) {
        // Don't fail immediately - might be temporary
        continue;
      }

      // Log status changes
      if (status != lastStatus) {
        if (deps.logger) {
          deps.logger->Info("FastAlignXY: Status=" + std::to_string(status) +
                            " (elapsed: " + std::to_string(elapsedMs/1000.0) + "s)", "MotionHandler");
        }
        lastStatus = status;
      }

      // Status interpretation: 0=idle (done), 1=running, negative=error
      if (status == 0) {
        // Scan completed successfully
        if (deps.logger) {
          deps.logger->Info("FastAlignXY: Completed successfully in " +
                            std::to_string(elapsedMs/1000.0) + "s", "MotionHandler");
        }
        return true;
      }
      else if (status < 0) {
        errorMessage = "FastAlignXY: Scan error, status=" + std::to_string(status);
        return false;
      }
      else if (status == 1) {
        // Status says running - but check if hexapod is actually moving
        // PI sometimes doesn't update FSS status after completion
        bool anyAxisMoving = piController->IsMoving("X") ||
                             piController->IsMoving("Y") ||
                             piController->IsMoving("Z");

        if (!anyAxisMoving) {
          notMovingCount++;
          if (notMovingCount >= NOT_MOVING_THRESHOLD) {
            // Motion stopped, consider done
            if (deps.logger) {
              deps.logger->Info("FastAlignXY: Completed (detected via motion stop, " +
                                std::to_string(notMovingCount) + " consecutive polls)", "MotionHandler");
            }
            return true;
          }
        } else {
          notMovingCount = 0;  // Reset counter if still moving
        }
      }
    }

    // Safety bound exhausted
    if (deps.logger) {
      deps.logger->Error("Loop safety bound reached in FastAlignXY", "MotionHandler");
    }
    errorMessage = "FastAlignXY: Loop safety bound reached";
    return false;
  }

  // ============================================================================
  // FAST ALIGN Z-XY - PI hexapod FSM with Z-iteration (3D alignment)
  // ============================================================================
  if (methodName == "FastAlignZXY") {
    // Validate device is a PI hexapod (hex-* pattern)
    if (deviceName.find("hex-") != 0) {
      errorMessage = "FastAlignZXY: Device must be a PI hexapod (hex-*), got: " + deviceName;
      return false;
    }

    // Get PI controller directly for FSM commands
    PIController* piController = motionManager->GetPIController(deviceName);
    if (!piController || !piController->IsConnected()) {
      errorMessage = "FastAlignZXY: PI controller not connected for device: " + deviceName;
      return false;
    }

    // ✅ XY SCAN PARAMETERS (same as FastAlignXY)
    std::string axis1 = "X";
    auto axis1It = params.find("axis1");
    if (axis1It != params.end() && !axis1It->second.empty()) {
      axis1 = axis1It->second;
    }

    double length1Um = 20.0;
    auto length1It = params.find("length1_um");
    if (length1It != params.end() && !length1It->second.empty()) {
      try {
        length1Um = std::stod(length1It->second);
      } catch (...) {
        errorMessage = "FastAlignZXY: Invalid length1_um value: " + length1It->second;
        return false;
      }
    }

    std::string axis2 = "Y";
    auto axis2It = params.find("axis2");
    if (axis2It != params.end() && !axis2It->second.empty()) {
      axis2 = axis2It->second;
    }

    double length2Um = 20.0;
    auto length2It = params.find("length2_um");
    if (length2It != params.end() && !length2It->second.empty()) {
      try {
        length2Um = std::stod(length2It->second);
      } catch (...) {
        errorMessage = "FastAlignZXY: Invalid length2_um value: " + length2It->second;
        return false;
      }
    }

    double threshold = 0.01;
    auto thresholdIt = params.find("threshold");
    if (thresholdIt != params.end() && !thresholdIt->second.empty()) {
      try {
        threshold = std::stod(thresholdIt->second);
      } catch (...) {
        errorMessage = "FastAlignZXY: Invalid threshold value: " + thresholdIt->second;
        return false;
      }
    }

    double lineDistanceUm = 1.0;
    auto lineDistIt = params.find("line_distance_um");
    if (lineDistIt != params.end() && !lineDistIt->second.empty()) {
      try {
        lineDistanceUm = std::stod(lineDistIt->second);
      } catch (...) {
        errorMessage = "FastAlignZXY: Invalid line_distance_um value: " + lineDistIt->second;
        return false;
      }
    }

    int analogChannel = 5;
    auto channelIt = params.find("analog_channel");
    if (channelIt != params.end() && !channelIt->second.empty()) {
      try {
        analogChannel = std::stoi(channelIt->second);
        if (analogChannel < 1 || analogChannel > 6) {
          errorMessage = "FastAlignZXY: analog_channel must be 1-6, got: " + channelIt->second;
          return false;
        }
      } catch (...) {
        errorMessage = "FastAlignZXY: Invalid analog_channel value: " + channelIt->second;
        return false;
      }
    }

    // ✅ Z-ITERATION PARAMETERS
    // z_direction: "Z+" or "Z-"
    int zDirection = -1;  // Default Z- (move down)
    auto zDirIt = params.find("z_direction");
    if (zDirIt != params.end() && !zDirIt->second.empty()) {
      if (zDirIt->second == "Z+") {
        zDirection = 1;
      } else if (zDirIt->second == "Z-") {
        zDirection = -1;
      } else {
        errorMessage = "FastAlignZXY: Invalid z_direction '" + zDirIt->second + "' (must be Z+ or Z-)";
        return false;
      }
    }

    // z_range_um: Total Z range to scan
    double zRangeUm = 5.0;  // Default 5 um
    auto zRangeIt = params.find("z_range_um");
    if (zRangeIt != params.end() && !zRangeIt->second.empty()) {
      try {
        zRangeUm = std::stod(zRangeIt->second);
      } catch (...) {
        errorMessage = "FastAlignZXY: Invalid z_range_um value: " + zRangeIt->second;
        return false;
      }
    }

    // z_step_um: Z step between iterations
    double zStepUm = 1.0;  // Default 1 um
    auto zStepIt = params.find("z_step_um");
    if (zStepIt != params.end() && !zStepIt->second.empty()) {
      try {
        zStepUm = std::stod(zStepIt->second);
      } catch (...) {
        errorMessage = "FastAlignZXY: Invalid z_step_um value: " + zStepIt->second;
        return false;
      }
    }

    // Convert units
    double length1Mm = length1Um / 1000.0;
    double length2Mm = length2Um / 1000.0;
    double lineDistanceMm = lineDistanceUm / 1000.0;
    double zRangeMm = zRangeUm / 1000.0;
    double zStepMm = zStepUm / 1000.0;

    // Calculate number of Z iterations
    int numZIterations = static_cast<int>(zRangeMm / zStepMm);
    if (numZIterations < 1) numZIterations = 1;

    if (deps.logger) {
      deps.logger->Info(
        "FastAlignZXY: Starting Z-iterated FSM on " + deviceName +
        " (axis1=" + axis1 + "/" + std::to_string(length1Um) + "um" +
        ", axis2=" + axis2 + "/" + std::to_string(length2Um) + "um" +
        ", zDir=" + (zDirection > 0 ? "Z+" : "Z-") +
        ", zRange=" + std::to_string(zRangeUm) + "um" +
        ", zStep=" + std::to_string(zStepUm) + "um" +
        ", iterations=" + std::to_string(numZIterations + 1) + ")",
        "MotionHandler"
      );
    }

    // Get starting Z position
    double startZ = 0.0;
    if (!piController->GetPosition("Z", startZ)) {
      errorMessage = "FastAlignZXY: Failed to read starting Z position";
      return false;
    }

    if (deps.logger) {
      deps.logger->Info("FastAlignZXY: Starting Z position = " + std::to_string(startZ) + " mm", "MotionHandler");
    }

    // Track best position
    double bestZ = startZ;
    double bestVoltage = -9999.0;
    bool foundAnyValid = false;

    // FSM polling constants
    const int TIMEOUT_MS = 60000;
    const int POLL_INTERVAL_MS = 200;
    const int NOT_MOVING_THRESHOLD = 5;

    // Lambda to run single FSM and wait for completion
    auto runSingleFSM = [&]() -> bool {
      // Start FSM
      if (!piController->FSM(axis1, length1Mm, axis2, length2Mm, threshold, lineDistanceMm, analogChannel)) {
        return false;
      }

      int elapsedMs = 0;
      int lastStatus = -999;
      int notMovingCount = 0;

      constexpr int MAX_ITERATIONS_FSM = 600; // Safety bound: 60s timeout / 200ms poll * 2x margin
      for (int iteration = 0; iteration < MAX_ITERATIONS_FSM; ++iteration) {
        // Check stop flags
        if (context && context->IsStopRequestedAny()) {
          return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
        elapsedMs += POLL_INTERVAL_MS;

        if (elapsedMs >= TIMEOUT_MS) {
          return false;
        }

        int status = 0;
        if (!piController->GetFastScanStatus(status)) {
          continue;
        }

        if (status != lastStatus) {
          lastStatus = status;
        }

        if (status == 0) {
          return true;  // Completed
        } else if (status < 0) {
          return false;  // Error
        } else if (status == 1) {
          bool anyAxisMoving = piController->IsMoving("X") ||
                               piController->IsMoving("Y") ||
                               piController->IsMoving("Z");
          if (!anyAxisMoving) {
            notMovingCount++;
            if (notMovingCount >= NOT_MOVING_THRESHOLD) {
              return true;
            }
          } else {
            notMovingCount = 0;
          }
        }
      }
      return false; // Safety bound exhausted
    };

    // ✅ ITERATE THROUGH Z POSITIONS
    for (int i = 0; i <= numZIterations; i++) {
      // Check stop flags
      if (context && context->IsStopRequestedAny()) {
        errorMessage = "FastAlignZXY: Cancelled by user";
        return false;
      }

      double currentZ = startZ + (i * zStepMm * zDirection);

      if (deps.logger) {
        deps.logger->Info("FastAlignZXY: === Z Iteration " + std::to_string(i+1) + "/" +
                          std::to_string(numZIterations+1) + " at Z=" + std::to_string(currentZ) + " mm ===",
                          "MotionHandler");
      }

      // Move to this Z position (skip for first iteration - already there)
      if (i > 0) {
        if (!piController->MoveToPosition("Z", currentZ, true)) {
          if (deps.logger) {
            deps.logger->Warning("FastAlignZXY: Failed to move to Z=" + std::to_string(currentZ) + ", skipping", "MotionHandler");
          }
          continue;
        }
        // Wait for motion to settle
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }

      // Run FSM at this Z level
      if (!runSingleFSM()) {
        if (context && context->IsStopRequestedAny()) {
          errorMessage = "FastAlignZXY: Cancelled during FSM";
          return false;
        }
        // Continue to next Z level even if this one failed
        if (deps.logger) {
          deps.logger->Warning("FastAlignZXY: FSM failed at Z=" + std::to_string(currentZ) + ", continuing", "MotionHandler");
        }
        continue;
      }

      // Read analog voltage after FSM completes
      double voltage = 0.0;
      if (piController->GetAnalogVoltage(analogChannel, voltage)) {
        if (deps.logger) {
          deps.logger->Info("FastAlignZXY: Z=" + std::to_string(currentZ) +
                            " mm, Voltage=" + std::to_string(voltage) + " V", "MotionHandler");
        }

        // Track best position (highest voltage = best alignment)
        if (voltage > bestVoltage) {
          bestVoltage = voltage;
          bestZ = currentZ;
          foundAnyValid = true;
          if (deps.logger) {
            deps.logger->Info("FastAlignZXY: New best! Z=" + std::to_string(bestZ) +
                              " mm, V=" + std::to_string(bestVoltage) + " V", "MotionHandler");
          }
        }
      } else {
        if (deps.logger) {
          deps.logger->Warning("FastAlignZXY: Failed to read analog voltage at Z=" +
                               std::to_string(currentZ), "MotionHandler");
        }
      }
    }

    if (!foundAnyValid) {
      errorMessage = "FastAlignZXY: No valid readings found during Z-scan";
      return false;
    }

    // ✅ MOVE TO BEST Z POSITION
    if (deps.logger) {
      deps.logger->Info("FastAlignZXY: Moving to best Z=" + std::to_string(bestZ) +
                        " mm (V=" + std::to_string(bestVoltage) + " V)", "MotionHandler");
    }

    if (!piController->MoveToPosition("Z", bestZ, true)) {
      errorMessage = "FastAlignZXY: Failed to move to best Z position";
      return false;
    }

    // Wait for motion to settle
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // ✅ RUN FINAL FSM AT BEST Z
    if (deps.logger) {
      deps.logger->Info("FastAlignZXY: Running final FSM at best Z position", "MotionHandler");
    }

    if (!runSingleFSM()) {
      // Still report success since we found the best Z
      if (deps.logger) {
        deps.logger->Warning("FastAlignZXY: Final FSM failed, but best Z was found", "MotionHandler");
      }
    }

    if (deps.logger) {
      deps.logger->Info("FastAlignZXY: Completed! Best Z=" + std::to_string(bestZ) +
                        " mm, Best V=" + std::to_string(bestVoltage) + " V", "MotionHandler");
    }

    return true;
  }

  return std::nullopt;
}
