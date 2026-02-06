// src/scanning/CoarseToFineStrategy.cpp
#include "ScanStrategy.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

// ============================================================================
// COARSE TO FINE STRATEGY - Matches Legacy Algorithm
// ============================================================================

bool CoarseToFineStrategy::Execute(ScanContext& context) {
  const auto& params = context.GetParameters();

  // Iterate through step sizes (coarse to fine)
  for (size_t stepIndex = 0; stepIndex < params.stepSizes.size(); ++stepIndex) {
    double stepSize = params.stepSizes[stepIndex];

    if (context.IsHaltRequested() || context.IsEmergencyStopRequested()) {
      return false;
    }

    // Scan all axes at this step size
    if (!ScanAllAxesAtStepSize(context, stepSize, stepIndex)) {
      return false;
    }

    // Small delay between step sizes
    if (!context.SleepInterruptible(std::chrono::milliseconds(100))) {
      return false;
    }
  }

  return true;
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

    // Small delay between axes
    if (!context.SleepInterruptible(std::chrono::milliseconds(100))) {
      return false;
    }

    // Return to global peak if significantly better
    PeakInfo globalPeak = context.GetGlobalPeak();
    if (globalPeak.valid) {
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
        context.SleepInterruptible(std::chrono::milliseconds(params.motionSettleTimeMs));
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

  // Scan positive direction
  auto [positiveMaxValue, positiveMaxPosition] = ScanDirection(context, axis, stepSize, 1);

  if (context.IsHaltRequested()) {
    return false;
  }

  // Return to start for negative scan
  if (!context.MoveToPosition(startPosition)) {
    return false;
  }

  context.SleepInterruptible(std::chrono::milliseconds(params.motionSettleTimeMs));

  if (context.IsHaltRequested()) {
    return false;
  }

  // Scan negative direction
  auto [negativeMaxValue, negativeMaxPosition] = ScanDirection(context, axis, stepSize, -1);

  // Determine best position
  double bestValue = std::max(positiveMaxValue, negativeMaxValue);
  PositionStruct bestPosition = (positiveMaxValue > negativeMaxValue) ?
    positiveMaxPosition : negativeMaxPosition;

  // Update global peak
  std::stringstream contextStr;
  contextStr << axis << " axis scan with " << std::fixed << std::setprecision(3)
    << (stepSize * 1000) << " micron steps";

  context.UpdateGlobalPeak(bestValue, bestPosition, contextStr.str());

  // Move to best position found
  if (!context.MoveToPosition(bestPosition)) {
    return false;
  }

  context.SleepInterruptible(std::chrono::milliseconds(params.motionSettleTimeMs));

  return true;
}

std::pair<double, PositionStruct> CoarseToFineStrategy::ScanDirection(
  ScanContext& context,
  const std::string& axis,
  double stepSize,
  int direction) {

  const auto& params = context.GetParameters();

  // Get starting position and value
  PositionStruct currentPosition = context.GetCurrentPosition();
  double maxValue = context.GetMeasurement();
  PositionStruct maxPosition = currentPosition;

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

    // Wait for settling
    context.SleepInterruptible(std::chrono::milliseconds(params.motionSettleTimeMs));

    // Get new position and value
    currentPosition = context.GetCurrentPosition();
    double currentValue = context.GetMeasurement();
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
    dataPoint.relativeChange = (currentValue - previousValue) / previousValue;

    // Check for new maximum
    if (currentValue > maxValue) {
      maxValue = currentValue;
      maxPosition = currentPosition;
      consecutiveDecreases = 0;
      hasMovedFromMax = false;
      dataPoint.isPeak = true;
    }
    else {
      consecutiveDecreases++;
      hasMovedFromMax = true;

      // Check for significant decrease
      if (relativeDecrease > SIGNIFICANT_DECREASE_THRESHOLD) {
        break;
      }
    }

    // Record data point
    context.RecordDataPoint(dataPoint);

    previousValue = currentValue;
  }

  // Return to local maximum if we moved away
  if (hasMovedFromMax) {
    if (!context.MoveToPosition(maxPosition)) {
      return { maxValue, maxPosition };
    }

    context.SleepInterruptible(std::chrono::milliseconds(params.motionSettleTimeMs));

    // Verify
    double verificationValue = context.GetMeasurement();
    if (verificationValue > maxValue) {
      maxValue = verificationValue;
    }
  }

  return { maxValue, maxPosition };
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

bool AdaptiveStepStrategy::Execute(ScanContext& /*context*/) {
  // TODO: Implement
  return false;
}

int AdaptiveStepStrategy::EstimateTotalSteps(const ScanParameters& /*params*/) const {
  return 100;
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