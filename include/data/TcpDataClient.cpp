// TcpDataClient.cpp
#include "TcpDataClient.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>
#include <QDateTime>

std::once_flag TcpDataClient::s_wsaInitFlag;

void TcpDataClient::InitializeWSA()
{
#ifdef _WIN32
  std::call_once(s_wsaInitFlag, []() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
  });
#endif
}

void TcpDataClient::CleanupWSA()
{
#ifdef _WIN32
  WSACleanup();
#endif
}

TcpDataClient::TcpDataClient()
  : m_socket()
  , m_port(0)
  , m_running(false)
  , m_connected(false)
  , m_autoReconnect(false)
  , m_reconnectInterval(5000)
  , m_maxReconnectAttempts(3)
  , m_reconnectCount(0)
  , m_protocolType(TcpProtocolType::Single)
  , m_channelCount(1)
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

  if (m_socket.valid()) {
    m_socket.reset();
  }

  std::cout << "[TcpDataClient] Disconnected from " << m_host << ":" << m_port << std::endl;
  NotifyConnection(false);
}

void TcpDataClient::EnableAutoReconnect(bool enable, int intervalMs)
{
  bool wasEnabled = m_autoReconnect.exchange(enable);
  m_reconnectInterval = intervalMs;

  if (enable && !wasEnabled && !m_connected.load()) {
    // ✅ CRITICAL FIX: Join old thread before creating new one to prevent crash
    // std::thread cannot be assigned to if it's already joinable (has an active thread)
    if (m_reconnectThread.joinable()) {
      m_reconnectThread.join();
    }

    // Start reconnect thread
    m_reconnectThread = std::thread(&TcpDataClient::ReconnectThread, this);
  }
  else if (!enable && wasEnabled) {
    // ✅ ADDED: When disabling auto-reconnect, wait for thread to finish
    // The thread will check m_autoReconnect flag and exit
    if (m_reconnectThread.joinable()) {
      m_reconnectThread.join();
    }
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

void TcpDataClient::SetMultiChannelDataCallback(MultiChannelDataCallback callback)
{
  std::lock_guard<std::mutex> lock(m_callbackMutex);
  m_multiChannelDataCallback = callback;
}

std::vector<float> TcpDataClient::GetLatestValues() const
{
  std::lock_guard<std::mutex> lock(m_dataMutex);
  return m_latestValues;
}

bool TcpDataClient::AttemptConnection()
{
  m_socket.reset(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
  if (!m_socket.valid()) {
    return false;
  }

  sockaddr_in serverAddr;
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(m_port);

  if (inet_pton(AF_INET, m_host.c_str(), &serverAddr.sin_addr) <= 0) {
    m_socket.reset();
    return false;
  }

  if (connect(m_socket.get(), (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
    m_socket.reset();
    return false;
  }

  // Set non-blocking
#ifdef _WIN32
  unsigned long mode = 1;
  ioctlsocket(m_socket.get(), FIONBIO, &mode);
#else
  int flags = fcntl(m_socket.get(), F_GETFL, 0);
  fcntl(m_socket.get(), F_SETFL, flags | O_NONBLOCK);
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

    int bytesReceived = recv(m_socket.get(), buffer, bufferSize - 1, 0);

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
  if (m_protocolType == TcpProtocolType::Single) {
    ProcessSingleChannelBuffer(buffer);
  }
  else {
    ProcessMultiChannelBuffer(buffer);
  }
}

void TcpDataClient::ProcessSingleChannelBuffer(std::string& buffer)
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
    catch (const std::exception& ) {
      // Ignore parse errors
    }
  }
}

void TcpDataClient::ProcessMultiChannelBuffer(std::string& buffer)
{
  // Process complete messages in format: $START,val1,val2,...,valN,$END
  constexpr int MAX_MESSAGES = 1000; // Safety bound: max messages per buffer parse
  for (int messageCount = 0; messageCount < MAX_MESSAGES; ++messageCount) {
    size_t startPos = buffer.find("$START");
    if (startPos == std::string::npos) {
      // No start marker found, keep buffer for next recv
      break;
    }

    size_t endPos = buffer.find("$END", startPos);
    if (endPos == std::string::npos) {
      // Incomplete message, wait for more data
      break;
    }

    // Extract complete message
    size_t messageEnd = endPos + 4; // Length of "$END"
    std::string message = buffer.substr(startPos, messageEnd - startPos);
    buffer.erase(0, messageEnd);

    // Parse the message: $START,val1,val2,...,valN,$END
    try {
      // Extract data between $START, and ,$END
      size_t dataStart = startPos + 7; // Length of "$START,"
      size_t dataEnd = endPos;

      if (dataEnd <= dataStart) {
        std::cerr << "[TcpDataClient] Invalid message format (empty data)" << std::endl;
        continue;
      }

      std::string dataStr = message.substr(7, message.length() - 7 - 5); // Remove "$START," and ",$END"

      // Split by comma
      std::vector<float> values;
      std::stringstream ss(dataStr);
      std::string token;

      while (std::getline(ss, token, ',')) {
        try {
          float value = std::stof(token);
          values.push_back(value);
        }
        catch (const std::exception&) {
          std::cerr << "[TcpDataClient] Failed to parse value: " << token << std::endl;
        }
      }

      // Validate channel count
      if (m_channelCount > 0 && values.size() != m_channelCount) {
        std::cerr << "[TcpDataClient] Channel count mismatch. Expected "
                  << m_channelCount << ", got " << values.size() << std::endl;
        // Continue anyway, process what we have
      }

      // Store latest values
      {
        std::lock_guard<std::mutex> lock(m_dataMutex);
        m_latestValues = values;
      }

      // Capture timestamp ONCE for entire message (all channels share same timestamp)
      qint64 messageTimestamp = QDateTime::currentMSecsSinceEpoch();

      // Notify callback for each channel with shared timestamp
      MultiChannelDataCallback callback;
      {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        callback = m_multiChannelDataCallback;
      }

      if (callback) {
        for (size_t i = 0; i < values.size(); ++i) {
          try {
            callback(i, values[i], messageTimestamp);
          }
          catch (const std::exception& e) {
            std::cerr << "[TcpDataClient] Multi-channel callback exception: "
                      << e.what() << std::endl;
          }
        }
      }
    }
    catch (const std::exception& e) {
      std::cerr << "[TcpDataClient] Error processing multi-channel message: "
                << e.what() << std::endl;
    }
  }

  // If we exhausted the message count bound, log a warning (should not happen normally)
  if (!buffer.empty() && buffer.find("$START") != std::string::npos) {
    std::cerr << "[TcpDataClient] Loop safety bound reached in ProcessMultiChannelBuffer" << std::endl;
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