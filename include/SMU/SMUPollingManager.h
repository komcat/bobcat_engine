// SMUPollingManager.h
#pragma once

#include "ISMUDevice.h"
#include <QObject>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <tuple>

class DataStore;

/**
 * @brief Centralized manager for SMU device polling
 *
 * Manages background worker threads for continuous SMU measurements.
 * Provides thread-safe interface for starting/stopping polling from
 * recipes, UI, or any other component.
 *
 * Architecture (ported from FireDog's Keithley6482 poll loop):
 *   - Uses std::thread workers (not Qt timers)
 *   - Thread-safe command interface
 *   - **Samples are published straight to DataStore from the poll thread.**
 *     The Qt signal is a UI convenience only and is throttled to
 *     kUiEmitIntervalMs; it is NOT on the data path. Routing every sample
 *     through a queued signal put the scan's fresh-sample gate behind a main
 *     event-loop turn, so a busy UI thread inflated the scan cycle time.
 *   - intervalMs <= 0 means continuous: the instrument paces the loop through
 *     NPLC + GPIB latency, with a 1 ms yield so a UI command can take the
 *     device mutex between reads. intervalMs > 0 is a *rate cap*, measured
 *     from the start of each read (no accumulated catch-up bursts).
 *   - Manages polling state for all devices
 */
class SMUPollingManager : public QObject
{
  Q_OBJECT

public:
  enum class MeasurementType {
    CURRENT,
    VOLTAGE
  };

  /// UI emit period. The DataStore publish is never throttled - only the
  /// Qt signal that feeds the labels/chart, which repaint on a slow tick anyway.
  static constexpr int kUiEmitIntervalMs = 100;

  explicit SMUPollingManager(QObject* parent = nullptr);
  ~SMUPollingManager();

  /**
   * @brief Give the manager the DataStore that poll threads publish into.
   *
   * Must be called before any StartPolling(). Publishing here rather than in a
   * slot on the main thread is what keeps ScanContext's fresh-sample gate
   * (DataStore::GetUpdateCount) tied to the instrument's cadence instead of
   * the Qt event loop's.
   */
  void SetDataStore(DataStore* dataStore);

  /// DataStore channel id a device/channel publishes into ("SMU_<dev>_Ch<n>").
  static std::string MakeDataStoreKey(const std::string& deviceName, int channel);

  // ========== Polling Control (Thread-Safe) ==========

  /**
   * @brief Start polling on a specific SMU device
   * @param deviceName Name of the SMU device
   * @param device Shared pointer to the SMU device
   * @param channel Channel to poll (1-4)
   * @param intervalMs Rate cap in milliseconds; <= 0 polls continuously
   * @param measurementType Type of measurement (CURRENT or VOLTAGE)
   * @return true if polling started successfully
   */
  bool StartPolling(const std::string& deviceName,
                    SMUDevicePtr device,
                    int channel,
                    int intervalMs,   // <= 0 : continuous (instrument-paced)
                    MeasurementType measurementType = MeasurementType::CURRENT);

  /**
   * @brief Stop polling on a specific device (stops ALL channels on that device)
   * @param deviceName Name of the device to stop
   */
  void StopPolling(const std::string& deviceName);

  /**
   * @brief Stop polling on a specific device channel
   * @param deviceName Name of the device
   * @param channel Channel number to stop
   */
  void StopPollingChannel(const std::string& deviceName, int channel);

  /**
   * @brief Stop all active polling
   */
  void StopAllPolling();

  /**
   * @brief Check if a device is currently polling (any channel)
   * @param deviceName Device name
   * @return true if polling is active on any channel
   */
  bool IsPolling(const std::string& deviceName) const;

  /**
   * @brief Check if a specific device channel is polling
   * @param deviceName Device name
   * @param channel Channel number
   * @return true if this channel is polling
   */
  bool IsPollingChannel(const std::string& deviceName, int channel) const;

  /**
   * @brief Get all polling channels for a device
   * @param deviceName Device name
   * @return Vector of channel numbers currently polling
   */
  std::vector<int> GetPollingChannels(const std::string& deviceName) const;

  /**
   * @brief Get sample count for a device channel
   * @param deviceName Device name
   * @param channel Channel number
   * @return Number of samples collected
   */
  uint64_t GetSampleCount(const std::string& deviceName, int channel) const;

  /**
   * @brief Reset sample counter for a device channel
   * @param deviceName Device name
   * @param channel Channel number
   */
  void ResetSampleCount(const std::string& deviceName, int channel);

  /**
   * @brief Get all active polling sessions
   * @return Map of session keys to (deviceName, channel, measurementType)
   */
  std::map<std::string, std::tuple<std::string, int, MeasurementType>> GetAllActiveSessions() const;

signals:
  /**
   * @brief Emitted when a new measurement is ready
   * @param deviceName Name of the SMU device
   * @param channel Channel number (1-4)
   * @param current Current value in amperes
   * @param timestamp ISO timestamp string
   */
  void measurementReady(QString deviceName, int channel, double current, QString timestamp);

  /**
   * @brief Emitted when polling state changes
   * @param deviceName Name of the device
   * @param isPolling true if polling started, false if stopped
   */
  void pollingStateChanged(QString deviceName, bool isPolling);

  /**
   * @brief Emitted when a polling error occurs
   * @param deviceName Name of the device
   * @param errorMessage Error description
   */
  void pollingError(QString deviceName, QString errorMessage);

private:
  struct ChannelConfig
  {
    int channel;
    MeasurementType measurementType;
    std::atomic<uint64_t> sampleCount{0};
    std::string dataStoreKey;   // precomputed - the poll loop must not allocate
  };

  struct PollingWorker
  {
    std::string deviceName;
    SMUDevicePtr device;
    int intervalMs;
    std::map<int, ChannelConfig> channels;  // Map of channel -> config
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> isRunning{false};
    std::unique_ptr<std::thread> workerThread;
    mutable std::mutex channelsMutex;
  };

  std::map<std::string, std::unique_ptr<PollingWorker>> m_workers;  // Key = deviceName
  mutable std::mutex m_workersMutex;

  // Set once during startup, read by every poll thread.
  std::atomic<DataStore*> m_dataStore{nullptr};

  void PollingThreadFunction(PollingWorker* worker);

  // Helper to create session key: "DeviceName_Ch1"
  static std::string MakeSessionKey(const std::string& deviceName, int channel);
};
