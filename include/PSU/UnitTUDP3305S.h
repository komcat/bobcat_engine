// UnitTUDP3305S.h - UNI-T UDP3305S-E 3-channel programmable DC PSU (USB/USBTMC + SCPI)
#pragma once

#include "IPowerSupply.h"
#include <string>
#include <mutex>
#include <visa.h>

/**
 * @brief UNI-T UDP3305S-E 3-channel programmable DC power supply driver.
 *
 * Transport: USB -> USBTMC -> NI-VISA -> SCPI.
 * VISA resource format: USB0::0x0483::0x5740::<serial>::INSTR
 *   (UNI-T enumerates with the STMicro USB VID 0x0483, not a UNI-T VID.)
 *
 * Channel specifications (datasheet):
 *   CH1, CH2 : 0..32 V / 0..5 A, independent
 *   CH3      : 0..6  V / 0..3 A
 *
 * SCPI command set (verified on hardware via docs/UDP3305S/test_channels.py):
 *   *IDN?
 *   APPLy CH<n>,<volt>,<curr>          - program V+I in one shot
 *   SOURce<n>:VOLTage?                 - read programmed V
 *   SOURce<n>:CURRent?                 - read programmed I
 *   OUTPut CH<n>,ON | OUTPut CH<n>,OFF
 *   OUTPut? CH<n>                      - returns "ON"/"OFF" (string), or "1"/"0"
 *   MEASure:VOLTage? CH<n>
 *   MEASure:CURRent? CH<n>
 *
 * SCPI command set (per HANDOFF, NOT yet verified on hardware - confirm
 * with the device's programming manual before relying on these):
 *   SOURce<n>:VOLTage <v>              - set V only
 *   SOURce<n>:CURRent <i>              - set I only
 *   SOURce<n>:VOLTage:PROTection <v>   - OVP setpoint
 *   SOURce<n>:CURRent:PROTection <i>   - OCP setpoint
 *
 * See docs/UDP3305S/HANDOFF.md for the integration brief and reference scripts.
 */
class UnitTUDP3305S : public IPowerSupply
{
public:
  /**
   * @param visaResource Full VISA resource string, e.g.
   *        "USB0::0x0483::0x5740::ADP3424500154::INSTR"
   * @param deviceName Friendly name for logging / UI.
   */
  explicit UnitTUDP3305S(const std::string& visaResource,
                         const std::string& deviceName = "UDP3305S");
  ~UnitTUDP3305S() override;

  // Non-copyable, non-movable (owns a VISA session)
  UnitTUDP3305S(const UnitTUDP3305S&) = delete;
  UnitTUDP3305S& operator=(const UnitTUDP3305S&) = delete;
  UnitTUDP3305S(UnitTUDP3305S&&) = delete;
  UnitTUDP3305S& operator=(UnitTUDP3305S&&) = delete;

  // IPowerSupply: connection management
  bool connect() override;
  void disconnect() override;
  bool isConnected() const override;

  std::string getDeviceName() const override { return m_deviceName; }
  std::string getDeviceID() const override { return m_deviceID; }

  // IPowerSupply: output control (channel: 1..3)
  [[nodiscard]] bool setOutputOn(int channel = 1) override;
  [[nodiscard]] bool setOutputOff(int channel = 1) override;

  // IPowerSupply: voltage/current control (channel: 1..3)
  [[nodiscard]] bool setVoltage(double voltage, int channel = 1) override;
  [[nodiscard]] bool setCurrent(double current, int channel = 1) override;

  // IPowerSupply: protection (channel: 1..3) - SCPI form NOT YET HW-VERIFIED
  [[nodiscard]] bool setOVP(double voltage, int channel = 1) override;
  [[nodiscard]] bool setOCP(double current, int channel = 1) override;

  // IPowerSupply: measurements (channel: 1..3). Return -1.0 on error.
  double readVoltage(int channel = 1) override;
  double readCurrent(int channel = 1) override;
  double readSetVoltage(int channel = 1) override;
  double readSetCurrent(int channel = 1) override;
  bool   readOutputState(int channel = 1) override;

  std::string getLastError() const override { return m_lastError; }

  // Escape hatch: send a raw SCPI query and return the response string.
  // Used by PowerSupplyControlPage's "Read Status" button to retrieve readings
  // for which IPowerSupply has no virtual method (e.g. OVP/OCP setpoints).
  // Returns an empty string on transport failure - check getLastError().
  std::string query(const std::string& scpi);

  // Channel limits
  static constexpr int NUM_CHANNELS = 3;

private:
  // Low-level SCPI primitives - assume m_mutex is held by caller.
  bool writeRaw(const std::string& command);
  bool queryRaw(const std::string& query, std::string& response);

  // Locking wrappers used by IPowerSupply methods.
  bool writeLocked(const std::string& command);
  bool queryLocked(const std::string& query, std::string& response);

  // Parameter validation (returns false and sets m_lastError on out-of-range).
  bool validateChannel(int channel) const;

  // Best-effort: force every channel OFF (called from disconnect()).
  void forceAllOutputsOff();

  // Configuration
  std::string m_visaResource;
  std::string m_deviceName;
  std::string m_deviceID;

  // VISA session handles
  ViSession m_defaultRM{VI_NULL};
  ViSession m_instrument{VI_NULL};
  bool      m_connected{false};

  mutable std::mutex  m_mutex;
  mutable std::string m_lastError;

  // Timeouts / buffer sizing
  static constexpr unsigned long TIMEOUT_MS       = 5000UL;
  static constexpr unsigned long READ_BUFFER_SIZE = 1024UL;
};
