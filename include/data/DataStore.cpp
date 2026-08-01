// DataStore.cpp
#include "DataStore.h"
#include <iostream>
#include <algorithm>

DataStore::DataStore()
  : m_debugMode(false)
  , m_shuttingDown(false)
{
  std::cout << "[DataStore] Created" << std::endl;
}

DataStore::~DataStore()
{
  m_shuttingDown = true;  // Mark as shutting down
  std::cout << "[DataStore] Destroyed (had " << m_values.size() << " channels)" << std::endl;
}

void DataStore::BeginShutdown()
{
  m_shuttingDown = true;
  std::cout << "[DataStore] Shutdown initiated - rejecting new writes" << std::endl;
}

bool DataStore::IsShuttingDown() const
{
  return m_shuttingDown.load();
}

void DataStore::SetValue(const std::string& channelId, float value)
{
  // ✅ CRITICAL: Check shutdown flag BEFORE locking mutex to prevent crash during destruction
  if (m_shuttingDown.load()) {
    // Silently ignore writes during shutdown - prevents mutex crash
    return;
  }

  {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Double-check after acquiring lock (in case shutdown started while waiting)
    if (m_shuttingDown.load()) {
      return;
    }

    bool isNewChannel = (m_values.find(channelId) == m_values.end());
    m_values[channelId] = value;
    ++m_updateCounts[channelId];

    if (m_debugMode && isNewChannel) {
      std::cout << "[DataStore] New channel: " << channelId
        << " = " << value << std::endl;
    }
  }

  // Notify outside lock to prevent deadlocks
  NotifySubscribers(channelId, value);
}

float DataStore::GetValue(const std::string& channelId, float defaultValue) const
{
  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = m_values.find(channelId);
  if (it != m_values.end()) {
    return it->second;
  }

  return defaultValue;
}

bool DataStore::HasValue(const std::string& channelId) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_values.find(channelId) != m_values.end();
}

bool DataStore::TryGetValue(const std::string& channelId, float& outValue) const
{
  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = m_values.find(channelId);
  if (it != m_values.end()) {
    outValue = it->second;
    return true;
  }

  return false;
}

uint64_t DataStore::GetUpdateCount(const std::string& channelId) const
{
  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = m_updateCounts.find(channelId);
  return (it != m_updateCounts.end()) ? it->second : 0u;
}

bool DataStore::TryGetValue(const std::string& channelId, float& outValue, uint64_t& outCount) const
{
  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = m_values.find(channelId);
  if (it == m_values.end()) {
    return false;
  }

  outValue = it->second;

  // Value and counter are read under the same lock, so outCount is exactly
  // the publish that produced outValue - never a later one.
  auto cit = m_updateCounts.find(channelId);
  outCount = (cit != m_updateCounts.end()) ? cit->second : 0u;

  return true;
}

std::vector<std::string> DataStore::GetAvailableChannels() const
{
  std::lock_guard<std::mutex> lock(m_mutex);

  std::vector<std::string> channels;
  channels.reserve(m_values.size());

  for (const auto& [channelId, value] : m_values) {
    channels.push_back(channelId);
  }

  return channels;
}

// ✅ NEW - Alias for GetAvailableChannels()
std::vector<std::string> DataStore::GetAllKeys() const
{
  return GetAvailableChannels();
}

std::vector<std::string> DataStore::GetAllChannelNames() const
{
  return GetAvailableChannels();
}

void DataStore::ClearChannel(const std::string& channelId)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_values.erase(channelId);
  // m_updateCounts is deliberately NOT erased - the publish counter must stay
  // monotonic so a fresh-sample gate already waiting on it cannot be fooled.
}

void DataStore::ClearAll()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_values.clear();
  // m_updateCounts deliberately kept - see ClearChannel().

  if (m_debugMode) {
    std::cout << "[DataStore] All channels cleared" << std::endl;
  }
}

size_t DataStore::GetChannelCount() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_values.size();
}

void DataStore::Subscribe(const std::string& channelId, DataCallback callback)
{
  if (!callback) return;

  std::lock_guard<std::mutex> lock(m_mutex);
  m_subscribers[channelId].push_back(callback);

  if (m_debugMode) {
    std::cout << "[DataStore] Subscriber added to channel: " << channelId << std::endl;
  }
}

void DataStore::SubscribeToAll(DataCallback callback)
{
  if (!callback) return;

  std::lock_guard<std::mutex> lock(m_mutex);
  m_globalSubscribers.push_back(callback);

  if (m_debugMode) {
    std::cout << "[DataStore] Global subscriber added" << std::endl;
  }
}

void DataStore::Unsubscribe(const std::string& channelId)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_subscribers.erase(channelId);
}

void DataStore::NotifySubscribers(const std::string& channelId, float value)
{
  std::vector<DataCallback> callbacksCopy;
  std::vector<DataCallback> globalCopy;

  // Copy callbacks while holding lock
  {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_subscribers.find(channelId);
    if (it != m_subscribers.end()) {
      callbacksCopy = it->second;
    }

    globalCopy = m_globalSubscribers;
  }

  // Call outside lock
  for (auto& callback : callbacksCopy) {
    try {
      callback(channelId, value);
    }
    catch (const std::exception& e) {
      std::cerr << "[DataStore] Subscriber exception: " << e.what() << std::endl;
    }
  }

  for (auto& callback : globalCopy) {
    try {
      callback(channelId, value);
    }
    catch (const std::exception& e) {
      std::cerr << "[DataStore] Global subscriber exception: " << e.what() << std::endl;
    }
  }
}

void DataStore::PrintAllChannels() const
{
  std::lock_guard<std::mutex> lock(m_mutex);

  std::cout << "[DataStore] Channels (" << m_values.size() << "):" << std::endl;
  for (const auto& [channelId, value] : m_values) {
    std::cout << "  " << channelId << " = " << value << std::endl;
  }
}