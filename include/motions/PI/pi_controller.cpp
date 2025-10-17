// pi_controller.cpp
#include "pi_controller.h"
#include <iostream>
#include <chrono>
#include <sstream>
#include <algorithm>

PIController::PIController()
  : m_controllerId(-1),
  m_port(50000) {

  m_isConnected.store(false);
  m_threadRunning.store(false);
  m_terminateThread.store(false);
  m_enableAnalogReading.store(true);

  std::cout << "[INFO] PIController: Initializing controller" << std::endl;

  m_availableAxes = { "X", "Y", "Z", "U", "V", "W" };
  m_lastStatusUpdate = std::chrono::steady_clock::now();
  m_lastPositionUpdate = std::chrono::steady_clock::now();

  StartCommunicationThread();
}

PIController::~PIController() {
  std::cout << "[INFO] PIController: Shutting down controller" << std::endl;
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
    std::cout << "[INFO] PIController: Communication thread started" << std::endl;
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
    std::cout << "[INFO] PIController: Communication thread stopped" << std::endl;
  }
}

void PIController::CommunicationThreadFunc() {
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
      std::cout << "[ERROR] PIController: Failed to get analog channel count. Error: "
        << error << std::endl;
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
      std::cout << "[ERROR] PIController: Failed to read analog channel " << channel
        << ". Error: " << error << std::endl;
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
      std::cout << "[ERROR] PIController: Failed to read analog channels. Error: "
        << error << std::endl;
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
    std::cout << "[WARNING] PIController: Already connected" << std::endl;
    return true;
  }

  std::cout << "[INFO] PIController: Connecting to " << ipAddress << ":" << port << std::endl;

  m_ipAddress = ipAddress;
  m_port = port;

  m_controllerId = PI_ConnectTCPIP(m_ipAddress.c_str(), m_port);

  if (m_controllerId < 0) {
    int errorCode = PI_GetInitError();
    std::cout << "[ERROR] PIController: Failed to connect. Error: " << errorCode << std::endl;
    return false;
  }

  m_isConnected.store(true);
  std::cout << "[INFO] PIController: Successfully connected (ID: " << m_controllerId << ")" << std::endl;

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
    std::cout << "[INFO] PIController: Found " << m_numAnalogChannels
      << " analog channels" << std::endl;

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
  std::cout << "[INFO] PIController: Disconnecting" << std::endl;

  StopAllAxes();
  PI_CloseConnection(m_controllerId);

  m_isConnected.store(false);
  m_controllerId = -1;

  std::cout << "[INFO] PIController: Disconnected" << std::endl;
}

bool PIController::MoveToPosition(const std::string& axis, double position, bool blocking) {
  if (!m_isConnected) {
    std::cout << "[ERROR] PIController: Cannot move - not connected" << std::endl;
    return false;
  }

  if (m_enableDebug) {
    std::cout << "[INFO] PIController: Moving " << axis << " to " << position << std::endl;
  }

  const char* axes = axis.c_str();
  double positions[1] = { position };

  if (!PI_MOV(m_controllerId, axes, positions)) {
    int error = 0;
    PI_qERR(m_controllerId, &error);
    std::cout << "[ERROR] PIController: Failed to move. Error: " << error << std::endl;
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
    std::cout << "[ERROR] PIController: Cannot move - not connected" << std::endl;
    return false;
  }

  if (m_debugVerbose) {
    std::cout << "[INFO] PIController: Moving " << axis << " relative " << distance << std::endl;
  }

  const char* axes = axis.c_str();
  double distances[1] = { distance };

  if (m_debugVerbose) {
    double currentPos = 0.0;
    if (GetPosition(axis, currentPos)) {
      std::cout << "[DEBUG] Pre-move position: " << axis << " = " << currentPos << std::endl;
    }
  }

  bool moveResult = PI_MVR(m_controllerId, axes, distances);

  if (!moveResult) {
    int error = 0;
    PI_qERR(m_controllerId, &error);
    std::cout << "[ERROR] PIController: Failed to move. Error: " << error << std::endl;

    if (m_debugVerbose) {
      char errorText[256] = { 0 };
      if (PI_TranslateError(error, errorText, sizeof(errorText))) {
        std::cout << "[ERROR] Translation: " << errorText << std::endl;
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
    std::cout << "[ERROR] PIController: Cannot home - not connected" << std::endl;
    return false;
  }

  std::cout << "[INFO] PIController: Homing axis " << axis << std::endl;

  const char* axes = axis.c_str();

  if (!PI_FRF(m_controllerId, axes)) {
    int error = 0;
    PI_qERR(m_controllerId, &error);
    std::cout << "[ERROR] PIController: Failed to home. Error: " << error << std::endl;
    return false;
  }

  return WaitForMotionCompletion(axis);
}

bool PIController::StopAxis(const std::string& axis) {
  if (!m_isConnected) {
    std::cout << "[ERROR] PIController: Cannot stop - not connected" << std::endl;
    return false;
  }

  std::cout << "[INFO] PIController: Stopping axis " << axis << std::endl;

  const char* axes = axis.c_str();

  if (!PI_HLT(m_controllerId, axes)) {
    int error = 0;
    PI_qERR(m_controllerId, &error);
    std::cout << "[ERROR] PIController: Failed to stop. Error: " << error << std::endl;
    return false;
  }

  return true;
}

bool PIController::StopAllAxes() {
  if (!m_isConnected) {
    std::cout << "[ERROR] PIController: Cannot stop - not connected" << std::endl;
    return false;
  }

  std::cout << "[INFO] PIController: Stopping all axes" << std::endl;

  if (!PI_STP(m_controllerId)) {
    int error = 0;
    PI_qERR(m_controllerId, &error);
    std::cout << "[ERROR] PIController: Failed to stop all. Error: " << error << std::endl;
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
      std::cout << "[DEBUG] IsMoving " << axis << ": "
        << (isMovingArray[0] ? "TRUE" : "FALSE") << std::endl;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_axisMoving[axis] = (isMovingArray[0] == TRUE);
    return (isMovingArray[0] == TRUE);
  }

  if (m_debugVerbose) {
    int error = PI_GetError(m_controllerId);
    std::cout << "[ERROR] IsMoving query failed for " << axis << ". Error: " << error << std::endl;
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
    std::cout << "[ERROR] PIController: Cannot change servo - not connected" << std::endl;
    return false;
  }

  std::cout << "[INFO] PIController: Setting servo " << axis << " to "
    << (enable ? "enabled" : "disabled") << std::endl;

  const char* axes = axis.c_str();
  BOOL states[1] = { enable ? TRUE : FALSE };

  if (!PI_SVO(m_controllerId, axes, states)) {
    int error = 0;
    PI_qERR(m_controllerId, &error);
    std::cout << "[ERROR] PIController: Failed to set servo. Error: " << error << std::endl;
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
    std::cout << "[ERROR] PIController: Cannot set velocity - not connected" << std::endl;
    return false;
  }

  std::cout << "[INFO] PIController: Setting velocity " << axis << " to " << velocity << std::endl;

  const char* axes = axis.c_str();
  double velocities[1] = { velocity };

  if (!PI_VEL(m_controllerId, axes, velocities)) {
    int error = 0;
    PI_qERR(m_controllerId, &error);
    std::cout << "[ERROR] PIController: Failed to set velocity. Error: " << error << std::endl;
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
    std::cout << "[ERROR] PIController: Cannot wait - not connected" << std::endl;
    return false;
  }

  if (m_debugVerbose) {
    std::cout << "[DEBUG] Waiting for " << axis << " completion" << std::endl;
  }

  auto startTime = std::chrono::steady_clock::now();
  int checkCount = 0;

  while (true) {
    checkCount++;
    bool stillMoving = IsMoving(axis);

    if (!stillMoving) {
      if (m_enableDebug) {
        std::cout << "[INFO] Motion completed on " << axis << " after "
          << checkCount << " checks" << std::endl;
      }
      return true;
    }

    auto currentTime = std::chrono::steady_clock::now();
    auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(
      currentTime - startTime).count();

    if (elapsedSeconds > timeoutSeconds) {
      std::cout << "[WARNING] Timeout waiting for " << axis << std::endl;
      return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}


// pi_controller.cpp (Part 2 - continued)

bool PIController::ConfigureFromDevice(const MotionDevice& device) {
  if (m_isConnected) {
    std::cout << "[WARNING] PIController: Cannot configure while connected" << std::endl;
    return false;
  }

  std::cout << "[INFO] PIController: Configuring from device: " << device.Name << std::endl;

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

    std::cout << "[INFO] PIController: Configured axes: " << device.InstalledAxes << std::endl;
  }
  else {
    m_availableAxes = { "X", "Y", "Z", "U", "V", "W" };
    std::cout << "[INFO] PIController: Using default hexapod axes" << std::endl;
  }

  return true;
}

bool PIController::MoveToNamedPosition(const std::string& deviceName, const std::string& positionName) {
  std::cout << "[INFO] PIController: Moving to named position " << positionName
    << " for " << deviceName << std::endl;
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
    std::cout << "[ERROR] PIController: Cannot move - not connected" << std::endl;
    return false;
  }

  std::cout << "[INFO] PIController: Moving all axes to X=" << x << ", Y=" << y
    << ", Z=" << z << ", U=" << u << ", V=" << v << ", W=" << w << std::endl;

  const char* szAxes = "X Y Z U V W";
  double pdValueArray[6] = { x, y, z, u, v, w };

  if (!PI_MOV(m_controllerId, szAxes, pdValueArray)) {
    int error = PI_GetError(m_controllerId);
    std::cout << "[ERROR] PIController: Failed to move all axes. Error: " << error << std::endl;
    return false;
  }

  if (blocking) {
    bool success = true;
    for (const auto& axis : { "X", "Y", "Z", "U", "V", "W" }) {
      if (!WaitForMotionCompletion(axis)) {
        std::cout << "[ERROR] Timeout waiting for " << axis << std::endl;
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
    std::cout << "[ERROR] PIController: Cannot move - not connected" << std::endl;
    return false;
  }

  if (axes.size() != positions.size() || axes.empty()) {
    std::cout << "[ERROR] PIController: Invalid axes/positions arrays" << std::endl;
    return false;
  }

  std::cout << "[INFO] PIController: Moving multiple axes" << std::endl;

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
    std::cout << "[ERROR] PIController: Failed to move. Error: " << error << std::endl;
    return false;
  }

  if (blocking) {
    bool success = true;
    for (const auto& axis : axes) {
      if (!WaitForMotionCompletion(axis)) {
        std::cout << "[ERROR] Timeout waiting for " << axis << std::endl;
        success = false;
      }
    }
    return success;
  }

  return true;
}

bool PIController::Home(const std::string& axis) {
  if (!IsConnected()) {
    std::cout << "[ERROR] PIController::Home: Not connected" << std::endl;
    return false;
  }

  if (axis.empty()) {
    std::cout << "[ERROR] PIController::Home: Empty axis name" << std::endl;
    return false;
  }

  std::cout << "[INFO] PIController: Homing axis " << axis << std::endl;

  BOOL result = PI_GOH(m_controllerId, axis.c_str());

  if (!result) {
    int errorCode = PI_GetError(m_controllerId);
    std::cout << "[ERROR] PIController::Home failed for " << axis
      << ". PI error: " << errorCode << std::endl;
    return false;
  }

  std::cout << "[INFO] PIController: Successfully started homing for " << axis << std::endl;
  return true;
}

bool PIController::HomeAll() {
  if (!IsConnected()) {
    std::cout << "[ERROR] PIController::HomeAll: Not connected" << std::endl;
    return false;
  }

  std::cout << "[INFO] PIController: Homing all axes" << std::endl;

  BOOL result = PI_GOH(m_controllerId, "");

  if (!result) {
    int errorCode = PI_GetError(m_controllerId);
    std::cout << "[ERROR] PIController::HomeAll failed. PI error: " << errorCode << std::endl;
    return false;
  }

  std::cout << "[INFO] PIController: Successfully started homing all axes" << std::endl;
  return true;
}

bool PIController::HomeAxes(const std::vector<std::string>& axes) {
  if (!IsConnected()) {
    std::cout << "[ERROR] PIController::HomeAxes: Not connected" << std::endl;
    return false;
  }

  if (axes.empty()) {
    std::cout << "[ERROR] PIController::HomeAxes: Empty axes list" << std::endl;
    return false;
  }

  std::string axesString = AxesToString(axes);
  std::cout << "[INFO] PIController: Homing axes: " << axesString << std::endl;

  BOOL result = PI_GOH(m_controllerId, axesString.c_str());

  if (!result) {
    int errorCode = PI_GetError(m_controllerId);
    std::cout << "[ERROR] PIController::HomeAxes failed. PI error: " << errorCode << std::endl;
    return false;
  }

  std::cout << "[INFO] PIController: Successfully started homing" << std::endl;
  return true;
}

bool PIController::HomeAxes(const std::string& axesString) {
  if (!IsConnected()) {
    std::cout << "[ERROR] PIController::HomeAxes: Not connected" << std::endl;
    return false;
  }

  if (axesString.empty()) {
    std::cout << "[ERROR] PIController::HomeAxes: Empty axes string" << std::endl;
    return false;
  }

  std::cout << "[INFO] PIController: Homing axes: " << axesString << std::endl;

  BOOL result = PI_GOH(m_controllerId, axesString.c_str());

  if (!result) {
    int errorCode = PI_GetError(m_controllerId);
    std::cout << "[ERROR] PIController::HomeAxes failed. PI error: " << errorCode << std::endl;
    return false;
  }

  std::cout << "[INFO] PIController: Successfully started homing" << std::endl;
  return true;
}

bool PIController::DefineHome(const std::string& axis) {
  if (!IsConnected()) {
    std::cout << "[ERROR] PIController::DefineHome: Not connected" << std::endl;
    return false;
  }

  if (axis.empty()) {
    std::cout << "[ERROR] PIController::DefineHome: Empty axis name" << std::endl;
    return false;
  }

  std::cout << "[INFO] PIController: Defining home for " << axis << std::endl;

  BOOL result = PI_DFH(m_controllerId, axis.c_str());

  if (!result) {
    int errorCode = PI_GetError(m_controllerId);
    std::cout << "[ERROR] PIController::DefineHome failed. PI error: " << errorCode << std::endl;
    return false;
  }

  std::cout << "[INFO] PIController: Successfully defined home for " << axis << std::endl;
  return true;
}

bool PIController::DefineHomeAll() {
  if (!IsConnected()) {
    std::cout << "[ERROR] PIController::DefineHomeAll: Not connected" << std::endl;
    return false;
  }

  std::cout << "[INFO] PIController: Defining home for all axes" << std::endl;

  auto axes = GetAvailableAxes();
  bool success = true;

  for (const std::string& axis : axes) {
    BOOL result = PI_DFH(m_controllerId, axis.c_str());
    if (!result) {
      int errorCode = PI_GetError(m_controllerId);
      std::cout << "[ERROR] DefineHomeAll failed for " << axis
        << ". PI error: " << errorCode << std::endl;
      success = false;
    }
  }

  if (success) {
    std::cout << "[INFO] PIController: Successfully defined home for all axes" << std::endl;
  }

  return success;
}

bool PIController::FSA(const std::string& axis1, double length1,
  const std::string& axis2, double length2,
  double threshold, double distance,
  double alignStep, int analogInput) {
  if (!m_isConnected) {
    std::cout << "[ERROR] PIController: Cannot perform FSA - not connected" << std::endl;
    return false;
  }

  std::cout << "[INFO] PIController: Starting FSA scan" << std::endl;

  bool result = PI_FSA(m_controllerId,
    axis1.c_str(), length1,
    axis2.c_str(), length2,
    threshold, distance,
    alignStep, analogInput);

  if (!result) {
    int error = PI_GetError(m_controllerId);
    std::cout << "[ERROR] PIController: FSA failed. Error: " << error << std::endl;
    return false;
  }

  std::cout << "[INFO] PIController: FSA scan started successfully" << std::endl;
  return true;
}

bool PIController::FSC(const std::string& axis1, double length1,
  const std::string& axis2, double length2,
  double threshold, double distance,
  int analogInput) {
  if (!m_isConnected) {
    std::cout << "[ERROR] PIController: Cannot perform FSC - not connected" << std::endl;
    return false;
  }

  std::cout << "[INFO] PIController: Starting FSC scan" << std::endl;

  bool result = PI_FSC(m_controllerId,
    axis1.c_str(), length1,
    axis2.c_str(), length2,
    threshold, distance,
    analogInput);

  if (!result) {
    int error = PI_GetError(m_controllerId);
    std::cout << "[ERROR] PIController: FSC failed. Error: " << error << std::endl;
    return false;
  }

  std::cout << "[INFO] PIController: FSC scan started successfully" << std::endl;
  return true;
}

bool PIController::FSM(const std::string& axis1, double length1,
  const std::string& axis2, double length2,
  double threshold, double distance,
  int analogInput) {
  if (!m_isConnected) {
    std::cout << "[ERROR] PIController: Cannot perform FSM - not connected" << std::endl;
    return false;
  }

  std::cout << "[INFO] PIController: Starting FSM scan" << std::endl;

  bool result = PI_FSM(m_controllerId,
    axis1.c_str(), length1,
    axis2.c_str(), length2,
    threshold, distance,
    analogInput);

  if (!result) {
    int error = PI_GetError(m_controllerId);
    std::cout << "[ERROR] PIController: FSM failed. Error: " << error << std::endl;
    return false;
  }

  std::cout << "[INFO] PIController: FSM scan started successfully" << std::endl;
  return true;
}

bool PIController::SetSystemVelocity(double velocity) {
  if (!m_isConnected) {
    std::cout << "[ERROR] PIController: Cannot set system velocity - not connected" << std::endl;
    return false;
  }

  std::cout << "[INFO] PIController: Setting system velocity to " << velocity << std::endl;

  if (!PI_VLS(m_controllerId, velocity)) {
    int error = 0;
    PI_qERR(m_controllerId, &error);
    std::cout << "[ERROR] PIController: Failed to set system velocity. Error: "
      << error << std::endl;
    return false;
  }

  return true;
}

bool PIController::GetSystemVelocity(double& velocity) {
  if (!m_isConnected) return false;

  if (!PI_qVLS(m_controllerId, &velocity)) {
    int error = 0;
    PI_qERR(m_controllerId, &error);
    std::cout << "[ERROR] PIController: Failed to get system velocity. Error: "
      << error << std::endl;
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
  PositionStruct pos = { 0.0, 0.0, 0.0 };
  std::map<std::string, double> positions;
  if (GetPositions(positions)) {
    if (positions.find("X") != positions.end()) pos.x = positions["X"];
    if (positions.find("Y") != positions.end()) pos.y = positions["Y"];
    if (positions.find("Z") != positions.end()) pos.z = positions["Z"];
  }
  return pos;
}