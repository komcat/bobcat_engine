// DualHexMaximizer.h
#pragma once

#include <string>
#include <atomic>
#include <thread>
#include <functional>
#include <chrono>

// Forward declarations — the implementation pulls in the full definitions.
class UnifiedMotionManager;
class DataStore;
class ILogger;

/**
 * @brief Parameters for the dual-hexapod "2-device maximize" routine.
 *
 * The routine maximizes ONE shared signal by coordinating two hexapods:
 *  - zStepDevice  — the collimating side; steps a fixed -Z each iteration.
 *  - gridDevice   — the search side; runs a centered NxN XY grid and moves to peak.
 *
 * Beam direction (left->right vs right->left) only decides which physical
 * device fills each role; that mapping is resolved by the caller.
 */
struct DualHexMaximizeParams {
  std::string channel;        ///< Shared signal channel (DataStore key).
  std::string zStepDevice;    ///< Device that steps -Z (collimating side).
  std::string gridDevice;     ///< Device that runs the XY grid (search side).

  double zStepMm    = 0.002;  ///< Z step magnitude in mm (2 um). Applied as -Z.
  double gridStepMm = 0.0002; ///< XY grid step in mm (0.2 um).
  int    gridSize   = 3;      ///< Grid is gridSize x gridSize (odd, e.g. 3 or 5).
  int    settleMs   = 200;    ///< Settle time after each move.

  double systemVelocity       = 1.0;   ///< System velocity (mm/s) during routine.
  double improvementThreshold = 0.01;  ///< Relative improvement needed to continue (1%).
  int    averagingCount       = 3;     ///< Samples averaged per measurement.

  // Safety bounds (avoid unbounded loops / runaway travel).
  int    maxIterations = 100;          ///< Hard cap on maximize iterations.
  double maxZTravelMm  = 0.2;          ///< Abort once cumulative |Z| reaches this.

  std::chrono::seconds measurementTimeout{5};
  double minValidValue = -1000.0;
  double maxValidValue = 1000.0;
};

/// Progress / informational event surfaced to the caller.
struct DualHexProgressEvent {
  double      progress = 0.0;  ///< Best-effort 0..1.
  std::string message;
};

/// Final outcome of the routine.
struct DualHexResult {
  bool   success    = false;  ///< True if a valid peak was found and not halted.
  bool   halted     = false;  ///< True if the user stopped the routine.
  double peakValue  = 0.0;    ///< Session-peak signal value.
  int    iterations = 0;      ///< Number of maximize iterations performed.
};

/**
 * @brief Background routine that maximizes one shared signal across two hexapods.
 *
 * Runs on its own thread. All side-effects (motion, measurement) go through the
 * injected UnifiedMotionManager / DataStore. No Qt, no exceptions — a Qt
 * executor wraps this and marshals callbacks onto the UI thread.
 */
class DualHexMaximizer {
public:
  DualHexMaximizer(UnifiedMotionManager& motionManager, DataStore& dataStore);
  ~DualHexMaximizer();

  DualHexMaximizer(const DualHexMaximizer&) = delete;
  DualHexMaximizer& operator=(const DualHexMaximizer&) = delete;

  void SetLogger(ILogger* logger) { m_logger = logger; }

  using ProgressCallback   = std::function<void(const DualHexProgressEvent&)>;
  using CompletionCallback = std::function<void(const DualHexResult&)>;
  using ErrorCallback      = std::function<void(const std::string&)>;

  void SetProgressCallback(ProgressCallback cb)    { m_progressCb   = std::move(cb); }
  void SetCompletionCallback(CompletionCallback cb){ m_completionCb = std::move(cb); }
  void SetErrorCallback(ErrorCallback cb)          { m_errorCb      = std::move(cb); }

  /// Start the routine on a background thread. Returns false if already running
  /// or the parameters/devices are invalid (an error callback is fired too).
  bool Start(const DualHexMaximizeParams& params);

  /// Request a graceful halt and BLOCK until the worker has returned both
  /// devices to the session peak and exited.
  void Halt();

  bool IsActive() const { return m_active.load(); }

private:
  void   Run();                       ///< Worker-thread body.
  double GridSearchToPeak();          ///< Centered NxN XY grid on gridDevice; returns peak signal.
  double AveragedMeasurement();
  double GetMeasurement();
  bool   SleepInterruptible(std::chrono::milliseconds ms);  ///< false if halt requested.
  bool   Settle();                    ///< SleepInterruptible(settleMs).
  void   Log(const std::string& msg);
  void   ReportProgress(double progress, const std::string& msg);
  void   FireError(const std::string& msg);

  UnifiedMotionManager& m_motion;
  DataStore&            m_dataStore;
  ILogger*              m_logger = nullptr;

  DualHexMaximizeParams m_params;

  std::thread       m_thread;
  std::atomic<bool> m_active{false};
  std::atomic<bool> m_halt{false};
  double            m_lastProgress = 0.0;

  ProgressCallback   m_progressCb;
  CompletionCallback m_completionCb;
  ErrorCallback      m_errorCb;
};
