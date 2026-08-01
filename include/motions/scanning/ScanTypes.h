// include/scanning/ScanTypes.h
#pragma once

#include "MotionTypes.h"
#include <string>
#include <vector>
#include <chrono>
#include <map>
#include <limits>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

// ============================================================================
// ENUMS
// ============================================================================

/**
 * @brief Scan state machine states
 */
enum class ScanState {
  Idle,
  Initializing,
  RecordingBaseline,
  Scanning,
  ReturningToPeak,
  Completed,
  Error,
  Halted
};

/**
 * @brief Scan error types
 */
enum class ScanErrorType {
  DeviceNotReady,
  InvalidParameters,
  MeasurementTimeout,
  MotionFailure,
  DataStoreError,
  UserHalt,
  Unknown
};

/**
 * @brief Termination criteria for scan direction
 */
enum class TerminationReason {
  ConsecutiveDecreases,
  SignificantDecrease,
  MaxDistanceReached,
  MotionError,
  UserHalt
};

// ============================================================================
// SCAN PARAMETERS
// ============================================================================

/**
 * @brief Configuration parameters for adaptive scanning
 */
struct ScanParameters {
  // Axes and step sizes
  std::vector<std::string> axesToScan = { "Z", "X", "Y" };
  std::vector<double> stepSizes = { 0.001, 0.005, 0.010 }; // mm (1, 5, 10 microns)

  // Motion parameters
  int motionSettleTimeMs = 400;
  double velocityPercent = 50.0;

  // Termination criteria
  int consecutiveDecreasesLimit = 3;
  // Hard break when the signal drops this much relative to the previous
  // sample. TUNING NOTE: this must sit above the device's noise-dip depth or
  // it kills productive walks. On the FireDog bench, noise dips reached ~6%
  // while true post-peak drops were 50%+, so 5% ended good walks early and
  // the value was raised to 0.15. Left at 0.05 here because the right value
  // is a property of THIS machine's noise floor - measure it before changing.
  double significantDecreaseThreshold = 0.05;  // 5%
  double maxTotalDistance = 5.0;  // mm

  // Position optimization
  double improvementThreshold = 0.01;  // 1% - return to peak if better

  // Measurement parameters
  int averagingCount = 5;
  std::chrono::milliseconds measurementTimeout = std::chrono::seconds(5);
  double minValidValue = std::numeric_limits<double>::lowest();
  double maxValidValue = std::numeric_limits<double>::max();

  // ==========================================================================
  // CYCLE-TIME TRIMS
  // ==========================================================================
  // Set any of these to 0/false to restore the original blind-settle
  // behaviour for that trim independently, which is what makes them A/B
  // testable on hardware one at a time.

  // --- Fresh-sample gate ----------------------------------------------------
  // Motion is now settle-verified by the controller itself (PI qONT), so the
  // mechanics need no settle delay after a move returns. The only thing left
  // stale is the instrument sample that was integrating WHILE the stage was
  // travelling - a physically mixed sample, not a measurement of the new
  // position. That is a sample-COUNT problem, not a time problem.
  //
  // So instead of sleeping, wait for N new publishes on the data channel
  // (DataStore::GetUpdateCount). Publish #1 may straddle the move; publish #2
  // is guaranteed fully post-move. The wait self-scales with the instrument
  // rate and is never shorter than one complete integration.
  int freshSamples = 2;         // 0 = blind motionSettleTimeMs sleep instead

  // Precautionary floor before the gate starts counting. The gate proves the
  // instrument integration is post-move but cannot see the analogue chain in
  // front of it (photodiode + TIA), where a real electrical time constant can
  // outlive the mechanical settle. Production value on FireDog is 50 ms.
  int settleMinMs = 0;

  // Ceiling for the gate, NOT a delay. If the stream stalls, the wait
  // degrades to "use the latest value" and warns once.
  int freshSampleTimeoutMs = 300;

  // --- Structural trims -----------------------------------------------------
  // A first direction that walked uphill already BRACKETS the peak, so the
  // opposite direction can only walk downhill - the axis is done.
  bool skipOppositeDirection = true;

  // Start each axis in the direction that last improved it.
  bool directionMemory = true;

  // Do not re-measure a point that was just measured (direction starts and
  // the return-to-best verification sample).
  bool reuseSample = true;

  // --- Early exit -----------------------------------------------------------
  // First sample >= this value ends the whole scan and STAYS on that point:
  // no walk-back, no return to the global peak. 0 = disabled.
  // NOTE: this latches on a single sample, so the value at the end of the
  // scan can land a few percent below the value that tripped it.
  double exitThreshold = 0.0;

  // --- Adaptive step ladder -------------------------------------------------
  // The signal level tells you roughly how far you are from the peak: for a
  // Gaussian coupling profile, distance from peak is proportional to
  // sqrt(log(peak/value)). So LOG signal zones map cleanly onto step sizes.
  //
  // [zoneLow, zoneHigh] is split into stepSizes.size() equal log sections.
  // Zone i owns stepSizes[i], coarsest for the lowest zone. A pass whose step
  // is coarser than the zone the current signal sits in is skipped, so a
  // strong start (e.g. handed off from a raster scan) goes straight to fine
  // steps instead of re-walking the coarse ladder.
  bool adaptiveLadder = false;

  // Feedback units (e.g. amps for a picoammeter channel).
  //
  // CRITICAL TUNING RULE: zoneLow must equal the level the scan is HANDED OFF
  // at, never below it. Set it too low and the first zone boundary lands
  // under the handoff level, the coarse pass is skipped, and the fine walk
  // gets captured by a micro-lobe. The coarse pass IS the side-lobe immunity
  // - adaptive must not gate it off below the handoff level.
  double zoneLow = 1.0e-6;
  double zoneHigh = 400.0e-6;

  // --- Diagnostics ----------------------------------------------------------
  // One CSV line per measured sample, tagged "xcan," so a whole walk can be
  // replayed offline.
  bool traceEnabled = false;

  // Safety limits
  double maxStepSize = 0.5;  // mm
  std::chrono::minutes scanTimeout = std::chrono::minutes(30);

  // Validate parameters
  bool Validate(std::string& error) const;
};

// ============================================================================
// DATA STRUCTURES
// ============================================================================

/**
 * @brief Single measurement data point
 */
struct ScanDataPoint {
  double value = 0.0;
  PositionStruct position;
  std::chrono::system_clock::time_point timestamp;

  std::string axis;
  double stepSize = 0.0;
  int direction = 0;  // +1 or -1

  double gradient = 0.0;
  double relativeChange = 0.0;

  bool isPeak = false;
  bool isValid = true;
};

/**
 * @brief Baseline information
 */
struct BaselineInfo {
  double value = 0.0;
  PositionStruct position;
  std::chrono::system_clock::time_point timestamp;
  int sampleCount = 0;
  bool valid = false;
};

/**
 * @brief Peak information
 */
struct PeakInfo {
  double value = 0.0;
  PositionStruct position;
  std::chrono::system_clock::time_point timestamp;
  std::string context;
  bool valid = false;
};

/**
 * @brief Information about a single axis scan
 */
struct AxisScanInfo {
  std::string axis;
  double stepSize = 0.0;

  double positiveMaxValue = 0.0;
  PositionStruct positiveMaxPosition;

  double negativeMaxValue = 0.0;
  PositionStruct negativeMaxPosition;

  double finalValue = 0.0;
  PositionStruct finalPosition;

  int totalMeasurements = 0;
  std::chrono::milliseconds duration;
};

/**
 * @brief Scan progress event
 */
struct ScanProgressEvent {
  double progress = 0.0;  // 0.0 to 1.0
  std::string message;
  ScanState state = ScanState::Idle;
  std::string currentAxis;
  double currentStepSize = 0.0;
  int measurementCount = 0;
};

/**
 * @brief Scan error information
 */
struct ScanError {
  ScanErrorType type = ScanErrorType::Unknown;
  std::string message;
  std::chrono::system_clock::time_point timestamp;
};

/**
 * @brief Complete scan results
 */
struct ScanResults {
  std::string deviceName;
  std::string scanId;

  std::chrono::system_clock::time_point startTime;
  std::chrono::system_clock::time_point endTime;
  std::chrono::seconds duration;

  BaselineInfo baseline;
  PeakInfo globalPeak;

  std::vector<ScanDataPoint> measurements;
  std::vector<AxisScanInfo> axisScanInfo;

  // Statistics
  int totalMeasurements = 0;
  double averageValue = 0.0;
  double stdDeviation = 0.0;
  double minValue = 0.0;
  double maxValue = 0.0;

  std::map<std::string, int> measurementsPerAxis;

  bool success = false;
  std::string errorMessage;

  // Convert to JSON for saving
  nlohmann::json ToJson() const;
};


// ============================================================================
// INLINE IMPLEMENTATIONS
// ============================================================================

inline bool ScanParameters::Validate(std::string& error) const {
  if (stepSizes.empty()) {
    error = "At least one step size must be specified";
    return false;
  }

  if (axesToScan.empty()) {
    error = "At least one axis must be specified";
    return false;
  }

  for (double stepSize : stepSizes) {
    if (stepSize <= 0 || stepSize > maxStepSize) {
      error = "Step size " + std::to_string(stepSize) +
        " is invalid. Must be between 0 and " + std::to_string(maxStepSize);
      return false;
    }
  }

  if (motionSettleTimeMs < 0) {
    error = "Motion settle time cannot be negative";
    return false;
  }

  if (consecutiveDecreasesLimit < 1) {
    error = "Consecutive decreases limit must be at least 1";
    return false;
  }

  if (improvementThreshold < 0 || improvementThreshold > 1) {
    error = "Improvement threshold must be between 0 and 1";
    return false;
  }

  if (significantDecreaseThreshold < 0 || significantDecreaseThreshold > 1) {
    error = "Significant decrease threshold must be between 0 and 1";
    return false;
  }

  return true;
}

// Helper function to convert state to string
inline std::string ToString(ScanState state) {
  switch (state) {
  case ScanState::Idle: return "Idle";
  case ScanState::Initializing: return "Initializing";
  case ScanState::RecordingBaseline: return "Recording Baseline";
  case ScanState::Scanning: return "Scanning";
  case ScanState::ReturningToPeak: return "Returning to Peak";
  case ScanState::Completed: return "Completed";
  case ScanState::Error: return "Error";
  case ScanState::Halted: return "Halted";
  default: return "Unknown";
  }
}

inline std::string ToString(ScanErrorType type) {
  switch (type) {
  case ScanErrorType::DeviceNotReady: return "Device Not Ready";
  case ScanErrorType::InvalidParameters: return "Invalid Parameters";
  case ScanErrorType::MeasurementTimeout: return "Measurement Timeout";
  case ScanErrorType::MotionFailure: return "Motion Failure";
  case ScanErrorType::DataStoreError: return "Data Store Error";
  case ScanErrorType::UserHalt: return "User Halt";
  default: return "Unknown Error";
  }
}