// MotionScanCommands.cpp
// Family C implementation. See MotionScanCommands.h.
#include "MotionScanCommands.h"

#include "unified_motion_manager.h"
#include "LogManager.h"
#include "SequenceContext.h"
#include "DataStore.h"
#include "AdaptiveScanController.h"
#include "ScanTypes.h"
#include "ScanActivityGuard.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

std::optional<bool> MotionScanCommands::TryExecute(
  const std::string& deviceName,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context,
  const MotionCommandDeps& deps)
{
  UnifiedMotionManager* motionManager = deps.motion;

  // ============================================================================
  // XCAN - HEXAPOD ADAPTIVE SCANNING
  // ============================================================================
  if (methodName == "XCAN") {
    // Scan-busy indicator — ref-counted Begin on enter, End on every return path.
    ScanActivityGuard _scanBusy(deps.scanAct, "XCAN");

    // ✅ VALIDATE DEVICE NAME (must match hex-*)
    std::regex hexPattern("^hex-.*");
    if (!std::regex_match(deviceName, hexPattern)) {
      errorMessage = "XCAN: Device must be a PI hexapod (hex-*), got: " + deviceName;
      return false;
    }

    // ✅ REQUIRED PARAMETER: mode (coarse/fine)
    auto modeIt = params.find("mode");
    if (modeIt == params.end()) {
      errorMessage = "XCAN: Missing required parameter 'mode' (coarse/fine/ultrafine)";
      return false;
    }
    std::string mode = modeIt->second;
    if (mode != "coarse" && mode != "fine" && mode != "ultrafine") {
      errorMessage = "XCAN: Invalid mode '" + mode + "' (must be 'coarse', 'fine', or 'ultrafine')";
      return false;
    }

    // ✅ REQUIRED PARAMETER: channel (data channel to optimize)
    auto channelIt = params.find("channel");
    if (channelIt == params.end()) {
      errorMessage = "XCAN: Missing required parameter 'channel'";
      return false;
    }
    std::string channel = channelIt->second;
    if (channel.empty()) {
      errorMessage = "XCAN: Data channel cannot be empty - please select a data channel in the recipe configuration";
      return false;
    }

    // ✅ OPTIONAL PARAMETER: settling_time_ms (default 300)
    int settlingTimeMs = 300;
    auto settlingIt = params.find("settling_time_ms");
    if (settlingIt != params.end()) {
      settlingTimeMs = std::stoi(settlingIt->second);
    }

    // ✅ GET DATA STORE
    if (!deps.dataStore) {
      errorMessage = "XCAN: DataStore not available";
      return false;
    }

    if (deps.logger) {
      deps.logger->Info(
        "XCAN: Starting " + mode + " scan on " + deviceName +
        " for channel '" + channel + "' (settling=" + std::to_string(settlingTimeMs) + "ms)",
        "MotionHandler"
      );
    }

    // ✅ CREATE SCAN PARAMETERS
    ScanParameters scanParams;
    scanParams.axesToScan = { "Z", "Y", "X" };  // Scan in ZYX order

    // Set step sizes based on mode
    if (mode == "coarse") {
      // Coarse: 10, 5, 2, 1 µm = 0.010, 0.005, 0.002, 0.001 mm
      scanParams.stepSizes = { 0.010, 0.005, 0.002, 0.001 };
    } else if (mode == "fine") {
      // Fine: 2, 1, 0.5 µm = 0.002, 0.001, 0.0005 mm
      scanParams.stepSizes = { 0.002, 0.001, 0.0005 };
    } else {
      // Ultra Fine: 0.5, 0.2, 0.1 µm = 0.0005, 0.0002, 0.0001 mm
      scanParams.stepSizes = { 0.0005, 0.0002, 0.0001 };
    }

    scanParams.motionSettleTimeMs = settlingTimeMs;
    scanParams.velocityPercent = 50.0;

    // Termination criteria
    scanParams.consecutiveDecreasesLimit = 3;
    scanParams.significantDecreaseThreshold = 0.05;  // 5%
    scanParams.maxTotalDistance = (mode == "ultrafine") ? 0.1 : 0.2;

    // Position optimization
    scanParams.improvementThreshold = 0.01;  // 1%

    // Measurement parameters
    scanParams.averagingCount = 3;
    scanParams.measurementTimeout = std::chrono::seconds(5);
    scanParams.minValidValue = -1000.0;
    scanParams.maxValidValue = 1000.0;

    // Safety limits
    scanParams.maxStepSize = 0.5;  // 500µm max
    scanParams.scanTimeout = std::chrono::minutes(10);

    // ✅ CREATE SCAN CONTROLLER
    AdaptiveScanController scanController(
      *motionManager,
      *deps.dataStore,
      deviceName,
      channel
    );

    if (deps.logger) {
      scanController.SetLogger(deps.logger);
    }

    // ✅ SETUP COMPLETION TRACKING
    std::atomic<bool> scanComplete(false);
    std::atomic<bool> scanSuccess(false);
    std::string scanErrorMsg;
    std::mutex completionMutex;
    std::condition_variable completionCV;

    // Completion callback
    scanController.SetCompletionCallback(
      [&](const ScanResults& results) {
        std::lock_guard<std::mutex> lock(completionMutex);
        scanComplete = true;
        scanSuccess = results.globalPeak.valid;
        if (deps.logger) {
          if (results.globalPeak.valid) {
            deps.logger->Info(
              "XCAN: Scan complete - Peak value: " + std::to_string(results.globalPeak.value),
              "MotionHandler"
            );
          } else {
            deps.logger->Warning("XCAN: Scan complete - No peak found", "MotionHandler");
          }
        }
        completionCV.notify_one();
      }
    );

    // Error callback
    scanController.SetErrorCallback(
      [&](const ScanError& error) {
        std::lock_guard<std::mutex> lock(completionMutex);
        scanComplete = true;
        scanSuccess = false;
        scanErrorMsg = error.message;
        if (deps.logger) {
          deps.logger->Error("XCAN: Scan error - " + error.message, "MotionHandler");
        }
        completionCV.notify_one();
      }
    );

    // ✅ START SCAN
    if (!scanController.StartScan(scanParams)) {
      errorMessage = "XCAN: Failed to start scan";
      return false;
    }

    // ✅ WAIT FOR COMPLETION WITH STOP FLAG MONITORING
    while (!scanComplete) {
      // Check if stop was requested
      if (context && context->IsStopRequestedAny()) {
        if (deps.logger) {
          deps.logger->Info("XCAN: Stop requested, halting scan", "MotionHandler");
        }
        scanController.HaltScan();
        errorMessage = "XCAN: Scan stopped by user";
        return false;
      }

      // Wait for 100ms before checking again
      std::unique_lock<std::mutex> lock(completionMutex);
      completionCV.wait_for(lock, std::chrono::milliseconds(100));
    }

    // ✅ CHECK FINAL RESULT
    if (!scanSuccess) {
      if (!scanErrorMsg.empty()) {
        errorMessage = "XCAN: Scan failed - " + scanErrorMsg;
      } else {
        errorMessage = "XCAN: Scan completed but no peak found";
      }
      return false;
    }

    if (deps.logger) {
      // Redundant with completion-callback log that already reports the peak value.
      deps.logger->Debug("XCAN: Scan completed successfully", "MotionHandler");
    }

    return true;
  }

  // ============================================================================
  // XCAN EXTENDED - FLEXIBLE ADAPTIVE SCANNING WITH CUSTOM PARAMETERS
  // ============================================================================
  if (methodName == "XCANExtended") {
    // Scan-busy indicator — ref-counted Begin on enter, End on every return path.
    ScanActivityGuard _scanBusy(deps.scanAct, "XCANExtended");

    // ✅ REQUIRED PARAMETER: axes
    auto axesIt = params.find("axes");
    if (axesIt == params.end()) {
      errorMessage = "XCANExtended: Missing required parameter 'axes'";
      return false;
    }
    std::string axesStr = axesIt->second;

    // Validate axes - must contain only valid axis characters (X, Y, Z)
    std::vector<std::string> axesToScan;
    for (char c : axesStr) {
      char upper = std::toupper(c);
      if (upper == 'X' || upper == 'Y' || upper == 'Z') {
        axesToScan.push_back(std::string(1, upper));
      } else if (c != ' ' && c != ',') {
        errorMessage = "XCANExtended: Invalid axis character '" + std::string(1, c) + "' in axes '" + axesStr + "'. Valid axes are X, Y, Z.";
        return false;
      }
    }

    if (axesToScan.empty()) {
      errorMessage = "XCANExtended: No valid axes specified in '" + axesStr + "'";
      return false;
    }

    // ✅ REQUIRED PARAMETER: step_sizes_um (comma-separated microns)
    auto stepSizesIt = params.find("step_sizes_um");
    if (stepSizesIt == params.end()) {
      errorMessage = "XCANExtended: Missing required parameter 'step_sizes_um'";
      return false;
    }
    std::string stepSizesStr = stepSizesIt->second;

    // Parse step sizes
    std::vector<double> stepSizesMm;
    std::stringstream ss(stepSizesStr);
    std::string token;
    while (std::getline(ss, token, ',')) {
      // Trim whitespace
      token.erase(0, token.find_first_not_of(" \t"));
      token.erase(token.find_last_not_of(" \t") + 1);

      if (!token.empty()) {
        try {
          double stepUm = std::stod(token);
          if (stepUm <= 0) {
            errorMessage = "XCANExtended: Step size must be positive, got: " + token;
            return false;
          }
          // Convert microns to mm
          stepSizesMm.push_back(stepUm / 1000.0);
        } catch (...) {
          errorMessage = "XCANExtended: Invalid step size value: " + token;
          return false;
        }
      }
    }

    if (stepSizesMm.empty()) {
      errorMessage = "XCANExtended: No valid step sizes specified in '" + stepSizesStr + "'";
      return false;
    }

    // Sort step sizes descending (largest first)
    std::sort(stepSizesMm.begin(), stepSizesMm.end(), std::greater<double>());

    // ✅ REQUIRED PARAMETER: channel
    auto channelIt = params.find("channel");
    if (channelIt == params.end()) {
      errorMessage = "XCANExtended: Missing required parameter 'channel'";
      return false;
    }
    std::string channel = channelIt->second;
    if (channel.empty()) {
      errorMessage = "XCANExtended: Data channel cannot be empty";
      return false;
    }

    // ✅ OPTIONAL PARAMETER: settling_time_ms (default 300)
    int settlingTimeMs = 300;
    auto settlingIt = params.find("settling_time_ms");
    if (settlingIt != params.end() && !settlingIt->second.empty()) {
      settlingTimeMs = std::stoi(settlingIt->second);
    }

    // ✅ OPTIONAL PARAMETER: exit_threshold (early exit if value exceeds threshold)
    double exitThreshold = std::numeric_limits<double>::max();
    bool hasExitThreshold = false;
    auto exitThresholdIt = params.find("exit_threshold");
    if (exitThresholdIt != params.end() && !exitThresholdIt->second.empty()) {
      try {
        exitThreshold = std::stod(exitThresholdIt->second);
        hasExitThreshold = true;
      } catch (...) {
        errorMessage = "XCANExtended: Invalid exit_threshold value: " + exitThresholdIt->second;
        return false;
      }
    }

    // ✅ OPTIONAL PARAMETER: capture_as (store result in context)
    std::string captureAs;
    auto captureAsIt = params.find("capture_as");
    if (captureAsIt != params.end()) {
      captureAs = captureAsIt->second;
    }

    // ✅ GET DATA STORE
    if (!deps.dataStore) {
      errorMessage = "XCANExtended: DataStore not available";
      return false;
    }

    if (deps.logger) {
      std::string axesLog;
      for (const auto& axis : axesToScan) axesLog += axis;

      std::string stepsLog;
      for (size_t i = 0; i < stepSizesMm.size(); ++i) {
        if (i > 0) stepsLog += ",";
        stepsLog += std::to_string(stepSizesMm[i] * 1000.0) + "um";
      }

      deps.logger->Info(
        "XCANExtended: Starting scan on " + deviceName +
        " (axes=" + axesLog + ", steps=" + stepsLog +
        ", channel=" + channel +
        ", settling=" + std::to_string(settlingTimeMs) + "ms" +
        (hasExitThreshold ? ", exitThreshold=" + std::to_string(exitThreshold) : "") +
        (!captureAs.empty() ? ", captureAs=" + captureAs : "") + ")",
        "MotionHandler"
      );
    }

    // ✅ CREATE SCAN PARAMETERS
    ScanParameters scanParams;
    scanParams.axesToScan = axesToScan;
    scanParams.stepSizes = stepSizesMm;
    scanParams.motionSettleTimeMs = settlingTimeMs;
    scanParams.velocityPercent = 50.0;

    // Termination criteria
    scanParams.consecutiveDecreasesLimit = 3;
    scanParams.significantDecreaseThreshold = 0.05;  // 5%
    scanParams.maxTotalDistance = 0.2;  // 200µm per direction

    // Position optimization
    scanParams.improvementThreshold = 0.01;  // 1%

    // Measurement parameters
    scanParams.averagingCount = 3;
    scanParams.measurementTimeout = std::chrono::seconds(5);
    scanParams.minValidValue = -1000.0;
    scanParams.maxValidValue = 1000.0;

    // Safety limits
    scanParams.maxStepSize = 0.5;  // 500µm max
    scanParams.scanTimeout = std::chrono::minutes(10);

    // ✅ CREATE SCAN CONTROLLER
    AdaptiveScanController scanController(
      *motionManager,
      *deps.dataStore,
      deviceName,
      channel
    );

    if (deps.logger) {
      scanController.SetLogger(deps.logger);
    }

    // ✅ COMPLETION TRACKING
    std::atomic<bool> scanComplete{false};
    std::atomic<bool> scanSuccess{false};
    std::atomic<bool> earlyExit{false};
    std::string scanErrorMsg;
    double peakValue = 0.0;
    std::mutex completionMutex;
    std::condition_variable completionCV;

    // Progress callback - check exit threshold
    scanController.SetDataPointCallback(
      [&](const ScanDataPoint& dataPoint) {
        if (hasExitThreshold && dataPoint.value > exitThreshold && !earlyExit.load()) {
          earlyExit.store(true);
          if (deps.logger) {
            deps.logger->Info(
              "XCANExtended: Exit threshold reached (" + std::to_string(dataPoint.value) +
              " > " + std::to_string(exitThreshold) + "), stopping scan",
              "MotionHandler"
            );
          }
          // The scan will halt and return to peak on next iteration
          scanController.HaltScan();
        }
      }
    );

    // Completion callback
    scanController.SetCompletionCallback(
      [&](const ScanResults& results) {
        std::lock_guard<std::mutex> lock(completionMutex);
        scanComplete = true;
        scanSuccess = results.globalPeak.valid;
        peakValue = results.globalPeak.value;
        if (deps.logger) {
          if (results.globalPeak.valid) {
            deps.logger->Info(
              "XCANExtended: Scan complete - Peak value: " + std::to_string(results.globalPeak.value),
              "MotionHandler"
            );
          } else {
            deps.logger->Warning("XCANExtended: Scan complete - No peak found", "MotionHandler");
          }
        }
        completionCV.notify_one();
      }
    );

    // Error callback
    scanController.SetErrorCallback(
      [&](const ScanError& error) {
        std::lock_guard<std::mutex> lock(completionMutex);
        scanComplete = true;
        // Early exit due to threshold is not an error
        if (earlyExit.load() && error.type == ScanErrorType::UserHalt) {
          scanSuccess = true;  // Consider it a success
        } else {
          scanSuccess = false;
          scanErrorMsg = error.message;
          if (deps.logger) {
            deps.logger->Error("XCANExtended: Scan error - " + error.message, "MotionHandler");
          }
        }
        completionCV.notify_one();
      }
    );

    // ✅ START SCAN
    if (!scanController.StartScan(scanParams)) {
      errorMessage = "XCANExtended: Failed to start scan";
      return false;
    }

    // ✅ WAIT FOR COMPLETION WITH STOP FLAG MONITORING
    while (!scanComplete) {
      // Check if stop was requested
      if (context && context->IsStopRequestedAny()) {
        if (deps.logger) {
          deps.logger->Info("XCANExtended: Stop requested, halting scan", "MotionHandler");
        }
        scanController.HaltScan();
        errorMessage = "XCANExtended: Scan stopped by user";
        return false;
      }

      // Wait for 100ms before checking again
      std::unique_lock<std::mutex> lock(completionMutex);
      completionCV.wait_for(lock, std::chrono::milliseconds(100));
    }

    // Get final peak value from scan results
    ScanResults results = scanController.GetResults();
    if (results.globalPeak.valid) {
      peakValue = results.globalPeak.value;
    }

    // ✅ CAPTURE PEAK VALUE TO CONTEXT IF REQUESTED
    if (!captureAs.empty() && context) {
      context->SetValue(captureAs, static_cast<float>(peakValue), deviceName, "XCANExtended");
      if (deps.logger) {
        deps.logger->Debug(
          "XCANExtended: Captured peak value " + std::to_string(peakValue) + " to context:" + captureAs,
          "MotionHandler"
        );
      }
    }

    // ✅ CHECK FINAL RESULT
    if (!scanSuccess && !earlyExit.load()) {
      if (!scanErrorMsg.empty()) {
        errorMessage = "XCANExtended: Scan failed - " + scanErrorMsg;
      } else {
        errorMessage = "XCANExtended: Scan completed but no peak found";
      }
      return false;
    }

    if (deps.logger) {
      // Redundant with completion-callback log that already reports the peak value,
      // except in the early-exit case — keep INFO only for early-exit, DEBUG otherwise.
      std::string completionReason = earlyExit.load() ? " (early exit - threshold reached)" : "";
      if (earlyExit.load()) {
        deps.logger->Info("XCANExtended: Scan completed successfully" + completionReason, "MotionHandler");
      } else {
        deps.logger->Debug("XCANExtended: Scan completed successfully" + completionReason, "MotionHandler");
      }
    }

    return true;
  }

  // ============================================================================
  // AXIS SCAN - 1D scan on a single axis with peak/valley detection
  // ============================================================================
  if (methodName == "AxisScan") {
    // REQUIRED: axis
    auto axisIt = params.find("axis");
    if (axisIt == params.end() || axisIt->second.empty()) {
      errorMessage = "AxisScan: Missing required parameter 'axis'";
      return false;
    }
    std::string axis = axisIt->second;

    // REQUIRED: range_um
    auto rangeIt = params.find("range_um");
    if (rangeIt == params.end() || rangeIt->second.empty()) {
      errorMessage = "AxisScan: Missing required parameter 'range_um'";
      return false;
    }
    double rangeUm = std::stod(rangeIt->second);
    if (rangeUm <= 0 || rangeUm > 50000) {
      errorMessage = "AxisScan: range_um must be between 1 and 50000 um";
      return false;
    }
    double rangeMm = rangeUm / 1000.0;

    // REQUIRED: step_size_um
    auto stepIt = params.find("step_size_um");
    if (stepIt == params.end() || stepIt->second.empty()) {
      errorMessage = "AxisScan: Missing required parameter 'step_size_um'";
      return false;
    }
    double stepSizeUm = std::stod(stepIt->second);
    if (stepSizeUm < 0.1 || stepSizeUm > 1000) {
      errorMessage = "AxisScan: step_size_um must be between 0.1 and 1000 um";
      return false;
    }
    double stepMm = stepSizeUm / 1000.0;

    // REQUIRED: channel
    auto channelIt = params.find("channel");
    if (channelIt == params.end() || channelIt->second.empty()) {
      errorMessage = "AxisScan: Missing required parameter 'channel'";
      return false;
    }
    std::string channel = channelIt->second;

    // OPTIONAL: direction (default "centered")
    std::string direction = "centered";
    auto dirIt = params.find("direction");
    if (dirIt != params.end() && !dirIt->second.empty()) {
      direction = dirIt->second;
    }

    // OPTIONAL: seek_mode (default "peak")
    bool seekPeak = true;
    auto seekIt = params.find("seek_mode");
    if (seekIt != params.end() && seekIt->second == "valley") {
      seekPeak = false;
    }

    // OPTIONAL: velocity (mm/s, 0 = don't change)
    double scanVelocity = 0.0;
    auto velIt = params.find("velocity");
    if (velIt != params.end() && !velIt->second.empty()) {
      scanVelocity = std::stod(velIt->second);
    }

    // OPTIONAL: settling_time_ms (default 200)
    int settlingTimeMs = 200;
    auto settleIt = params.find("settling_time_ms");
    if (settleIt != params.end() && !settleIt->second.empty()) {
      settlingTimeMs = std::stoi(settleIt->second);
      if (settlingTimeMs < 10) settlingTimeMs = 10;
      if (settlingTimeMs > 5000) settlingTimeMs = 5000;
    }

    // OPTIONAL: exit_threshold
    bool hasExitThreshold = false;
    double exitThreshold = 0.0;
    auto exitIt = params.find("exit_threshold");
    if (exitIt != params.end() && !exitIt->second.empty()) {
      exitThreshold = std::stod(exitIt->second);
      hasExitThreshold = true;
    }

    // OPTIONAL: return_to_best (default true)
    bool returnToBest = true;
    auto retIt = params.find("return_to_best");
    if (retIt != params.end() && (retIt->second == "false" || retIt->second == "0")) {
      returnToBest = false;
    }

    // OPTIONAL: capture_as, capture_position_as
    std::string captureAs;
    auto capIt = params.find("capture_as");
    if (capIt != params.end()) captureAs = capIt->second;
    std::string capturePosAs;
    auto capPosIt = params.find("capture_position_as");
    if (capPosIt != params.end()) capturePosAs = capPosIt->second;

    // OPTIONAL: process_uid
    std::string processUid;
    auto uidIt = params.find("process_uid");
    if (uidIt != params.end()) processUid = uidIt->second;

    // ---- Get current position as baseline ----
    PositionStruct baselinePos = motionManager->GetCurrentPosition(deviceName);
    double baselineAxisPos = 0.0;
    if (axis == "X") baselineAxisPos = baselinePos.x;
    else if (axis == "Y") baselineAxisPos = baselinePos.y;
    else if (axis == "Z") baselineAxisPos = baselinePos.z;
    else if (axis == "U") baselineAxisPos = baselinePos.u;
    else if (axis == "V") baselineAxisPos = baselinePos.v;
    else if (axis == "W") baselineAxisPos = baselinePos.w;
    else {
      errorMessage = "AxisScan: Invalid axis '" + axis + "' (must be X/Y/Z/U/V/W)";
      return false;
    }

    // ---- Compute scan start and end ----
    double scanStart = 0.0, scanEnd = 0.0;
    if (direction == "positive") {
      scanStart = baselineAxisPos;
      scanEnd = baselineAxisPos + rangeMm;
    } else if (direction == "negative") {
      scanStart = baselineAxisPos;
      scanEnd = baselineAxisPos - rangeMm;
    } else {  // centered
      scanStart = baselineAxisPos - rangeMm / 2.0;
      scanEnd = baselineAxisPos + rangeMm / 2.0;
    }

    // Ensure iterStart < iterEnd for step arithmetic
    double iterStart = std::min(scanStart, scanEnd);
    double iterEnd   = std::max(scanStart, scanEnd);
    int numSteps = static_cast<int>(std::round((iterEnd - iterStart) / stepMm)) + 1;
    if (numSteps < 2) numSteps = 2;

    // Direction of iteration: if negative direction, iterate from high to low
    bool iterateReverse = (scanStart > scanEnd);

    if (deps.logger) {
      deps.logger->Info("AxisScan: device=" + deviceName + " axis=" + axis +
                     " range=" + std::to_string(rangeUm) + "um step=" + std::to_string(stepSizeUm) + "um" +
                     " direction=" + direction + " seek=" + (seekPeak ? "peak" : "valley") +
                     " baseline=" + std::to_string(baselineAxisPos) + "mm" +
                     " start=" + std::to_string(scanStart) + "mm end=" + std::to_string(scanEnd) + "mm" +
                     " steps=" + std::to_string(numSteps), "MotionHandler");
    }

    // ---- Save and set velocity ----
    double savedVelocity = 0.0;
    bool velocityChanged = false;
    if (scanVelocity > 0.0) {
      if (motionManager->GetVelocity(deviceName, axis, savedVelocity)) {
        if (motionManager->SetVelocity(deviceName, axis, scanVelocity)) {
          velocityChanged = true;
          if (deps.logger) {
            deps.logger->Info("AxisScan: Set velocity " + axis + "=" + std::to_string(scanVelocity) +
                           "mm/s (saved: " + std::to_string(savedVelocity) + "mm/s)", "MotionHandler");
          }
        }
      }
    }

    // ---- Move to scan start position ----
    double scanStartPos = iterateReverse ? iterEnd : iterStart;
    if (!motionManager->MoveToPosition(deviceName, axis, scanStartPos, false)) {
      errorMessage = "AxisScan: Failed to move to scan start";
      if (velocityChanged) motionManager->SetVelocity(deviceName, axis, savedVelocity);
      return false;
    }
    for (int w = 0; w < 1000 && motionManager->IsMoving(deviceName, axis); ++w) {
      if (context && context->IsStopRequestedAny()) {
        if (velocityChanged) motionManager->SetVelocity(deviceName, axis, savedVelocity);
        errorMessage = "AxisScan: Stopped by user before scan started";
        return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // Settle at start
    std::this_thread::sleep_for(std::chrono::milliseconds(settlingTimeMs));

    // ---- Scan loop ----
    double bestValue    = seekPeak ? -1e30 : 1e30;
    double bestPosition = scanStartPos;
    bool earlyExit  = false;
    bool userStopped = false;
    int measuredPoints = 0;
    nlohmann::json measurements = nlohmann::json::array();

    constexpr int MAX_SCAN_STEPS = 100000;  // Safety bound
    for (int step = 0; step < numSteps && step < MAX_SCAN_STEPS && !earlyExit && !userStopped; ++step) {
      // Check stop request
      if (context && context->IsStopRequestedAny()) {
        if (deps.logger) {
          deps.logger->Info("AxisScan: Stop requested at step " + std::to_string(step), "MotionHandler");
        }
        userStopped = true;
        break;
      }

      // Compute target position for this step
      double targetPos = iterateReverse
          ? (iterEnd   - step * stepMm)
          : (iterStart + step * stepMm);

      // Clamp to valid range
      if (targetPos < iterStart) targetPos = iterStart;
      if (targetPos > iterEnd)   targetPos = iterEnd;

      // Move to position
      if (!motionManager->MoveToPosition(deviceName, axis, targetPos, false)) {
        errorMessage = "AxisScan: Failed to start move at step " + std::to_string(step);
        if (velocityChanged) motionManager->SetVelocity(deviceName, axis, savedVelocity);
        return false;
      }

      // Wait for move completion (10 s timeout)
      auto moveStart = std::chrono::steady_clock::now();
      for (int w = 0; w < 1000 && motionManager->IsMoving(deviceName, axis); ++w) {
        if (context && context->IsStopRequestedAny()) {
          userStopped = true;
          break;
        }
        if (std::chrono::steady_clock::now() - moveStart > std::chrono::seconds(10)) {
          errorMessage = "AxisScan: Timeout waiting for " + axis + " motion at step " + std::to_string(step);
          if (velocityChanged) motionManager->SetVelocity(deviceName, axis, savedVelocity);
          return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      if (userStopped) break;

      // Settling time
      std::this_thread::sleep_for(std::chrono::milliseconds(settlingTimeMs));

      // Read channel value
      double value = static_cast<double>(deps.dataStore->GetValue(channel, 0.0f));

      // Record measurement
      PositionStruct fullPos = motionManager->GetCurrentPosition(deviceName);
      {
        nlohmann::json point;
        point["step"]          = step;
        point["target_pos_mm"] = targetPos;
        point["value"]         = value;
        point["x"] = fullPos.x;
        point["y"] = fullPos.y;
        point["z"] = fullPos.z;
        point["u"] = fullPos.u;
        point["v"] = fullPos.v;
        point["w"] = fullPos.w;
        measurements.push_back(point);
      }
      measuredPoints++;

      // Track best value
      bool newBest = seekPeak ? (value > bestValue) : (value < bestValue);
      if (newBest) {
        bestValue    = value;
        bestPosition = targetPos;
        if (deps.logger) {
          deps.logger->Info("AxisScan: New best at step " + std::to_string(step) +
                         " pos=" + std::to_string(targetPos) + "mm value=" + std::to_string(value),
                         "MotionHandler");
        }
      }

      // Check exit threshold
      if (hasExitThreshold) {
        bool thresholdMet = seekPeak ? (value >= exitThreshold) : (value <= exitThreshold);
        if (thresholdMet) {
          if (deps.logger) {
            deps.logger->Info("AxisScan: Exit threshold met at step " + std::to_string(step) +
                           " value=" + std::to_string(value) + " threshold=" + std::to_string(exitThreshold),
                           "MotionHandler");
          }
          earlyExit = true;
        }
      }

      // Log progress every 10 steps
      if (step > 0 && step % 10 == 0 && deps.logger) {
        deps.logger->Info("AxisScan: Progress " + std::to_string(step) + "/" + std::to_string(numSteps) +
                       " best=" + std::to_string(bestValue) + " at " + std::to_string(bestPosition) + "mm",
                       "MotionHandler");
      }
    }

    // ---- Return to best or baseline ----
    if (returnToBest || userStopped) {
      const bool noBestFound = (seekPeak ? (bestValue == -1e30) : (bestValue == 1e30));
      double returnPos = noBestFound ? baselineAxisPos : bestPosition;
      if (deps.logger) {
        deps.logger->Info("AxisScan: Returning to " +
                       std::string(noBestFound ? "baseline" : "best") +
                       " position: " + std::to_string(returnPos) + "mm", "MotionHandler");
      }
      if (!motionManager->MoveToPosition(deviceName, axis, returnPos, false)) {
        if (deps.logger) {
          deps.logger->Warning("AxisScan: Failed to start return move", "MotionHandler");
        }
      } else {
        auto retStart = std::chrono::steady_clock::now();
        for (int w = 0; w < 1000 && motionManager->IsMoving(deviceName, axis); ++w) {
          if (std::chrono::steady_clock::now() - retStart > std::chrono::seconds(10)) break;
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
      }
    }

    // ---- Restore velocity ----
    if (velocityChanged) {
      motionManager->SetVelocity(deviceName, axis, savedVelocity);
      if (deps.logger) {
        deps.logger->Info("AxisScan: Restored velocity " + axis + "=" + std::to_string(savedVelocity) + "mm/s",
                       "MotionHandler");
      }
    }

    // ---- Capture results to context ----
    if (context) {
      if (!captureAs.empty()) {
        context->SetValue(captureAs, std::to_string(bestValue));
      }
      if (!capturePosAs.empty()) {
        context->SetValue(capturePosAs, std::to_string(bestPosition));
      }
      context->SetValue("axis_scan.best_value",        std::to_string(bestValue));
      context->SetValue("axis_scan.best_position",     std::to_string(bestPosition));
      context->SetValue("axis_scan.measured_points",   std::to_string(measuredPoints));
      context->SetValue("axis_scan.baseline_position", std::to_string(baselineAxisPos));
    }

    // ---- Save results to file ----
    {
      nlohmann::json result;
      result["device"]               = deviceName;
      result["axis"]                 = axis;
      result["seek_mode"]            = seekPeak ? "peak" : "valley";
      result["direction"]            = direction;
      result["range_um"]             = rangeUm;
      result["step_size_um"]         = stepSizeUm;
      result["settling_time_ms"]     = settlingTimeMs;
      result["baseline_position_mm"] = baselineAxisPos;
      result["scan_start_mm"]        = scanStart;
      result["scan_end_mm"]          = scanEnd;
      result["best_value"]           = bestValue;
      result["best_position_mm"]     = bestPosition;
      result["measured_points"]      = measuredPoints;
      result["early_exit"]           = earlyExit;
      result["user_stopped"]         = userStopped;
      result["channel"]              = channel;
      result["measurements"]         = measurements;

      std::string filename = "logs/axis_scan_result";
      if (!processUid.empty()) filename += "_" + processUid;
      filename += ".json";

      std::ofstream outFile(filename);
      if (outFile.is_open()) {
        outFile << result.dump(2);
        outFile.close();
        if (deps.logger) {
          deps.logger->Info("AxisScan: Results saved to " + filename, "MotionHandler");
        }
      }
    }

    if (deps.logger) {
      deps.logger->Info("AxisScan: Completed. " + std::to_string(measuredPoints) + " points measured. Best " +
                     (seekPeak ? "peak" : "valley") + "=" + std::to_string(bestValue) +
                     " at " + std::to_string(bestPosition) + "mm" +
                     (earlyExit ? " (early exit)" : "") + (userStopped ? " (user stopped)" : ""),
                     "MotionHandler");
    }

    if (userStopped) {
      errorMessage = "AxisScan: Stopped by user (returned to best position)";
      return false;
    }
    return true;
  }

  return std::nullopt;
}
