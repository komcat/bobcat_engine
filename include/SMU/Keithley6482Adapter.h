// Keithley6482Adapter.h - Keithley 6482 Picoammeter Driver (VISA/GPIB)
#pragma once

#include "ISMUDevice.h"
#include "SMUTypes.h"
#include <visa.h>
#include <QObject>
#include <QTimer>
#include <string>
#include <mutex>
#include <atomic>

/**
 * @brief Keithley 6482 Picoammeter Driver
 *
 * The Keithley 6482 is a dual-channel picoammeter (measurement-only device).
 * It does NOT source voltage or current - it only measures current.
 *
 * Features:
 * - Dual channel current measurement
 * - Range: 2nA to 21mA full scale
 * - Resolution: 10fA (femtoamp)
 * - GPIB/VISA communication
 * - SCPI commands
 *
 * Connection:
 * - VISA Resource: "GPIB0::1::INSTR" (or other GPIB address)
 * - Baud rate: N/A (GPIB)
 * - Timeout: 5000ms
 *
 * Common SCPI Commands:
 * - *IDN?                          - Get device identification
 * - :SENS:CURR:RANG <range>        - Set current range (e.g., 2e-9 for 2nA)
 * - :SENS:CURR:RANG:AUTO ON/OFF    - Auto-range on/off
 * - :SENS:CURR:NPLC <value>        - Set integration time (0.01 to 10)
 * - :READ?                         - Read current measurement
 * - :SYST:ZCH ON/OFF               - Zero check on/off
 * - :SYST:ZCOR:ACQ                 - Perform zero correction
 * - :SYST:ERR?                     - Read error queue
 */
class Keithley6482Adapter : public QObject, public ISMUDevice
{
  Q_OBJECT

public:
  /**
   * @param visaResource VISA resource string (e.g., "GPIB0::1::INSTR")
   * @param config Device configuration
   */
  explicit Keithley6482Adapter(const std::string& visaResource, const SMUDeviceConfig& config);
  ~Keithley6482Adapter() override;

  // ========== ISMUDevice Interface Implementation ==========

  // Connection Management
  bool Connect() override;
  void Disconnect() override;
  bool IsConnected() const override { return m_connected; }

  // Device Information
  std::string GetDeviceName() const override { return m_config.Name; }
  std::string GetDeviceID() const override;
  SMUMode GetSupportedMode() const override { return SMUMode::MEASURE_ONLY; }

  // Output Control (N/A for picoammeter - measurement only)
  bool SetOutputOn() override;   // Not applicable, returns false
  bool SetOutputOff() override;  // Not applicable, returns false
  bool IsOutputOn() override;    // Not applicable, returns false

  // Source Control (N/A for picoammeter)
  bool SetVoltage(double voltage) override;       // Not applicable, returns false
  bool SetCurrent(double current) override;       // Not applicable, returns false
  bool SetCompliance(double limit) override;      // Not applicable, returns false

  // Measurement (PRIMARY FUNCTIONALITY)
  double ReadVoltage() override;                  // Not applicable, returns -999.0
  double ReadCurrent() override;                  // Reads current channel (backward compatible)
  SMUMeasurement MeasureCurrent() override;
  SMUMeasurement MeasureVoltage() override;       // Not applicable

  // Multi-Channel Measurement
  int GetChannelCount() const override;
  std::vector<int> GetEnabledChannels() const override;
  double ReadChannel(int channel) override;       // Read specific channel (1-2 for 6482)
  std::vector<double> ReadAllChannels() override; // Read all enabled channels

  // Configuration
  bool SetMeasurementRange(double range, bool autoRange = false) override;
  bool SetIntegrationTime(double nplc) override;
  bool SetZeroCheck(bool enabled) override;
  bool PerformZeroCorrection() override;

  /**
   * @brief Speed/accuracy mode for measurements
   */
  enum class MeasurementMode {
    ACCURATE,    // NPLC=1.0, auto-zero ON, averaging ON (~10 Hz, high accuracy)
    FAST,        // NPLC=0.1, auto-zero ON, averaging ON (~80 Hz, good accuracy) - COARSE mode
    ULTRA_FAST   // NPLC=0.01, auto-zero OFF, averaging OFF (~150 Hz, lower accuracy) - MONITOR mode
  };

  /**
   * @brief Configure measurement mode for speed vs accuracy tradeoff
   * @param mode Measurement mode (ACCURATE, FAST, or ULTRA_FAST)
   * @return true if successful
   */
  bool ConfigureMeasurementMode(MeasurementMode mode);

  /**
   * @brief Read both channels simultaneously
   * @return pair of (channel1_current, channel2_current) in amperes
   *         Returns (-999.0, -999.0) on error
   */
  std::pair<double, double> ReadBothChannels();

  // Error Handling
  std::string GetLastError() const override { return m_lastError; }
  void ClearError() override { m_lastError.clear(); }

  // ========== Additional Methods ==========

  /**
   * @brief Select measurement channel (1 or 2)
   * @param channel Channel number (1 or 2)
   * @return true if successful
   */
  bool SelectChannel(int channel);

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

  // ========== Polling Methods ==========

  /**
   * @brief Get current selected channel
   * @return Current channel number (1 or 2)
   */
  int GetCurrentChannel() const { return m_currentChannel; }

  /**
   * @brief Start continuous polling (measurements in background thread)
   * @param intervalMs Polling interval in milliseconds (default: 100ms = 10Hz)
   * @return true if polling started successfully
   */
  bool StartPolling(int intervalMs = 100);

  /**
   * @brief Stop continuous polling
   */
  void StopPolling();

  /**
   * @brief Check if polling is active
   * @return true if currently polling
   */
  bool IsPolling() const { return m_isPolling; }

  /**
   * @brief Get polling sample count (total samples acquired)
   * @return Number of samples acquired since polling started
   */
  uint64_t GetSampleCount() const { return m_sampleCount; }

  /**
   * @brief Reset sample count to zero
   */
  void ResetSampleCount() { m_sampleCount = 0; }

signals:
  /**
   * @brief Emitted when a new measurement is available (from polling)
   * @param deviceName Name of the device
   * @param channel Channel number (1 or 2)
   * @param current Current value in amperes
   * @param timestamp ISO 8601 timestamp string
   */
  void measurementReady(QString deviceName, int channel, double current, QString timestamp);

  /**
   * @brief Emitted when polling encounters an error
   * @param deviceName Name of the device
   * @param errorMessage Error description
   */
  void pollingError(QString deviceName, QString errorMessage);

private slots:
  /**
   * @brief Internal slot called by QTimer for periodic polling
   */
  void onPollTimeout();

private:
  // VISA session handles
  ViSession m_defaultRM;      // Resource Manager
  ViSession m_instrument;     // Instrument session

  // Configuration
  SMUDeviceConfig m_config;
  std::string m_visaResource;
  std::string m_deviceID;

  // State
  bool m_connected;
  int m_currentChannel;       // Current selected channel (1 or 2)
  MeasurementMode m_currentMode;  // Current measurement mode

  // Polling state
  QTimer* m_pollTimer;                    // Timer for periodic polling
  std::atomic<bool> m_isPolling;          // Polling active flag
  std::atomic<uint64_t> m_sampleCount;    // Total samples acquired

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

  // Internal methods (assume mutex is already locked by caller)
  bool QueryCommandInternal(const std::string& command, std::string& response);
  bool WriteCommandInternal(const std::string& command);
  bool SetIntegrationTimeInternal(double nplc);
  bool SetMeasurementRangeInternal(double range, bool autoRange);
  bool ConfigureMeasurementModeInternal(MeasurementMode mode);
};
