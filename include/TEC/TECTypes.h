// TECTypes.h
#pragma once
#include <string>
#include <map>

/**
 * @brief TEC device configuration structure
 */
struct TECDevice {
  std::string Name;                   // Device name (e.g., "laser-main")
  std::string TypeController;         // Controller type (e.g., "CLD101x")
  std::string IpAddress;              // Server IP address
  int Port = 65432;                   // Server port
  bool Enabled = true;                // Is device enabled

  // Optional setpoints (loaded from config)
  double DefaultTemperature = 25.0;   // Default TEC temperature (°C)
  double DefaultLaserCurrent = 0.0;   // Default laser current (mA)
};

/**
 * @brief TEC status structure
 */
struct TECStatus {
  bool Connected = false;
  bool TECOn = false;
  bool LaserOn = false;
  double CurrentTemperature = 0.0;    // °C
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