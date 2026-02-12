// BeamServerClient.h
// TCP client for the Python beam detection server (beam_server.py).
// Sends camera images for beam detection and retrieves Gaussian fit parameters.
#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <optional>
#include "IO/SocketGuard.h"
#include "nlohmann/json.hpp"

class BeamServerClient {
public:
  BeamServerClient();
  ~BeamServerClient();

  // Non-copyable, non-movable
  BeamServerClient(const BeamServerClient&) = delete;
  BeamServerClient& operator=(const BeamServerClient&) = delete;

  [[nodiscard]] bool Connect(const std::string& host, int port);
  void Disconnect();
  [[nodiscard]] bool IsConnected() const { return m_connected.load(); }

  // Send a detect request with raw uint16 image data.
  // Returns parsed JSON result from server, or empty object on failure.
  [[nodiscard]] nlohmann::json SendDetect(
    const std::vector<uint16_t>& imageData,
    int width, int height,
    double z_um,
    bool rotate180,
    double umPerPxX, double umPerPxY);

  // Send a detect-from-file request (server reads image from disk path).
  // rotate180: if nullopt, server uses its config default.
  // Returns parsed JSON result from server, or empty object on failure.
  [[nodiscard]] nlohmann::json SendDetectFile(
    const std::string& filePath,
    double z_um,
    std::optional<bool> rotate180,
    double umPerPxX, double umPerPxY);

  // Send a fit request to compute beam waist, M2, poynting angle.
  // name: optional label for the saved data folder on the server.
  [[nodiscard]] nlohmann::json SendFit(
    double umPerPxX, double umPerPxY, double wavelengthM,
    const std::string& name = "");

  // Send a reset request to clear accumulated data on the server.
  [[nodiscard]] bool SendReset();

  // Get last error message
  std::string GetLastError() const;

private:
  static void InitializeWSA();

  // Send exactly n bytes. Returns true on success.
  [[nodiscard]] bool SendAll(const char* data, size_t length);

  // Receive exactly n bytes. Returns true on success.
  [[nodiscard]] bool RecvAll(char* buffer, size_t length);

  // Send a binary-framed command and receive the JSON response.
  // Protocol: [4B cmd_id (LE)][4B payload_len (LE)][payload bytes]
  // Response: [4B json_len (LE)][JSON bytes]
  [[nodiscard]] nlohmann::json SendCommand(uint32_t cmdId,
    const std::vector<uint8_t>& payload = {});

  SocketGuard m_socket;
  std::mutex m_socketMutex;
  std::atomic<bool> m_connected{false};
  std::string m_lastError;
};
