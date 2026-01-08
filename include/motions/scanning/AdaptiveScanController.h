// include/scanning/AdaptiveScanController.h
#pragma once

#include "unified_motion_manager.h"
#include "DataStore.h"
#include "ScanTypes.h"
#include "ScanStateMachine.h"
#include "ScanStrategy.h"
#include "ScanDataCollector.h"
#include "ILogger.h"

#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <functional>

// Forward declaration
class SafetyWatchManager;

/**
 * @brief Modern adaptive scanning controller with state machine architecture
 *
 * Key improvements over legacy:
 * - Uses UnifiedMotionManager instead of PIController
 * - Uses DataStore for measurements
 * - State machine for clear scan phases
 * - Strategy pattern for different scan algorithms
 * - Better separation of concerns
 * - Enhanced testability
 */
class AdaptiveScanController {
public:
  AdaptiveScanController(
    UnifiedMotionManager& motionManager,
    DataStore& dataStore,
    const std::string& deviceName,
    const std::string& dataChannel);

  ~AdaptiveScanController();

  // ============================================================================
  // CONFIGURATION
  // ============================================================================

  /**
   * @brief Set logger for scan operations
   * @param logger Pointer to ILogger implementation
   */
  void SetLogger(ILogger* logger) { m_logger = logger; }

  /**
   * @brief Subscribe to SafetyWatchManager for emergency stop on critical safety
   * @param safetyManager Pointer to SafetyWatchManager
   */
  void SetSafetyWatchManager(SafetyWatchManager* safetyManager);



  // ============================================================================
  // SCAN CONTROL
  // ============================================================================

  /**
   * @brief Start scan with specified parameters and strategy
   * @param params Scan parameters (step sizes, axes, limits, etc.)
   * @param strategy Scan strategy (CoarseToFine, SinglePass, Adaptive, etc.)
   * @return true if scan started successfully
   */
  bool StartScan(const ScanParameters& params,
    std::unique_ptr<IScanStrategy> strategy = nullptr);

  /**
   * @brief Halt the current scan gracefully
   * Returns to best position found before stopping
   */
  void HaltScan();

  /**
   * @brief Emergency stop - immediate halt without returning
   */
  void EmergencyStop();

  /**
   * @brief Check if scan is currently active
   */
  bool IsScanActive() const { return m_state != ScanState::Idle; }

  /**
   * @brief Get current scan state
   */
  ScanState GetState() const { return m_state; }

  // ============================================================================
  // CALLBACKS
  // ============================================================================

  using ProgressCallback = std::function<void(const ScanProgressEvent&)>;
  using DataPointCallback = std::function<void(const ScanDataPoint&)>;
  using PeakFoundCallback = std::function<void(const PeakInfo&)>;
  using StateChangedCallback = std::function<void(ScanState oldState, ScanState newState)>;
  using CompletionCallback = std::function<void(const ScanResults&)>;
  using ErrorCallback = std::function<void(const ScanError&)>;

  void SetProgressCallback(ProgressCallback callback) { m_progressCallback = callback; }
  void SetDataPointCallback(DataPointCallback callback) { m_dataPointCallback = callback; }
  void SetPeakFoundCallback(PeakFoundCallback callback) { m_peakFoundCallback = callback; }
  void SetStateChangedCallback(StateChangedCallback callback) { m_stateCallback = callback; }
  void SetCompletionCallback(CompletionCallback callback) { m_completionCallback = callback; }
  void SetErrorCallback(ErrorCallback callback) { m_errorCallback = callback; }

  // ============================================================================
  // QUERY METHODS
  // ============================================================================

  /**
   * @brief Get baseline position and value
   */
  BaselineInfo GetBaseline() const;

  /**
   * @brief Get current global peak
   */
  PeakInfo GetGlobalPeak() const;

  /**
   * @brief Get current scan progress (0.0 to 1.0)
   */
  double GetProgress() const;

  /**
   * @brief Get scan results (available during and after scan)
   */
  ScanResults GetResults() const;

  /**
   * @brief Get current device position
   */
  PositionStruct GetCurrentPosition() const;

private:
  // ============================================================================
  // CORE COMPONENTS
  // ============================================================================

  UnifiedMotionManager& m_motionManager;
  DataStore& m_dataStore;
  // Change the private member (around line 110):
  ILogger* m_logger;  // Changed from Logger* to ILogger*

  std::string m_deviceName;
  std::string m_dataChannel;

  // State machine
  std::atomic<ScanState> m_state;
  std::unique_ptr<ScanStateMachine> m_stateMachine;

  // Scan strategy
  std::unique_ptr<IScanStrategy> m_strategy;
  ScanParameters m_parameters;

  // Data collection
  std::unique_ptr<ScanDataCollector> m_dataCollector;

  // Thread management
  std::thread m_scanThread;
  std::atomic<bool> m_haltRequested;
  std::atomic<bool> m_emergencyStopRequested;

  // Callbacks
  ProgressCallback m_progressCallback;
  DataPointCallback m_dataPointCallback;
  PeakFoundCallback m_peakFoundCallback;
  StateChangedCallback m_stateCallback;
  CompletionCallback m_completionCallback;
  ErrorCallback m_errorCallback;

  // ============================================================================
  // SCAN EXECUTION (runs in separate thread)
  // ============================================================================

  void ScanThreadFunction();

  bool ExecuteScan();
  bool RecordBaseline();
  bool ExecuteScanStrategy();
  bool ReturnToBestPosition();
  void HandleScanCompletion(bool success);
  void HandleScanError(const std::string& message);

  // ============================================================================
  // STATE MANAGEMENT
  // ============================================================================

  void ChangeState(ScanState newState);
  void NotifyProgress(double progress, const std::string& message);
  void NotifyDataPoint(const ScanDataPoint& dataPoint);
  void NotifyPeakFound(const PeakInfo& peak);
  void NotifyError(const std::string& message, ScanErrorType type);

  // ============================================================================
  // MOTION HELPERS
  // ============================================================================

  bool MoveToPosition(const PositionStruct& position);
  bool MoveRelative(const std::string& axis, double distance);
  bool WaitForMotionComplete();

  // ============================================================================
  // MEASUREMENT HELPERS
  // ============================================================================

  double GetMeasurement();
  double GetAveragedMeasurement(int numSamples = 5);
  bool WaitForValidMeasurement(double& outValue);

  // ============================================================================
  // VALIDATION
  // ============================================================================

  bool ValidateParameters(const ScanParameters& params, std::string& error);
  bool ValidateDeviceReady(std::string& error);
};