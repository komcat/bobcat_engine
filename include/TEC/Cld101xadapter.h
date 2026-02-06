// CLD101xAdapter.h
#pragma once
#include "ITECDevice.h"
#include "TECTypes.h"
#include "SocketGuard.h"
#include <string>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

/**
 * @brief Adapter for CLD101x Laser Diode and TEC Controller
 *
 * Communicates with CLD101x device via TCP socket to server.
 * Implements ITECDevice interface.
 */
class CLD101xAdapter : public ITECDevice {
public:
  /**
   * @brief Constructor
   * @param config Device configuration
   */
  explicit CLD101xAdapter(const TECDevice& config);

  /**
   * @brief Destructor - ensures disconnection
   */
  ~CLD101xAdapter() override;

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

  // TCP socket
  SocketGuard m_socket;
  bool m_connected;

  // Cached status
  mutable TECStatus m_status;

  // Communication timeout (milliseconds)
  static constexpr int TIMEOUT_MS = 5000;

  /**
   * @brief Send command to server and receive response
   * @param command Command string to send
   * @param response Output response string
   * @return true if communication successful
   */
  bool SendCommand(const std::string& command, std::string& response);

  /**
   * @brief Parse numeric value from response string
   * @param response Response from server
   * @return Parsed value (0.0 if parse fails)
   */
  double ParseNumericResponse(const std::string& response) const;

  /**
   * @brief Query laser state (ON/OFF)
   */
  bool QueryLaserState();

  /**
   * @brief Query TEC state (ON/OFF)
   */
  bool QueryTECState();

  /**
   * @brief Update cached status
   */
  void UpdateStatus() const;
};