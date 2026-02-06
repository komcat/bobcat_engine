// pi_controller.cpp
#include "pi_controller.h"
#include "common/ThreadPriority.h"
#include <iostream>
#include <chrono>
#include <sstream>
#include <algorithm>

// Logging macros
#define LOG_DEBUG(msg) if (m_logger) m_logger->LogDebug(msg, "PIController")
#define LOG_INFO(msg) if (m_logger) m_logger->LogInfo(msg, "PIController")
#define LOG_WARNING(msg) if (m_logger) m_logger->LogWarning(msg, "PIController")
#define LOG_ERROR(msg) if (m_logger) m_logger->LogError(msg, "PIController")

PIController::PIController()
  : m_controllerId(-1),
  m_port(50000) {

  m_isConnected.store(false);
  m_threadRunning.store(false);
  m_terminateThread.store(false);
  m_enableAnalogReading.store(true);

  LOG_INFO("Initializing controller");

  m_availableAxes = { "X", "Y", "Z", "U", "V", "W" };
  m_lastStatusUpdate = std::chrono::steady_clock::now();
  m_lastPositionUpdate = std::chrono::steady_clock::now();

  StartCommunicationThread();
}

PIController::~PIController() {
  LOG_INFO("Shutting down controller");
  StopCommunicationThread();
  if (m_isConnected) {
    Disconnect();
  }
}

void PIController::StartCommunicationThread() {
  if (!m_threadRunning) {
    m_threadRunning.store(true);
    m_terminateThread.store(false);
    m_communicationThread = std::thread(&PIController::CommunicationThreadFunc, this);
    LOG_INFO("Communication thread started");
  }
}

void PIController::StopCommunicationThread() {
  if (m_threadRunning) {
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_terminateThread.store(true);
    }
    m_condVar.notify_all();

    if (m_communicationThread.joinable()) {
      m_communicationThread.join();
    }

    m_threadRunning.store(false);
    LOG_INFO("Communication thread stopped");
  }
}

void PIController::CommunicationThreadFunc() {
  ThreadUtil::SetCurrentThreadPriority(ThreadUtil::Priority::AboveNormal, "PIController");
  const auto updateInterval = std::chrono::milliseconds(50);
  int frameCounter = 0;

  while (!m_terminateThread) {
    if (m_isConnected) {
      frameCounter++;

      // Update positions
      std::map<std::string, double> positions;
      if (GetPositions(positions)) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_axisPositions = positions;
      }

      // Update motion status
      {
        const char* allAxes = "X Y Z U V W";
        BOOL isMovingArray[6] = { FALSE };

        if (PI_IsMoving(m_controllerId, allAxes, isMovingArray)) {
          std::lock_guard<std::mutex> lock(m_mutex);
          const std::vector<std::string> axisNames = { "X", "Y", "Z", "U", "V", "W" };

          for (int i = 0; i < 6; i++) {
            m_axisMoving[axisNames[i]] = (isMovingArray[i] == TRUE);
          }
        }
      }

      // Update servo status less frequently
      if (frameCounter % 3 == 0) {
        for (const auto& axis : m_availableAxes) {
          bool enabled;
          if (IsServoEnabled(axis, enabled)) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_axisServoEnabled[axis] = enabled;
          }
        }
      }

      // Update analog readings
      if (m_enableAnalogReading && frameCounter % 2 == 0) {
        UpdateAnalogReadings();
      }
    }

    // Wait for next update
    std::unique_lock<std::mutex> lock(m_mutex);
    m_condVar.wait_for(lock, updateInterval, [this]() { return m_terminateThread.load(); });
  }
}

void PIController::UpdateAnalogReadings() {
  if (!m_isConnected || !m_enableAnalogReading || m_activeAnalogChannels.empty()) {
    return;
  }

  std::map<int, double> voltages;
  if (GetAnalogVoltages(m_activeAnalogChannels, voltages)) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_analogVoltages = voltages;
  }
}

bool PIController::GetAnalogChannelCount(int& numChannels) {
  if (!m_isConnected) return false;

  if (!PI_qTAC(m_controllerId, &numChannels)) {
    if (m_debugVerbose) {
      int error = PI_GetError(m_controllerId);
      LOG_DEBUG("Failed to get analog channel count. Error: " + std::to_string(error));
    }
    return false;
  }

  return true;
}

bool PIController::GetAnalogVoltage(int channel, double& voltage) {
  if (!m_isConnected) return false;

  int channelId = channel;
  if (!PI_qTAV(m_controllerId, &channelId, &voltage, 1)) {
    if (m_debugVerbose) {
      int error = PI_GetError(m_controllerId);
      LOG_DEBUG("Failed to read analog channel " + std::to_string(channel) + ". Error: " + std::to_string(error));
    }
    return false;
  }

  return true;
}

bool PIController::GetAnalogVoltages(std::vector<int> channels, std::map<int, double>& voltages) {
  if (!m_isConnected || channels.empty()) return false;

  std::vector<int> channelIds = channels;
  std::vector<double> values(channels.size(), 0.0);

  if (!PI_qTAV(m_controllerId, channelIds.data(), values.data(), static_cast<int>(channels.size()))) {
    if (m_debugVerbose) {
      int error = PI_GetError(m_controllerId);
      LOG_DEBUG("Failed to read analog channels. Error: " + std::to_string(error));
    }
    return false;
  }

  voltages.clear();
  for (size_t i = 0; i < channels.size(); i++) {
    voltages[channels[i]] = values[i];
  }

  return true;
}

bool PIController::Connect(const std::string& ipAddress, int port) {
  if (m_isConnected) {
    LOG_WARNING("Already connected");
    return true;
  }

  LOG_INFO("Connecting to " + ipAddress + ":" + std::to_string(port));

  m_ipAddress = ipAddress;
  m_port = port;

  m_controllerId = PI_ConnectTCPIP(m_ipAddress.c_str(), m_port);

  if (m_controllerId < 0) {
    int errorCode = PI_GetInitError();
    LOG_ERROR("Failed to connect. Error: " + std::to_string(errorCode));
    return false;
  }

  m_isConnected.store(true);
  LOG_INFO("Successfully connected (ID: " + std::to_string(m_controllerId) + ")");

  // Initialize status maps
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& axis : m_availableAxes) {
      m_axisPositions[axis] = 0.0;
      m_axisMoving[axis] = false;
      m_axisServoEnabled[axis] = false;
    }
    m_lastStatusUpdate = std::chrono::steady_clock::now();
    m_lastPositionUpdate = std::chrono::steady_clock::now();
  }

  // Initialize controller
  PI_INI(m_controllerId, NULL);

  // Initialize analog channels
  InitializeAnalogChannels();

  // Update cached positions
  std::map<std::string, double> positions;
  if (GetPositions(positions)) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_axisPositions = positions;
  }

  return true;
}

void PIController::InitializeAnalogChannels() {
  if (!m_isConnected || !m_enableAnalogReading) return;

  if (GetAnalogChannelCount(m_numAnalogChannels)) {
    LOG_INFO("Found " + std::to_string(m_numAnalogChannels) + " analog channels");

    std::lock_guard<std::mutex> lock(m_mutex);
    for (int channel : m_activeAnalogChannels) {
      if (channel <= m_numAnalogChannels) {
        m_analogVoltages[channel] = 0.0;
      }
    }
  }
}

void PIController::Disconnect() {
  if (!m_isConnected) return;

  StopCommunicationThread();
  LOG_INFO("Disconnecting");

  (void)StopAllAxes();
  PI_CloseConnection(m_controllerId);

  m_isConnected.store(false);
  m_controllerId = -1;

  LOG_INFO("Disconnected");
}

bool PIController::MoveToPosition(const std::string& axis, double position, bool blocking) {
  if (!m_isConnected) {
    LOG_ERROR("Cannot move - not connected");
    return false;
  }

  if (m_enableDebug) {
    LOG_INFO("Moving " + axis + " to " + std::to_string(position));
  }

  const char* axes = axis.c_str();
  double positions[1] = { position };

  if (!PI_MOV(m_controllerId, axes, positions)) {
    int error = 0;
    PI_qERR(m_controllerId, &error);
    LOG_ERROR("Failed to move. Error: " + std::to_string(error));
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_axisMoving[axis] = true;
  }

  if (blocking) {
    return WaitForMotionCompletion(axis);
  }

  return true;
}

bool PIController::MoveRelative(const std::string& axis, double distance, bool blocking) {
  if (!m_isConnected) {
    LOG_ERROR("Cannot move - not connected");
    return false;
  }

  if (m_debugVerbose) {
    LOG_DEBUG("Moving " + axis + " relative " + std::to_string(distance));
  }

  const char* axes = axis.c_str();
  double distances[1] = { distance };

  if (m_debugVerbose) {
    double currentPos = 0.0;
    if (GetPosition(axis, currentPos)) {
      LOG_DEBUG("Pre-move position: " + axis + " = " + std::to_string(currentPos));
    }
  }

  bool moveResult = PI_MVR(m_controllerId, axes, distances);

  if (!moveResult) {
    int error = 0;
    PI_qERR(m_controllerId, &error);
    LOG_ERROR("Failed to move. Error: " + std::to_string(error));

    if (m_debugVerbose) {
      char errorText[256] = { 0 };
      if (PI_TranslateError(error, errorText, sizeof(errorText))) {
        LOG_ERROR("Translation: " + std::string(errorText));
      }
    }
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_axisMoving[axis] = true;
  }

  if (blocking) {
    return WaitForMotionCompletion(axis);
  }

  return true;
}

bool PIController::HomeAxis(const std::string& axis) {
  if (!m_isConnected) {
    LOG_ERROR("Cannot home - not connected");
    return false;
  }

  LOG_INFO("Homing axis " + axis);

  const char* axes = axis.c_str();

  if (!PI_FRF(m_controllerId, axes)) {
    int error = 0;
    PI_qERR(m_controllerId, &error);
    LOG_ERROR("Failed to home. Error: " + std::to_string(error));
    return false;
  }

  return WaitForMotionCompletion(axis);
}

bool PIController::StopAxis(const std::string& axis) {
  if (!m_isConnected) {
    LOG_ERROR("Cannot stop - not connected");
    return false;
  }

  LOG_INFO("Stopping axis " + axis);

  const char* axes = axis.c_str();

  if (!PI_HLT(m_controllerId, axes)) {
    int error = 0;
    PI_qERR(m_controllerId, &error);
    LOG_ERROR("Failed to stop. Error: " + std::to_string(error));
    return false;
  }

  return true;
}

bool PIController::StopAllAxes() {
  if (!m_isConnected) {
    LOG_ERROR("Cannot stop - not connected");
    return false;
  }

  LOG_INFO("Stopping all axes");

  if (!PI_STP(m_controllerId)) {
    int error = 0;
    PI_qERR(m_controllerId, &error);
    LOG_ERROR("Failed to stop all. Error: " + std::to_string(error));
    return false;
  }

  return true;
}

bool PIController::IsMoving(const std::string& axis) {
  if (!m_isConnected) return false;

  const char* axes = axis.c_str();
  BOOL isMovingArray[1] = { FALSE };

  bool success = PI_IsMoving(m_controllerId, axes, isMovingArray);

  if (success) {
    if (m_debugVerbose) {
      LOG_DEBUG("IsMoving " + axis + ": " + (isMovingArray[0] ? "TRUE" : "FALSE"));
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_axisMoving[axis] = (isMovingArray[0] == TRUE);
    return (isMovingArray[0] == TRUE);
  }

  if (m_debugVerbose) {
    int error = PI_GetError(m_controllerId);
    LOG_DEBUG("IsMoving query failed for " + axis + ". Error: " + std::to_string(error));
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_axisMoving.find(axis);
  return (it != m_axisMoving.end() && it->second);
}

bool PIController::GetPositions(std::map<std::string, double>& positions) {
  if (!m_isConnected || m_availableAxes.empty()) return false;

  std::string allAxes = "X Y Z U V W";
  double posArray[6] = { 0.0 };

  bool success = PI_qPOS(m_controllerId, allAxes.c_str(), posArray);

  if (success) {
    positions["X"] = posArray[0];
    positions["Y"] = posArray[1];
    positions["Z"] = posArray[2];
    positions["U"] = posArray[3];
    positions["V"] = posArray[4];
    positions["W"] = posArray[5];
  }

  return success;
}

bool PIController::EnableServo(const std::string& axis, bool enable) {
  if (!m_isConnected) {
    LOG_ERROR("Cannot change servo - not connected");
    return false;
  }

  LOG_INFO("Setting servo " + axis + " to " + (enable ? "enabled" : "disabled"));

  const char* axes = axis.c_str();
  BOOL states[1] = { enable ? TRUE : FALSE };

  if (!PI_SVO(m_controllerId, axes, states)) {
    int error = 0;
    PI_qERR(m_controllerId, &error);
    LOG_ERROR("Failed to set servo. Error: " + std::to_string(error));
    return false;
  }

  return true;
}

bool PIController::IsServoEnabled(const std::string& axis, bool& enabled) {
  if (!m_isConnected) return false;

  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
    now - m_lastStatusUpdate).count();

  if (elapsed < m_statusUpdateInterval) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_axisServoEnabled.find(axis);
    if (it != m_axisServoEnabled.end()) {
      enabled = it->second;
      return true;
    }
  }

  const char* axes = axis.c_str();
  BOOL states[1] = { FALSE };

  bool success = PI_qSVO(m_controllerId, axes, states);

  if (success) {
    enabled = (states[0] == TRUE);
    std::lock_guard<std::mutex> lock(m_mutex);
    m_axisServoEnabled[axis] = enabled;
    m_lastStatusUpdate = now;
    return true;
  }

  return false;
}

bool PIController::SetVelocity(const std::string& axis, double velocity) {
  if (!m_isConnected) {
    LOG_ERROR("Cannot set velocity - not connected");
    return false;
  }

  LOG_INFO("Setting velocity " + axis + " to " + std::to_string(velocity));

  const char* axes = axis.c_str();
  double velocities[1] = { velocity };

  if (!PI_VEL(m_controllerId, axes, velocities)) {
    int error = 0;
    PI_qERR(m_controllerId, &error);
    LOG_ERROR("Failed to set velocity. Error: " + std::to_string(error));
    return false;
  }

  return true;
}

bool PIController::GetVelocity(const std::string& axis, double& velocity) {
  if (!m_isConnected) return false;

  const char* axes = axis.c_str();
  double velocities[1] = { 0.0 };

  if (!PI_qVEL(m_controllerId, axes, velocities)) {
    return false;
  }

  velocity = velocities[0];
  return true;
}

bool PIController::WaitForMotionCompletion(const std::string& axis, double timeoutSeconds) {
  if (!m_isConnected) {
    LOG_ERROR("Cannot wait - not connected");
    return false;
  }

  if (m_debugVerbose) {
    LOG_DEBUG("Waiting for " + axis + " completion");
  }

  auto startTime = std::chrono::steady_clock::now();
  int checkCount = 0;

  constexpr int MAX_ITERATIONS = 1200; // Safety bound: 30s timeout / 50ms poll * 2x margin
  for (int iteration = 0; iteration < MAX_ITERATIONS; ++iteration) {
    checkCount++;
    bool stillMoving = IsMoving(axis);

    if (!stillMoving) {
      if (m_enableDebug) {
        LOG_INFO("Motion completed on " + axis + " after " + std::to_string(checkCount) + " checks");
      }
      return true;
    }

    auto currentTime = std::chrono::steady_clock::now();
    auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(
      currentTime - startTime).count();

    if (elapsedSeconds > timeoutSeconds) {
      LOG_WARNING("Timeout waiting for " + axis);
      return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  LOG_ERROR("Loop safety bound reached in WaitForMotionCompletion (PI)");
  return false;
}


// pi_controller.cpp (Part 2 - continued)

bool PIController::ConfigureFromDevice(const MotionDevice& device) {
  if (m_isConnected) {
    LOG_WARNING("Cannot configure while connected");
    return false;
  }

  LOG_INFO("Configuring from device: " + device.Name);

  m_deviceName = device.Name;
  m_ipAddress = device.IpAddress;
  m_port = device.Port;

  m_availableAxes.clear();
  if (!device.InstalledAxes.empty()) {
    std::string axisStr = device.InstalledAxes;
    std::string delimiter = " ";
    size_t pos = 0;
    std::string token;

    while ((pos = axisStr.find(delimiter)) != std::string::npos) {
      token = axisStr.substr(0, pos);
      if (!token.empty()) {
        m_availableAxes.push_back(token);
      }
      axisStr.erase(0, pos + delimiter.length());
    }

    if (!axisStr.empty()) {
      m_availableAxes.push_back(axisStr);
    }

    LOG_INFO("Configured axes: " + device.InstalledAxes);
  }
  else {
    m_availableAxes = { "X", "Y", "Z", "U", "V", "W" };
    LOG_INFO("Using default hexapod axes");
  }

  return true;
}

bool PIController::MoveToNamedPosition(const std::string& deviceName, const std::string& positionName) {
  LOG_INFO("Moving to named position " + positionName + " for " + deviceName);
  return true;
}

float PIController::GetAxisPositionFloat(const std::string& axis) {
  double position = 0.0;
  if (GetPosition(axis, position)) {
    return static_cast<float>(position);
  }
  return 0.0f;
}

bool PIController::GetPosition(const std::string& axis, double& position) {
  if (!m_isConnected) return false;

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_axisPositions.find(axis);
    if (it != m_axisPositions.end()) {
      position = it->second;
      return true;
    }
  }

  const char* axes = axis.c_str();
  double positions[1] = { 0.0 };

  bool result = PI_qPOS(m_controllerId, axes, positions);

  if (result) {
    position = positions[0];
    std::lock_guard<std::mutex> lock(m_mutex);
    m_axisPositions[axis] = position;
  }

  return result;
}

bool PIController::MoveToPositionAll(double x, double y, double z, double u, double v, double w, bool blocking) {
  if (!m_isConnected) {
    LOG_ERROR("Cannot move - not connected");
    return false;
  }

  LOG_INFO("Moving all axes to X=" + std::to_string(x) + ", Y=" + std::to_string(y) + ", Z=" + std::to_string(z) + ", U=" + std::to_string(u) + ", V=" + std::to_string(v) + ", W=" + std::to_string(w));

  const char* szAxes = "X Y Z U V W";
  double pdValueArray[6] = { x, y, z, u, v, w };

  if (!PI_MOV(m_controllerId, szAxes, pdValueArray)) {
    int error = PI_GetError(m_controllerId);
    LOG_ERROR("Failed to move all axes. Error: " + std::to_string(error));
    return false;
  }

  if (blocking) {
    bool success = true;
    for (const auto& axis : { "X", "Y", "Z", "U", "V", "W" }) {
      if (!WaitForMotionCompletion(axis)) {
        LOG_ERROR("Timeout waiting for " + std::string(axis));
        success = false;
      }
    }
    return success;
  }

  return true;
}

bool PIController::MoveToPositionMultiAxis(const std::vector<std::string>& axes,
  const std::vector<double>& positions,
  bool blocking) {
  if (!m_isConnected) {
    LOG_ERROR("Cannot move - not connected");
    return false;
  }

  if (axes.size() != positions.size() || axes.empty()) {
    LOG_ERROR("Invalid axes/positions arrays");
    return false;
  }

  LOG_INFO("Moving multiple axes");

  std::string axesStr;
  for (size_t i = 0; i < axes.size(); i++) {
    axesStr += axes[i];
    if (i < axes.size() - 1) {
      axesStr += " ";
    }
  }

  const char* szAxes = axesStr.c_str();
  std::vector<double> posArray = positions;

  if (!PI_MOV(m_controllerId, szAxes, posArray.data())) {
    int error = PI_GetError(m_controllerId);
    LOG_ERROR("Failed to move. Error: " + std::to_string(error));
    return false;
  }

  if (blocking) {
    bool success = true;
    for (const auto& axis : axes) {
      if (!WaitForMotionCompletion(axis)) {
        LOG_ERROR("Timeout waiting for " + axis);
        success = false;
      }
    }
    return success;
  }

  return true;
}

bool PIController::Home(const std::string& axis) {
  if (!IsConnected()) {
    LOG_ERROR("Home: Not connected");
    return false;
  }

  if (axis.empty()) {
    LOG_ERROR("Home: Empty axis name");
    return false;
  }

  LOG_INFO("Homing axis " + axis);

  BOOL result = PI_GOH(m_controllerId, axis.c_str());

  if (!result) {
    int errorCode = PI_GetError(m_controllerId);
    LOG_ERROR("Home failed for " + axis + ". PI error: " + std::to_string(errorCode));
    return false;
  }

  LOG_INFO("Successfully started homing for " + axis);
  return true;
}

bool PIController::HomeAll() {
  if (!IsConnected()) {
    LOG_ERROR("HomeAll: Not connected");
    return false;
  }

  LOG_INFO("Homing all axes");

  BOOL result = PI_GOH(m_controllerId, "");

  if (!result) {
    int errorCode = PI_GetError(m_controllerId);
    LOG_ERROR("HomeAll failed. PI error: " + std::to_string(errorCode));
    return false;
  }

  LOG_INFO("Successfully started homing all axes");
  return true;
}

bool PIController::HomeAxes(const std::vector<std::string>& axes) {
  if (!IsConnected()) {
    LOG_ERROR("HomeAxes: Not connected");
    return false;
  }

  if (axes.empty()) {
    LOG_ERROR("HomeAxes: Empty axes list");
    return false;
  }

  std::string axesString = AxesToString(axes);
  LOG_INFO("Homing axes: " + axesString);

  BOOL result = PI_GOH(m_controllerId, axesString.c_str());

  if (!result) {
    int errorCode = PI_GetError(m_controllerId);
    LOG_ERROR("HomeAxes failed. PI error: " + std::to_string(errorCode));
    return false;
  }

  LOG_INFO("Successfully started homing");
  return true;
}

bool PIController::HomeAxes(const std::string& axesString) {
  if (!IsConnected()) {
    LOG_ERROR("HomeAxes: Not connected");
    return false;
  }

  if (axesString.empty()) {
    LOG_ERROR("HomeAxes: Empty axes string");
    return false;
  }

  LOG_INFO("Homing axes: " + axesString);

  BOOL result = PI_GOH(m_controllerId, axesString.c_str());

  if (!result) {
    int errorCode = PI_GetError(m_controllerId);
    LOG_ERROR("HomeAxes failed. PI error: " + std::to_string(errorCode));
    return false;
  }

  LOG_INFO("Successfully started homing");
  return true;
}

bool PIController::DefineHome(const std::string& axis) {
  if (!IsConnected()) {
    LOG_ERROR("DefineHome: Not connected");
    return false;
  }

  if (axis.empty()) {
    LOG_ERROR("DefineHome: Empty axis name");
    return false;
  }

  LOG_INFO("Defining home for " + axis);

  BOOL result = PI_DFH(m_controllerId, axis.c_str());

  if (!result) {
    int errorCode = PI_GetError(m_controllerId);
    LOG_ERROR("DefineHome failed. PI error: " + std::to_string(errorCode));
    return false;
  }

  LOG_INFO("Successfully defined home for " + axis);
  return true;
}

bool PIController::DefineHomeAll() {
  if (!IsConnected()) {
    LOG_ERROR("DefineHomeAll: Not connected");
    return false;
  }

  LOG_INFO("Defining home for all axes");

  auto axes = GetAvailableAxes();
  bool success = true;

  for (const std::string& axis : axes) {
    BOOL result = PI_DFH(m_controllerId, axis.c_str());
    if (!result) {
      int errorCode = PI_GetError(m_controllerId);
      LOG_ERROR("DefineHomeAll failed for " + axis + ". PI error: " + std::to_string(errorCode));
      success = false;
    }
  }

  if (success) {
    LOG_INFO("Successfully defined home for all axes");
  }

  return success;
}

bool PIController::FSA(const std::string& axis1, double length1,
  const std::string& axis2, double length2,
  double threshold, double distance,
  double alignStep, int analogInput) {
  if (!m_isConnected) {
    LOG_ERROR("Cannot perform FSA - not connected");
    return false;
  }

  LOG_INFO("Starting FSA scan");

  bool result = PI_FSA(m_controllerId,
    axis1.c_str(), length1,
    axis2.c_str(), length2,
    threshold, distance,
    alignStep, analogInput);

  if (!result) {
    int error = PI_GetError(m_controllerId);
    LOG_ERROR("FSA failed. Error: " + std::to_string(error));
    return false;
  }

  LOG_INFO("FSA scan started successfully");
  return true;
}

bool PIController::FSC(const std::string& axis1, double length1,
  const std::string& axis2, double length2,
  double threshold, double distance,
  int analogInput) {
  if (!m_isConnected) {
    LOG_ERROR("Cannot perform FSC - not connected");
    return false;
  }

  LOG_INFO("Starting FSC scan");

  bool result = PI_FSC(m_controllerId,
    axis1.c_str(), length1,
    axis2.c_str(), length2,
    threshold, distance,
    analogInput);

  if (!result) {
    int error = PI_GetError(m_controllerId);
    LOG_ERROR("FSC failed. Error: " + std::to_string(error));
    return false;
  }

  LOG_INFO("FSC scan started successfully");
  return true;
}

bool PIController::FSM(const std::string& axis1, double length1,
  const std::string& axis2, double length2,
  double threshold, double distance,
  int analogInput) {
  if (!m_isConnected) {
    LOG_ERROR("Cannot perform FSM - not connected");
    return false;
  }

  LOG_INFO("Starting FSM scan");

  bool result = PI_FSM(m_controllerId,
    axis1.c_str(), length1,
    axis2.c_str(), length2,
    threshold, distance,
    analogInput);

  if (!result) {
    int error = PI_GetError(m_controllerId);
    LOG_ERROR("FSM failed. Error: " + std::to_string(error));
    return false;
  }

  LOG_INFO("FSM scan started successfully");
  return true;
}

bool PIController::FLM(const std::string& axis, double length, double threshold,
  int analogInput, int direction) {
  if (!m_isConnected) {
    LOG_ERROR("Cannot perform FLM - not connected");
    return false;
  }

  LOG_INFO("Starting FLM (Fast Line Maximum) scan on axis " + axis + ", length=" + std::to_string(length) + ", threshold=" + std::to_string(threshold) + ", analogInput=" + std::to_string(analogInput) + ", direction=" + std::to_string(direction));

  bool result = PI_FLM(m_controllerId,
    axis.c_str(),
    length,
    threshold,
    analogInput,
    direction);

  if (!result) {
    int error = PI_GetError(m_controllerId);
    char errorText[256] = { 0 };
    PI_TranslateError(error, errorText, sizeof(errorText));
    LOG_ERROR("FLM failed. Error: " + std::to_string(error) + " - " + std::string(errorText));
    return false;
  }

  LOG_INFO("FLM scan started successfully");
  return true;
}

bool PIController::FLS(const std::string& axis, double length, double threshold,
  int analogInput, int direction) {
  if (!m_isConnected) {
    LOG_ERROR("Cannot perform FLS - not connected");
    return false;
  }

  LOG_INFO("Starting FLS (Fast Line Scan) on axis " + axis + ", length=" + std::to_string(length) + ", threshold=" + std::to_string(threshold) + ", analogInput=" + std::to_string(analogInput) + ", direction=" + std::to_string(direction));

  bool result = PI_FLS(m_controllerId,
    axis.c_str(),
    length,
    threshold,
    analogInput,
    direction);

  if (!result) {
    int error = PI_GetError(m_controllerId);
    char errorText[256] = { 0 };
    PI_TranslateError(error, errorText, sizeof(errorText));
    LOG_ERROR("FLS failed. Error: " + std::to_string(error) + " - " + std::string(errorText));
    return false;
  }

  LOG_INFO("FLS scan started successfully");
  return true;
}

bool PIController::GetFastScanStatus(int& status) {
  if (!m_isConnected) {
    LOG_ERROR("Cannot query scan status - not connected");
    return false;
  }

  if (!PI_qFSS(m_controllerId, &status)) {
    int error = PI_GetError(m_controllerId);
    LOG_ERROR("qFSS failed. Error: " + std::to_string(error));
    return false;
  }

  if (m_debugVerbose) {
    LOG_DEBUG("Fast scan status = " + std::to_string(status) + (status == 0 ? " (idle)" : status > 0 ? " (running)" : " (error)"));
  }

  return true;
}

bool PIController::SetSystemVelocity(double velocity) {
  if (!m_isConnected) {
    LOG_ERROR("Cannot set system velocity - not connected");
    return false;
  }

  LOG_INFO("Setting system velocity to " + std::to_string(velocity));

  if (!PI_VLS(m_controllerId, velocity)) {
    int error = 0;
    PI_qERR(m_controllerId, &error);
    LOG_ERROR("Failed to set system velocity. Error: " + std::to_string(error));
    return false;
  }

  return true;
}

bool PIController::GetSystemVelocity(double& velocity) {
  if (!m_isConnected) return false;

  if (!PI_qVLS(m_controllerId, &velocity)) {
    int error = 0;
    PI_qERR(m_controllerId, &error);
    LOG_ERROR("Failed to get system velocity. Error: " + std::to_string(error));
    return false;
  }

  return true;
}

std::string PIController::AxesToString(const std::vector<std::string>& axes) const {
  if (axes.empty()) return "";

  std::string result = axes[0];
  for (size_t i = 1; i < axes.size(); ++i) {
    result += " " + axes[i];
  }

  return result;
}

// Add anywhere in the implementation file

bool PIController::IsInMotion() {
  if (!m_isConnected) return false;

  std::lock_guard<std::mutex> lock(m_mutex);
  for (const auto& [axis, moving] : m_axisMoving) {
    if (moving) return true;
  }
  return false;
}

PositionStruct PIController::GetCurrentPosition() {
  PositionStruct pos = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };  // Initialize all 6 axes
  std::map<std::string, double> positions;
  if (GetPositions(positions)) {
    if (positions.find("X") != positions.end()) pos.x = positions["X"];
    if (positions.find("Y") != positions.end()) pos.y = positions["Y"];
    if (positions.find("Z") != positions.end()) pos.z = positions["Z"];
    if (positions.find("U") != positions.end()) pos.u = positions["U"];
    if (positions.find("V") != positions.end()) pos.v = positions["V"];
    if (positions.find("W") != positions.end()) pos.w = positions["W"];
  }
  return pos;
}

bool PIController::GetPivotPoint(double& pivotX, double& pivotY, double& pivotZ) {
  if (!m_isConnected) {
    LOG_ERROR("Cannot get pivot point - not connected");
    return false;
  }

  LOG_INFO("Reading pivot point from controller");

  // Use PI_qSPI to query pivot point coordinates
  // Try both "X Y Z" and "R S T" axis naming conventions
  const char* axisNames[] = { "X Y Z", "R S T" };

  for (const char* axes : axisNames) {
    double values[3] = { 0.0, 0.0, 0.0 };

    if (PI_qSPI(m_controllerId, axes, values)) {
      pivotX = values[0];
      pivotY = values[1];
      pivotZ = values[2];

      LOG_INFO("Successfully read pivot point using axes: " + std::string(axes));
      LOG_INFO("Current pivot point: X=" + std::to_string(pivotX) + ", Y=" + std::to_string(pivotY) + ", Z=" + std::to_string(pivotZ));
      return true;
    }
    else {
      int error = PI_GetError(m_controllerId);
      LOG_DEBUG("Failed to read pivot with axes '" + std::string(axes) + "' (Error: " + std::to_string(error) + ")");
    }
  }

  LOG_ERROR("Failed to read pivot point");
  return false;
}

bool PIController::SetPivotPoint(double pivotX, double pivotY, double pivotZ) {
  if (!m_isConnected) {
    LOG_ERROR("Cannot set pivot point - not connected");
    return false;
  }

  LOG_INFO("Setting pivot point: X=" + std::to_string(pivotX) + ", Y=" + std::to_string(pivotY) + ", Z=" + std::to_string(pivotZ));

  // Use PI_SPI to set pivot point coordinates
  // Try both "X Y Z" and "R S T" axis naming conventions
  const char* axisNames[] = { "X Y Z", "R S T" };

  for (const char* axes : axisNames) {
    double values[3] = { pivotX, pivotY, pivotZ };

    if (PI_SPI(m_controllerId, axes, values)) {
      LOG_INFO("Successfully set pivot point using axes: " + std::string(axes));
      return true;
    }
    else {
      int error = PI_GetError(m_controllerId);
      LOG_DEBUG("Failed to set pivot with axes '" + std::string(axes) + "' (Error: " + std::to_string(error) + ")");
    }
  }

  LOG_ERROR("Failed to set pivot point");
  return false;
}

bool PIController::QueryAvailableParameters(int itemId) {
  if (!m_isConnected) {
    LOG_ERROR("Cannot query parameters - not connected");
    return false;
  }

  LOG_INFO("Querying available parameters for item " + std::to_string(itemId));

  // PI_qHPA (Hardware Parameter Available) can list available parameters
  // This is a simplified version - full implementation would require more GCS2 commands

  // Try to query some common parameter ranges
  LOG_INFO("Testing common pivot-related parameter IDs...");

  unsigned int testParams[] = {
    0x50, 0x51, 0x52,           // Common pivot params
    80, 81, 82,                  // Decimal equivalent
    0x07000500, 0x07000501, 0x07000502,  // Extended range
    0x3000001, 0x3000002, 0x3000003      // Another range
  };

  for (unsigned int paramId : testParams) {
    double value = 0.0;
    if (GetParameterValue(itemId, paramId, value)) {
      std::ostringstream oss;
      oss << "  Parameter 0x" << std::hex << paramId << std::dec << " (" << paramId << ") = " << value;
      LOG_INFO(oss.str());
    }
  }

  return true;
}

bool PIController::GetParameterValue(int itemId, unsigned int parameterId, double& value) {
  if (!m_isConnected) return false;

  char szItems[32];
  snprintf(szItems, sizeof(szItems), "%d", itemId);

  unsigned int paramIds[1] = { parameterId };
  double values[1] = { 0.0 };
  char szStrings[256] = { 0 };

  if (PI_qSPA(m_controllerId, szItems, paramIds, values, szStrings, 256)) {
    value = values[0];
    return true;
  }

  return false;
}

bool PIController::SetParameterValue(int itemId, unsigned int parameterId, double value) {
  if (!m_isConnected) return false;

  char szItems[32];
  snprintf(szItems, sizeof(szItems), "%d", itemId);

  unsigned int paramIds[1] = { parameterId };
  double values[1] = { value };

  return PI_SPA(m_controllerId, szItems, paramIds, values, nullptr) != 0;
}