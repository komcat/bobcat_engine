// include/motions/scanning/RotationScanController.h
#pragma once

#include "unified_motion_manager.h"
#include "DataStore.h"
#include "RotationScanTypes.h"
#include "RotationScanStrategy.h"
#include "ILogger.h"

#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <functional>

/**
 * @brief Controller for rotation optimization scanning
 *
 * Manages the execution of rotation scans including:
 * - Thread management for background scanning
 * - State machine for scan phases
 * - Progress and completion callbacks
 * - Halt/stop handling
 *
 * Usage:
 *   RotationScanController controller(motionManager, dataStore, "hex-right", "power_channel");
 *   controller.SetCompletionCallback([](const RotationScanResults& results) { ... });
 *   controller.StartScan(params);
 */
class RotationScanController {
public:
  RotationScanController(
    UnifiedMotionManager& motionManager,
    DataStore& dataStore,
    const std::string& deviceName,
    const std::string& dataChannel);

  ~RotationScanController();

  // ============================================================================
  // CONFIGURATION
  // ============================================================================

  /**
   * @brief Set logger for scan operations
   */
  void SetLogger(ILogger* logger) { m_logger = logger; }

  // ============================================================================
  // SCAN CONTROL
  // ============================================================================

  /**
   * @brief Start rotation scan with specified parameters
   * @param params Rotation scan parameters
   * @return true if scan started successfully
   */
  bool StartScan(const RotationScanParameters& params);

  /**
   * @brief Halt the current scan gracefully
   * Returns to best position found before stopping
   */
  void HaltScan();

  /**
   * @brief Emergency stop - immediate halt
   */
  void EmergencyStop();

  /**
   * @brief Check if scan is currently active
   */
  bool IsScanActive() const { return m_state != RotationScanState::Idle; }

  /**
   * @brief Get current scan state
   */
  RotationScanState GetState() const { return m_state; }

  // ============================================================================
  // CALLBACKS
  // ============================================================================

  using ProgressCallback = std::function<void(const RotationScanProgressEvent&)>;
  using PeakFoundCallback = std::function<void(const PeakInfo&)>;
  using StateChangedCallback = std::function<void(RotationScanState oldState, RotationScanState newState)>;
  using CompletionCallback = std::function<void(const RotationScanResults&)>;
  using ErrorCallback = std::function<void(const std::string&)>;

  void SetProgressCallback(ProgressCallback callback) { m_progressCallback = callback; }
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
  RotationScanResults GetResults() const;

private:
  // ============================================================================
  // CORE COMPONENTS
  // ============================================================================

  UnifiedMotionManager& m_motionManager;
  DataStore& m_dataStore;
  ILogger* m_logger = nullptr;

  std::string m_deviceName;
  std::string m_dataChannel;

  // State
  std::atomic<RotationScanState> m_state;
  RotationScanParameters m_parameters;

  // Strategy and context
  std::unique_ptr<RotationScanStrategy> m_strategy;
  std::unique_ptr<RotationScanContext> m_context;

  // Results
  RotationScanResults m_results;

  // Thread management
  std::thread m_scanThread;
  std::atomic<bool> m_haltRequested;
  std::atomic<bool> m_emergencyStopRequested;

  // Progress tracking
  std::atomic<double> m_progress;

  // Callbacks
  ProgressCallback m_progressCallback;
  PeakFoundCallback m_peakFoundCallback;
  StateChangedCallback m_stateCallback;
  CompletionCallback m_completionCallback;
  ErrorCallback m_errorCallback;

  // ============================================================================
  // SCAN EXECUTION (runs in separate thread)
  // ============================================================================

  void ScanThreadFunction();
  bool ExecuteScan();
  void HandleScanCompletion(bool success);
  void HandleScanError(const std::string& message);

  // ============================================================================
  // STATE MANAGEMENT
  // ============================================================================

  void ChangeState(RotationScanState newState);
  void NotifyProgress(double progress, const std::string& message);
  void NotifyError(const std::string& message);

  // ============================================================================
  // VALIDATION
  // ============================================================================

  bool ValidateParameters(const RotationScanParameters& params, std::string& error);
  bool ValidateDeviceReady(std::string& error);
};
