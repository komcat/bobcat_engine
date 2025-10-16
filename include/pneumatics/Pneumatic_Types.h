// Pneumatic_Types.h - Types and configuration for pneumatic control
#pragma once

#include "EziIO_Types.h"
#include <string>
#include <functional>
#include <cstdint>

// ============================================================================
// ENUMS
// ============================================================================

/**
 * @brief Pneumatic slide position states
 */
enum class PneumaticPosition {
  EXTENDED,      // Fully extended (extended sensor active)
  RETRACTED,     // Fully retracted (retracted sensor active)
  EXTENDING,     // Moving toward extended position
  RETRACTING,    // Moving toward retracted position
  UNKNOWN,       // Position cannot be determined
  HARDWARE_ERROR,         // Both sensors active (hardware error)
  TIMEOUT        // Movement timeout occurred
};

/**
 * @brief Error codes for pneumatic operations
 */
enum class PneumaticError {
  SUCCESS = 0,
  SLIDE_NOT_FOUND,
  IO_MANAGER_NULL,
  DEVICE_NOT_FOUND,
  PIN_NOT_FOUND,
  TIMEOUT,
  ALREADY_AT_POSITION,
  HARDWARE_ERROR,
  OPERATION_FAILED
};

// ============================================================================
// STRUCTURES
// ============================================================================

/**
 * @brief Reference to an IO pin (device + pin name)
 */
struct IOPinReference {
  std::string DeviceName;
  std::string PinName;
};

/**
 * @brief Configuration for a pneumatic slide
 */
struct PneumaticConfig {
  std::string Name;                    // Slide name (e.g., "UV_Head")
  IOPinReference Output;               // Output pin to control extend/retract
  IOPinReference ExtendedSensor;       // Input pin - extended position sensor
  IOPinReference RetractedSensor;      // Input pin - retracted position sensor
  uint32_t TimeoutMs = 5000;           // Timeout for movement (default 5 seconds)
};

/**
 * @brief Status information for a pneumatic slide
 */
struct PneumaticStatus {
  PneumaticPosition Position = PneumaticPosition::UNKNOWN;
  bool OutputActive = false;           // Is output currently active?
  bool ExtendedSensorActive = false;   // Extended sensor state
  bool RetractedSensorActive = false;  // Retracted sensor state
  uint32_t LastMoveTimeMs = 0;         // Time of last movement start
  bool Valid = false;                  // Is status valid?
};

// ============================================================================
// CALLBACKS
// ============================================================================

/**
 * @brief Callback type for position change events
 * @param slideName Name of the slide
 * @param oldPosition Previous position
 * @param newPosition Current position
 */
using PneumaticPositionCallback = std::function<void(
  const std::string& slideName,
  PneumaticPosition oldPosition,
  PneumaticPosition newPosition
  )>;

/**
 * @brief Callback type for error events
 * @param slideName Name of the slide
 * @param error Error that occurred
 * @param message Error message
 */
using PneumaticErrorCallback = std::function<void(
  const std::string& slideName,
  PneumaticError error,
  const std::string& message
  )>;

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/**
 * @brief Convert PneumaticPosition to string
 */
inline std::string GetPositionString(PneumaticPosition pos) {
  switch (pos) {
  case PneumaticPosition::EXTENDED:        return "Extended";
  case PneumaticPosition::RETRACTED:       return "Retracted";
  case PneumaticPosition::EXTENDING:       return "Extending";
  case PneumaticPosition::RETRACTING:      return "Retracting";
  case PneumaticPosition::UNKNOWN:         return "Unknown";
  case PneumaticPosition::HARDWARE_ERROR:  return "Hardware Error";  // ✅ FIXED
  case PneumaticPosition::TIMEOUT:         return "Timeout";
  default: return "Invalid";
  }
}

/**
 * @brief Convert PneumaticError to string
 */
inline std::string GetPneumaticErrorString(PneumaticError err) {
  switch (err) {
  case PneumaticError::SUCCESS:              return "Success";
  case PneumaticError::SLIDE_NOT_FOUND:      return "Slide not found";
  case PneumaticError::IO_MANAGER_NULL:      return "IO Manager is null";
  case PneumaticError::DEVICE_NOT_FOUND:     return "Device not found";
  case PneumaticError::PIN_NOT_FOUND:        return "Pin not found";
  case PneumaticError::TIMEOUT:              return "Operation timeout";
  case PneumaticError::ALREADY_AT_POSITION:  return "Already at position";
  case PneumaticError::HARDWARE_ERROR:       return "Hardware error";
  case PneumaticError::OPERATION_FAILED:     return "Operation failed";
  default: return "Unknown error";
  }
}