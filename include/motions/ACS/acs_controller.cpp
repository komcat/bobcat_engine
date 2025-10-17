// acs_controller.cpp
#include "acs_controller.h"
#include <iostream>
#include <chrono>
#include <sstream>
#include <algorithm>

// Constructor
ACSController::ACSController()
  : m_controllerId(ACSC_INVALID),
  m_port(ACSC_SOCKET_STREAM_PORT) {

  m_isConnected.store(false);
  m_threadRunning.store(false);
  m_terminateThread.store(false);

  std::cout << "[INFO] ACSController: Initializing controller" << std::endl;

  m_availableAxes = { "X", "Y", "Z" };
  StartCommunicationThread();
}

ACSController::~ACSController() {
  std::cout << "[INFO] ACSController: Shutting down controller" << std::endl;
  StopCommunicationThread();
  if (m_isConnected) {
    Disconnect();
  }
}

void ACSController::StartCommunicationThread() {
  if (!m_threadRunning) {
    m_threadRunning.store(true);
    m_terminateThread.store(false);
    m_communicationThread = std::thread(&ACSController::CommunicationThreadFunc, this);
    std::cout << "[INFO] ACSController: Communication thread started" << std::endl;
  }
}

void ACSController::StopCommunicationThread() {
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
    std::cout << "[INFO] ACSController: Communication thread stopped" << std::endl;
  }
}

void ACSController::CommunicationThreadFunc() {
  const auto updateInterval = std::chrono::milliseconds(200);
  int frameCounter = 0;

  m_lastStatusUpdate = std::chrono::steady_clock::now();
  m_lastPositionUpdate = m_lastStatusUpdate;

  while (!m_terminateThread) {
    auto cycleStartTime = std::chrono::steady_clock::now();

    // Process pending motor commands
    {
      std::lock_guard<std::mutex> lock(m_commandMutex);
      for (auto& cmd : m_commandQueue) {
        if (!cmd.executed) {
          MoveRelative(cmd.axis, cmd.distance, false);
          cmd.executed = true;
        }
      }

      m_commandQueue.erase(
        std::remove_if(m_commandQueue.begin(), m_commandQueue.end(),
          [](const MotorCommand& cmd) { return cmd.executed; }),
        m_commandQueue.end());
    }

    if (m_isConnected) {
      frameCounter++;

      // Update positions
      std::map<std::string, double> positions;
      if (GetPositions(positions)) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_axisPositions = positions;
        m_lastPositionUpdate = std::chrono::steady_clock::now();
      }

      // Update motion status less frequently
      if (frameCounter % 3 == 0) {
        for (const auto& axis : { "X", "Y", "Z" }) {
          bool moving = IsMoving(axis);
          std::lock_guard<std::mutex> lock(m_mutex);
          m_axisMoving[axis] = moving;
        }

        for (const auto& axis : { "X", "Y", "Z" }) {
          bool enabled;
          if (IsServoEnabled(axis, enabled)) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_axisServoEnabled[axis] = enabled;
          }
        }

        m_lastStatusUpdate = std::chrono::steady_clock::now();
      }
    }

    // Sleep
    auto cycleEndTime = std::chrono::steady_clock::now();
    auto cycleDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
      cycleEndTime - cycleStartTime);
    auto sleepTime = updateInterval - cycleDuration;

    std::unique_lock<std::mutex> lock(m_mutex);
    if (sleepTime.count() > 0) {
      m_condVar.wait_for(lock, sleepTime, [this]() { return m_terminateThread.load(); });
    }
    else {
      lock.unlock();
      std::this_thread::yield();
    }
  }
}

void ACSController::ProcessCommandQueue() {
  std::lock_guard<std::mutex> lock(m_commandMutex);
  for (auto& cmd : m_commandQueue) {
    if (!cmd.executed) {
      MoveRelative(cmd.axis, cmd.distance, false);
      cmd.executed = true;
    }
  }

  m_commandQueue.erase(
    std::remove_if(m_commandQueue.begin(), m_commandQueue.end(),
      [](const MotorCommand& cmd) { return cmd.executed; }),
    m_commandQueue.end());
}

void ACSController::UpdatePositions() {
  if (!m_isConnected) return;

  std::map<std::string, double> positions;
  if (GetPositions(positions)) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_axisPositions = positions;
    m_lastPositionUpdate = std::chrono::steady_clock::now();
  }
}

void ACSController::UpdateMotorStatus() {
  if (!m_isConnected) return;

  auto now = std::chrono::steady_clock::now();

  for (const auto& axis : m_availableAxes) {
    int axisIndex = GetAxisIndex(axis);
    if (axisIndex >= 0) {
      int state = 0;
      if (acsc_GetMotorState(m_controllerId, axisIndex, &state, NULL)) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_axisMoving[axis] = (state & ACSC_MST_MOVE) != 0;
        m_axisServoEnabled[axis] = (state & ACSC_MST_ENABLE) != 0;
      }
    }
  }

  m_lastStatusUpdate = now;
}

int ACSController::GetAxisIndex(const std::string& axis) {
  if (axis == "X") return ACSC_AXIS_X;
  if (axis == "Y") return ACSC_AXIS_Y;
  if (axis == "Z") return ACSC_AXIS_Z;

  std::cout << "[WARNING] ACSController: Unknown axis identifier: " << axis << std::endl;
  return -1;
}

bool ACSController::Connect(const std::string& ipAddress, int port) {
  if (m_isConnected) {
    std::cout << "[WARNING] ACSController: Already connected" << std::endl;
    return true;
  }

  std::cout << "[INFO] ACSController: Connecting to " << ipAddress << ":" << port << std::endl;

  m_ipAddress = ipAddress;
  m_port = port;

  char ipBuffer[64];
  strncpy_s(ipBuffer, sizeof(ipBuffer), m_ipAddress.c_str(), _TRUNCATE);

  m_controllerId = acsc_OpenCommEthernet(ipBuffer, m_port);

  if (m_controllerId == ACSC_INVALID) {
    int errorCode = acsc_GetLastError();
    std::cout << "[ERROR] ACSController: Failed to connect. Error: " << errorCode << std::endl;
    return false;
  }

  m_isConnected.store(true);
  std::cout << "[INFO] ACSController: Successfully connected" << std::endl;

  // Enable all axes
  for (const auto& axis : m_availableAxes) {
    int axisIndex = GetAxisIndex(axis);
    if (axisIndex >= 0) {
      if (acsc_Enable(m_controllerId, axisIndex, NULL)) {
        std::cout << "[INFO] ACSController: Enabled axis " << axis << std::endl;
      }
      else {
        int error = acsc_GetLastError();
        std::cout << "[ERROR] ACSController: Failed to enable axis " << axis
          << ". Error: " << error << std::endl;
      }
    }
  }

  // Initialize position cache
  std::map<std::string, double> initialPositions;
  if (GetPositions(initialPositions)) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_axisPositions = initialPositions;
    m_lastPositionUpdate = std::chrono::steady_clock::now();

    if (m_enableDebug) {
      std::cout << "[DEBUG] Initial positions: ";
      for (const auto& [axis, pos] : initialPositions) {
        std::cout << axis << "=" << pos << " ";
      }
      std::cout << std::endl;
    }
  }

  return true;
}

void ACSController::Disconnect() {
  if (!m_isConnected) return;

  std::cout << "[INFO] ACSController: Disconnecting" << std::endl;

  StopAllAxes();
  acsc_CloseComm(m_controllerId);

  m_isConnected.store(false);
  m_controllerId = ACSC_INVALID;

  std::cout << "[INFO] ACSController: Disconnected" << std::endl;
}

bool ACSController::MoveToAbsolutePosition(double x, double y, double z, double velocity, bool blocking) {
  if (!m_isConnected) {
    std::cout << "[ERROR] ACSController: Cannot move - not connected" << std::endl;
    return false;
  }

  if (!MoveToPosition("X", x, false) ||
    !MoveToPosition("Y", y, false) ||
    !MoveToPosition("Z", z, false)) {
    return false;
  }

  if (blocking) {
    bool success = true;
    success &= WaitForMotionCompletion("X");
    success &= WaitForMotionCompletion("Y");
    success &= WaitForMotionCompletion("Z");
    return success;
  }

  return true;
}

bool ACSController::MoveToPosition(const std::string& axis, double position, bool blocking) {
  if (!m_isConnected) {
    std::cout << "[ERROR] ACSController: Cannot move - not connected" << std::endl;
    return false;
  }

  int axisIndex = GetAxisIndex(axis);
  if (axisIndex < 0) return false;

  std::cout << "[INFO] ACSController: Moving " << axis << " to " << position << std::endl;

  int axes[2] = { axisIndex, -1 };
  double points[1] = { position };

  if (!acsc_ToPointM(m_controllerId, ACSC_AMF_WAIT, axes, points, NULL)) {
    int error = acsc_GetLastError();
    std::cout << "[ERROR] ACSController: Failed to move. Error: " << error << std::endl;
    return false;
  }

  if (!StartMotion(axis)) return false;

  if (blocking) {
    return WaitForMotionCompletion(axis);
  }

  return true;
}

bool ACSController::MoveRelative(const std::string& axis, double distance, bool blocking) {
  if (!m_isConnected) {
    std::cout << "[ERROR] ACSController: Cannot move - not connected" << std::endl;
    return false;
  }

  int axisIndex = GetAxisIndex(axis);
  if (axisIndex < 0) return false;

  std::cout << "[INFO] ACSController: Moving " << axis << " relative " << distance << std::endl;

  int axes[2] = { axisIndex, -1 };
  double distances[1] = { distance };

  if (!acsc_ToPointM(m_controllerId, ACSC_AMF_WAIT | ACSC_AMF_RELATIVE, axes, distances, NULL)) {
    int error = acsc_GetLastError();
    std::cout << "[ERROR] ACSController: Failed to move. Error: " << error << std::endl;
    return false;
  }

  if (!StartMotion(axis)) return false;

  if (blocking) {
    return WaitForMotionCompletion(axis);
  }

  return true;
}

bool ACSController::HomeAxis(const std::string& axis) {
  if (!m_isConnected) {
    std::cout << "[ERROR] ACSController: Cannot home - not connected" << std::endl;
    return false;
  }

  int axisIndex = GetAxisIndex(axis);
  if (axisIndex < 0) return false;

  std::cout << "[INFO] ACSController: Homing axis " << axis << std::endl;

  if (!acsc_FaultClear(m_controllerId, axisIndex, NULL)) {
    int error = acsc_GetLastError();
    std::cout << "[ERROR] ACSController: Failed to clear faults. Error: " << error << std::endl;
  }

  return WaitForMotionCompletion(axis);
}

bool ACSController::StopAxis(const std::string& axis) {
  if (!m_isConnected) {
    std::cout << "[ERROR] ACSController: Cannot stop - not connected" << std::endl;
    return false;
  }

  int axisIndex = GetAxisIndex(axis);
  if (axisIndex < 0) return false;

  std::cout << "[INFO] ACSController: Stopping axis " << axis << std::endl;

  if (!acsc_Halt(m_controllerId, axisIndex, NULL)) {
    int error = acsc_GetLastError();
    std::cout << "[ERROR] ACSController: Failed to stop. Error: " << error << std::endl;
    return false;
  }

  return true;
}

bool ACSController::StopAllMotion() {
  return StopAllAxes();
}

bool ACSController::StopAllAxes() {
  if (!m_isConnected) {
    std::cout << "[ERROR] ACSController: Cannot stop - not connected" << std::endl;
    return false;
  }

  std::cout << "[INFO] ACSController: Stopping all axes" << std::endl;

  if (!acsc_KillAll(m_controllerId, NULL)) {
    int error = acsc_GetLastError();
    std::cout << "[ERROR] ACSController: Failed to stop all. Error: " << error << std::endl;
    return false;
  }

  return true;
}

bool ACSController::IsInMotion() {
  if (!m_isConnected) return false;

  std::lock_guard<std::mutex> lock(m_mutex);
  for (const auto& [axis, moving] : m_axisMoving) {
    if (moving) return true;
  }
  return false;
}

bool ACSController::IsMoving(const std::string& axis) {
  if (!m_isConnected) return false;

  // Check cached value if recent
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
    now - m_lastStatusUpdate).count();

  if (elapsed < m_statusUpdateInterval) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_axisMoving.find(axis);
    if (it != m_axisMoving.end()) {
      return it->second;
    }
  }

  // Direct query
  int axisIndex = GetAxisIndex(axis);
  if (axisIndex < 0) return false;

  int state = 0;
  if (!acsc_GetMotorState(m_controllerId, axisIndex, &state, NULL)) {
    return false;
  }

  bool isMoving = (state & ACSC_MST_MOVE) != 0;

  // Update cache
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_axisMoving[axis] = isMoving;
    m_lastStatusUpdate = now;
  }

  return isMoving;
}

float ACSController::GetAxisPositionFloat(const std::string& axis) {
  double position = 0.0;
  if (GetPosition(axis, position)) {
    return static_cast<float>(position);
  }
  return 0.0f;
}

bool ACSController::GetPosition(const std::string& axis, double& position) {
  if (!m_isConnected) return false;

  int axisIndex = GetAxisIndex(axis);
  if (axisIndex < 0) return false;

  if (!acsc_GetFPosition(m_controllerId, axisIndex, &position, NULL)) {
    if (m_enableDebug) {
      int error = acsc_GetLastError();
      std::cout << "[DEBUG] Error getting position for " << axis << ": " << error << std::endl;
    }
    return false;
  }

  return true;
}

PositionStruct ACSController::GetCurrentPosition() {
  PositionStruct pos = { 0.0, 0.0, 0.0 };
  std::map<std::string, double> positions;
  if (GetPositions(positions)) {
    if (positions.find("X") != positions.end()) pos.x = positions["X"];
    if (positions.find("Y") != positions.end()) pos.y = positions["Y"];
    if (positions.find("Z") != positions.end()) pos.z = positions["Z"];
  }
  return pos;
}

bool ACSController::GetPositions(std::map<std::string, double>& positions) {
  if (!m_isConnected || m_availableAxes.empty()) return false;

  int axisIndices[3] = {
      GetAxisIndex("X"), GetAxisIndex("Y"), GetAxisIndex("Z")
  };
  double posArray[3] = { 0.0 };

  bool success = true;
  for (int i = 0; i < 3; i++) {
    if (axisIndices[i] >= 0) {
      if (!acsc_GetFPosition(m_controllerId, axisIndices[i], &posArray[i], NULL)) {
        success = false;
      }
    }
  }

  if (success) {
    for (int i = 0; i < 3; i++) {
      if (axisIndices[i] >= 0) {
        positions[m_availableAxes[i]] = posArray[i];
      }
    }
  }

  return success;
}

bool ACSController::EnableServo(const std::string& axis, bool enable) {
  if (!m_isConnected) {
    std::cout << "[ERROR] ACSController: Cannot change servo - not connected" << std::endl;
    return false;
  }

  int axisIndex = GetAxisIndex(axis);
  if (axisIndex < 0) return false;

  std::cout << "[INFO] ACSController: Setting servo " << axis << " to "
    << (enable ? "enabled" : "disabled") << std::endl;

  bool result = false;
  if (enable) {
    result = acsc_Enable(m_controllerId, axisIndex, NULL) != 0;
  }
  else {
    result = acsc_Disable(m_controllerId, axisIndex, NULL) != 0;
  }

  if (!result) {
    int error = acsc_GetLastError();
    std::cout << "[ERROR] ACSController: Failed to set servo. Error: " << error << std::endl;
  }

  return result;
}

bool ACSController::IsServoEnabled(const std::string& axis, bool& enabled) {
  if (!m_isConnected) return false;

  int axisIndex = GetAxisIndex(axis);
  if (axisIndex < 0) return false;

  int state = 0;
  if (!acsc_GetMotorState(m_controllerId, axisIndex, &state, NULL)) {
    return false;
  }

  enabled = (state & ACSC_MST_ENABLE) != 0;
  return true;
}

bool ACSController::SetVelocity(const std::string& axis, double velocity) {
  if (!m_isConnected) {
    std::cout << "[ERROR] ACSController: Cannot set velocity - not connected" << std::endl;
    return false;
  }

  int axisIndex = GetAxisIndex(axis);
  if (axisIndex < 0) return false;

  std::cout << "[INFO] ACSController: Setting velocity " << axis << " to " << velocity << std::endl;

  if (!acsc_SetVelocity(m_controllerId, axisIndex, velocity, NULL)) {
    int error = acsc_GetLastError();
    std::cout << "[ERROR] ACSController: Failed to set velocity. Error: " << error << std::endl;
    return false;
  }

  return true;
}

bool ACSController::GetVelocity(const std::string& axis, double& velocity) {
  if (!m_isConnected) return false;

  int axisIndex = GetAxisIndex(axis);
  if (axisIndex < 0) return false;

  if (!acsc_GetVelocity(m_controllerId, axisIndex, &velocity, NULL)) {
    return false;
  }

  return true;
}

bool ACSController::WaitForMotionCompletion(const std::string& axis, double timeoutSeconds) {
  if (!m_isConnected) {
    std::cout << "[ERROR] ACSController: Cannot wait - not connected" << std::endl;
    return false;
  }

  int axisIndex = GetAxisIndex(axis);
  if (axisIndex < 0) return false;

  std::cout << "[INFO] ACSController: Waiting for " << axis << " completion" << std::endl;

  auto startTime = std::chrono::steady_clock::now();

  while (true) {
    bool stillMoving = IsMoving(axis);

    if (!stillMoving) {
      std::cout << "[INFO] ACSController: Motion completed on " << axis << std::endl;
      return true;
    }

    auto currentTime = std::chrono::steady_clock::now();
    auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(
      currentTime - startTime).count();

    if (elapsedSeconds > timeoutSeconds) {
      std::cout << "[WARNING] ACSController: Timeout waiting for " << axis << std::endl;
      return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

bool ACSController::ConfigureFromDevice(const MotionDevice& device) {
  if (m_isConnected) {
    std::cout << "[WARNING] ACSController: Cannot configure while connected" << std::endl;
    return false;
  }

  m_deviceName = device.Name;
  std::cout << "[INFO] ACSController: Configuring from device: " << device.Name << std::endl;

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

    std::cout << "[INFO] ACSController: Configured axes: " << device.InstalledAxes << std::endl;
  }
  else {
    m_availableAxes = { "X", "Y", "Z" };
    std::cout << "[INFO] ACSController: Using default axes (X Y Z)" << std::endl;
  }

  return true;
}

bool ACSController::MoveToNamedPosition(const std::string& deviceName, const std::string& positionName) {
  std::cout << "[INFO] ACSController: Moving to named position " << positionName
    << " for " << deviceName << std::endl;
  return true;
}

bool ACSController::StartMotion(const std::string& axis) {
  if (!m_isConnected) {
    std::cout << "[ERROR] ACSController: Cannot start motion - not connected" << std::endl;
    return false;
  }

  int axisIndex = GetAxisIndex(axis);
  if (axisIndex < 0) {
    std::cout << "[ERROR] ACSController: Invalid axis: " << axis << std::endl;
    return false;
  }

  int axes[2] = { axisIndex, -1 };

  if (!acsc_GoM(m_controllerId, axes, NULL)) {
    int error = acsc_GetLastError();
    std::cout << "[ERROR] ACSController: Failed to start motion. Error: " << error << std::endl;
    return false;
  }

  return true;
}

bool ACSController::MoveToPositionMultiAxis(const std::vector<std::string>& axes,
  const std::vector<double>& positions,
  bool blocking) {
  if (!m_isConnected) {
    std::cout << "[ERROR] ACSController: Cannot move - not connected" << std::endl;
    return false;
  }

  if (axes.size() != positions.size() || axes.empty()) {
    std::cout << "[ERROR] ACSController: Invalid axes/positions arrays" << std::endl;
    return false;
  }

  std::cout << "[INFO] ACSController: Moving multiple axes" << std::endl;

  std::vector<int> axisIndices;
  for (const auto& axis : axes) {
    int axisIndex = GetAxisIndex(axis);
    if (axisIndex < 0) {
      std::cout << "[ERROR] ACSController: Invalid axis: " << axis << std::endl;
      return false;
    }
    axisIndices.push_back(axisIndex);
  }

  std::vector<int> axesArray(axisIndices.size() + 1);
  for (size_t i = 0; i < axisIndices.size(); i++) {
    axesArray[i] = axisIndices[i];
  }
  axesArray[axisIndices.size()] = -1;

  if (!acsc_ToPointM(m_controllerId, ACSC_AMF_WAIT, axesArray.data(),
    const_cast<double*>(positions.data()), NULL)) {
    int error = acsc_GetLastError();
    std::cout << "[ERROR] ACSController: Failed to move. Error: " << error << std::endl;
    return false;
  }

  if (!acsc_GoM(m_controllerId, axesArray.data(), NULL)) {
    int error = acsc_GetLastError();
    std::cout << "[ERROR] ACSController: Failed to start motion. Error: " << error << std::endl;
    return false;
  }

  if (blocking) {
    bool allCompleted = true;
    for (const auto& axis : axes) {
      if (!WaitForMotionCompletion(axis)) {
        allCompleted = false;
      }
    }
    return allCompleted;
  }

  return true;
}

bool ACSController::RunBuffer(int bufferNumber, const std::string& labelName) {
  if (!m_isConnected) {
    std::cout << "[ERROR] ACSController: Cannot run buffer - not connected" << std::endl;
    return false;
  }

  if (bufferNumber < 0 || bufferNumber > 63) {
    std::cout << "[ERROR] ACSController: Invalid buffer number " << bufferNumber << std::endl;
    return false;
  }

  char* labelPtr = nullptr;
  char labelBuffer[256] = { 0 };

  if (!labelName.empty()) {
    std::string upperLabel = labelName;
    std::transform(upperLabel.begin(), upperLabel.end(), upperLabel.begin(), ::toupper);

    if (upperLabel[0] != '_' && (upperLabel[0] < 'A' || upperLabel[0] > 'Z')) {
      std::cout << "[ERROR] ACSController: Invalid label name: " << labelName << std::endl;
      return false;
    }

    strncpy_s(labelBuffer, sizeof(labelBuffer), upperLabel.c_str(), _TRUNCATE);
    labelPtr = labelBuffer;

    std::cout << "[INFO] ACSController: Running buffer " << bufferNumber
      << " from label " << labelName << std::endl;
  }
  else {
    std::cout << "[INFO] ACSController: Running buffer " << bufferNumber << std::endl;
  }

  if (!acsc_RunBuffer(m_controllerId, bufferNumber, labelPtr, ACSC_SYNCHRONOUS)) {
    int error = acsc_GetLastError();
    std::cout << "[ERROR] ACSController: Failed to run buffer. Error: " << error << std::endl;
    return false;
  }

  return true;
}

bool ACSController::StopBuffer(int bufferNumber) {
  if (!m_isConnected) {
    std::cout << "[ERROR] ACSController: Cannot stop buffer - not connected" << std::endl;
    return false;
  }

  if (bufferNumber < 0 || bufferNumber > 63) {
    std::cout << "[ERROR] ACSController: Invalid buffer number" << std::endl;
    return false;
  }

  std::cout << "[INFO] ACSController: Stopping buffer " << bufferNumber << std::endl;

  if (!acsc_StopBuffer(m_controllerId, bufferNumber, ACSC_SYNCHRONOUS)) {
    int error = acsc_GetLastError();
    std::cout << "[ERROR] ACSController: Failed to stop buffer. Error: " << error << std::endl;
    return false;
  }

  return true;
}

bool ACSController::StopAllBuffers() {
  if (!m_isConnected) {
    std::cout << "[ERROR] ACSController: Cannot stop buffers - not connected" << std::endl;
    return false;
  }

  std::cout << "[INFO] ACSController: Stopping all buffers" << std::endl;

  if (!acsc_StopBuffer(m_controllerId, ACSC_NONE, ACSC_SYNCHRONOUS)) {
    int error = acsc_GetLastError();
    std::cout << "[ERROR] ACSController: Failed to stop all buffers. Error: " << error << std::endl;
    return false;
  }

  return true;
}