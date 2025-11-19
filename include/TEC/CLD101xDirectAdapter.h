// CLD101xDirectAdapter.h
#pragma once
#include "ITECDevice.h"
#include "TECTypes.h"
#include <string>
#include <visa.h>  // Include VISA header for type definitions

/**
 * @brief Direct VISA adapter for CLD101x Laser Diode and TEC Controller
 *
 * Communicates directly with CLD101x device via NI-VISA library.
 * Removes the need for Python middleware server.
 * Implements ITECDevice interface.
 *
 * Requirements:
 * - NI-VISA Runtime installed on the system
 * - USB connection to CLD101x device
 *
 * VISA Resource String Format:
 *   USB0::0x1313::0x804F::M00930341::INSTR
 *   where:
 *     0x1313 = Thorlabs Vendor ID
 *     0x804F = CLD101x Product ID
 *     M00930341 = Serial number (unique to each device)
 */
class CLD101xDirectAdapter : public ITECDevice {
public:
  /**
   * @brief Constructor
   * @param config Device configuration (must include VISAResource field)
   */
  explicit CLD101xDirectAdapter(const TECDevice& config);

  /**
   * @brief Destructor - ensures disconnection and cleanup
   */
  ~CLD101xDirectAdapter() override;

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
  // LASER CONTROL
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
  static constexpr unsigned long TIMEOUT_MS = 2000;

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
   * @brief Query laser state (ON/OFF)
   * @return true if query successful
   */
  bool QueryLaserState();

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
