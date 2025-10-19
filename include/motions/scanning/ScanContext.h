// include/scanning/ScanContext.h
#pragma once

#include "ScanTypes.h"
#include "unified_motion_manager.h"
#include "DataStore.h"
#include "ScanDataCollector.h"
#include <functional>
#include <atomic>

/**
 * @brief Context object passed to scan strategies
 *
 * Provides controlled access to:
 * - Motion control
 * - Measurement acquisition
 * - Data collection
 * - Progress reporting
 * - Halt/stop checking
 *
 * This abstraction allows strategies to be unit tested without real hardware.
 */
class ScanContext {
public:
  ScanContext(
    UnifiedMotionManager& motionManager,
    DataStore& dataStore,
    ScanDataCollector& dataCollector,
    const std::string& deviceName,
    const std::string& dataChannel,
    const ScanParameters& parameters);

  // ============================================================================
  // MOTION CONTROL
  // ============================================================================

  /**
   * @brief Move to absolute position
   */
  bool MoveToPosition(const PositionStruct& position);

  /**
   * @brief Move relative on single axis
   */
  bool MoveRelative(const std::string& axis, double distance);

  /**
   * @brief Get current device position
   */
  PositionStruct GetCurrentPosition() const;

  /**
   * @brief Check if device is moving
   */
  bool IsMoving() const;

  /**
   * @brief Wait for all motion to complete
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

  /**
   * @brief Wait for valid measurement within timeout
   */
  bool WaitForValidMeasurement(double& outValue);

  // ============================================================================
  // DATA COLLECTION
  // ============================================================================

  /**
   * @brief Record a measurement data point
   */
  void RecordDataPoint(const ScanDataPoint& dataPoint);

  /**
   * @brief Update global peak if this value is better
   */
  bool UpdateGlobalPeak(double value, const PositionStruct& position,
    const std::string& context);

  /**
   * @brief Get current global peak
   */
  PeakInfo GetGlobalPeak() const;

  /**
   * @brief Get baseline information
   */
  BaselineInfo GetBaseline() const;

  // ============================================================================
  // PROGRESS & CONTROL
  // ============================================================================

  /**
   * @brief Report progress (0.0 to 1.0)
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
  // PARAMETERS
  // ============================================================================

  const ScanParameters& GetParameters() const { return m_parameters; }
  const std::string& GetDeviceName() const { return m_deviceName; }
  const std::string& GetDataChannel() const { return m_dataChannel; }

  // ============================================================================
  // CALLBACKS (set by AdaptiveScanController)
  // ============================================================================

  using ProgressCallback = std::function<void(double, const std::string&)>;
  using DataPointCallback = std::function<void(const ScanDataPoint&)>;
  using PeakFoundCallback = std::function<void(const PeakInfo&)>;

  void SetProgressCallback(ProgressCallback callback) { m_progressCallback = callback; }
  void SetDataPointCallback(DataPointCallback callback) { m_dataPointCallback = callback; }
  void SetPeakFoundCallback(PeakFoundCallback callback) { m_peakFoundCallback = callback; }

  // Control flags (set by AdaptiveScanController)
  void SetHaltFlag(std::atomic<bool>* haltFlag) { m_haltRequested = haltFlag; }
  void SetEmergencyStopFlag(std::atomic<bool>* emergencyFlag) { m_emergencyStopRequested = emergencyFlag; }

private:
  // References to external systems
  UnifiedMotionManager& m_motionManager;
  DataStore& m_dataStore;
  ScanDataCollector& m_dataCollector;

  // Configuration
  std::string m_deviceName;
  std::string m_dataChannel;
  ScanParameters m_parameters;

  // Current peak tracking
  PeakInfo m_globalPeak;
  BaselineInfo m_baseline;

  // Control flags (owned by AdaptiveScanController)
  std::atomic<bool>* m_haltRequested = nullptr;
  std::atomic<bool>* m_emergencyStopRequested = nullptr;

  // Callbacks
  ProgressCallback m_progressCallback;
  DataPointCallback m_dataPointCallback;
  PeakFoundCallback m_peakFoundCallback;

  // Helper methods
  bool ValidateMeasurement(double value) const;
  void WaitForSettling();
};