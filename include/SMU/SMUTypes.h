// SMUTypes.h - Common types for SMU devices
#pragma once

#include <string>
#include <vector>

/**
 * @brief SMU Device Configuration
 */
struct SMUDeviceConfig {
  std::string Name;                   // Device name (e.g., "SMU1")
  std::string Type;                   // Device type (e.g., "keithley_6482")
  std::string VISAResource;           // VISA resource string (e.g., "GPIB0::1::INSTR")
  bool Enabled = true;                // Enable/disable device

  // Channel configuration
  int ChannelCount = 1;                    // Total number of channels (1-4)
  std::vector<int> EnabledChannels = {1};  // Which channels are enabled (e.g., {1, 2})

  // Default settings
  double DefaultVoltage = 0.0;        // Default voltage (V)
  double DefaultCurrent = 0.0;        // Default current (A)
  double DefaultCompliance = 0.001;   // Default compliance limit (A or V)

  // Measurement settings
  int MeasurementNPLC = 1;            // Number of Power Line Cycles (0.01 to 10)
  bool AutoRange = true;              // Auto-range measurement
  double FixedRange = 0.0;            // Fixed range if AutoRange is false
};

/**
 * @brief SMU Operation Mode
 */
enum class SMUMode {
  VOLTAGE_SOURCE,  // Source voltage, measure current
  CURRENT_SOURCE,  // Source current, measure voltage
  MEASURE_ONLY     // Measurement only (like picoammeter)
};

/**
 * @brief SMU Measurement Result
 */
struct SMUMeasurement {
  double value = 0.0;           // Measured value
  bool success = false;         // Measurement success flag
  std::string errorMessage;     // Error message if failed
  std::string timestamp;        // Timestamp of measurement
};
