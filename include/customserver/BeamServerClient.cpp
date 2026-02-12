// BeamServerClient.cpp
// TCP client implementation for the Python beam detection server.
#include "BeamServerClient.h"
#include <iostream>
#include <mutex>

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
#define SOCKET int
#define INVALID_SOCKET -1
#define closesocket close
#endif

using json = nlohmann::json;

static std::once_flag s_wsaInitFlag;

BeamServerClient::BeamServerClient() = default;

BeamServerClient::~BeamServerClient()
{
  Disconnect();
}

void BeamServerClient::InitializeWSA()
{
#ifdef _WIN32
  std::call_once(s_wsaInitFlag, []() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
  });
#endif
}

bool BeamServerClient::Connect(const std::string& host, int port)
{
  std::lock_guard<std::mutex> lock(m_socketMutex);

  // Disconnect existing connection first
  if (m_connected.load()) {
    m_socket.reset();
    m_connected.store(false);
  }

  InitializeWSA();

  // Create socket
  SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock == INVALID_SOCKET) {
    m_lastError = "Failed to create socket";
    return false;
  }
  m_socket.reset(sock);

  // Set receive timeout (30 seconds)
  int timeout = 30000;
#ifdef _WIN32
  setsockopt(m_socket.get(), SOL_SOCKET, SO_RCVTIMEO,
    reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
  struct timeval tv;
  tv.tv_sec = 30;
  tv.tv_usec = 0;
  setsockopt(m_socket.get(), SOL_SOCKET, SO_RCVTIMEO,
    reinterpret_cast<const char*>(&tv), sizeof(tv));
#endif

  // Resolve address
  struct sockaddr_in serverAddr{};
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(static_cast<uint16_t>(port));

  if (inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr) != 1) {
    m_lastError = "Invalid host address: " + host;
    m_socket.reset();
    return false;
  }

  // Connect
  if (::connect(m_socket.get(),
        reinterpret_cast<struct sockaddr*>(&serverAddr),
        sizeof(serverAddr)) != 0) {
    m_lastError = "Connection failed to " + host + ":" + std::to_string(port);
    m_socket.reset();
    return false;
  }

  m_connected.store(true);
  std::cout << "[BeamServerClient] Connected to " << host << ":" << port << std::endl;
  return true;
}

void BeamServerClient::Disconnect()
{
  std::lock_guard<std::mutex> lock(m_socketMutex);
  if (m_connected.load()) {
    m_socket.reset();
    m_connected.store(false);
    std::cout << "[BeamServerClient] Disconnected" << std::endl;
  }
}

bool BeamServerClient::SendAll(const char* data, size_t length)
{
  static constexpr int MAX_SEND_ITERATIONS = 100000;
  size_t totalSent = 0;

  for (int i = 0; i < MAX_SEND_ITERATIONS && totalSent < length; ++i) {
    int toSend = static_cast<int>(std::min(length - totalSent, static_cast<size_t>(65536)));
    int sent = ::send(m_socket.get(), data + totalSent, toSend, 0);
    if (sent <= 0) {
      m_lastError = "Send failed at byte " + std::to_string(totalSent) +
                    " of " + std::to_string(length);
      return false;
    }
    totalSent += static_cast<size_t>(sent);
  }

  return totalSent == length;
}

bool BeamServerClient::RecvAll(char* buffer, size_t length)
{
  static constexpr int MAX_RECV_ITERATIONS = 100000;
  size_t totalRecv = 0;

  for (int i = 0; i < MAX_RECV_ITERATIONS && totalRecv < length; ++i) {
    int toRecv = static_cast<int>(std::min(length - totalRecv, static_cast<size_t>(65536)));
    int received = ::recv(m_socket.get(), buffer + totalRecv, toRecv, 0);
    if (received <= 0) {
      m_lastError = "Recv failed at byte " + std::to_string(totalRecv) +
                    " of " + std::to_string(length);
      return false;
    }
    totalRecv += static_cast<size_t>(received);
  }

  return totalRecv == length;
}

json BeamServerClient::SendCommand(uint32_t cmdId, const std::vector<uint8_t>& payload)
{
  // Wire protocol (matches beam_server.py):
  //   Send:     [4B cmd_id (uint32 LE)][4B payload_len (uint32 LE)][payload bytes]
  //   Response: [4B json_len (uint32 LE)][JSON bytes]

  uint32_t payloadLen = static_cast<uint32_t>(payload.size());

  // Send command ID (little-endian)
  if (!SendAll(reinterpret_cast<const char*>(&cmdId), 4)) {
    m_lastError = "Failed to send command ID";
    m_connected.store(false);
    return {};
  }

  // Send payload length (little-endian)
  if (!SendAll(reinterpret_cast<const char*>(&payloadLen), 4)) {
    m_lastError = "Failed to send payload length";
    m_connected.store(false);
    return {};
  }

  // Send payload if present
  if (!payload.empty()) {
    if (!SendAll(reinterpret_cast<const char*>(payload.data()), payload.size())) {
      m_lastError = "Failed to send payload";
      m_connected.store(false);
      return {};
    }
  }

  // Receive response: [4B json_len][JSON bytes]
  uint32_t responseLen = 0;
  if (!RecvAll(reinterpret_cast<char*>(&responseLen), 4)) {
    m_lastError = "Failed to receive response length";
    m_connected.store(false);
    return {};
  }

  // Sanity check (max 10MB response)
  if (responseLen > 10 * 1024 * 1024) {
    m_lastError = "Response too large: " + std::to_string(responseLen) + " bytes";
    m_connected.store(false);
    return {};
  }

  // Receive response JSON
  std::string responseStr(responseLen, '\0');
  if (!RecvAll(responseStr.data(), responseLen)) {
    m_lastError = "Failed to receive response body";
    m_connected.store(false);
    return {};
  }

  try {
    return json::parse(responseStr);
  }
  catch (const json::parse_error& e) {
    m_lastError = std::string("JSON parse error: ") + e.what();
    return {};
  }
}

json BeamServerClient::SendDetect(
  const std::vector<uint16_t>& imageData,
  int width, int height,
  double z_um,
  bool rotate180,
  double umPerPxX, double umPerPxY)
{
  static constexpr uint32_t CMD_DETECT = 0x01;

  std::lock_guard<std::mutex> lock(m_socketMutex);

  if (!m_connected.load()) {
    m_lastError = "Not connected";
    return {};
  }

  // Build JSON header (matches beam_server.py detect handler)
  json header = {
    {"width", width},
    {"height", height},
    {"z_um", z_um},
    {"rotate_180", rotate180},
    {"um_per_px", {umPerPxX, umPerPxY}}
  };

  std::string headerStr = header.dump();
  uint32_t headerLen = static_cast<uint32_t>(headerStr.size());

  // DETECT payload: [4B json_len (LE)][JSON bytes][raw uint16 pixels]
  size_t pixelBytes = imageData.size() * sizeof(uint16_t);
  std::vector<uint8_t> payload(4 + headerStr.size() + pixelBytes);

  // Copy json_len
  std::memcpy(payload.data(), &headerLen, 4);
  // Copy JSON bytes
  std::memcpy(payload.data() + 4, headerStr.data(), headerStr.size());
  // Copy raw pixel data (uint16 little-endian)
  std::memcpy(payload.data() + 4 + headerStr.size(), imageData.data(), pixelBytes);

  json response = SendCommand(CMD_DETECT, payload);
  if (response.empty()) {
    std::cout << "[BeamServerClient] Detect failed: " << m_lastError << std::endl;
  }
  return response;
}

json BeamServerClient::SendDetectFile(
  const std::string& filePath,
  double z_um,
  std::optional<bool> rotate180,
  double umPerPxX, double umPerPxY)
{
  static constexpr uint32_t CMD_DETECT_FILE = 0x04;

  std::lock_guard<std::mutex> lock(m_socketMutex);

  if (!m_connected.load()) {
    m_lastError = "Not connected";
    return {};
  }

  // DETECT_FILE payload: JSON bytes only (server reads image from disk)
  // Only include rotate_180 if explicitly set; otherwise server uses its config default
  json params = {
    {"file_path", filePath},
    {"z_um", z_um},
    {"um_per_px", {umPerPxX, umPerPxY}}
  };
  if (rotate180.has_value()) {
    params["rotate_180"] = rotate180.value();
  }

  std::string payloadStr = params.dump();
  std::vector<uint8_t> payload(payloadStr.begin(), payloadStr.end());

  json response = SendCommand(CMD_DETECT_FILE, payload);
  if (response.empty()) {
    std::cout << "[BeamServerClient] DetectFile failed: " << m_lastError << std::endl;
  }
  return response;
}

json BeamServerClient::SendFit(double umPerPxX, double umPerPxY, double wavelengthM,
                               const std::string& name)
{
  static constexpr uint32_t CMD_FIT = 0x02;

  std::lock_guard<std::mutex> lock(m_socketMutex);

  if (!m_connected.load()) {
    m_lastError = "Not connected";
    return {};
  }

  // FIT payload: JSON bytes only (matches beam_server.py fit handler)
  json fitParams = {
    {"um_per_px", {umPerPxX, umPerPxY}},
    {"wavelength_m", wavelengthM}
  };
  if (!name.empty()) {
    fitParams["name"] = name;
  }

  std::string payloadStr = fitParams.dump();
  std::vector<uint8_t> payload(payloadStr.begin(), payloadStr.end());

  json response = SendCommand(CMD_FIT, payload);
  if (response.empty()) {
    std::cout << "[BeamServerClient] Fit failed: " << m_lastError << std::endl;
  }
  return response;
}

bool BeamServerClient::SendReset()
{
  static constexpr uint32_t CMD_RESET = 0x03;

  std::lock_guard<std::mutex> lock(m_socketMutex);

  if (!m_connected.load()) {
    m_lastError = "Not connected";
    return false;
  }

  // RESET payload: empty (0 bytes)
  json response = SendCommand(CMD_RESET);
  if (response.empty()) {
    std::cout << "[BeamServerClient] Reset failed: " << m_lastError << std::endl;
    return false;
  }

  // Check for success in response
  if (response.contains("status") && response["status"] == "ok") {
    return true;
  }

  m_lastError = response.value("error", "Unknown error from server");
  return false;
}

std::string BeamServerClient::GetLastError() const
{
  return m_lastError;
}
