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
  double significantDecreaseThreshold = 0.05;  // 5%
  double maxTotalDistance = 5.0;  // mm

  // Position optimization
  double improvementThreshold = 0.01;  // 1% - return to peak if better

  // Measurement parameters
  int averagingCount = 5;
  std::chrono::milliseconds measurementTimeout = std::chrono::seconds(5);
  double minValidValue = std::numeric_limits<double>::lowest();
  double maxValidValue = std::numeric_limits<double>::max();

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