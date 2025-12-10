// KeysightPSUAdapter.cpp
#include "KeysightPSUAdapter.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/select.h>
#endif

using json = nlohmann::json;

bool KeysightPSUAdapter::s_wsaInitialized = false;

void KeysightPSUAdapter::InitializeWSA()
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

void KeysightPSUAdapter::CleanupWSA()
{
#ifdef _WIN32
  if (s_wsaInitialized) {
    WSACleanup();
    s_wsaInitialized = false;
  }
#endif
}

KeysightPSUAdapter::KeysightPSUAdapter(const std::string& deviceName,
  const std::string& serverHost,
  int serverPort,
  const std::string& psuIP,
  int psuPort)
  : m_deviceName(deviceName)
  , m_serverHost(serverHost)
  , m_serverPort(serverPort)
  , m_psuIP(psuIP)
  , m_psuPort(psuPort)
  , m_socket(INVALID_SOCKET)
  , m_isConnected(false)
{
  InitializeWSA();
}

KeysightPSUAdapter::~KeysightPSUAdapter()
{
  disconnect();
}

bool KeysightPSUAdapter::connect()
{
  // Don't lock here - let individual operations handle their own locking
  if (m_isConnected) {
    return true;
  }

  std::cout << "[KeysightPSUAdapter] Connecting to server "
    << m_serverHost << ":" << m_serverPort << std::endl;

  if (!connectSocket()) {
    return false;
  }

  std::cout << "[KeysightPSUAdapter] Connected to server, registering device..." << std::endl;

  // Register device with server
  if (!registerDevice()) {
    closeSocket();
    return false;
  }

  m_isConnected = true;
  emit connectionStatusChanged(true);

  std::cout << "[KeysightPSUAdapter] Device '" << m_deviceName
    << "' connected successfully" << std::endl;

  return true;
}

void KeysightPSUAdapter::disconnect()
{
  // Don't lock here - let individual operations handle their own locking
  if (m_socket != INVALID_SOCKET) {
    closeSocket();
  }

  m_isConnected = false;
  emit connectionStatusChanged(false);

  std::cout << "[KeysightPSUAdapter] Device '" << m_deviceName
    << "' disconnected" << std::endl;
}

bool KeysightPSUAdapter::isConnected() const
{
  return m_isConnected && m_socket != INVALID_SOCKET;
}

bool KeysightPSUAdapter::connectSocket()
{
  m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (m_socket == INVALID_SOCKET) {
    setError("Failed to create socket");
    return false;
  }

  sockaddr_in serverAddr;
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(m_serverPort);

  if (inet_pton(AF_INET, m_serverHost.c_str(), &serverAddr.sin_addr) <= 0) {
    setError("Invalid server address: " + m_serverHost);
    closesocket(m_socket);
    m_socket = INVALID_SOCKET;
    return false;
  }

  if (::connect(m_socket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
    setError("Failed to connect to server: " + m_serverHost + ":" + std::to_string(m_serverPort));
    closesocket(m_socket);
    m_socket = INVALID_SOCKET;
    return false;
  }

  return true;
}

void KeysightPSUAdapter::closeSocket()
{
  if (m_socket != INVALID_SOCKET) {
    closesocket(m_socket);
    m_socket = INVALID_SOCKET;
  }
}

bool KeysightPSUAdapter::registerDevice()
{
  json command;
  command["command"] = "set_device_name";
  command["device"] = m_deviceName;
  command["ip"] = m_psuIP;
  command["port"] = m_psuPort;

  std::string response;
  if (!sendCommand(command.dump(), response)) {
    return false;
  }

  try {
    json responseJson = json::parse(response);

    if (responseJson["status"] != "success") {
      setError("Device registration failed: " + responseJson["message"].get<std::string>());
      return false;
    }

    // Extract device ID from message
    m_deviceID = responseJson["message"].get<std::string>();

    return true;
  }
  catch (const json::exception& e) {
    setError("Failed to parse registration response: " + std::string(e.what()));
    return false;
  }
}

bool KeysightPSUAdapter::sendCommand(const std::string& jsonCommand, std::string& jsonResponse)
{
  std::lock_guard<std::mutex> lock(m_socketMutex);

  if (m_socket == INVALID_SOCKET) {
    setError("Socket not connected");
    return false;
  }

  // Send command
  int bytesSent = send(m_socket, jsonCommand.c_str(), static_cast<int>(jsonCommand.length()), 0);
  if (bytesSent == SOCKET_ERROR) {
    setError("Failed to send command");
    return false;
  }

  // Wait for response with timeout
  fd_set readSet;
  FD_ZERO(&readSet);
  FD_SET(m_socket, &readSet);

  struct timeval timeout;
  timeout.tv_sec = COMMAND_TIMEOUT_MS / 1000;
  timeout.tv_usec = (COMMAND_TIMEOUT_MS % 1000) * 1000;

  int selectResult = select(static_cast<int>(m_socket) + 1, &readSet, nullptr, nullptr, &timeout);
  if (selectResult == 0) {
    setError("Timeout waiting for response");
    return false;
  }
  else if (selectResult == SOCKET_ERROR) {
    setError("Select error");
    return false;
  }

  // Read response
  char buffer[4096];
  int bytesReceived = recv(m_socket, buffer, sizeof(buffer) - 1, 0);

  if (bytesReceived <= 0) {
    setError("Failed to receive response");
    return false;
  }

  buffer[bytesReceived] = '\0';
  jsonResponse = std::string(buffer, bytesReceived);

  // Verify response is valid JSON
  try {
    json response = json::parse(jsonResponse);

    if (response["status"] == "error") {
      setError(response["message"].get<std::string>());
      return false;
    }

    return true;
  }
  catch (const json::exception& e) {
    setError("Invalid JSON response: " + std::string(e.what()));
    return false;
  }
}

bool KeysightPSUAdapter::setOutputOn(int channel)
{
  // Server-based adapter is single-channel, ignore channel parameter
  json command;
  command["command"] = "output_on";
  command["device"] = m_deviceName;

  std::string response;
  return sendCommand(command.dump(), response);
}

bool KeysightPSUAdapter::setOutputOff(int channel)
{
  // Server-based adapter is single-channel, ignore channel parameter
  json command;
  command["command"] = "output_off";
  command["device"] = m_deviceName;

  std::string response;
  return sendCommand(command.dump(), response);
}

bool KeysightPSUAdapter::setVoltage(double voltage, int channel)
{
  // Server-based adapter is single-channel, ignore channel parameter
  json command;
  command["command"] = "set_voltage";
  command["device"] = m_deviceName;
  command["value"] = voltage;

  std::string response;
  return sendCommand(command.dump(), response);
}

bool KeysightPSUAdapter::setCurrent(double current, int channel)
{
  // Server-based adapter is single-channel, ignore channel parameter
  json command;
  command["command"] = "set_current";
  command["device"] = m_deviceName;
  command["value"] = current;

  std::string response;
  return sendCommand(command.dump(), response);
}

bool KeysightPSUAdapter::setOVP(double voltage, int channel)
{
  // Server-based adapter is single-channel, ignore channel parameter
  json command;
  command["command"] = "set_ovp";
  command["device"] = m_deviceName;
  command["value"] = voltage;

  std::string response;
  return sendCommand(command.dump(), response);
}

bool KeysightPSUAdapter::setOCP(double current, int channel)
{
  // Server-based adapter is single-channel, ignore channel parameter
  json command;
  command["command"] = "set_ocp";
  command["device"] = m_deviceName;
  command["value"] = current;

  std::string response;
  return sendCommand(command.dump(), response);
}

double KeysightPSUAdapter::readVoltage(int channel)
{
  // Server-based adapter is single-channel, ignore channel parameter
  json command;
  command["command"] = "read_voltage";
  command["device"] = m_deviceName;

  std::string response;
  if (!sendCommand(command.dump(), response)) {
    return -1.0;
  }

  try {
    json responseJson = json::parse(response);
    return responseJson["data"]["voltage"].get<double>();
  }
  catch (const json::exception& e) {
    setError("Failed to parse voltage response: " + std::string(e.what()));
    return -1.0;
  }
}

double KeysightPSUAdapter::readCurrent(int channel)
{
  // Server-based adapter is single-channel, ignore channel parameter
  json command;
  command["command"] = "read_current";
  command["device"] = m_deviceName;

  std::string response;
  if (!sendCommand(command.dump(), response)) {
    return -1.0;
  }

  try {
    json responseJson = json::parse(response);
    return responseJson["data"]["current"].get<double>();
  }
  catch (const json::exception& e) {
    setError("Failed to parse current response: " + std::string(e.what()));
    return -1.0;
  }
}

bool KeysightPSUAdapter::readOutputState(int channel)
{
  // Server-based adapter is single-channel, ignore channel parameter
  json command;
  command["command"] = "read_output_state";
  command["device"] = m_deviceName;

  std::string response;
  if (!sendCommand(command.dump(), response)) {
    return false;
  }

  try {
    json responseJson = json::parse(response);
    return responseJson["data"]["output_state"].get<bool>();
  }
  catch (const json::exception& e) {
    setError("Failed to parse output state response: " + std::string(e.what()));
    return false;
  }
}

void KeysightPSUAdapter::setError(const std::string& error)
{
  m_lastError = error;
  emit errorOccurred(QString::fromStdString(error));
  std::cerr << "[KeysightPSUAdapter] Error: " << error << std::endl;
}