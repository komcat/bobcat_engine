// TcpDataClient.cpp
#include "TcpDataClient.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>

bool TcpDataClient::s_wsaInitialized = false;

void TcpDataClient::InitializeWSA()
{
#ifdef _WIN32
  if (!s_wsaInitialized) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0) {
      s_wsaInitialized = true;
    }
  }
#endif
}

void TcpDataClient::CleanupWSA()
{
#ifdef _WIN32
  if (s_wsaInitialized) {
    WSACleanup();
    s_wsaInitialized = false;
  }
#endif
}

TcpDataClient::TcpDataClient()
  : m_socket(INVALID_SOCKET)
  , m_port(0)
  , m_running(false)
  , m_connected(false)
  , m_autoReconnect(false)
  , m_reconnectInterval(5000)
  , m_maxReconnectAttempts(3)      // ADD THIS
  , m_reconnectCount(0)             // ADD THIS
  , m_latestValue(0.0f)
{
  InitializeWSA();
}

TcpDataClient::~TcpDataClient()
{
  Disconnect();
}

bool TcpDataClient::Connect(const std::string& host, int port)
{
  if (m_connected.load()) {
    Disconnect();
  }

  m_host = host;
  m_port = port;
  m_reconnectCount.store(0);

  // ✅ CRITICAL: Ensure old thread is joined before creating new one
  if (m_receiveThread.joinable()) {
    m_running.store(false);
    m_receiveThread.join();
  }

  if (AttemptConnection()) {
    // Start receive thread
    m_running.store(true);
    m_receiveThread = std::thread(&TcpDataClient::ReceiveThread, this);

    std::cout << "[TcpDataClient] Connected to " << host << ":" << port << std::endl;
    return true;
  }

  return false;
}

void TcpDataClient::Disconnect()
{
  m_autoReconnect.store(false);
  m_running.store(false);
  m_connected.store(false);

  if (m_reconnectThread.joinable()) {
    m_reconnectThread.join();
  }

  if (m_receiveThread.joinable()) {
    m_receiveThread.join();
  }

  if (m_socket != INVALID_SOCKET) {
    closesocket(m_socket);
    m_socket = INVALID_SOCKET;
  }

  std::cout << "[TcpDataClient] Disconnected from " << m_host << ":" << m_port << std::endl;
  NotifyConnection(false);
}

void TcpDataClient::EnableAutoReconnect(bool enable, int intervalMs)
{
  bool wasEnabled = m_autoReconnect.exchange(enable);
  m_reconnectInterval = intervalMs;

  if (enable && !wasEnabled && !m_connected.load()) {
    // Start reconnect thread
    m_reconnectThread = std::thread(&TcpDataClient::ReconnectThread, this);
  }
}

float TcpDataClient::GetLatestValue() const
{
  std::lock_guard<std::mutex> lock(m_dataMutex);
  return m_latestValue;
}

std::deque<float> TcpDataClient::GetPendingValues()
{
  std::lock_guard<std::mutex> lock(m_dataMutex);
  std::deque<float> values = std::move(m_pendingValues);
  m_pendingValues.clear();
  return values;
}

size_t TcpDataClient::GetPendingCount() const
{
  std::lock_guard<std::mutex> lock(m_dataMutex);
  return m_pendingValues.size();
}

void TcpDataClient::SetDataCallback(DataCallback callback)
{
  std::lock_guard<std::mutex> lock(m_callbackMutex);
  m_dataCallback = callback;
}

void TcpDataClient::SetConnectionCallback(ConnectionCallback callback)
{
  std::lock_guard<std::mutex> lock(m_callbackMutex);
  m_connectionCallback = callback;
}

bool TcpDataClient::AttemptConnection()
{
  m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (m_socket == INVALID_SOCKET) {
    return false;
  }

  sockaddr_in serverAddr;
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(m_port);

  if (inet_pton(AF_INET, m_host.c_str(), &serverAddr.sin_addr) <= 0) {
    closesocket(m_socket);
    m_socket = INVALID_SOCKET;
    return false;
  }

  if (connect(m_socket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
    closesocket(m_socket);
    m_socket = INVALID_SOCKET;
    return false;
  }

  // Set non-blocking
#ifdef _WIN32
  unsigned long mode = 1;
  ioctlsocket(m_socket, FIONBIO, &mode);
#else
  int flags = fcntl(m_socket, F_GETFL, 0);
  fcntl(m_socket, F_SETFL, flags | O_NONBLOCK);
#endif

  m_connected.store(true);
  NotifyConnection(true);
  return true;
}

void TcpDataClient::ReceiveThread()
{
  const int bufferSize = 1024;
  char buffer[bufferSize];
  std::string dataBuffer;

  while (m_running.load()) {
    if (!m_connected.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }

    int bytesReceived = recv(m_socket, buffer, bufferSize - 1, 0);

    if (bytesReceived > 0) {
      buffer[bytesReceived] = '\0';
      dataBuffer.append(buffer, bytesReceived);
      ProcessBuffer(dataBuffer);
    }
    else if (bytesReceived == 0) {
      // Connection closed
      m_connected.store(false);
      std::cout << "[TcpDataClient] Connection closed by server" << std::endl;
      NotifyConnection(false);

      if (m_autoReconnect.load()) {
        // Reconnect thread will handle it
      }
      break;
    }
    else if (bytesReceived == SOCKET_ERROR) {
#ifdef _WIN32
      int errorCode = WSAGetLastError();
      if (errorCode != WSAEWOULDBLOCK) {
        m_connected.store(false);
        NotifyConnection(false);
        break;
      }
#else
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        m_connected.store(false);
        NotifyConnection(false);
        break;
      }
#endif
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}


void TcpDataClient::ReconnectThread()
{
  while (m_autoReconnect.load()) {
    if (!m_connected.load()) {
      // Check if max attempts reached
      if (m_reconnectCount.load() >= m_maxReconnectAttempts) {
        std::cout << "[TcpDataClient] Max reconnection attempts ("
          << m_maxReconnectAttempts << ") reached. Giving up." << std::endl;
        m_autoReconnect.store(false);
        break;
      }

      std::cout << "[TcpDataClient] Attempting reconnection... (Attempt "
        << (m_reconnectCount.load() + 1) << "/" << m_maxReconnectAttempts << ")" << std::endl;

      m_reconnectCount.fetch_add(1);

      if (AttemptConnection()) {
        std::cout << "[TcpDataClient] Reconnected successfully" << std::endl;
        m_reconnectCount.store(0);

        // ✅ CRITICAL: Ensure old thread is fully stopped before creating new one
        m_running.store(false);
        if (m_receiveThread.joinable()) {
          m_receiveThread.join();
        }

        m_running.store(true);
        m_receiveThread = std::thread(&TcpDataClient::ReceiveThread, this);
      }
      else {
        std::cout << "[TcpDataClient] Reconnection failed" << std::endl;
      }
    }

    // Sleep in small chunks, check flag frequently to avoid UI freeze
    for (int i = 0; i < m_reconnectInterval / 100 && m_autoReconnect.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
}



void TcpDataClient::ProcessBuffer(std::string& buffer)
{
  size_t pos;
  while ((pos = buffer.find('\n')) != std::string::npos) {
    std::string line = buffer.substr(0, pos);
    buffer.erase(0, pos + 1);

    try {
      float value = std::stof(line);

      {
        std::lock_guard<std::mutex> lock(m_dataMutex);
        m_pendingValues.push_back(value);
        m_latestValue = value;

        // Limit queue size
        if (m_pendingValues.size() > 10000) {
          m_pendingValues.pop_front();
        }
      }

      // Notify callback
      DataCallback callback;
      {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        callback = m_dataCallback;
      }

      if (callback) {
        try {
          callback(value);
        }
        catch (const std::exception& e) {
          std::cerr << "[TcpDataClient] Callback exception: " << e.what() << std::endl;
        }
      }
    }
    catch (const std::exception& e) {
      // Ignore parse errors
    }
  }
}

void TcpDataClient::NotifyConnection(bool connected)
{
  ConnectionCallback callback;
  {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    callback = m_connectionCallback;
  }

  if (callback) {
    try {
      callback(connected);
    }
    catch (const std::exception& e) {
      std::cerr << "[TcpDataClient] Connection callback exception: " << e.what() << std::endl;
    }
  }
}