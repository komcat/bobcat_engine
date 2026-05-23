// TTC001Adapter.cpp - Kinesis-based driver for Thorlabs TTC-001 T-Cube TEC
#include "TTC001Adapter.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>

// Kinesis headers pull in <windows.h>; keep them isolated to this TU.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "Thorlabs.MotionControl.TCube.TEC.h"

namespace {

// Kinesis returns FT_OK (0) on success across both TC_* and TLI_* calls.
constexpr short kFtOk = static_cast<short>(FT_OK);

// Settle time after Connect/StartPolling before the first reading is trustworthy.
// One poll interval is enough in practice; round up slightly for jitter.
constexpr int kPostConnectSettleMs = 300;

std::string FtStatusToString(short code) {
  switch (static_cast<FT_Status>(code)) {
    case FT_OK:                   return "OK";
    case FT_InvalidHandle:        return "InvalidHandle";
    case FT_DeviceNotFound:       return "DeviceNotFound";
    case FT_DeviceNotOpened:      return "DeviceNotOpened";
    case FT_IOError:              return "IOError";
    case FT_InsufficientResources:return "InsufficientResources";
    case FT_InvalidParameter:     return "InvalidParameter";
    case FT_DeviceNotPresent:     return "DeviceNotPresent";
    default:                      return "Unknown(" + std::to_string(code) + ")";
  }
}

}  // namespace

TTC001Adapter::TTC001Adapter(const TECDevice& config)
    : m_config(config),
      m_serialNumber(config.SerialNumber),
      m_connected(false),
      m_sensorMode(SensorMode::Unknown),
      m_diagReadsRemaining(0) {
  m_status.Connected = false;
  m_status.SetpointTemperature = config.DefaultTemperature;
}

TTC001Adapter::~TTC001Adapter() {
  if (m_connected) {
    Disconnect();
  }
}

// ----------------------------------------------------------------------------
// Connection management
// ----------------------------------------------------------------------------

bool TTC001Adapter::Connect() {
  if (m_serialNumber.empty()) {
    std::cerr << "[ERROR] TTC001Adapter '" << m_config.Name
              << "': missing serial_number in config" << std::endl;
    return false;
  }

  // Refresh discovery cache. Safe to call repeatedly.
  const short buildResult = TLI_BuildDeviceList();
  if (buildResult != kFtOk) {
    std::cerr << "[ERROR] TTC001Adapter '" << m_config.Name
              << "': TLI_BuildDeviceList failed (" << FtStatusToString(buildResult)
              << ")" << std::endl;
    return false;
  }

  const char* sn = m_serialNumber.c_str();

  const short openResult = TC_Open(sn);
  if (openResult != kFtOk) {
    std::cerr << "[ERROR] TTC001Adapter '" << m_config.Name
              << "': TC_Open(" << m_serialNumber << ") failed ("
              << FtStatusToString(openResult) << ")" << std::endl;
    return false;
  }

  // Load device-persisted settings (sensor type, PID, current limit, etc.).
  // Non-fatal if it fails - device still operates with current settings.
  if (!TC_LoadSettings(sn)) {
    std::cerr << "[WARN] TTC001Adapter '" << m_config.Name
              << "': TC_LoadSettings returned false (continuing)" << std::endl;
  }

  // Force the sensor mode from config. We do this because Kinesis GUI's "Persist
  // Settings to the Device" is often left unticked, so the device boots in
  // factory-default Transducer mode and the adapter would misinterpret raw
  // thermistor counts as 0.01 deg C per unit.
  std::string modeKey = m_config.SensorMode;
  std::transform(modeKey.begin(), modeKey.end(), modeKey.begin(),
                 [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
  TC_SensorTypes desiredSensor = TC_TH20kOhm;
  bool modeRecognised = true;
  if (modeKey == "transducer" || modeKey == "ad590" || modeKey == "ad592") {
    desiredSensor = TC_Transducer;
  } else if (modeKey == "thermistor_20k" || modeKey == "20k" ||
             modeKey == "th20k"          || modeKey == "20kohm") {
    desiredSensor = TC_TH20kOhm;
  } else if (modeKey == "thermistor_200k" || modeKey == "200k" ||
             modeKey == "th200k"          || modeKey == "200kohm") {
    desiredSensor = TC_TH200kOhm;
  } else {
    modeRecognised = false;
    std::cerr << "[WARN] TTC001Adapter '" << m_config.Name
              << "': unrecognised sensor_mode '" << m_config.SensorMode
              << "', defaulting to thermistor_20k" << std::endl;
  }
  if (modeRecognised) {
    const short setRc = TC_SetSensorType(sn, desiredSensor);
    if (setRc != kFtOk) {
      std::cerr << "[WARN] TTC001Adapter '" << m_config.Name
                << "': TC_SetSensorType(" << static_cast<int>(desiredSensor)
                << ") failed (" << FtStatusToString(setRc) << ")" << std::endl;
    }
    // Sensor type takes a moment to propagate; let polling pick it up.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  // Sensor type is needed to interpret TC_GetTemperatureReading / TC_SetTemperature
  // device units (Celsius vs Ohms - see Kinesis SDK header). Request a fresh copy
  // and read it back. If the request fails we still try to read the cached value.
  (void)TC_RequestSensorType(sn);
  const TC_SensorTypes rawSensor = TC_GetSensorType(sn);
  switch (rawSensor) {
    case TC_Transducer:  m_sensorMode = SensorMode::Transducer;     break;
    case TC_TH20kOhm:    m_sensorMode = SensorMode::Thermistor20k;  break;
    case TC_TH200kOhm:   m_sensorMode = SensorMode::Thermistor200k; break;
    default:             m_sensorMode = SensorMode::Unknown;        break;
  }
  std::cout << "[INFO] TTC001Adapter '" << m_config.Name
            << "': sensor mode = " << static_cast<int>(rawSensor);
  if (m_sensorMode == SensorMode::Thermistor20k ||
      m_sensorMode == SensorMode::Thermistor200k) {
    std::cout << " (thermistor; beta=" << m_config.ThermistorBeta
              << " R0=" << m_config.ThermistorR0Ohms
              << " Ohms @ " << m_config.ThermistorT0Celsius << " C)";
  }
  std::cout << std::endl;

  if (!TC_StartPolling(sn, POLL_INTERVAL_MS)) {
    std::cerr << "[ERROR] TTC001Adapter '" << m_config.Name
              << "': TC_StartPolling failed" << std::endl;
    TC_Close(sn);
    return false;
  }

  // Let one poll cycle populate the cached readings before any caller queries them.
  std::this_thread::sleep_for(std::chrono::milliseconds(kPostConnectSettleMs));

  // Re-read sensor type now that polling has had a chance to refresh the cache.
  // TC_RequestSensorType earlier was async, so the value above may have been
  // stale (often defaulting to Transducer=0 -> 0.01 C/unit interpretation,
  // which silently turns thermistor counts into a ~0 C reading).
  const TC_SensorTypes rawSensorAfter = TC_GetSensorType(sn);
  switch (rawSensorAfter) {
    case TC_Transducer:  m_sensorMode = SensorMode::Transducer;     break;
    case TC_TH20kOhm:    m_sensorMode = SensorMode::Thermistor20k;  break;
    case TC_TH200kOhm:   m_sensorMode = SensorMode::Thermistor200k; break;
    default: /* keep prior value */                                 break;
  }
  std::cout << "[INFO] TTC001Adapter '" << m_config.Name
            << "': sensor mode after settle = " << static_cast<int>(rawSensorAfter)
            << " (resolved=" << static_cast<int>(m_sensorMode) << ")" << std::endl;

  // Log raw device units on the first 3 ReadTemperature / ReadTECCurrent calls
  // so the operator can confirm sensor mode + units match expectations.
  m_diagReadsRemaining = 3;

  m_connected = true;
  m_status.Connected = true;
  std::cout << "[INFO] TTC001Adapter '" << m_config.Name
            << "': connected to SN " << m_serialNumber << std::endl;
  return true;
}

void TTC001Adapter::Disconnect() {
  if (!m_connected) {
    return;
  }
  const char* sn = m_serialNumber.c_str();

  // Safety: turn TEC output off before tearing down the channel.
  (void)TC_Disable(sn);
  TC_StopPolling(sn);
  TC_Close(sn);

  m_connected = false;
  m_status.Connected = false;
  m_status.TECOn = false;
  std::cout << "[INFO] TTC001Adapter '" << m_config.Name
            << "': disconnected" << std::endl;
}

bool TTC001Adapter::IsConnected() const {
  return m_connected;
}

bool TTC001Adapter::Reconnect() {
  Disconnect();
  return Connect();
}

// ----------------------------------------------------------------------------
// TEC control
// ----------------------------------------------------------------------------

TECCommandResult TTC001Adapter::SetTemperature(double temperature) {
  TECCommandResult result;
  if (!m_connected) {
    result.Message = "TTC-001 not connected";
    return result;
  }
  if (!std::isfinite(temperature) ||
      temperature < TEMP_MIN_C || temperature > TEMP_MAX_C) {
    result.Message = "Setpoint " + std::to_string(temperature)
                     + " out of range (-45 to +145 deg C)";
    return result;
  }

  // Encode setpoint as Kinesis device units. Per Thorlabs.MotionControl.TCube.TEC.h:
  //   Transducer mode   : 1 unit = 0.01 deg C, range -4500..14500
  //   20 kOhm thermistor: 1 unit = 10 Ohm,    range 0..2000  (= 0..20 kOhm)
  //   200 kOhm thermistor: 1 unit = 100 Ohm,  range 0..2000  (= 0..200 kOhm)
  short deviceUnits = 0;
  if (m_sensorMode == SensorMode::Thermistor20k ||
      m_sensorMode == SensorMode::Thermistor200k) {
    const double ohms = CelsiusToOhms(temperature);
    const double ohmsPerUnit = OhmsPerDeviceUnit();
    double rawUnits = std::lround(ohms / ohmsPerUnit);
    if (rawUnits < 0.0)    rawUnits = 0.0;
    if (rawUnits > 2000.0) rawUnits = 2000.0;
    deviceUnits = static_cast<short>(rawUnits);
    if (ohms / ohmsPerUnit > 2000.0 || ohms <= 0.0) {
      std::cerr << "[WARN] TTC001Adapter '" << m_config.Name
                << "': setpoint " << temperature << " C -> " << ohms
                << " Ohm clamped to device range (0.."
                << (2000.0 * ohmsPerUnit) << " Ohm)" << std::endl;
    }
  } else {
    // Transducer or Unknown: fall back to the AD590 0.01-degC encoding.
    deviceUnits = static_cast<short>(std::lround(temperature * 100.0));
  }

  const short rc = TC_SetTemperature(m_serialNumber.c_str(), deviceUnits);
  if (rc != kFtOk) {
    result.Message = "TC_SetTemperature failed: " + FtStatusToString(rc);
    return result;
  }

  m_status.SetpointTemperature = temperature;
  result.Success = true;
  result.Value = temperature;
  return result;
}

TECCommandResult TTC001Adapter::TEC_On() {
  TECCommandResult result;
  if (!m_connected) {
    result.Message = "TTC-001 not connected";
    return result;
  }
  const short rc = TC_Enable(m_serialNumber.c_str());
  if (rc != kFtOk) {
    result.Message = "TC_Enable failed: " + FtStatusToString(rc);
    return result;
  }
  m_status.TECOn = true;
  result.Success = true;
  return result;
}

TECCommandResult TTC001Adapter::TEC_Off() {
  TECCommandResult result;
  if (!m_connected) {
    result.Message = "TTC-001 not connected";
    return result;
  }
  const short rc = TC_Disable(m_serialNumber.c_str());
  if (rc != kFtOk) {
    result.Message = "TC_Disable failed: " + FtStatusToString(rc);
    return result;
  }
  m_status.TECOn = false;
  result.Success = true;
  return result;
}

TECCommandResult TTC001Adapter::ReadTemperature() {
  TECCommandResult result;
  if (!m_connected) {
    result.Message = "TTC-001 not connected";
    return result;
  }
  // Belt-and-suspenders: even with TC_StartPolling active, explicitly ask the
  // device to refresh its status cache. TC_RequestStatus is async; the value
  // we read on this call is whatever the most recent poll populated, which is
  // exactly the behaviour we want.
  (void)TC_RequestStatus(m_serialNumber.c_str());
  const short deviceUnits = TC_GetTemperatureReading(m_serialNumber.c_str());

  double celsius = 0.0;
  if (m_sensorMode == SensorMode::Thermistor20k ||
      m_sensorMode == SensorMode::Thermistor200k) {
    const double ohms = static_cast<double>(deviceUnits) * OhmsPerDeviceUnit();
    celsius = OhmsToCelsius(ohms);
    if (m_diagReadsRemaining > 0) {
      std::cout << "[DIAG] TTC001Adapter '" << m_config.Name
                << "': raw temp units=" << deviceUnits
                << " mode=" << static_cast<int>(m_sensorMode)
                << " ohms=" << ohms
                << " C=" << celsius << std::endl;
    }
    if (!std::isfinite(celsius)) {
      result.Message = "Thermistor reading invalid (R=" + std::to_string(ohms)
                       + " Ohm; check sensor connection)";
      if (m_diagReadsRemaining > 0) { --m_diagReadsRemaining; }
      return result;
    }
  } else {
    celsius = static_cast<double>(deviceUnits) / 100.0;
    if (m_diagReadsRemaining > 0) {
      std::cout << "[DIAG] TTC001Adapter '" << m_config.Name
                << "': raw temp units=" << deviceUnits
                << " mode=" << static_cast<int>(m_sensorMode)
                << " (transducer) C=" << celsius << std::endl;
    }
  }
  if (m_diagReadsRemaining > 0) { --m_diagReadsRemaining; }

  m_status.CurrentTemperature = celsius;
  result.Success = true;
  result.Value = celsius;
  return result;
}

TECCommandResult TTC001Adapter::ReadTECCurrent() {
  TECCommandResult result;
  if (!m_connected) {
    result.Message = "TTC-001 not connected";
    return result;
  }
  // Kinesis declares TC_GetCurrentReading as WORD, but the underlying device
  // value is signed 16-bit milliamps (negative = cooling, positive = heating).
  // Reinterpret via short so negative readings don't wrap into nonsensical
  // 60+ A values that the UI then suppresses to "-- A".
  const WORD raw = TC_GetCurrentReading(m_serialNumber.c_str());
  const short signedMilliAmps = static_cast<short>(raw);
  const double amps = static_cast<double>(signedMilliAmps) / 1000.0;
  m_status.CurrentTECCurrent = amps;
  result.Success = true;
  result.Value = amps;
  return result;
}

// ----------------------------------------------------------------------------
// Laser control - TTC-001 is TEC-only, all laser ops report unsupported.
// ----------------------------------------------------------------------------

TECCommandResult TTC001Adapter::MakeLaserUnsupported() {
  TECCommandResult result;
  result.Success = false;
  result.Message = "Laser control not supported on TTC-001 (TEC-only)";
  return result;
}

TECCommandResult TTC001Adapter::SetLaserCurrent(double /*current*/) {
  return MakeLaserUnsupported();
}
TECCommandResult TTC001Adapter::Laser_On()         { return MakeLaserUnsupported(); }
TECCommandResult TTC001Adapter::Laser_Off()        { return MakeLaserUnsupported(); }
TECCommandResult TTC001Adapter::ReadLaserCurrent() { return MakeLaserUnsupported(); }

// ----------------------------------------------------------------------------
// Status
// ----------------------------------------------------------------------------

TECStatus TTC001Adapter::GetStatus() const {
  return m_status;
}

std::string TTC001Adapter::GetDeviceName() const {
  return m_config.Name;
}

std::string TTC001Adapter::GetDeviceType() const {
  return "TTC001";
}

// ----------------------------------------------------------------------------
// Thermistor conversions (beta equation)
//   1/T = 1/T0 + (1/beta) * ln(R/R0)        with T, T0 in Kelvin
//   R   = R0 * exp(beta * (1/T - 1/T0))
// ----------------------------------------------------------------------------

double TTC001Adapter::CelsiusToOhms(double celsius) const {
  const double tK  = celsius + 273.15;
  const double t0K = m_config.ThermistorT0Celsius + 273.15;
  if (tK <= 0.0 || t0K <= 0.0 || m_config.ThermistorBeta <= 0.0) {
    return 0.0;
  }
  return m_config.ThermistorR0Ohms *
         std::exp(m_config.ThermistorBeta * (1.0 / tK - 1.0 / t0K));
}

double TTC001Adapter::OhmsToCelsius(double ohms) const {
  const double t0K = m_config.ThermistorT0Celsius + 273.15;
  if (ohms <= 0.0 || m_config.ThermistorR0Ohms <= 0.0 ||
      t0K <= 0.0 || m_config.ThermistorBeta <= 0.0) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  const double invT = 1.0 / t0K +
                      std::log(ohms / m_config.ThermistorR0Ohms) /
                      m_config.ThermistorBeta;
  if (invT <= 0.0) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  return (1.0 / invT) - 273.15;
}

double TTC001Adapter::OhmsPerDeviceUnit() const {
  switch (m_sensorMode) {
    case SensorMode::Thermistor20k:  return 10.0;   // 0..2000 -> 0..20 kOhm
    case SensorMode::Thermistor200k: return 100.0;  // 0..2000 -> 0..200 kOhm
    default:                         return 0.0;
  }
}
