// DataStore.cpp
#include "DataStore.h"
#include <iostream>
#include <algorithm>

DataStore::DataStore()
  : m_debugMode(false)
{
  std::cout << "[DataStore] Created" << std::endl;
}

DataStore::~DataStore()
{
  std::cout << "[DataStore] Destroyed (had " << m_values.size() << " channels)" << std::endl;
}

void DataStore::SetValue(const std::string& channelId, float value)
{
  {
    std::lock_guard<std::mutex> lock(m_mutex);

    bool isNewChannel = (m_values.find(channelId) == m_values.end());
    m_values[channelId] = value;

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
}

void DataStore::ClearAll()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_values.clear();

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