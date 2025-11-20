// TcpDataClient.h - Core TCP client for real-time float streams
#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <deque>
#include <mutex>
#include <functional>
#include <QtGlobal>  // For qint64

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

/**
 * @brief Protocol type for TCP data stream
 */
enum class TcpProtocolType {
  Single,      // Single float per line: "1.234\n"
  MultiChannel // Multi-channel CSV: "$START,val1,val2,...,$END"
};

/**
 * @brief Non-blocking TCP client for real-time float data streams
 *
 * Supports two protocols:
 * - Single: Newline-delimited float values (e.g., "1.234\n5.678\n")
 * - MultiChannel: CSV format with markers (e.g., "$START,1.2,3.4,5.6,$END")
 *
 * Features:
 * - Non-blocking I/O
 * - Auto-reconnection
 * - Thread-safe
 * - Callback support
 */
class TcpDataClient {
public:
  TcpDataClient();
  ~TcpDataClient();

  // Connection management
  bool Connect(const std::string& host, int port);
  void Disconnect();
  bool IsConnected() const { return m_connected.load(); }

  // Protocol configuration
  void SetProtocolType(TcpProtocolType type) { m_protocolType = type; }
  TcpProtocolType GetProtocolType() const { return m_protocolType; }
  void SetChannelCount(size_t count) { m_channelCount = count; }
  size_t GetChannelCount() const { return m_channelCount; }

  // Auto-reconnection
  void EnableAutoReconnect(bool enable, int intervalMs = 5000);
  bool IsAutoReconnectEnabled() const { return m_autoReconnect.load(); }

  // Data access (for single-channel mode)
  float GetLatestValue() const;
  std::deque<float> GetPendingValues(); // Returns and clears pending values
  size_t GetPendingCount() const;

  // Data access (for multi-channel mode)
  std::vector<float> GetLatestValues() const;

  // Callbacks
  using DataCallback = std::function<void(float value)>;
  void SetDataCallback(DataCallback callback);

  using MultiChannelDataCallback = std::function<void(size_t channelIndex, float value, qint64 timestamp)>;
  void SetMultiChannelDataCallback(MultiChannelDataCallback callback);

  using ConnectionCallback = std::function<void(bool connected)>;
  void SetConnectionCallback(ConnectionCallback callback);

  // Connection info
  std::string GetHost() const { return m_host; }
  int GetPort() const { return m_port; }

private:
  void ReceiveThread();
  void ReconnectThread();
  void ProcessBuffer(std::string& buffer);
  void ProcessSingleChannelBuffer(std::string& buffer);
  void ProcessMultiChannelBuffer(std::string& buffer);
  bool AttemptConnection();
  void NotifyConnection(bool connected);

  SOCKET m_socket;
  std::string m_host;
  int m_port;

  std::thread m_receiveThread;
  std::thread m_reconnectThread;
  std::atomic<bool> m_running;
  std::atomic<bool> m_connected;
  std::atomic<bool> m_autoReconnect;
  int m_reconnectInterval;
  int m_maxReconnectAttempts;
  std::atomic<int> m_reconnectCount;

  // Protocol configuration
  TcpProtocolType m_protocolType;
  size_t m_channelCount;

  // Single-channel data storage
  mutable std::mutex m_dataMutex;
  std::deque<float> m_pendingValues;
  float m_latestValue;

  // Multi-channel data storage
  std::vector<float> m_latestValues;

  // Callbacks
  mutable std::mutex m_callbackMutex;
  DataCallback m_dataCallback;
  MultiChannelDataCallback m_multiChannelDataCallback;
  ConnectionCallback m_connectionCallback;

  static bool s_wsaInitialized;
  static void InitializeWSA();
  static void CleanupWSA();
};