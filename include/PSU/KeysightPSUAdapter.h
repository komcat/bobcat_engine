// KeysightPSUAdapter.h - TCP client adapter for Keysight PSU (Native Sockets)
#pragma once

#include "IPowerSupply.h"
#include <QObject>
#include <string>
#include <memory>
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
#define SOCKET_ERROR -1
#define closesocket close
#endif

/**
 * @brief Adapter for Keysight E36103B power supply
 * Communicates with Python server via TCP/JSON protocol using native sockets
 */
class KeysightPSUAdapter : public QObject, public IPowerSupply
{
  Q_OBJECT

public:
  /**
   * @brief Constructor
   * @param deviceName Friendly name for this PSU
   * @param serverHost Python server IP address
   * @param serverPort Python server port
   * @param psuIP Actual PSU device IP (sent to server for registration)
   * @param psuPort Actual PSU device port (default 5025)
   */
  KeysightPSUAdapter(const std::string& deviceName,
    const std::string& serverHost,
    int serverPort,
    const std::string& psuIP,
    int psuPort = 5025);

  ~KeysightPSUAdapter() override;

  // IPowerSupply interface implementation
  bool connect() override;
  void disconnect() override;
  bool isConnected() const override;

  std::string getDeviceName() const override { return m_deviceName; }
  std::string getDeviceID() const override { return m_deviceID; }

  bool setOutputOn(int channel = 1) override;
  bool setOutputOff(int channel = 1) override;

  bool setVoltage(double voltage, int channel = 1) override;
  bool setCurrent(double current, int channel = 1) override;

  bool setOVP(double voltage, int channel = 1) override;
  bool setOCP(double current, int channel = 1) override;

  double readVoltage(int channel = 1) override;
  double readCurrent(int channel = 1) override;
  bool readOutputState(int channel = 1) override;

  std::string getLastError() const override { return m_lastError; }

signals:
  void connectionStatusChanged(bool connected);
  void errorOccurred(const QString& error);

private:
  bool sendCommand(const std::string& jsonCommand, std::string& jsonResponse);
  bool registerDevice();
  void setError(const std::string& error);
  bool connectSocket();
  void closeSocket();

  static void InitializeWSA();
  static void CleanupWSA();

  std::string m_deviceName;
  std::string m_deviceID;
  std::string m_serverHost;
  int m_serverPort;
  std::string m_psuIP;
  int m_psuPort;

  SOCKET m_socket;
  bool m_isConnected;
  std::string m_lastError;
  mutable std::mutex m_socketMutex;

  static bool s_wsaInitialized;
  static constexpr int COMMAND_TIMEOUT_MS = 5000;
};