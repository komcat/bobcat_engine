// src/motions/scanning/RotationScanStrategy.cpp
#include "RotationScanStrategy.h"
#include "unified_motion_manager.h"
#include "DataStore.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <iostream>
#include <thread>

// ============================================================================
// ROTATION SCAN CONTEXT IMPLEMENTATION
// ============================================================================

RotationScanContext::RotationScanContext(
  UnifiedMotionManager& motionManager,
  DataStore& dataStore,
  const std::string& deviceName,
  const std::string& dataChannel,
  const RotationScanParameters& parameters)
  : m_motionManager(motionManager)
  , m_dataStore(dataStore)
  , m_deviceName(deviceName)
  , m_dataChannel(dataChannel)
  , m_parameters(parameters)
{
}

bool RotationScanContext::MoveToPosition(const PositionStruct& position) {
  if (IsHaltRequested() || IsEmergencyStopRequested()) {
    return false;
  }

  // Move all 6 axes (XYZUVW) together using coordinated motion
  bool success = m_motionManager.MoveToAbs6(m_deviceName, position, true);

  if (!success) {
    std::cerr << "[RotationScanContext] Failed to move to position (XYZUVW)" << std::endl;
    return false;
  }

  SleepInterruptible(std::chrono::milliseconds(m_parameters.motionSettleTimeMs));
  return success;
}

bool RotationScanContext::MoveRelative(const std::string& axis, double distance) {
  if (IsHaltRequested() || IsEmergencyStopRequested()) {
    return false;
  }

  bool success = m_motionManager.MoveRelative(m_deviceName, axis, distance, true);
  return success;
}

PositionStruct RotationScanContext::GetCurrentPosition() const {
  return m_motionManager.GetCurrentPosition(m_deviceName);
}

bool RotationScanContext::IsMoving() const {
  return m_motionManager.IsDeviceInMotion(m_deviceName);
}

bool RotationScanContext::WaitForMotionComplete(std::chrono::milliseconds timeout) {
  auto startTime = std::chrono::steady_clock::now();

  while (IsMoving()) {
    if (IsHaltRequested() || IsEmergencyStopRequested()) {
      return false;
    }

    auto elapsed = std::chrono::steady_clock::now() - startTime;
    if (elapsed > timeout) {
      return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return true;
}

double RotationScanContext::GetMeasurement() {
  float value;
  if (m_dataStore.TryGetValue(m_dataChannel, value)) {
    return static_cast<double>(value);
  }
  return 0.0;
}

double RotationScanContext::GetAveragedMeasurement(int numSamples) {
  if (numSamples <= 0) numSamples = 1;

  double sum = 0.0;
  for (int i = 0; i < numSamples; ++i) {
    sum += GetMeasurement();
    if (i < numSamples - 1) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }
  return sum / numSamples;
}

bool RotationScanContext::ExecuteTranslationScan(double& peakValue, PositionStruct& peakPosition) {
  const auto& params = m_parameters;
  auto translationAxes = params.GetTranslationAxes();

  // Get baseline for early exit check
  double baseline = GetBaseline().value;
  double earlyExitThreshold = baseline * (1.0 + params.improvementThreshold);

  // Initialize with current position/value
  peakPosition = GetCurrentPosition();
  peakValue = GetAveragedMeasurement(params.averagingCount);

  // Reset LOCAL peak for this translation scan
  // Local peak = best position found at THIS rotation angle
  // Global peak = best position across ALL rotation angles (preserved)
  ResetLocalPeak();
  UpdateLocalPeak(peakValue, peakPosition, "Translation scan start");

  std::stringstream startMsg;
  startMsg << "Translation scan starting - current value: " << std::fixed << std::setprecision(6) << peakValue
           << " (early exit threshold: " << earlyExitThreshold << ")";
  ReportProgress(0.0, startMsg.str());

  // Check for early exit at start
  if (peakValue > earlyExitThreshold) {
    std::stringstream earlyMsg;
    earlyMsg << "EARLY EXIT: Value " << std::fixed << std::setprecision(6) << peakValue
             << " exceeds threshold " << earlyExitThreshold << " - skipping full scan";
    ReportProgress(0.0, earlyMsg.str());

    // Pause before continuing
    SleepInterruptible(std::chrono::milliseconds(params.earlyExitPauseMs));

    // Update global peak
    UpdateGlobalPeak(peakValue, peakPosition, "Early exit - threshold exceeded");
    return true;
  }

  // For each step size (fine scanning: 2→1→0.5 µm)
  for (size_t stepIndex = 0; stepIndex < params.translationStepSizes.size(); ++stepIndex) {
    double stepSize = params.translationStepSizes[stepIndex];

    if (IsHaltRequested() || IsEmergencyStopRequested()) {
      return false;
    }

    // Scan all translation axes at this step size
    for (const auto& axis : translationAxes) {
      if (IsHaltRequested() || IsEmergencyStopRequested()) {
        return false;
      }

      std::stringstream msg;
      msg << "Translation scan: " << axis << " axis, "
          << std::fixed << std::setprecision(1) << (stepSize * 1000) << " µm steps";
      ReportProgress(0.0, msg.str());

      double axisbestValue = 0.0;
      PositionStruct axisBestPosition;

      if (!ScanTranslationAxis(axis, stepSize, axisbestValue, axisBestPosition)) {
        if (IsHaltRequested()) {
          return false;
        }
        continue;
      }

      // Update LOCAL peak if this axis found better
      if (axisbestValue > peakValue) {
        peakValue = axisbestValue;
        peakPosition = axisBestPosition;
        UpdateLocalPeak(peakValue, peakPosition, axis + " axis scan");

        // Check for early exit after axis scan
        if (peakValue > earlyExitThreshold) {
          std::stringstream earlyMsg;
          earlyMsg << "EARLY EXIT: Value " << std::fixed << std::setprecision(6) << peakValue
                   << " exceeds threshold - skipping remaining steps";
          ReportProgress(0.0, earlyMsg.str());

          // Move to best position and pause
          MoveToPosition(peakPosition);
          SleepInterruptible(std::chrono::milliseconds(params.earlyExitPauseMs));

          // Update global peak
          UpdateGlobalPeak(peakValue, peakPosition, "Early exit - threshold exceeded");
          return true;
        }
      }

      // Small delay between axes
      SleepInterruptible(std::chrono::milliseconds(100));
    }

    // After completing all axes at this step size, move to LOCAL best position and verify
    PeakInfo localPeak = GetLocalPeak();
    if (localPeak.valid) {
      MoveToPosition(localPeak.position);
      SleepInterruptible(std::chrono::milliseconds(params.motionSettleTimeMs));

      // Read current value at best position
      double verifiedValue = GetAveragedMeasurement(params.averagingCount);
      peakPosition = GetCurrentPosition();

      // Update peaks if verification shows better value
      if (verifiedValue > peakValue) {
        peakValue = verifiedValue;
        UpdateLocalPeak(verifiedValue, peakPosition, "Step size verification");
      }

      // Report power after this step size
      std::stringstream stepCompleteMsg;
      stepCompleteMsg << "Step size " << std::fixed << std::setprecision(1) << (stepSize * 1000)
                      << " µm complete - peak value: " << std::setprecision(6) << peakValue;
      ReportProgress(0.0, stepCompleteMsg.str());

      // Check for early exit after step size complete
      if (peakValue > earlyExitThreshold) {
        std::stringstream earlyMsg;
        earlyMsg << "EARLY EXIT: Value " << std::fixed << std::setprecision(6) << peakValue
                 << " exceeds threshold - skipping remaining step sizes";
        ReportProgress(0.0, earlyMsg.str());

        SleepInterruptible(std::chrono::milliseconds(params.earlyExitPauseMs));
        UpdateGlobalPeak(peakValue, peakPosition, "Early exit - threshold exceeded");
        return true;
      }
    }

    SleepInterruptible(std::chrono::milliseconds(100));
  }

  // Final verification at LOCAL peak position (best XY at this rotation angle)
  PeakInfo finalLocalPeak = GetLocalPeak();
  if (finalLocalPeak.valid) {
    MoveToPosition(finalLocalPeak.position);
    SleepInterruptible(std::chrono::milliseconds(params.motionSettleTimeMs));

    // Final power reading
    peakValue = GetAveragedMeasurement(params.averagingCount);
    peakPosition = GetCurrentPosition();

    // Update local peak with final verified value
    UpdateLocalPeak(peakValue, peakPosition, "Final translation scan verification");

    // Also update GLOBAL peak if this is the best across all rotations
    UpdateGlobalPeak(peakValue, peakPosition, "Translation scan result");

    std::stringstream finalMsg;
    finalMsg << "Translation scan complete - final peak value: " << std::fixed << std::setprecision(6) << peakValue;
    ReportProgress(0.0, finalMsg.str());
  }

  return true;
}

bool RotationScanContext::ScanTranslationAxis(
  const std::string& axis,
  double stepSize,
  double& bestValue,
  PositionStruct& bestPosition) {

  const auto& params = m_parameters;

  // Store start position
  PositionStruct startPosition = GetCurrentPosition();

  // Scan positive direction
  auto [positiveMaxValue, positiveMaxPosition] = ScanTranslationDirection(axis, stepSize, 1);

  if (IsHaltRequested()) {
    return false;
  }

  // Return to start for negative scan
  if (!MoveToPosition(startPosition)) {
    return false;
  }
  SleepInterruptible(std::chrono::milliseconds(params.motionSettleTimeMs));

  if (IsHaltRequested()) {
    return false;
  }

  // Scan negative direction
  auto [negativeMaxValue, negativeMaxPosition] = ScanTranslationDirection(axis, stepSize, -1);

  // Determine best
  if (positiveMaxValue > negativeMaxValue) {
    bestValue = positiveMaxValue;
    bestPosition = positiveMaxPosition;
  } else {
    bestValue = negativeMaxValue;
    bestPosition = negativeMaxPosition;
  }

  // Update global peak
  std::stringstream context;
  context << axis << " translation scan @ " << std::fixed << std::setprecision(1)
          << (stepSize * 1000) << " µm";
  UpdateGlobalPeak(bestValue, bestPosition, context.str());

  // Move to best position
  if (!MoveToPosition(bestPosition)) {
    return false;
  }
  SleepInterruptible(std::chrono::milliseconds(params.motionSettleTimeMs));

  return true;
}

std::pair<double, PositionStruct> RotationScanContext::ScanTranslationDirection(
  const std::string& axis,
  double stepSize,
  int direction) {

  const auto& params = m_parameters;

  PositionStruct currentPosition = GetCurrentPosition();
  double maxValue = GetMeasurement();
  PositionStruct maxPosition = currentPosition;

  double previousValue = maxValue;
  int consecutiveDecreases = 0;
  double totalDistance = 0.0;
  bool hasMovedFromMax = false;

  while (!IsHaltRequested() &&
         !IsEmergencyStopRequested() &&
         consecutiveDecreases < params.consecutiveDecreasesLimit &&
         totalDistance < params.maxTranslationDistance) {

    // Move relative
    if (!MoveRelative(axis, direction * stepSize)) {
      break;
    }

    // Wait for settling
    SleepInterruptible(std::chrono::milliseconds(params.motionSettleTimeMs));

    // Get new position and value
    currentPosition = GetCurrentPosition();
    double currentValue = GetMeasurement();
    totalDistance += stepSize;

    // Check for new maximum
    if (currentValue > maxValue) {
      maxValue = currentValue;
      maxPosition = currentPosition;
      consecutiveDecreases = 0;
      hasMovedFromMax = false;

      // Update global peak
      UpdateGlobalPeak(maxValue, maxPosition, "Translation direction scan");
    } else {
      consecutiveDecreases++;
      hasMovedFromMax = true;

      // Check for significant decrease
      double relativeDecrease = 0.0;
      if (previousValue > 0) {
        relativeDecrease = (previousValue - currentValue) / previousValue;
      }
      if (relativeDecrease > params.significantDecreaseThreshold) {
        break;
      }
    }

    previousValue = currentValue;
  }

  // Return to local maximum if we moved away
  if (hasMovedFromMax && !IsHaltRequested()) {
    MoveToPosition(maxPosition);
    SleepInterruptible(std::chrono::milliseconds(params.motionSettleTimeMs));

    // Verify
    double verificationValue = GetMeasurement();
    if (verificationValue > maxValue) {
      maxValue = verificationValue;
      UpdateGlobalPeak(maxValue, maxPosition, "Translation peak verification");
    }
  }

  return { maxValue, maxPosition };
}

bool RotationScanContext::UpdateGlobalPeak(double value, const PositionStruct& position,
                                            const std::string& context) {
  if (!m_globalPeak.valid || value > m_globalPeak.value) {
    m_globalPeak.value = value;
    m_globalPeak.position = position;
    m_globalPeak.timestamp = std::chrono::system_clock::now();
    m_globalPeak.context = context;
    m_globalPeak.valid = true;

    if (m_peakFoundCallback) {
      m_peakFoundCallback(m_globalPeak);
    }
    return true;
  }
  return false;
}

PeakInfo RotationScanContext::GetGlobalPeak() const {
  return m_globalPeak;
}

bool RotationScanContext::UpdateLocalPeak(double value, const PositionStruct& position,
                                           const std::string& context) {
  if (!m_localPeak.valid || value > m_localPeak.value) {
    m_localPeak.value = value;
    m_localPeak.position = position;
    m_localPeak.timestamp = std::chrono::system_clock::now();
    m_localPeak.context = context;
    m_localPeak.valid = true;
    return true;
  }
  return false;
}

PeakInfo RotationScanContext::GetLocalPeak() const {
  return m_localPeak;
}

void RotationScanContext::ResetLocalPeak() {
  m_localPeak = PeakInfo();  // Reset to default (invalid)
}

void RotationScanContext::SetBaseline(double value, const PositionStruct& position) {
  m_baseline.value = value;
  m_baseline.position = position;
  m_baseline.timestamp = std::chrono::system_clock::now();
  m_baseline.valid = true;
}

BaselineInfo RotationScanContext::GetBaseline() const {
  return m_baseline;
}

void RotationScanContext::ReportProgress(double progress, const std::string& message) {
  if (m_progressCallback) {
    m_progressCallback(progress, message);
  }
}

bool RotationScanContext::IsHaltRequested() const {
  return m_haltRequested && m_haltRequested->load();
}

bool RotationScanContext::IsEmergencyStopRequested() const {
  return m_emergencyStopRequested && m_emergencyStopRequested->load();
}

bool RotationScanContext::SleepInterruptible(std::chrono::milliseconds duration) {
  auto endTime = std::chrono::steady_clock::now() + duration;

  while (std::chrono::steady_clock::now() < endTime) {
    if (IsHaltRequested() || IsEmergencyStopRequested()) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return true;
}

// ============================================================================
// ROTATION SCAN STRATEGY IMPLEMENTATION
// ============================================================================

bool RotationScanStrategy::Execute(RotationScanContext& context) {
  const auto& params = context.GetParameters();
  m_rotationSteps.clear();
  m_positiveImproved = false;
  m_negativeImproved = false;

  // 1. Record baseline
  PositionStruct startPosition = context.GetCurrentPosition();
  double baselineValue = context.GetAveragedMeasurement(params.averagingCount);
  context.SetBaseline(baselineValue, startPosition);
  context.UpdateGlobalPeak(baselineValue, startPosition, "Baseline");

  std::stringstream baselineMsg;
  baselineMsg << "Baseline recorded: " << std::fixed << std::setprecision(6) << baselineValue
              << " at position " << params.GetRotationAxisName() << "="
              << std::setprecision(3) << startPosition.w;
  context.ReportProgress(0.0, baselineMsg.str());

  if (context.IsHaltRequested()) {
    return false;
  }

  // 2. Scan positive direction first
  context.ReportProgress(0.1, "Scanning positive " + params.GetRotationAxisName() + " direction...");

  bool posCompleted = ScanRotationDirection(context, +1, m_positiveImproved);

  if (!posCompleted) {
    // Halted - return to global peak
    PeakInfo peak = context.GetGlobalPeak();
    if (peak.valid) {
      context.MoveToPosition(peak.position);
    }
    return false;
  }

  // 3. Check if positive direction found improvement
  if (m_positiveImproved) {
    // Positive direction succeeded - return to peak and done
    PeakInfo peak = context.GetGlobalPeak();
    context.ReportProgress(0.95, "Positive direction improved - returning to peak");

    if (peak.valid) {
      context.MoveToPosition(peak.position);
      double finalValue = context.GetAveragedMeasurement(params.averagingCount);
      context.UpdateGlobalPeak(finalValue, context.GetCurrentPosition(), "Final verification");
    }

    context.ReportProgress(1.0, "Rotation scan complete - improvement found in positive direction");
    return true;
  }

  // 4. Positive direction failed - return to start and try negative
  context.ReportProgress(0.5, "No improvement in positive direction - returning to start");
  context.MoveToPosition(startPosition);

  if (context.IsHaltRequested()) {
    return false;
  }

  // 5. Reset global peak to baseline for negative direction
  // (We want to compare against original baseline, not any intermediate values)
  context.SetBaseline(baselineValue, startPosition);

  // 6. Scan negative direction
  context.ReportProgress(0.55, "Scanning negative " + params.GetRotationAxisName() + " direction...");

  bool negCompleted = ScanRotationDirection(context, -1, m_negativeImproved);

  if (!negCompleted) {
    // Halted - return to global peak
    PeakInfo peak = context.GetGlobalPeak();
    if (peak.valid) {
      context.MoveToPosition(peak.position);
    }
    return false;
  }

  // 7. Final result
  PeakInfo finalPeak = context.GetGlobalPeak();

  if (m_negativeImproved && finalPeak.valid) {
    // Negative direction found improvement
    context.ReportProgress(0.95, "Negative direction improved - returning to peak");
    context.MoveToPosition(finalPeak.position);
    double finalValue = context.GetAveragedMeasurement(params.averagingCount);
    context.UpdateGlobalPeak(finalValue, context.GetCurrentPosition(), "Final verification");
    context.ReportProgress(1.0, "Rotation scan complete - improvement found in negative direction");
    return true;
  }

  // 8. Both directions failed - return to original start position
  context.ReportProgress(0.95, "No improvement found - returning to start position");
  context.MoveToPosition(startPosition);
  context.ReportProgress(1.0, "Rotation scan complete - no improvement found");

  return true;
}

bool RotationScanStrategy::ScanRotationDirection(
  RotationScanContext& context,
  int direction,
  bool& foundImprovement) {

  const auto& params = context.GetParameters();
  std::string rotAxis = params.GetRotationAxisName();

  foundImprovement = false;
  int consecutiveNoImprovement = 0;
  double totalRotation = 0.0;
  double previousPeakValue = context.GetBaseline().value;

  // Get starting position for this direction
  PositionStruct directionStartPosition = context.GetCurrentPosition();

  while (!context.IsHaltRequested() &&
         !context.IsEmergencyStopRequested() &&
         consecutiveNoImprovement < params.maxNoImprovementCount &&
         std::abs(totalRotation) < params.maxRotationRange) {

    // a. Make rotation step
    double rotationAmount = direction * params.rotationStep;
    totalRotation += rotationAmount;

    std::stringstream rotMsg;
    rotMsg << "Rotating " << rotAxis << " by " << std::fixed << std::setprecision(2)
           << rotationAmount << "° (total: " << totalRotation << "°)";
    context.ReportProgress(0.0, rotMsg.str());

    if (!context.MoveRelative(rotAxis, rotationAmount)) {
      std::cerr << "[RotationScan] Failed to rotate " << rotAxis << std::endl;
      break;
    }

    context.SleepInterruptible(std::chrono::milliseconds(params.motionSettleTimeMs));

    if (context.IsHaltRequested()) {
      return false;
    }

    // b. Run translation scan (XY, XZ, or YZ)
    double peakValue = 0.0;
    PositionStruct peakPosition;

    std::stringstream scanMsg;
    scanMsg << "Running translation scan at " << rotAxis << "=" << std::fixed
            << std::setprecision(2) << totalRotation << "°";
    context.ReportProgress(0.0, scanMsg.str());

    if (!context.ExecuteTranslationScan(peakValue, peakPosition)) {
      if (context.IsHaltRequested()) {
        return false;
      }
      // Translation scan failed but not halted - continue
      consecutiveNoImprovement++;
      continue;
    }

    // c. Record this rotation step result
    RotationStepResult stepResult;
    stepResult.rotationAngle = totalRotation;
    stepResult.direction = (direction > 0) ? RotationDirection::Positive : RotationDirection::Negative;
    stepResult.peakValue = peakValue;
    stepResult.peakPosition = peakPosition;
    stepResult.timestamp = std::chrono::system_clock::now();

    // Calculate improvements
    double baseline = context.GetBaseline().value;
    if (baseline > 0) {
      stepResult.improvementFromBaseline = (peakValue - baseline) / baseline;
    }
    if (previousPeakValue > 0) {
      stepResult.improvementFromPrevious = (peakValue - previousPeakValue) / previousPeakValue;
    }

    // d. Check if improved over baseline by threshold
    bool thisStepImproved = stepResult.improvementFromBaseline >= params.improvementThreshold;
    stepResult.improved = thisStepImproved;

    m_rotationSteps.push_back(stepResult);

    std::stringstream resultMsg;
    resultMsg << "Rotation step complete at " << rotAxis << "=" << std::fixed << std::setprecision(2) << totalRotation << "°"
              << " - peak=" << std::setprecision(6) << peakValue
              << " (baseline=" << context.GetBaseline().value << ")"
              << ", change=" << std::setprecision(2) << (stepResult.improvementFromBaseline * 100) << "%"
              << (thisStepImproved ? " [IMPROVED]" : " [no improvement]");
    context.ReportProgress(0.0, resultMsg.str());

    // e. Decision logic
    if (thisStepImproved) {
      // This step improved - continue in this direction
      foundImprovement = true;
      consecutiveNoImprovement = 0;

      // Check if we should continue or if peak value decreased from previous
      if (previousPeakValue > 0 && peakValue < previousPeakValue * (1.0 - params.improvementThreshold)) {
        // Value decreased significantly from previous - we passed the peak
        // Stop and return to best position
        std::stringstream stopMsg;
        stopMsg << "Peak value decreased - stopping " << (direction > 0 ? "positive" : "negative")
                << " direction";
        context.ReportProgress(0.0, stopMsg.str());
        break;
      }
    } else {
      // No improvement over baseline
      consecutiveNoImprovement++;

      std::stringstream noImpMsg;
      noImpMsg << "No improvement (" << consecutiveNoImprovement << "/"
               << params.maxNoImprovementCount << ")";
      context.ReportProgress(0.0, noImpMsg.str());

      if (consecutiveNoImprovement >= params.maxNoImprovementCount) {
        // Stopping this direction
        break;
      }
    }

    previousPeakValue = peakValue;
  }

  return true;  // Completed (not halted)
}
