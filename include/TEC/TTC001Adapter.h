// TTC001Adapter.h
#pragma once
#include "ITECDevice.h"
#include "TECTypes.h"
#include <string>

/**
 * @brief Kinesis-based adapter for Thorlabs TTC-001 T-Cube TEC Controller.
 *
 * TEC-only device (no laser); laser methods return Success=false.
 * Identified by 8-digit USB serial number (e.g. "87814387").
 *
 * Requirements:
 * - Kinesis SDK present in external/kinesis/ (HAS_KINESIS defined)
 * - Thorlabs.MotionControl.TCube.TEC.dll + DeviceManager.dll next to exe
 * - FTDI driver installed (delivered by Kinesis installer)
 *
 * Hardware envelope:
 * - Sensor: thermistor (TC_Transducer), set via Kinesis GUI / persisted in device
 * - Setpoint range: -45 .. +145 deg C (rejected at adapter boundary)
 * - Resolution: 0.01 deg C (device units), exposed as 0.1 deg C step in UI
 *
 * Polling model:
 * - TC_StartPolling(250 ms) launched at Connect(), stopped at Disconnect()
 * - Read* methods return the latest polled value (no blocking SCPI round-trip)
 */
class TTC001Adapter : public ITECDevice {
public:
  explicit TTC001Adapter(const TECDevice& config);
  ~TTC001Adapter() override;

  // Non-copyable, non-movable (owns native device handle by serial number)
  TTC001Adapter(const TTC001Adapter&) = delete;
  TTC001Adapter& operator=(const TTC001Adapter&) = delete;
  TTC001Adapter(TTC001Adapter&&) = delete;
  TTC001Adapter& operator=(TTC001Adapter&&) = delete;

  // ITECDevice - connection
  bool Connect() override;
  void Disconnect() override;
  [[nodiscard]] bool IsConnected() const override;
  bool Reconnect() override;

  // ITECDevice - TEC control
  TECCommandResult SetTemperature(double temperature) override;
  TECCommandResult TEC_On() override;
  TECCommandResult TEC_Off() override;
  TECCommandResult ReadTemperature() override;
  TECCommandResult ReadTECCurrent() override;

  // ITECDevice - laser control (not supported on TTC-001, TEC-only)
  TECCommandResult SetLaserCurrent(double current) override;
  TECCommandResult Laser_On() override;
  TECCommandResult Laser_Off() override;
  TECCommandResult ReadLaserCurrent() override;

  // ITECDevice - status
  TECStatus GetStatus() const override;
  std::string GetDeviceName() const override;
  std::string GetDeviceType() const override;

private:
  // Mirrors Kinesis TC_SensorTypes without dragging <windows.h> into this header.
  // The numeric values intentionally match TC_Transducer/TC_TH20kOhm/TC_TH200kOhm.
  enum class SensorMode {
    Unknown        = -1,
    Transducer     = 0,   // AD590/AD592 - device units are 0.01 deg C
    Thermistor20k  = 1,   // 0..2000 = 0..20 kOhm (1 unit = 10 Ohm)
    Thermistor200k = 2,   // 0..2000 = 0..200 kOhm (1 unit = 100 Ohm)
  };

  TECDevice   m_config;
  std::string m_serialNumber;
  bool        m_connected;
  SensorMode  m_sensorMode;
  mutable TECStatus m_status;

  // Diagnostic counter so the first few reads after Connect log raw device
  // units. Helps verify sensor mode + unit interpretation against hardware.
  mutable int m_diagReadsRemaining;

  // Hardware envelope (setpoint range valid in Transducer mode; in thermistor
  // mode the device additionally clamps the converted resistance to 0..max).
  static constexpr double  TEMP_MIN_C       = -45.0;
  static constexpr double  TEMP_MAX_C       = 145.0;
  static constexpr int     POLL_INTERVAL_MS = 250;

  // Beta-equation conversions for thermistor mode. R0/Beta/T0 come from m_config.
  [[nodiscard]] double CelsiusToOhms(double celsius) const;
  [[nodiscard]] double OhmsToCelsius(double ohms) const;

  // Ohms-per-device-unit for the active sensor mode (10 for 20k, 100 for 200k).
  // Returns 0.0 for non-thermistor modes.
  [[nodiscard]] double OhmsPerDeviceUnit() const;

  // Helper: build the canonical "not supported on TTC-001" result for laser ops
  static TECCommandResult MakeLaserUnsupported();
};
