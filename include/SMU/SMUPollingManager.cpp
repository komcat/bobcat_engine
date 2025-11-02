// SMUPollingManager.cpp
#include "SMUPollingManager.h"
#include <iostream>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <ctime>

SMUPollingManager::SMUPollingManager(QObject* parent)
  : QObject(parent)
{
  std::cout << "[SMUPollingManager] Created" << std::endl;
}

SMUPollingManager::~SMUPollingManager()
{
  std::cout << "[SMUPollingManager] Destroying - stopping all polling" << std::endl;
  StopAllPolling();
}

bool SMUPollingManager::StartPolling(const std::string& deviceName,
                                      SMUDevicePtr device,
                                      int channel,
                                      int intervalMs,
                                      MeasurementType measurementType)
{
  if (!device) {
    std::cerr << "[SMUPollingManager] ERROR: Null device pointer for " << deviceName << std::endl;
    return false;
  }

  if (!device->IsConnected()) {
    std::cerr << "[SMUPollingManager] ERROR: Device not connected: " << deviceName << std::endl;
    emit pollingError(QString::fromStdString(deviceName), "Device not connected");
    return false;
  }

  std::lock_guard<std::mutex> lock(m_workersMutex);

  // Stop existing polling if any
  auto it = m_workers.find(deviceName);
  if (it != m_workers.end()) {
    std::cout << "[SMUPollingManager] Stopping existing polling for " << deviceName << std::endl;
    it->second->stopRequested = true;
    if (it->second->workerThread && it->second->workerThread->joinable()) {
      it->second->workerThread->join();
    }
    m_workers.erase(it);
  }

  // Create new worker
  auto worker = std::make_unique<PollingWorker>();
  worker->deviceName = deviceName;
  worker->device = device;
  worker->channel = channel;
  worker->intervalMs = intervalMs;
  worker->measurementType = measurementType;
  worker->stopRequested = false;
  worker->isRunning = false;
  worker->sampleCount = 0;

  const char* typeStr = (measurementType == MeasurementType::CURRENT) ? "CURRENT" : "VOLTAGE";
  std::cout << "[SMUPollingManager] Starting polling: " << deviceName
            << " (channel=" << channel << ", interval=" << intervalMs << "ms, type=" << typeStr << ")" << std::endl;

  // Start worker thread
  worker->workerThread = std::make_unique<std::thread>(
    &SMUPollingManager::PollingThreadFunction, this, worker.get());

  m_workers[deviceName] = std::move(worker);

  emit pollingStateChanged(QString::fromStdString(deviceName), true);
  return true;
}

void SMUPollingManager::StopPolling(const std::string& deviceName)
{
  std::unique_ptr<PollingWorker> worker;

  // Scope the lock to avoid holding it during thread join
  {
    std::lock_guard<std::mutex> lock(m_workersMutex);

    auto it = m_workers.find(deviceName);
    if (it == m_workers.end()) {
      std::cout << "[SMUPollingManager] No active polling for " << deviceName << std::endl;
      return;
    }

    std::cout << "[SMUPollingManager] Stopping polling: " << deviceName
              << " (samples collected: " << it->second->sampleCount << ")" << std::endl;

    it->second->stopRequested = true;

    // Move worker out and erase from map
    worker = std::move(it->second);
    m_workers.erase(it);
  }  // Lock is automatically released here

  // Wait for thread to finish (mutex is now unlocked)
  if (worker && worker->workerThread && worker->workerThread->joinable()) {
    worker->workerThread->join();
  }

  emit pollingStateChanged(QString::fromStdString(deviceName), false);
}

void SMUPollingManager::StopAllPolling()
{
  std::lock_guard<std::mutex> lock(m_workersMutex);

  std::cout << "[SMUPollingManager] Stopping all polling (" << m_workers.size() << " active)" << std::endl;

  // Request all workers to stop
  for (auto& [name, worker] : m_workers) {
    worker->stopRequested = true;
  }

  // Wait for all threads to finish
  for (auto& [name, worker] : m_workers) {
    if (worker->workerThread && worker->workerThread->joinable()) {
      worker->workerThread->join();
    }
    emit pollingStateChanged(QString::fromStdString(name), false);
  }

  m_workers.clear();
}

bool SMUPollingManager::IsPolling(const std::string& deviceName) const
{
  std::lock_guard<std::mutex> lock(m_workersMutex);
  auto it = m_workers.find(deviceName);
  return (it != m_workers.end() && it->second->isRunning);
}

uint64_t SMUPollingManager::GetSampleCount(const std::string& deviceName) const
{
  std::lock_guard<std::mutex> lock(m_workersMutex);
  auto it = m_workers.find(deviceName);
  return (it != m_workers.end()) ? it->second->sampleCount.load() : 0;
}

void SMUPollingManager::ResetSampleCount(const std::string& deviceName)
{
  std::lock_guard<std::mutex> lock(m_workersMutex);
  auto it = m_workers.find(deviceName);
  if (it != m_workers.end()) {
    it->second->sampleCount = 0;
    std::cout << "[SMUPollingManager] Reset sample count for " << deviceName << std::endl;
  }
}

void SMUPollingManager::PollingThreadFunction(PollingWorker* worker)
{
  if (!worker || !worker->device) {
    std::cerr << "[SMUPollingManager] ERROR: Invalid worker" << std::endl;
    return;
  }

  worker->isRunning = true;
  std::cout << "[SMUPollingManager] Polling thread started for " << worker->deviceName << std::endl;

  auto nextPollTime = std::chrono::steady_clock::now();

  while (!worker->stopRequested) {
    // Read measurement from device based on type
    double value;
    if (worker->measurementType == MeasurementType::CURRENT) {
      value = worker->device->ReadChannel(worker->channel);
    } else {
      value = worker->device->ReadVoltage();
    }

    if (value != -999.0) {
      // Success - emit signal
      worker->sampleCount++;

      // Generate timestamp
      std::time_t now = std::time(nullptr);
      std::stringstream ss;
      ss << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S");

      // Emit measurement signal (thread-safe via Qt's queued connection)
      // Note: Signal parameter is named 'current' but actually contains the measured value (current or voltage)
      emit measurementReady(
        QString::fromStdString(worker->deviceName),
        worker->channel,
        value,
        QString::fromStdString(ss.str())
      );
    }
    else {
      // Error reading
      std::string error = worker->device->GetLastError();
      if (!error.empty()) {
        std::cerr << "[SMUPollingManager] Read error on " << worker->deviceName
                  << ": " << error << std::endl;
        emit pollingError(QString::fromStdString(worker->deviceName),
                         QString::fromStdString(error));
      }
    }

    // Sleep until next poll time
    nextPollTime += std::chrono::milliseconds(worker->intervalMs);
    std::this_thread::sleep_until(nextPollTime);
  }

  worker->isRunning = false;
  std::cout << "[SMUPollingManager] Polling thread stopped for " << worker->deviceName
            << " (total samples: " << worker->sampleCount << ")" << std::endl;
}
