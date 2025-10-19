// include/scanning/ScanStrategy.h
#pragma once

#include "ScanTypes.h"
#include "ScanContext.h"
#include <memory>

/**
 * @brief Strategy pattern interface for different scan algorithms
 *
 * Allows for different scanning approaches:
 * - CoarseToFine: Multiple passes with decreasing step sizes (legacy behavior)
 * - SinglePass: One pass with fixed step size
 * - AdaptiveStep: Dynamically adjust step size based on gradient
 * - SpiralScan: 2D spiral pattern
 * - GridScan: Full grid coverage
 */
class IScanStrategy {
public:
  virtual ~IScanStrategy() = default;

  /**
   * @brief Execute the scan strategy
   * @param context Provides access to motion, measurements, and progress
   * @return true if scan completed successfully
   */
  virtual bool Execute(ScanContext& context) = 0;

  /**
   * @brief Get strategy name for logging
   */
  virtual std::string GetName() const = 0;

  /**
   * @brief Estimate total steps for progress calculation
   */
  virtual int EstimateTotalSteps(const ScanParameters& params) const = 0;
};

// ============================================================================
// COARSE TO FINE STRATEGY (Legacy behavior)
// ============================================================================

/**
 * @brief Multi-pass scan with progressively finer step sizes
 *
 * Matches original algorithm:
 * 1. Scan all axes with coarsest step
 * 2. Return to global peak
 * 3. Repeat with finer steps
 */
class CoarseToFineStrategy : public IScanStrategy {
public:
  bool Execute(ScanContext& context) override;
  std::string GetName() const override { return "CoarseToFine"; }
  int EstimateTotalSteps(const ScanParameters& params) const override;

private:
  bool ScanAllAxesAtStepSize(ScanContext& context, double stepSize, size_t stepIndex);
  bool ScanSingleAxis(ScanContext& context, const std::string& axis, double stepSize);
  std::pair<double, PositionStruct> ScanDirection(
    ScanContext& context,
    const std::string& axis,
    double stepSize,
    int direction);
};

// ============================================================================
// SINGLE PASS STRATEGY
// ============================================================================

/**
 * @brief Single pass through all axes with one step size
 */
class SinglePassStrategy : public IScanStrategy {
public:
  explicit SinglePassStrategy(double stepSize) : m_stepSize(stepSize) {}

  bool Execute(ScanContext& context) override;
  std::string GetName() const override { return "SinglePass"; }
  int EstimateTotalSteps(const ScanParameters& params) const override;

private:
  double m_stepSize;
};

// ============================================================================
// ADAPTIVE STEP STRATEGY
// ============================================================================

/**
 * @brief Dynamically adjust step size based on gradient
 *
 * - Large steps when far from peak (low gradient)
 * - Small steps near peak (high gradient)
 * - Efficient for finding peaks in large search spaces
 */
class AdaptiveStepStrategy : public IScanStrategy {
public:
  struct AdaptiveParams {
    double initialStepSize = 0.010;  // 10 microns
    double minStepSize = 0.001;       // 1 micron
    double maxStepSize = 0.050;       // 50 microns
    double gradientThresholdHigh = 10.0;  // Reduce step if gradient > this
    double gradientThresholdLow = 1.0;    // Increase step if gradient < this
    double stepAdjustmentFactor = 0.5;    // Multiply/divide by this
  };

  explicit AdaptiveStepStrategy(const AdaptiveParams& params = AdaptiveParams())
    : m_params(params) {
  }

  bool Execute(ScanContext& context) override;
  std::string GetName() const override { return "AdaptiveStep"; }
  int EstimateTotalSteps(const ScanParameters& params) const override;

private:
  AdaptiveParams m_params;

  double CalculateNextStepSize(double currentStep, double gradient);
};

// ============================================================================
// GRID SCAN STRATEGY
// ============================================================================

/**
 * @brief Complete grid coverage over defined range
 *
 * Useful for:
 * - Full characterization
 * - Multiple peaks
 * - Heat maps
 */
class GridScanStrategy : public IScanStrategy {
public:
  struct GridParams {
    double xStart = -1.0;
    double xEnd = 1.0;
    double yStart = -1.0;
    double yEnd = 1.0;
    double stepSize = 0.010;
    bool snakePattern = true;  // Reduces backlash
  };

  explicit GridScanStrategy(const GridParams& params) : m_params(params) {}

  bool Execute(ScanContext& context) override;
  std::string GetName() const override { return "GridScan"; }
  int EstimateTotalSteps(const ScanParameters& params) const override;

private:
  GridParams m_params;
};

// ============================================================================
// SPIRAL SCAN STRATEGY
// ============================================================================

/**
 * @brief Spiral pattern from center outward
 *
 * Good for:
 * - Circular features
 * - Centering operations
 * - Lens alignment
 */
class SpiralScanStrategy : public IScanStrategy {
public:
  struct SpiralParams {
    double maxRadius = 2.0;
    double angleStep = 15.0;  // degrees
    double radiusStep = 0.050;  // mm per revolution
  };

  explicit SpiralScanStrategy(const SpiralParams& params) : m_params(params) {}

  bool Execute(ScanContext& context) override;
  std::string GetName() const override { return "SpiralScan"; }
  int EstimateTotalSteps(const ScanParameters& params) const override;

private:
  SpiralParams m_params;
};

// ============================================================================
// STRATEGY FACTORY
// ============================================================================

/**
 * @brief Factory for creating scan strategies
 */
class ScanStrategyFactory {
public:
  enum class StrategyType {
    CoarseToFine,
    SinglePass,
    AdaptiveStep,
    GridScan,
    SpiralScan
  };

  static std::unique_ptr<IScanStrategy> Create(StrategyType type);
  static std::unique_ptr<IScanStrategy> CreateCoarseToFine();
  static std::unique_ptr<IScanStrategy> CreateSinglePass(double stepSize);
  static std::unique_ptr<IScanStrategy> CreateAdaptiveStep(
    const AdaptiveStepStrategy::AdaptiveParams& params = {});
  static std::unique_ptr<IScanStrategy> CreateGridScan(
    const GridScanStrategy::GridParams& params);
  static std::unique_ptr<IScanStrategy> CreateSpiralScan(
    const SpiralScanStrategy::SpiralParams& params);
};