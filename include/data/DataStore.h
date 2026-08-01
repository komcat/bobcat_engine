// DataStore.h - Thread-safe data cache for real-time values
#pragma once

#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <functional>
#include <memory>
#include <atomic>
#include <cstdint>

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

  // ✅ Shutdown control - call before destruction to prevent crash from pending callbacks
  void BeginShutdown();
  bool IsShuttingDown() const;

  // Value operations
  void SetValue(const std::string& channelId, float value);
  float GetValue(const std::string& channelId, float defaultValue = 0.0f) const;
  bool HasValue(const std::string& channelId) const;
  bool TryGetValue(const std::string& channelId, float& outValue) const;

  // Publish counter -------------------------------------------------------
  // Every SetValue() bumps a per-channel monotonic counter. Consumers that
  // must prove a sample is NEW (rather than merely waiting a fixed time for
  // it) gate on this counter instead of sleeping - see the fresh-sample gate
  // in ScanContext::GetFreshMeasurement().
  //
  // The counter is monotonic for the process lifetime: ClearChannel/ClearAll
  // deliberately do NOT reset it, so a counter can never appear to run
  // backwards underneath a gate that is already waiting on it.

  /// @return number of SetValue() calls on this channel, 0 if never written
  uint64_t GetUpdateCount(const std::string& channelId) const;

  /// Snapshot value and its publish counter atomically (single lock).
  /// outCount is the counter value that produced outValue, so a caller
  /// waiting for "count >= n0 + N" gets exactly the matching sample.
  bool TryGetValue(const std::string& channelId, float& outValue, uint64_t& outCount) const;

  std::vector<std::string> GetAvailableChannels() const;
  std::vector<std::string> GetAllChannelNames() const;
  
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

  // Monotonic publish counter per channel. Never erased - see GetUpdateCount().
  std::map<std::string, uint64_t> m_updateCounts;

  // Subscriptions: channelId -> list of callbacks
  std::map<std::string, std::vector<DataCallback>> m_subscribers;
  std::vector<DataCallback> m_globalSubscribers; // Subscribe to all channels

  bool m_debugMode;
  std::atomic<bool> m_shuttingDown;  // ✅ Shutdown flag to reject writes during destruction

  void NotifySubscribers(const std::string& channelId, float value);
};