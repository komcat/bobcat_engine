// TECTypes.h
#pragma once
#include <string>
#include <map>

/**
 * @brief TEC device configuration structure
 */
struct TECDevice {
  std::string Name;                   // Device name (e.g., "laser-main")
  std::string TypeController;         // Controller type (e.g., "CLD101x", "CLD101x_Direct")
  std::string IpAddress;              // Server IP address (for TCP-based adapters)
  int Port = 65432;                   // Server port (for TCP-based adapters)
  std::string VISAResource;           // VISA resource string (for direct VISA adapters)
                                      // e.g., "USB0::0x1313::0x804F::M00930341::INSTR"
  std::string SerialNumber;           // Kinesis USB serial number (for TTC-001)
                                      // e.g., "87814387"
  bool Enabled = true;                // Is device enabled

  // Optional setpoints (loaded from config)
  double DefaultTemperature = 25.0;   // Default TEC temperature (�C)
  double DefaultLaserCurrent = 0.0;   // Default laser current (mA)

  // Thermistor parameters. Used by TTC-001 (Kinesis) when the device sensor
  // mode is "20 kOhm Thermistor" or "200 kOhm Thermistor" to convert between
  // Celsius (adapter contract) and Ohms (Kinesis device units). Ignored by
  // adapter types whose hardware exposes Celsius directly (VISA/SCPI TECs,
  // TTC-001 in AD590 Transducer mode).
  double ThermistorBeta = 3977.0;     // Beta value (Kelvin)
  double ThermistorR0Ohms = 10000.0;  // Nominal resistance at T0
  double ThermistorT0Celsius = 25.0;  // Reference temperature

  // TTC-001 sensor mode. The TTC-001 supports three hardware sensor modes; the
  // mode set in the Kinesis GUI is only persisted to device flash when the GUI's
  // "Persist Settings to the Device" box is ticked. If unticked, the device boots
  // in factory default (transducer), which makes the adapter misinterpret raw
  // thermistor counts. Setting this field forces the adapter to push the desired
  // mode via TC_SetSensorType on Connect(), regardless of flash state.
  // Accepted values: "transducer", "thermistor_20k", "thermistor_200k".
  std::string SensorMode = "thermistor_20k";
};

/**
 * @brief TEC status structure
 */
struct TECStatus {
  bool Connected = false;
  bool TECOn = false;
  bool LaserOn = false;
  double CurrentTemperature = 0.0;    // °C
  double CurrentTECCurrent = 0.0;     // A (Amps)
  double CurrentTECVoltage = 0.0;     // V (Volts)
  double CurrentLaserCurrent = 0.0;   // mA
  double SetpointTemperature = 0.0;   // °C
  double SetpointLaserCurrent = 0.0;  // mA
  std::string LastError;
};

/**
 * @brief TEC command result
 */
struct TECCommandResult {
  bool Success = false;
  std::string Message;
  double Value = 0.0;  // For read operations
};