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

  /// True when the scan ended because a sample reached ScanParameters::exitThreshold.
  /// The stage is left ON that point - no walk-back, no return to global peak.
  bool ExitThresholdHit() const { return m_exitHit; }

protected:
  /// Outcome of walking one axis in one direction.
  /// On return the stage is standing on maxPosition and that point has just
  /// been measured, so the caller does not need to measure it again.
  struct DirectionResult {
    double maxValue = 0.0;
    PositionStruct maxPosition;
    /// True if any step in this direction beat the value we started with -
    /// i.e. the walk went uphill, so the peak is bracketed on this side.
    bool improved = false;
  };

  bool ScanAllAxesAtStepSize(ScanContext& context, double stepSize, size_t stepIndex);
  bool ScanSingleAxis(ScanContext& context, const std::string& axis, double stepSize);
  DirectionResult ScanDirection(
    ScanContext& context,
    const std::string& axis,
    double stepSize,
    int direction,
    double startValue,
    bool startValueValid);

  /// Direction that last improved this axis (+1 / -1), for directionMemory.
  int PreferredDirection(const ScanContext& context, const std::string& axis) const;

  /// Records an exit-threshold hit. Returns true if the scan should unwind.
  bool CheckExitThreshold(const ScanContext& context, double value);

  /// Step size the adaptive ladder assigns to a signal level.
  /// Splits [zoneLow, zoneHigh] into stepSizes.size() equal LOG sections;
  /// zone i owns stepSizes[i], coarsest for the lowest zone.
  double ZoneStepSize(const ScanParameters& params, double value) const;

  /// True when the adaptive ladder should gate passes, either because the
  /// caller set ScanParameters::adaptiveLadder or because this is an
  /// AdaptiveStepStrategy instance.
  bool AdaptiveEnabled(const ScanParameters& params) const {
    return params.adaptiveLadder || m_forceAdaptive;
  }

  // Per-scan state, reset at the top of Execute().
  bool m_exitHit = false;
  std::map<std::string, int> m_lastGoodDirection;

  // Set by AdaptiveStepStrategy; makes the ladder gate active regardless of
  // the parameter.
  bool m_forceAdaptive = false;

  // Per-instance zone overrides; 0 means "use ScanParameters".
  double m_zoneLowOverride = 0.0;
  double m_zoneHighOverride = 0.0;
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
 * @brief Coarse-to-fine with the log-zone step ladder always enabled
 *
 * Same walk as CoarseToFineStrategy, but passes whose step size is coarser
 * than the zone the current signal sits in are SKIPPED. The signal level is a
 * proxy for distance from the peak (for a Gaussian coupling profile, distance
 * is proportional to sqrt(log(peak/value))), so a scan that starts strong -
 * for instance handed off from a raster scan - drops straight to fine steps
 * instead of re-walking the whole ladder.
 *
 * Zone bounds come from ScanParameters::zoneLow / zoneHigh. Read the tuning
 * rule on zoneLow in ScanTypes.h before changing them: setting zoneLow below
 * the handoff signal level gates off the coarse pass, and the coarse pass is
 * what gives the scan its side-lobe immunity.
 */
class AdaptiveStepStrategy : public CoarseToFineStrategy {
public:
  /// Optional per-instance zone overrides. Leave at 0 to use the values in
  /// ScanParameters, which is the normal path.
  struct AdaptiveParams {
    double zoneLowOverride = 0.0;
    double zoneHighOverride = 0.0;
  };

  explicit AdaptiveStepStrategy(const AdaptiveParams& params = AdaptiveParams()) {
    m_forceAdaptive = true;
    m_zoneLowOverride = params.zoneLowOverride;
    m_zoneHighOverride = params.zoneHighOverride;
  }

  bool Execute(ScanContext& context) override;
  std::string GetName() const override { return "AdaptiveStep"; }
  int EstimateTotalSteps(const ScanParameters& params) const override;
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