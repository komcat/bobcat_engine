// TED4015DirectAdapter.h
#pragma once
#include "ITECDevice.h"
#include "TECTypes.h"
#include <string>
#include <visa.h>  // Include VISA header for type definitions

/**
 * @brief Direct VISA adapter for Thorlabs TED4015 TEC Controller
 *
 * Communicates directly with TED4015 device via NI-VISA library.
 * The TED4015 is a TEC-only controller (no laser diode functionality).
 * Implements ITECDevice interface.
 *
 * Requirements:
 * - NI-VISA Runtime installed on the system
 * - USB connection to TED4015 device
 *
 * VISA Resource String Format:
 *   USB0::0x1313::0x8048::M01256053::INSTR
 *   where:
 *     0x1313 = Thorlabs Vendor ID
 *     0x8048 = TED4015 Product ID
 *     M01256053 = Serial number (unique to each device)
 *
 * SCPI Commands Used:
 *   *IDN?                     - Device identification
 *   OUTP:STAT?                - Query TEC output state
 *   OUTP:STAT <0|1>           - Set TEC output state (0=OFF, 1=ON)
 *   MEAS:TEMP?                - Read actual temperature (°C)
 *   SOUR:TEMP?                - Query temperature setpoint (°C)
 *   SOUR:TEMP <value>         - Set temperature setpoint (°C)
 *   MEAS:CURR?                - Read TEC current (A)
 *   MEAS:VOLT?                - Read TEC voltage (V)
 *   SOUR:CURR:LIM?            - Query current limit (A)
 *   SOUR:TEMP:LIM:HIGH?       - Query high temperature limit (°C)
 *   SOUR:TEMP:LIM:LOW?        - Query low temperature limit (°C)
 */
class TED4015DirectAdapter : public ITECDevice {
public:
  /**
   * @brief Constructor
   * @param config Device configuration (must include VISAResource field)
   */
  explicit TED4015DirectAdapter(const TECDevice& config);

  /**
   * @brief Destructor - ensures disconnection and cleanup
   */
  ~TED4015DirectAdapter() override;

  // ============================================================================
  // CONNECTION MANAGEMENT
  // ============================================================================

  bool Connect() override;
  void Disconnect() override;
  bool IsConnected() const override;
  bool Reconnect() override;

  // ============================================================================
  // TEC CONTROL
  // ============================================================================

  TECCommandResult SetTemperature(double temperature) override;
  TECCommandResult TEC_On() override;
  TECCommandResult TEC_Off() override;
  TECCommandResult ReadTemperature() override;
  TECCommandResult ReadTECCurrent() override;

  // ============================================================================
  // LASER CONTROL (NOT SUPPORTED - TED4015 is TEC-only)
  // ============================================================================

  TECCommandResult SetLaserCurrent(double current) override;
  TECCommandResult Laser_On() override;
  TECCommandResult Laser_Off() override;
  TECCommandResult ReadLaserCurrent() override;

  // ============================================================================
  // STATUS QUERIES
  // ============================================================================

  TECStatus GetStatus() const override;
  std::string GetDeviceName() const override;
  std::string GetDeviceType() const override;

private:
  // Device configuration
  TECDevice m_config;

  // VISA session handles
  ViSession m_defaultRM;      // VISA Resource Manager
  ViSession m_instrument;     // Instrument session
  bool m_connected;

  // Cached status
  mutable TECStatus m_status;

  // Communication timeout (milliseconds)
  static constexpr unsigned long TIMEOUT_MS = 5000;

  // Buffer size for VISA read operations
  static constexpr unsigned long READ_BUFFER_SIZE = 1024;

  /**
   * @brief Send SCPI command to device (write only, no response)
   * @param command SCPI command string
   * @return true if write successful
   */
  bool WriteCommand(const std::string& command);

  /**
   * @brief Send SCPI query to device and read response
   * @param query SCPI query string (should end with '?')
   * @param response Output response string
   * @return true if query successful
   */
  bool QueryCommand(const std::string& query, std::string& response);

  /**
   * @brief Parse numeric value from SCPI response
   * @param response Response from device
   * @return Parsed value (0.0 if parse fails)
   */
  double ParseNumericResponse(const std::string& response) const;

  /**
   * @brief Query TEC state (ON/OFF)
   * @return true if query successful
   */
  bool QueryTECState();

  /**
   * @brief Get VISA error description
   * @param status VISA status code
   * @return Error description string
   */
  std::string GetVISAErrorDescription(ViStatus status) const;
};
