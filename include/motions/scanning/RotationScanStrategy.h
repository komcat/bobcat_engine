// include/motions/scanning/RotationScanStrategy.h
#pragma once

#include "RotationScanTypes.h"
#include "ScanContext.h"
#include "ScanStrategy.h"
#include <memory>
#include <functional>

// Forward declarations
class UnifiedMotionManager;
class DataStore;
class ScanDataCollector;

/**
 * @brief Context object for rotation scanning
 *
 * Extends ScanContext with rotation-specific functionality:
 * - 6-axis position tracking (XYZUVW)
 * - Rotation movements
 * - Translation scan execution
 */
class RotationScanContext {
public:
  RotationScanContext(
    UnifiedMotionManager& motionManager,
    DataStore& dataStore,
    const std::string& deviceName,
    const std::string& dataChannel,
    const RotationScanParameters& parameters);

  // ============================================================================
  // MOTION CONTROL
  // ============================================================================

  /**
   * @brief Move to absolute 6-axis position (XYZUVW)
   */
  bool MoveToPosition(const PositionStruct& position);

  /**
   * @brief Move relative on single axis (translation or rotation)
   */
  bool MoveRelative(const std::string& axis, double distance);

  /**
   * @brief Get current 6-axis position
   */
  PositionStruct GetCurrentPosition() const;

  /**
   * @brief Check if device is moving
   */
  bool IsMoving() const;

  /**
   * @brief Wait for motion to complete
   */
  bool WaitForMotionComplete(std::chrono::milliseconds timeout = std::chrono::seconds(10));

  // ============================================================================
  // MEASUREMENTS
  // ============================================================================

  /**
   * @brief Get single measurement from data store
   */
  double GetMeasurement();

  /**
   * @brief Get averaged measurement
   */
  double GetAveragedMeasurement(int numSamples);

  // ============================================================================
  // TRANSLATION SCAN EXECUTION
  // ============================================================================

  /**
   * @brief Execute a translation scan (XY, XZ, or YZ based on rotation axis)
   *
   * This runs a full CoarseToFine scan on the translation axes,
   * tracking the global peak position (all 6 axes) throughout.
   *
   * @param peakValue Output: best value found
   * @param peakPosition Output: 6-axis position at best value
   * @return true if scan completed successfully
   */
  bool ExecuteTranslationScan(double& peakValue, PositionStruct& peakPosition);

  // ============================================================================
  // PEAK TRACKING
  // ============================================================================

  /**
   * @brief Update global peak if value is better (across entire rotation scan)
   * @return true if peak was updated
   */
  bool UpdateGlobalPeak(double value, const PositionStruct& position, const std::string& context);

  /**
   * @brief Get current global peak (best across all rotation steps)
   */
  PeakInfo GetGlobalPeak() const;

  /**
   * @brief Update local peak if value is better (within current translation scan)
   * @return true if peak was updated
   */
  bool UpdateLocalPeak(double value, const PositionStruct& position, const std::string& context);

  /**
   * @brief Get current local peak (best within current translation scan)
   */
  PeakInfo GetLocalPeak() const;

  /**
   * @brief Reset local peak for new translation scan
   */
  void ResetLocalPeak();

  /**
   * @brief Set baseline information
   */
  void SetBaseline(double value, const PositionStruct& position);

  /**
   * @brief Get baseline information
   */
  BaselineInfo GetBaseline() const;

  // ============================================================================
  // PROGRESS & CONTROL
  // ============================================================================

  /**
   * @brief Report progress
   */
  void ReportProgress(double progress, const std::string& message);

  /**
   * @brief Check if halt was requested
   */
  bool IsHaltRequested() const;

  /**
   * @brief Check if emergency stop was requested
   */
  bool IsEmergencyStopRequested() const;

  /**
   * @brief Sleep for specified time while checking for halt
   * @return false if halt requested during sleep
   */
  bool SleepInterruptible(std::chrono::milliseconds duration);

  // ============================================================================
  // PARAMETERS & CONFIGURATION
  // ============================================================================

  const RotationScanParameters& GetParameters() const { return m_parameters; }
  const std::string& GetDeviceName() const { return m_deviceName; }
  const std::string& GetDataChannel() const { return m_dataChannel; }

  // ============================================================================
  // CALLBACKS
  // ============================================================================

  using ProgressCallback = std::function<void(double, const std::string&)>;
  using PeakFoundCallback = std::function<void(const PeakInfo&)>;

  void SetProgressCallback(ProgressCallback callback) { m_progressCallback = callback; }
  void SetPeakFoundCallback(PeakFoundCallback callback) { m_peakFoundCallback = callback; }

  // Control flags (set by controller)
  void SetHaltFlag(std::atomic<bool>* haltFlag) { m_haltRequested = haltFlag; }
  void SetEmergencyStopFlag(std::atomic<bool>* emergencyFlag) { m_emergencyStopRequested = emergencyFlag; }

private:
  // References to external systems
  UnifiedMotionManager& m_motionManager;
  DataStore& m_dataStore;

  // Configuration
  std::string m_deviceName;
  std::string m_dataChannel;
  RotationScanParameters m_parameters;

  // Peak tracking
  PeakInfo m_globalPeak;  // Best across entire rotation scan
  PeakInfo m_localPeak;   // Best within current translation scan (reset each rotation step)
  BaselineInfo m_baseline;

  // Control flags
  std::atomic<bool>* m_haltRequested = nullptr;
  std::atomic<bool>* m_emergencyStopRequested = nullptr;

  // Callbacks
  ProgressCallback m_progressCallback;
  PeakFoundCallback m_peakFoundCallback;

  // Helper for translation scans
  bool ScanTranslationAxis(const std::string& axis, double stepSize,
                           double& bestValue, PositionStruct& bestPosition);
  std::pair<double, PositionStruct> ScanTranslationDirection(
    const std::string& axis, double stepSize, int direction);
};

// ============================================================================
// ROTATION SCAN STRATEGY
// ============================================================================

/**
 * @brief Strategy for rotation optimization scanning
 *
 * Algorithm:
 * 1. Record baseline at starting position
 * 2. Scan positive rotation direction:
 *    a. Rotate by step (e.g., +0.2°)
 *    b. Run translation scan (XY/XZ/YZ) to find peak
 *    c. If peak improved by threshold, continue rotating
 *    d. If no improvement for N steps, stop
 * 3. If positive direction found improvement, done
 * 4. Otherwise, return to start and try negative direction
 * 5. Return to best position found
 */
class RotationScanStrategy {
public:
  RotationScanStrategy() = default;

  /**
   * @brief Execute the rotation scan
   * @param context Provides motion, measurements, and control
   * @return true if scan completed successfully
   */
  bool Execute(RotationScanContext& context);

  /**
   * @brief Get strategy name
   */
  std::string GetName() const { return "RotationScan"; }

  /**
   * @brief Get rotation step results (for reporting)
   */
  const std::vector<RotationStepResult>& GetRotationSteps() const { return m_rotationSteps; }

  /**
   * @brief Check if positive direction found improvement
   */
  bool PositiveDirectionImproved() const { return m_positiveImproved; }

  /**
   * @brief Check if negative direction found improvement
   */
  bool NegativeDirectionImproved() const { return m_negativeImproved; }

private:
  /**
   * @brief Scan one rotation direction (positive or negative)
   * @param context Scan context
   * @param direction +1 for positive, -1 for negative
   * @param foundImprovement Output: true if this direction found improvement
   * @return true if scan completed (not halted)
   */
  bool ScanRotationDirection(RotationScanContext& context, int direction, bool& foundImprovement);

  // Results tracking
  std::vector<RotationStepResult> m_rotationSteps;
  bool m_positiveImproved = false;
  bool m_negativeImproved = false;
};
