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

std::string SMUPollingManager::MakeSessionKey(const std::string& deviceName, int channel)
{
  return deviceName + "_Ch" + std::to_string(channel);
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

  const char* typeStr = (measurementType == MeasurementType::CURRENT) ? "CURRENT" : "VOLTAGE";

  // Find or create worker for this device
  auto it = m_workers.find(deviceName);
  if (it == m_workers.end()) {
    // Create new device-level worker
    auto worker = std::make_unique<PollingWorker>();
    worker->deviceName = deviceName;
    worker->device = device;
    worker->intervalMs = intervalMs;
    worker->stopRequested = false;
    worker->isRunning = false;

    // Add first channel (construct in place to avoid copying atomic)
    worker->channels.emplace(std::piecewise_construct,
                             std::forward_as_tuple(channel),
                             std::forward_as_tuple());
    worker->channels[channel].channel = channel;
    worker->channels[channel].measurementType = measurementType;
    worker->channels[channel].sampleCount = 0;

    std::cout << "[SMUPollingManager] Starting polling for device: " << deviceName
              << " (Ch" << channel << ", interval=" << intervalMs << "ms, type=" << typeStr << ")" << std::endl;

    // Start worker thread
    worker->workerThread = std::make_unique<std::thread>(
      &SMUPollingManager::PollingThreadFunction, this, worker.get());

    m_workers[deviceName] = std::move(worker);
  }
  else {
    // Worker exists, add channel to it
    std::lock_guard<std::mutex> channelLock(it->second->channelsMutex);

    if (it->second->channels.find(channel) != it->second->channels.end()) {
      std::cout << "[SMUPollingManager] Channel " << channel << " already polling on " << deviceName << std::endl;
      return false;
    }

    // Add channel (construct in place to avoid copying atomic)
    it->second->channels.emplace(std::piecewise_construct,
                                  std::forward_as_tuple(channel),
                                  std::forward_as_tuple());
    it->second->channels[channel].channel = channel;
    it->second->channels[channel].measurementType = measurementType;
    it->second->channels[channel].sampleCount = 0;

    std::cout << "[SMUPollingManager] Added Ch" << channel << " to existing polling worker for " << deviceName
              << " (type=" << typeStr << ", now polling " << it->second->channels.size() << " channels)" << std::endl;
  }

  emit pollingStateChanged(QString::fromStdString(deviceName), true);
  return true;
}

void SMUPollingManager::StopPolling(const std::string& deviceName)
{
  std::unique_ptr<PollingWorker> workerToStop;

  // Scope the lock to avoid holding it during thread join
  {
    std::lock_guard<std::mutex> lock(m_workersMutex);

    auto it = m_workers.find(deviceName);
    if (it == m_workers.end()) {
      std::cout << "[SMUPollingManager] No active polling for " << deviceName << std::endl;
      return;
    }

    // Calculate total samples across all channels for logging
    uint64_t totalSamples = 0;
    {
      std::lock_guard<std::mutex> channelLock(it->second->channelsMutex);
      for (const auto& [channelNum, channelConfig] : it->second->channels) {
        totalSamples += channelConfig.sampleCount.load();
      }
    }

    std::cout << "[SMUPollingManager] Stopping polling for device: " << deviceName
              << " (" << it->second->channels.size() << " channels, total samples: " << totalSamples << ")" << std::endl;

    it->second->stopRequested = true;
    workerToStop = std::move(it->second);
    m_workers.erase(it);
  }  // Lock is automatically released here

  // Wait for thread to finish (mutex is now unlocked)
  if (workerToStop && workerToStop->workerThread && workerToStop->workerThread->joinable()) {
    workerToStop->workerThread->join();
  }

  emit pollingStateChanged(QString::fromStdString(deviceName), false);
}

void SMUPollingManager::StopPollingChannel(const std::string& deviceName, int channel)
{
  std::unique_ptr<PollingWorker> workerToStop;

  // Scope the lock to avoid holding it during thread join
  {
    std::lock_guard<std::mutex> lock(m_workersMutex);

    auto it = m_workers.find(deviceName);
    if (it == m_workers.end()) {
      std::cout << "[SMUPollingManager] No active polling for device " << deviceName << std::endl;
      return;
    }

    // Remove channel from worker's channel map
    {
      std::lock_guard<std::mutex> channelLock(it->second->channelsMutex);

      auto channelIt = it->second->channels.find(channel);
      if (channelIt == it->second->channels.end()) {
        std::cout << "[SMUPollingManager] Channel " << channel << " not polling on " << deviceName << std::endl;
        return;
      }

      uint64_t samples = channelIt->second.sampleCount.load();
      std::cout << "[SMUPollingManager] Stopping polling: " << deviceName << " Ch" << channel
                << " (samples: " << samples << ")" << std::endl;

      it->second->channels.erase(channelIt);

      // If this was the last channel, stop the worker thread
      if (it->second->channels.empty()) {
        std::cout << "[SMUPollingManager] Last channel removed, stopping worker for " << deviceName << std::endl;
        it->second->stopRequested = true;
        workerToStop = std::move(it->second);
        m_workers.erase(it);
      } else {
        std::cout << "[SMUPollingManager] Channel removed, " << it->second->channels.size()
                  << " channels still polling on " << deviceName << std::endl;
      }
    }
  }  // Lock is automatically released here

  // Wait for thread to finish if we stopped the worker
  if (workerToStop && workerToStop->workerThread && workerToStop->workerThread->joinable()) {
    workerToStop->workerThread->join();
  }

  // Check if device still has channels polling
  bool stillPolling = IsPolling(deviceName);
  if (!stillPolling) {
    emit pollingStateChanged(QString::fromStdString(deviceName), false);
  }
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
  // Check if ANY channel on this device is polling
  for (const auto& [key, worker] : m_workers) {
    if (worker->deviceName == deviceName && worker->isRunning) {
      return true;
    }
  }
  return false;
}

bool SMUPollingManager::IsPollingChannel(const std::string& deviceName, int channel) const
{
  std::lock_guard<std::mutex> lock(m_workersMutex);
  auto it = m_workers.find(deviceName);
  if (it == m_workers.end()) {
    return false;
  }

  std::lock_guard<std::mutex> channelLock(it->second->channelsMutex);
  return (it->second->channels.find(channel) != it->second->channels.end() && it->second->isRunning);
}

std::vector<int> SMUPollingManager::GetPollingChannels(const std::string& deviceName) const
{
  std::lock_guard<std::mutex> lock(m_workersMutex);
  std::vector<int> channels;

  auto it = m_workers.find(deviceName);
  if (it != m_workers.end() && it->second->isRunning) {
    std::lock_guard<std::mutex> channelLock(it->second->channelsMutex);
    for (const auto& [channelNum, channelConfig] : it->second->channels) {
      channels.push_back(channelNum);
    }
  }

  return channels;
}

uint64_t SMUPollingManager::GetSampleCount(const std::string& deviceName, int channel) const
{
  std::lock_guard<std::mutex> lock(m_workersMutex);
  auto it = m_workers.find(deviceName);
  if (it == m_workers.end()) {
    return 0;
  }

  std::lock_guard<std::mutex> channelLock(it->second->channelsMutex);
  auto channelIt = it->second->channels.find(channel);
  return (channelIt != it->second->channels.end()) ? channelIt->second.sampleCount.load() : 0;
}

void SMUPollingManager::ResetSampleCount(const std::string& deviceName, int channel)
{
  std::lock_guard<std::mutex> lock(m_workersMutex);
  auto it = m_workers.find(deviceName);
  if (it == m_workers.end()) {
    return;
  }

  std::lock_guard<std::mutex> channelLock(it->second->channelsMutex);
  auto channelIt = it->second->channels.find(channel);
  if (channelIt != it->second->channels.end()) {
    channelIt->second.sampleCount = 0;
    std::cout << "[SMUPollingManager] Reset sample count for " << deviceName << " Ch" << channel << std::endl;
  }
}

std::map<std::string, std::tuple<std::string, int, SMUPollingManager::MeasurementType>>
SMUPollingManager::GetAllActiveSessions() const
{
  std::lock_guard<std::mutex> lock(m_workersMutex);
  std::map<std::string, std::tuple<std::string, int, MeasurementType>> sessions;

  for (const auto& [deviceName, worker] : m_workers) {
    if (worker->isRunning) {
      std::lock_guard<std::mutex> channelLock(worker->channelsMutex);
      for (const auto& [channelNum, channelConfig] : worker->channels) {
        std::string sessionKey = MakeSessionKey(deviceName, channelNum);
        sessions[sessionKey] = std::make_tuple(deviceName, channelNum, channelConfig.measurementType);
      }
    }
  }

  return sessions;
}

void SMUPollingManager::PollingThreadFunction(PollingWorker* worker)
{
  if (!worker || !worker->device) {
    std::cerr << "[SMUPollingManager] ERROR: Invalid worker" << std::endl;
    return;
  }

  worker->isRunning = true;

  // Log which channels are being polled
  {
    std::lock_guard<std::mutex> channelLock(worker->channelsMutex);
    std::cout << "[SMUPollingManager] Polling thread started for " << worker->deviceName
              << " (" << worker->channels.size() << " channels)" << std::endl;
  }

  auto nextPollTime = std::chrono::steady_clock::now();

  while (!worker->stopRequested) {
    // Read all channels from device in one efficient call
    std::vector<double> allChannelValues = worker->device->ReadAllChannels();

    // Generate timestamp for this measurement cycle
    std::time_t now = std::time(nullptr);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S");
    std::string timestamp = ss.str();

    // Distribute results to each active channel
    {
      std::lock_guard<std::mutex> channelLock(worker->channelsMutex);

      for (auto& [channelNum, channelConfig] : worker->channels) {
        double value = -999.0;

        // Extract value for this channel based on measurement type
        if (channelConfig.measurementType == MeasurementType::CURRENT) {
          // Current measurement - get from channel array (1-indexed)
          if (channelNum > 0 && channelNum <= static_cast<int>(allChannelValues.size())) {
            value = allChannelValues[channelNum - 1];
          }
        } else {
          // Voltage measurement - use dedicated voltage read
          value = worker->device->ReadVoltage();
        }

        if (value != -999.0) {
          // Success - increment this channel's sample count
          channelConfig.sampleCount.fetch_add(1);

          // Emit measurement signal for this channel
          emit measurementReady(
            QString::fromStdString(worker->deviceName),
            channelNum,
            value,
            QString::fromStdString(timestamp)
          );
        } else {
          // Error reading this channel
          std::string error = worker->device->GetLastError();
          if (!error.empty()) {
            std::cerr << "[SMUPollingManager] Read error on " << worker->deviceName
                      << " Ch" << channelNum << ": " << error << std::endl;
            emit pollingError(QString::fromStdString(worker->deviceName),
                             QString::fromStdString(error));
          }
        }
      }
    }

    // Sleep until next poll time
    nextPollTime += std::chrono::milliseconds(worker->intervalMs);
    std::this_thread::sleep_until(nextPollTime);
  }

  worker->isRunning = false;

  // Calculate total samples across all channels for logging
  uint64_t totalSamples = 0;
  {
    std::lock_guard<std::mutex> channelLock(worker->channelsMutex);
    for (const auto& [channelNum, channelConfig] : worker->channels) {
      totalSamples += channelConfig.sampleCount.load();
    }
  }

  std::cout << "[SMUPollingManager] Polling thread stopped for " << worker->deviceName
            << " (total samples across all channels: " << totalSamples << ")" << std::endl;
}
