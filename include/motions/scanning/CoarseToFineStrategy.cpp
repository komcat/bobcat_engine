// src/scanning/CoarseToFineStrategy.cpp
#include "ScanStrategy.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>

// ============================================================================
// COARSE TO FINE STRATEGY - Matches Legacy Algorithm
// ============================================================================

bool CoarseToFineStrategy::Execute(ScanContext& context) {
  const auto& params = context.GetParameters();

  // Per-scan state
  m_exitHit = false;
  m_lastGoodDirection.clear();

  // Iterate through step sizes (coarse to fine)
  for (size_t stepIndex = 0; stepIndex < params.stepSizes.size(); ++stepIndex) {
    double stepSize = params.stepSizes[stepIndex];

    if (context.IsHaltRequested() || context.IsEmergencyStopRequested()) {
      return false;
    }

    // Adaptive ladder: skip a pass whose step is coarser than the zone the
    // signal currently sits in.
    if (AdaptiveEnabled(params)) {
      // Reference is the best REACHABLE signal, not one raw sample, so a
      // single noisy reading cannot bounce the ladder back to coarse.
      PeakInfo globalPeak = context.GetGlobalPeak();
      double ref = context.GetMeasurement();
      if (globalPeak.valid && globalPeak.value > ref) {
        ref = globalPeak.value;
      }

      const double zoneStep = ZoneStepSize(params, ref);

      // 1.001 tolerance so a step that equals the zone step is never skipped
      // by floating-point noise.
      if (stepSize > zoneStep * 1.001) {
        std::stringstream skipMsg;
        skipMsg << "Adaptive: skipping " << std::fixed << std::setprecision(3)
          << (stepSize * 1000) << " um pass (signal " << std::scientific
          << std::setprecision(3) << ref << " maps to "
          << std::fixed << std::setprecision(3) << (zoneStep * 1000) << " um)";
        context.ReportProgress(
          static_cast<double>(stepIndex) / static_cast<double>(params.stepSizes.size()),
          skipMsg.str());
        continue;
      }
    }

    // Scan all axes at this step size
    if (!ScanAllAxesAtStepSize(context, stepSize, stepIndex)) {
      return false;
    }

    // Early exit: a sample reached the target. Unwind and STAY on this point.
    if (m_exitHit) {
      context.ReportProgress(1.0, "Exit threshold reached");
      return true;
    }

    // No inter-pass delay: every move in the pass returned settle-verified
    // (controller on-target), and every measurement was taken through the
    // fresh-sample gate, so there is nothing left to wait for here.
  }

  return true;
}

int CoarseToFineStrategy::PreferredDirection(
  const ScanContext& context,
  const std::string& axis) const {

  if (!context.GetParameters().directionMemory) {
    return 1;
  }

  auto it = m_lastGoodDirection.find(axis);
  return (it != m_lastGoodDirection.end()) ? it->second : 1;
}

double CoarseToFineStrategy::ZoneStepSize(const ScanParameters& params, double value) const {
  const auto& steps = params.stepSizes;

  if (steps.empty()) {
    return 0.0;
  }

  const double zoneLow = (m_zoneLowOverride > 0.0) ? m_zoneLowOverride : params.zoneLow;
  const double zoneHigh = (m_zoneHighOverride > 0.0) ? m_zoneHighOverride : params.zoneHigh;

  // Degenerate bounds - fall back to the coarsest step, which is the safe
  // answer because it never gates off the coarse pass.
  if (zoneLow <= 0.0 || zoneHigh <= zoneLow) {
    return steps.front();
  }

  if (value <= zoneLow) {
    return steps.front();   // weakest signal -> coarsest step
  }

  if (value >= zoneHigh) {
    return steps.back();    // strongest signal -> finest step
  }

  // Equal log sections across [zoneLow, zoneHigh]
  const double span = std::log(zoneHigh / zoneLow);
  const double frac = std::log(value / zoneLow) / span;

  int idx = static_cast<int>(std::floor(frac * static_cast<double>(steps.size())));
  idx = std::max(0, std::min(idx, static_cast<int>(steps.size()) - 1));

  return steps[static_cast<size_t>(idx)];
}

bool CoarseToFineStrategy::CheckExitThreshold(const ScanContext& context, double value) {
  const double threshold = context.GetParameters().exitThreshold;

  if (threshold > 0.0 && value >= threshold) {
    m_exitHit = true;
  }

  return m_exitHit;
}

int CoarseToFineStrategy::EstimateTotalSteps(const ScanParameters& params) const {
  // Rough estimate: assume average of 10 measurements per direction
  int stepsPerDirection = 10;
  int directionsPerAxis = 2;  // positive and negative
  int axisCount = static_cast<int>(params.axesToScan.size());
  int stepSizeCount = static_cast<int>(params.stepSizes.size());

  return stepsPerDirection * directionsPerAxis * axisCount * stepSizeCount;
}

bool CoarseToFineStrategy::ScanAllAxesAtStepSize(
  ScanContext& context,
  double stepSize,
  size_t stepIndex) {

  const auto& params = context.GetParameters();

  // Iterate through axes
  for (size_t axisIndex = 0; axisIndex < params.axesToScan.size(); ++axisIndex) {
    const std::string& axis = params.axesToScan[axisIndex];

    if (context.IsHaltRequested() || context.IsEmergencyStopRequested()) {
      return false;
    }

    // Calculate and report progress
    size_t currentStep = axisIndex + stepIndex * params.axesToScan.size();
    double totalSteps = static_cast<double>(params.axesToScan.size() * params.stepSizes.size());
    double progress = static_cast<double>(currentStep) / totalSteps;

    std::stringstream progressMsg;
    progressMsg << "Scanning " << axis << " axis with "
      << std::fixed << std::setprecision(3) << (stepSize * 1000)
      << " micron steps";

    context.ReportProgress(progress, progressMsg.str());

    // Scan this axis
    if (!ScanSingleAxis(context, axis, stepSize)) {
      if (context.IsHaltRequested()) {
        return false;
      }
      // Continue with next axis even if this one failed
      continue;
    }

    // Early exit fired inside the axis walk - stop here and stay put.
    if (m_exitHit) {
      return true;
    }

    // No inter-axis delay - moves are settle-verified, see Execute().

    // Return to global peak if significantly better.
    // This is the guard against a fine pass wandering off a peak that a
    // coarse pass already found.
    PeakInfo globalPeak = context.GetGlobalPeak();
    if (globalPeak.valid) {
      // ScanSingleAxis left the stage on its best point and measured it, so
      // the cached value is current - no need to pay for another sample.
      double currentValue = context.GetMeasurement();
      double improvement = globalPeak.value - currentValue;
      double relativeImprovement = 0.0;

      if (currentValue > 0) {
        relativeImprovement = improvement / currentValue;
      }

      if (relativeImprovement > params.improvementThreshold) {
        if (!context.MoveToPosition(globalPeak.position)) {
          // Log warning but continue
          continue;
        }

        // We moved, so the cached sample is stale - gate a fresh one.
        double recoveredValue = context.GetFreshMeasurement();
        context.TraceSample("check", axis, 0, stepSize,
          context.GetCurrentPosition(), recoveredValue, "recovered");

        if (CheckExitThreshold(context, recoveredValue)) {
          return true;
        }
      }
    }
  }

  return true;
}

bool CoarseToFineStrategy::ScanSingleAxis(
  ScanContext& context,
  const std::string& axis,
  double stepSize) {

  const auto& params = context.GetParameters();

  // Store start position
  PositionStruct startPosition = context.GetCurrentPosition();

  // Direction memory: start with whatever last worked on this axis.
  const int firstDir = PreferredDirection(context, axis);

  // Nothing has moved since the previous measurement, so the cached sample is
  // still valid for this point - reuse it rather than paying for another.
  const double startValue = context.GetMeasurement();

  DirectionResult first = ScanDirection(
    context, axis, stepSize, firstDir, startValue, params.reuseSample);

  if (context.IsHaltRequested() || context.IsEmergencyStopRequested()) {
    return false;
  }

  if (m_exitHit) {
    return true;
  }

  if (first.improved && params.directionMemory) {
    m_lastGoodDirection[axis] = firstDir;
  }

  double bestValue = first.maxValue;
  PositionStruct bestPosition = first.maxPosition;

  // ScanDirection always leaves the stage standing on its own local best.
  bool standingOnBest = true;

  // skip_opposite: a first direction that walked uphill has already BRACKETED
  // the peak (it climbed, then came back down enough to stop), so the
  // opposite direction can only walk downhill. Skipping it is the single
  // biggest structural saving in the scan.
  const bool scanOpposite = !(params.skipOppositeDirection && first.improved);

  if (scanOpposite) {
    // Return to start for the opposite scan
    if (!context.MoveToPosition(startPosition)) {
      return false;
    }

    if (context.IsHaltRequested() || context.IsEmergencyStopRequested()) {
      return false;
    }

    // We moved, so the cached sample is stale - gate a fresh one. This is
    // also the opposite walk's starting value, so it is measured once, here.
    const double backValue = context.GetFreshMeasurement();

    if (CheckExitThreshold(context, backValue)) {
      return true;
    }

    DirectionResult second = ScanDirection(
      context, axis, stepSize, -firstDir, backValue, true);

    if (m_exitHit) {
      return true;
    }

    if (second.maxValue > bestValue) {
      bestValue = second.maxValue;
      bestPosition = second.maxPosition;
      standingOnBest = true;   // stage is on the opposite walk's best

      if (second.improved && params.directionMemory) {
        m_lastGoodDirection[axis] = -firstDir;
      }
    }
    else {
      standingOnBest = false;  // stage is on the opposite best, which lost
    }
  }

  // Update global peak
  std::stringstream contextStr;
  contextStr << axis << " axis scan with " << std::fixed << std::setprecision(3)
    << (stepSize * 1000) << " micron steps";

  context.UpdateGlobalPeak(bestValue, bestPosition, contextStr.str());

  // Move to best position found - skipped when we are already standing on it,
  // which is the common case once skip_opposite is enabled.
  if (!standingOnBest) {
    if (!context.MoveToPosition(bestPosition)) {
      return false;
    }

    const double finalValue = context.GetFreshMeasurement();
    context.TraceSample("check", axis, 0, stepSize, bestPosition, finalValue, "axis-done");

    if (CheckExitThreshold(context, finalValue)) {
      return true;
    }
  }

  return true;
}

CoarseToFineStrategy::DirectionResult CoarseToFineStrategy::ScanDirection(
  ScanContext& context,
  const std::string& axis,
  double stepSize,
  int direction,
  double startValue,
  bool startValueValid) {

  const auto& params = context.GetParameters();

  DirectionResult result;

  // Get starting position and value. When the caller can vouch that nothing
  // has moved since this point was last measured, reuse that sample instead
  // of gating for another one.
  PositionStruct currentPosition = context.GetCurrentPosition();
  double maxValue = startValueValid ? startValue : context.GetFreshMeasurement();
  PositionStruct maxPosition = currentPosition;

  // The value we entered this direction with - "improved" means some step
  // beat it, i.e. the walk went uphill and the peak is bracketed on this side.
  const double entryValue = maxValue;

  context.TraceSample("scan", axis, direction, stepSize, currentPosition, maxValue, "start");

  result.maxValue = maxValue;
  result.maxPosition = maxPosition;

  if (CheckExitThreshold(context, maxValue)) {
    return result;
  }

  double previousValue = maxValue;
  int consecutiveDecreases = 0;
  double totalDistance = 0.0;
  bool hasMovedFromMax = false;

  const double SIGNIFICANT_DECREASE_THRESHOLD = params.significantDecreaseThreshold;

  while (!context.IsHaltRequested() &&
    !context.IsEmergencyStopRequested() &&
    consecutiveDecreases < params.consecutiveDecreasesLimit &&
    totalDistance < params.maxTotalDistance) {

    // Move relative
    if (!context.MoveRelative(axis, direction * stepSize)) {
      break;
    }

    // The move returned settle-verified (controller reports on-target), so
    // there is no mechanical settle left to wait for. What IS still stale is
    // the instrument sample that was integrating during travel - gate on new
    // publishes rather than sleeping a fixed time for it.
    currentPosition = context.GetCurrentPosition();
    double currentValue = context.GetFreshMeasurement();
    totalDistance += stepSize;

    // Calculate metrics
    double relativeDecrease = 0.0;
    if (previousValue > 0) {
      relativeDecrease = (previousValue - currentValue) / previousValue;
    }

    double gradient = (currentValue - previousValue) / stepSize;

    // Create data point
    ScanDataPoint dataPoint;
    dataPoint.value = currentValue;
    dataPoint.position = currentPosition;
    dataPoint.timestamp = std::chrono::system_clock::now();
    dataPoint.axis = axis;
    dataPoint.stepSize = stepSize;
    dataPoint.direction = direction;
    dataPoint.gradient = gradient;
    // Guard the divide: previousValue is a measured signal and can legitimately
    // be 0 (dark / no coupling), which would otherwise write inf or NaN into
    // the recorded scan data.
    dataPoint.relativeChange = (previousValue != 0.0)
      ? ((currentValue - previousValue) / previousValue)
      : 0.0;

    // Check for new maximum
    if (currentValue > maxValue) {
      maxValue = currentValue;
      maxPosition = currentPosition;
      consecutiveDecreases = 0;
      hasMovedFromMax = false;
      dataPoint.isPeak = true;

      context.RecordDataPoint(dataPoint);
      context.TraceSample("scan", axis, direction, stepSize,
        currentPosition, currentValue, "better");

      // Early exit latches on this single sample and stops everything.
      if (CheckExitThreshold(context, currentValue)) {
        result.maxValue = maxValue;
        result.maxPosition = maxPosition;
        result.improved = (maxValue > entryValue);
        return result;
      }

      previousValue = currentValue;
      continue;
    }

    consecutiveDecreases++;
    hasMovedFromMax = true;

    // Check for significant decrease
    if (relativeDecrease > SIGNIFICANT_DECREASE_THRESHOLD) {
      context.TraceSample("scan", axis, direction, stepSize,
        currentPosition, currentValue, "drop-break");
      break;
    }

    // Record data point
    context.RecordDataPoint(dataPoint);
    context.TraceSample("scan", axis, direction, stepSize, currentPosition, currentValue,
      "worse(" + std::to_string(consecutiveDecreases) + "/" +
      std::to_string(params.consecutiveDecreasesLimit) + ")");

    if (CheckExitThreshold(context, currentValue)) {
      result.maxValue = maxValue;
      result.maxPosition = maxPosition;
      result.improved = (maxValue > entryValue);
      return result;
    }

    previousValue = currentValue;
  }

  result.improved = (maxValue > entryValue);

  // Return to local maximum if we moved away
  if (hasMovedFromMax) {
    if (!context.MoveToPosition(maxPosition)) {
      result.maxValue = maxValue;
      result.maxPosition = maxPosition;
      return result;
    }

    // We moved, so gate a fresh sample. This doubles as the return-to-best
    // verification, so the point is measured exactly once.
    double verificationValue = context.GetFreshMeasurement();
    context.TraceSample("scan", axis, direction, stepSize,
      maxPosition, verificationValue, "return-to-best");

    if (verificationValue > maxValue) {
      maxValue = verificationValue;
    }

    CheckExitThreshold(context, verificationValue);
  }

  result.maxValue = maxValue;
  result.maxPosition = maxPosition;
  return result;
}

// ============================================================================
// STRATEGY FACTORY
// ============================================================================

std::unique_ptr<IScanStrategy> ScanStrategyFactory::Create(StrategyType type) {
  switch (type) {
  case StrategyType::CoarseToFine:
    return CreateCoarseToFine();
  case StrategyType::SinglePass:
    return CreateSinglePass(0.005);  // Default 5 microns
  case StrategyType::AdaptiveStep:
    return CreateAdaptiveStep();
  case StrategyType::GridScan:
    return CreateGridScan(GridScanStrategy::GridParams{});
  case StrategyType::SpiralScan:
    return CreateSpiralScan(SpiralScanStrategy::SpiralParams{});
  default:
    return CreateCoarseToFine();
  }
}

std::unique_ptr<IScanStrategy> ScanStrategyFactory::CreateCoarseToFine() {
  return std::make_unique<CoarseToFineStrategy>();
}

std::unique_ptr<IScanStrategy> ScanStrategyFactory::CreateSinglePass(double stepSize) {
  return std::make_unique<SinglePassStrategy>(stepSize);
}

std::unique_ptr<IScanStrategy> ScanStrategyFactory::CreateAdaptiveStep(
  const AdaptiveStepStrategy::AdaptiveParams& params) {
  return std::make_unique<AdaptiveStepStrategy>(params);
}

std::unique_ptr<IScanStrategy> ScanStrategyFactory::CreateGridScan(
  const GridScanStrategy::GridParams& params) {
  return std::make_unique<GridScanStrategy>(params);
}

std::unique_ptr<IScanStrategy> ScanStrategyFactory::CreateSpiralScan(
  const SpiralScanStrategy::SpiralParams& params) {
  return std::make_unique<SpiralScanStrategy>(params);
}


// Stub implementations for other strategies
bool SinglePassStrategy::Execute(ScanContext& /*context*/) {
  // TODO: Implement
  return false;
}

int SinglePassStrategy::EstimateTotalSteps(const ScanParameters& /*params*/) const {
  return 50;
}

bool AdaptiveStepStrategy::Execute(ScanContext& context) {
  // Same walk as CoarseToFine; m_forceAdaptive (set in the constructor) turns
  // on the log-zone skip gate in the base Execute().
  return CoarseToFineStrategy::Execute(context);
}

int AdaptiveStepStrategy::EstimateTotalSteps(const ScanParameters& params) const {
  // The ladder skips passes, so the real count is lower than CoarseToFine's -
  // but by how much depends on the signal level at run time. Assume roughly
  // half the passes survive the gate.
  return std::max(1, CoarseToFineStrategy::EstimateTotalSteps(params) / 2);
}

bool GridScanStrategy::Execute(ScanContext& /*context*/) {
  // TODO: Implement
  return false;
}

int GridScanStrategy::EstimateTotalSteps(const ScanParameters& /*params*/) const {
  return 200;
}

bool SpiralScanStrategy::Execute(ScanContext& /*context*/) {
  // TODO: Implement
  return false;
}

int SpiralScanStrategy::EstimateTotalSteps(const ScanParameters& /*params*/) const {
  return 150;
}