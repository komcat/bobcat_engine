// KeithleySMUAdapter.h - Generic Keithley SourceMeter Driver (2400/2600 series)
#pragma once

#include "ISMUDevice.h"
#include "SMUTypes.h"
#include <visa.h>
#include <QObject>
#include <string>
#include <mutex>
#include <atomic>

/**
 * @brief Generic Keithley SourceMeter Driver
 *
 * Supports Keithley SourceMeter units including:
 * - 2400 series: 2400, 2401, 2410, 2420, 2425, 2430 (single channel)
 * - 2600 series: 2601B, 2602B, 2604B, 2611B, 2612B, 2614B (dual channel)
 *
 * Features:
 * - Voltage/Current sourcing with compliance
 * - Voltage/Current measurement
 * - Auto-detection of model and channel count
 * - SCPI command interface (compatible across models)
 *
 * Connection:
 * - VISA Resource: "GPIB1::24::INSTR" (or other GPIB/USB address)
 * - Timeout: 5000ms
 *
 * Common SCPI Commands:
 * - *IDN?                          - Get device identification
 * - *RST                           - Reset device
 * - :SOUR:FUNC {VOLT|CURR}         - Set source function
 * - :SOUR:VOLT <value>             - Set voltage source
 * - :SOUR:CURR <value>             - Set current source
 * - :SENS:FUNC "VOLT"|"CURR"       - Set measurement function
 * - :SENS:VOLT:PROT <value>        - Set voltage compliance
 * - :SENS:CURR:PROT <value>        - Set current compliance
 * - :OUTP ON|OFF                   - Output on/off
 * - :READ?                         - Trigger and read
 * - :MEAS:VOLT?                    - Measure voltage
 * - :MEAS:CURR?                    - Measure current
 */
class KeithleySMUAdapter : public QObject, public ISMUDevice
{
  Q_OBJECT

public:
  /**
   * @brief Keithley model family
   */
  enum class ModelFamily {
    UNKNOWN,
    SERIES_2400,  // 2400, 2401, 2410, 2420, etc. (single channel)
    SERIES_2600   // 2601B, 2602B, 2611B, etc. (dual channel)
  };

  /**
   * @brief Source mode
   */
  enum class SourceMode {
    VOLTAGE,  // Source voltage, measure current
    CURRENT   // Source current, measure voltage
  };

  /**
   * @param visaResource VISA resource string (e.g., "GPIB1::24::INSTR")
   * @param config Device configuration
   */
  explicit KeithleySMUAdapter(const std::string& visaResource, const SMUDeviceConfig& config);
  ~KeithleySMUAdapter() override;

  // ========== ISMUDevice Interface Implementation ==========

  // Connection Management
  bool Connect() override;
  void Disconnect() override;
  bool IsConnected() const override { return m_connected; }

  // Device Information
  std::string GetDeviceName() const override { return m_config.Name; }
  std::string GetDeviceID() const override;
  SMUMode GetSupportedMode() const override;

  // Output Control
  bool SetOutputOn() override;
  bool SetOutputOff() override;
  bool IsOutputOn() override;

  // Source Control
  bool SetVoltage(double voltage) override;
  bool SetCurrent(double current) override;
  bool SetCompliance(double limit) override;

  // Measurement
  double ReadVoltage() override;
  double ReadCurrent() override;
  SMUMeasurement MeasureCurrent() override;
  SMUMeasurement MeasureVoltage() override;

  // Multi-Channel Measurement
  int GetChannelCount() const override;
  std::vector<int> GetEnabledChannels() const override;
  double ReadChannel(int channel) override;
  std::vector<double> ReadAllChannels() override;

  // Configuration
  bool SetMeasurementRange(double range, bool autoRange = false) override;
  bool SetIntegrationTime(double nplc) override;
  bool SetZeroCheck(bool enabled) override;
  bool PerformZeroCorrection() override;

  // Error Handling
  std::string GetLastError() const override { return m_lastError; }
  void ClearError() override { m_lastError.clear(); }

  // ========== Additional Methods ==========

  /**
   * @brief Get detected model family
   */
  ModelFamily GetModelFamily() const { return m_modelFamily; }

  /**
   * @brief Get model string (e.g., "2400", "2602B")
   */
  std::string GetModelString() const { return m_modelString; }

  /**
   * @brief Set source mode (voltage or current)
   * @param mode Source mode
   * @return true if successful
   */
  bool SetSourceMode(SourceMode mode);

  /**
   * @brief Get current source mode
   */
  SourceMode GetSourceMode() const { return m_sourceMode; }

  /**
   * @brief Select channel (for dual-channel models)
   * @param channel Channel number (1 or 2)
   * @return true if successful
   */
  bool SelectChannel(int channel);

  /**
   * @brief Get current selected channel
   */
  int GetCurrentChannel() const { return m_currentChannel; }

  /**
   * @brief Query SCPI command
   * @param command SCPI command string
   * @param response Output response string
   * @return true if successful
   */
  bool QueryCommand(const std::string& command, std::string& response);

  /**
   * @brief Write SCPI command
   * @param command SCPI command string
   * @return true if successful
   */
  bool WriteCommand(const std::string& command);

  /**
   * @brief Reset device to factory defaults
   * @return true if successful
   */
  bool Reset();

signals:
  /**
   * @brief Emitted when a new measurement is available (from polling)
   * @param deviceName Name of the device
   * @param channel Channel number
   * @param value Measured value (current or voltage depending on mode)
   * @param timestamp ISO 8601 timestamp string
   */
  void measurementReady(QString deviceName, int channel, double value, QString timestamp);

  /**
   * @brief Emitted on error
   * @param deviceName Name of the device
   * @param errorMessage Error description
   */
  void errorOccurred(QString deviceName, QString errorMessage);

private:
  // VISA session handles
  ViSession m_defaultRM;      // Resource Manager
  ViSession m_instrument;     // Instrument session

  // Configuration
  SMUDeviceConfig m_config;
  std::string m_visaResource;
  std::string m_deviceID;

  // Model detection
  ModelFamily m_modelFamily;
  std::string m_modelString;
  int m_channelCount;

  // State
  bool m_connected;
  bool m_outputOn;
  SourceMode m_sourceMode;
  int m_currentChannel;       // Current selected channel (1 or 2)

  // Thread safety
  mutable std::mutex m_mutex;

  // Error handling
  mutable std::string m_lastError;

  // Constants
  static constexpr unsigned long TIMEOUT_MS = 5000;  // 5 seconds
  static constexpr int BUFFER_SIZE = 1024;

  // Helper methods
  std::string GetVISAErrorDescription(ViStatus status) const;
  void SetLastError(const std::string& error);
  std::string TrimString(const std::string& str) const;
  bool DetectModelFromIDN(const std::string& idn);

  // Internal methods (assume mutex is already locked by caller)
  bool QueryCommandInternal(const std::string& command, std::string& response);
  bool WriteCommandInternal(const std::string& command);
};
