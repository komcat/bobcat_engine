// SMUPollingManager.h
#pragma once

#include "ISMUDevice.h"
#include <QObject>
#include <thread>
#include <atomic>
#include <mutex>
#include <map>
#include <memory>
#include <string>

/**
 * @brief Centralized manager for SMU device polling
 *
 * Manages background worker threads for continuous SMU measurements.
 * Provides thread-safe interface for starting/stopping polling from
 * recipes, UI, or any other component.
 *
 * Architecture:
 *   - Uses std::thread workers (not Qt timers)
 *   - Thread-safe command interface
 *   - Emits Qt signals for data distribution
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

  explicit SMUPollingManager(QObject* parent = nullptr);
  ~SMUPollingManager();

  // ========== Polling Control (Thread-Safe) ==========

  /**
   * @brief Start polling on a specific SMU device
   * @param deviceName Name of the SMU device
   * @param device Shared pointer to the SMU device
   * @param channel Channel to poll (1-4)
   * @param intervalMs Polling interval in milliseconds
   * @param measurementType Type of measurement (CURRENT or VOLTAGE)
   * @return true if polling started successfully
   */
  bool StartPolling(const std::string& deviceName,
                    SMUDevicePtr device,
                    int channel,
                    int intervalMs,
                    MeasurementType measurementType = MeasurementType::CURRENT);

  /**
   * @brief Stop polling on a specific device
   * @param deviceName Name of the device to stop
   */
  void StopPolling(const std::string& deviceName);

  /**
   * @brief Stop all active polling
   */
  void StopAllPolling();

  /**
   * @brief Check if a device is currently polling
   * @param deviceName Device name
   * @return true if polling is active
   */
  bool IsPolling(const std::string& deviceName) const;

  /**
   * @brief Get sample count for a device
   * @param deviceName Device name
   * @return Number of samples collected
   */
  uint64_t GetSampleCount(const std::string& deviceName) const;

  /**
   * @brief Reset sample counter for a device
   * @param deviceName Device name
   */
  void ResetSampleCount(const std::string& deviceName);

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
  struct PollingWorker
  {
    std::string deviceName;
    SMUDevicePtr device;
    int channel;
    int intervalMs;
    MeasurementType measurementType;
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> isRunning{false};
    std::atomic<uint64_t> sampleCount{0};
    std::unique_ptr<std::thread> workerThread;
  };

  std::map<std::string, std::unique_ptr<PollingWorker>> m_workers;
  mutable std::mutex m_workersMutex;

  void PollingThreadFunction(PollingWorker* worker);
};
