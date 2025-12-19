// include/motions/scanning/RotationScanTypes.h
#pragma once

#include "ScanTypes.h"
#include <string>
#include <vector>
#include <chrono>
#include <map>

// ============================================================================
// ROTATION SCAN ENUMS
// ============================================================================

/**
 * @brief Rotation axis identifiers
 */
enum class RotationAxis {
  U,  // Yaw - rotation around Z axis -> scans Y, Z
  V,  // Pitch - rotation around Y axis -> scans X, Z
  W   // Roll - rotation around X axis -> scans X, Y
};

/**
 * @brief State machine for rotation scanning
 */
enum class RotationScanState {
  Idle,
  Initializing,
  RecordingBaseline,
  RotatingPositive,
  RotatingNegative,
  TranslationScanning,
  ReturningToPeak,
  ReturningToStart,
  Completed,
  Error,
  Halted
};

/**
 * @brief Direction of rotation scan
 */
enum class RotationDirection {
  Positive = 1,
  Negative = -1
};

// ============================================================================
// ROTATION SCAN PARAMETERS
// ============================================================================

/**
 * @brief Configuration parameters for rotation scanning
 */
struct RotationScanParameters {
  // Rotation axis configuration
  RotationAxis rotationAxis = RotationAxis::W;  // Default: Roll
  double rotationStep = 0.2;  // degrees

  // Termination criteria for rotation
  double improvementThreshold = 0.01;  // 1% - must improve by this much to continue
  int maxNoImprovementCount = 1;  // Stop after this many rotations without improvement
  double maxRotationRange = 2.5;  // Maximum total rotation in degrees (safety limit)

  // Early exit optimization
  int earlyExitPauseMs = 1000;  // Pause 1 second when early exit condition met

  // Translation scan parameters (used for XY/XZ/YZ scan after each rotation)
  std::vector<double> translationStepSizes = { 0.002, 0.001, 0.0005 };  // mm (2, 1, 0.5 µm FINE)
  int motionSettleTimeMs = 400;
  double velocityPercent = 50.0;

  // Translation termination criteria
  int consecutiveDecreasesLimit = 3;
  double significantDecreaseThreshold = 0.05;  // 5%
  double maxTranslationDistance = 0.2;  // mm per direction

  // Measurement parameters
  int averagingCount = 3;
  std::chrono::milliseconds measurementTimeout = std::chrono::seconds(5);
  double minValidValue = -1000.0;
  double maxValidValue = 1000.0;

  // Safety limits
  std::chrono::minutes scanTimeout = std::chrono::minutes(30);

  // Get the translation axes - always X and Y for all rotation axes
  std::vector<std::string> GetTranslationAxes() const {
    // Fixed to X, Y for all rotation axes (U, V, W)
    return { "X", "Y" };
  }

  // Get rotation axis name
  std::string GetRotationAxisName() const {
    switch (rotationAxis) {
      case RotationAxis::U: return "U";
      case RotationAxis::V: return "V";
      case RotationAxis::W: return "W";
      default: return "W";
    }
  }

  // Validate parameters
  bool Validate(std::string& error) const {
    if (rotationStep <= 0 || rotationStep > 1.0) {
      error = "Rotation step must be between 0 and 1 degree";
      return false;
    }

    if (maxNoImprovementCount < 1) {
      error = "Max no-improvement count must be at least 1";
      return false;
    }

    if (translationStepSizes.empty()) {
      error = "At least one translation step size must be specified";
      return false;
    }

    if (improvementThreshold < 0 || improvementThreshold > 1) {
      error = "Improvement threshold must be between 0 and 1";
      return false;
    }

    return true;
  }
};

// ============================================================================
// ROTATION SCAN DATA STRUCTURES
// ============================================================================

/**
 * @brief Data for a single rotation step result
 */
struct RotationStepResult {
  double rotationAngle = 0.0;  // Total rotation from start
  RotationDirection direction = RotationDirection::Positive;

  // Peak found after translation scan at this rotation
  double peakValue = 0.0;
  PositionStruct peakPosition;  // Full 6-axis position (XYZUVW)

  // Improvement metrics
  double improvementFromBaseline = 0.0;  // Relative to starting baseline
  double improvementFromPrevious = 0.0;  // Relative to previous rotation step

  bool improved = false;  // Did this step improve over threshold?

  std::chrono::system_clock::time_point timestamp;
};

/**
 * @brief Progress event for rotation scanning
 */
struct RotationScanProgressEvent {
  double overallProgress = 0.0;  // 0.0 to 1.0
  std::string message;
  RotationScanState state = RotationScanState::Idle;

  // Current rotation info
  RotationDirection currentDirection = RotationDirection::Positive;
  double currentRotation = 0.0;  // Total rotation from start
  int rotationStepCount = 0;

  // Current translation scan info (if in translation scan)
  std::string currentTranslationAxis;
  double currentTranslationStepSize = 0.0;
  double translationScanProgress = 0.0;
};

/**
 * @brief Complete rotation scan results
 */
struct RotationScanResults {
  std::string deviceName;
  std::string scanId;
  RotationAxis rotationAxis = RotationAxis::W;

  std::chrono::system_clock::time_point startTime;
  std::chrono::system_clock::time_point endTime;
  std::chrono::seconds duration;

  // Baseline (starting position)
  BaselineInfo baseline;

  // Global peak (best found during entire scan)
  PeakInfo globalPeak;

  // All rotation step results
  std::vector<RotationStepResult> rotationSteps;

  // Summary
  double totalRotation = 0.0;  // Final rotation from start
  int totalRotationSteps = 0;
  int totalTranslationScans = 0;

  // Which direction found improvement (if any)
  bool positiveDirectionImproved = false;
  bool negativeDirectionImproved = false;

  bool success = false;
  std::string errorMessage;

  // Convert to JSON for saving
  nlohmann::json ToJson() const;
};

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

inline std::string ToString(RotationScanState state) {
  switch (state) {
    case RotationScanState::Idle: return "Idle";
    case RotationScanState::Initializing: return "Initializing";
    case RotationScanState::RecordingBaseline: return "Recording Baseline";
    case RotationScanState::RotatingPositive: return "Rotating Positive";
    case RotationScanState::RotatingNegative: return "Rotating Negative";
    case RotationScanState::TranslationScanning: return "Translation Scanning";
    case RotationScanState::ReturningToPeak: return "Returning to Peak";
    case RotationScanState::ReturningToStart: return "Returning to Start";
    case RotationScanState::Completed: return "Completed";
    case RotationScanState::Error: return "Error";
    case RotationScanState::Halted: return "Halted";
    default: return "Unknown";
  }
}

inline std::string ToString(RotationAxis axis) {
  switch (axis) {
    case RotationAxis::U: return "U (Yaw)";
    case RotationAxis::V: return "V (Pitch)";
    case RotationAxis::W: return "W (Roll)";
    default: return "Unknown";
  }
}

inline std::string ToString(RotationDirection dir) {
  return dir == RotationDirection::Positive ? "Positive" : "Negative";
}
