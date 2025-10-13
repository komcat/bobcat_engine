// TcpDataClient.h - Core TCP client for real-time float streams
#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <deque>
#include <mutex>
#include <functional>

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
 * @brief Non-blocking TCP client for real-time float data streams
 *
 * Protocol: Newline-delimited float values (e.g., "1.234\n5.678\n")
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

  // Auto-reconnection
  void EnableAutoReconnect(bool enable, int intervalMs = 5000);
  bool IsAutoReconnectEnabled() const { return m_autoReconnect.load(); }

  // Data access
  float GetLatestValue() const;
  std::deque<float> GetPendingValues(); // Returns and clears pending values
  size_t GetPendingCount() const;

  // Callbacks
  using DataCallback = std::function<void(float value)>;
  void SetDataCallback(DataCallback callback);

  using ConnectionCallback = std::function<void(bool connected)>;
  void SetConnectionCallback(ConnectionCallback callback);

  // Connection info
  std::string GetHost() const { return m_host; }
  int GetPort() const { return m_port; }

private:
  void ReceiveThread();
  void ReconnectThread();
  void ProcessBuffer(std::string& buffer);
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
  int m_maxReconnectAttempts;        // ADD THIS
  std::atomic<int> m_reconnectCount; // ADD THIS

  mutable std::mutex m_dataMutex;
  std::deque<float> m_pendingValues;
  float m_latestValue;

  mutable std::mutex m_callbackMutex;
  DataCallback m_dataCallback;
  ConnectionCallback m_connectionCallback;

  static bool s_wsaInitialized;
  static void InitializeWSA();
  static void CleanupWSA();
};