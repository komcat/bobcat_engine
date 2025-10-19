// DataStore.h - Thread-safe data cache for real-time values
#pragma once

#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <functional>
#include <memory>

/**
 * @brief Central data cache for real-time measurement values
 *
 * Thread-safe storage for channel-based data (e.g., "Voltage", "Current", "Temperature")
 * Supports value storage, retrieval, and optional subscription callbacks
 *
 * NOT a singleton - managed through ApplicationContext
 */
class DataStore {
public:
  DataStore();
  ~DataStore();

  // Value operations
  void SetValue(const std::string& channelId, float value);
  float GetValue(const std::string& channelId, float defaultValue = 0.0f) const;
  bool HasValue(const std::string& channelId) const;
  bool TryGetValue(const std::string& channelId, float& outValue) const;

  std::vector<std::string> GetAvailableChannels() const;
  std::vector<std::string> GetAllKeys() const;  // ✅ ADD THIS - Alias for GetAvailableChannels
  void ClearChannel(const std::string& channelId);
  void ClearAll();

  // Statistics
  size_t GetChannelCount() const;

  // Subscription (optional - for reactive updates)
  using DataCallback = std::function<void(const std::string& channelId, float value)>;
  void Subscribe(const std::string& channelId, DataCallback callback);
  void SubscribeToAll(DataCallback callback);
  void Unsubscribe(const std::string& channelId);

  // Debug
  void SetDebugMode(bool enable) { m_debugMode = enable; }
  void PrintAllChannels() const;

private:
  mutable std::mutex m_mutex;
  std::map<std::string, float> m_values;

  // Subscriptions: channelId -> list of callbacks
  std::map<std::string, std::vector<DataCallback>> m_subscribers;
  std::vector<DataCallback> m_globalSubscribers; // Subscribe to all channels

  bool m_debugMode;

  void NotifySubscribers(const std::string& channelId, float value);
};