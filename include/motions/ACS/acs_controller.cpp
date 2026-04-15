// acs_controller.cpp
#include "acs_controller.h"
#include "common/ThreadPriority.h"
#include <chrono>
#include <thread>
#include <sstream>
#include <algorithm>

#define LOG_DEBUG(msg) if (m_logger) m_logger->LogDebug(msg, "ACSController")
#define LOG_INFO(msg) if (m_logger) m_logger->LogInfo(msg, "ACSController")
#define LOG_WARNING(msg) if (m_logger) m_logger->LogWarning(msg, "ACSController")
#define LOG_ERROR(msg) if (m_logger) m_logger->LogError(msg, "ACSController")

// Constructor
ACSController::ACSController()
  : m_controllerId(ACSC_INVALID),
  m_port(ACSC_SOCKET_STREAM_PORT) {

  m_isConnected.store(false);
  m_threadRunning.store(false);
  m_terminateThread.store(false);

  LOG_DEBUG("Initializing controller");

  m_availableAxes = { "X", "Y", "Z" };
  StartCommunicationThread();
}

ACSController::~ACSController() {
  LOG_DEBUG("Shutting down controller");
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
    LOG_DEBUG("Communication thread started");
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
    LOG_DEBUG("Communication thread stopped");
  }
}

void ACSController::CommunicationThreadFunc() {
  ThreadUtil::SetCurrentThreadPriority(ThreadUtil::Priority::AboveNormal, "ACSController");
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
        for (const auto& axis : m_availableAxes) {
          bool moving = IsMoving(axis);
          std::lock_guard<std::mutex> lock(m_mutex);
          m_axisMoving[axis] = moving;
        }

        for (const auto& axis : m_availableAxes) {
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
  // Map logical axis name to physical controller axis index
  // The index is based on position in m_availableAxes (configured from JSON)
  // Example: "axes": "Y" -> Y maps to physical axis 0
  // Example: "axes": "X Y Z" -> X=0, Y=1, Z=2
  for (size_t i = 0; i < m_availableAxes.size(); ++i) {
    if (m_availableAxes[i] == axis) {
      return static_cast<int>(i);
    }
  }

  // Return -1 silently - callers should check for this
  // This is expected behavior for partial axis configurations
  return -1;
}

bool ACSController::Connect(const std::string& ipAddress, int port) {
  if (m_isConnected) {
    LOG_WARNING("Already connected");
    return true;
  }

  LOG_INFO("Connecting to " + ipAddress + ":" + std::to_string(port));

  m_ipAddress = ipAddress;
  m_port = port;

  char ipBuffer[64];
  strncpy_s(ipBuffer, sizeof(ipBuffer), m_ipAddress.c_str(), _TRUNCATE);

  m_controllerId = acsc_OpenCommEthernet(ipBuffer, m_port);

  if (m_controllerId == ACSC_INVALID) {
    int errorCode = acsc_GetLastError();
    LOG_ERROR("Failed to connect. Error: " + std::to_string(errorCode));
    return false;
  }

  m_isConnected.store(true);
  LOG_INFO("Successfully connected");

  // Enable all axes
  for (const auto& axis : m_availableAxes) {
    int axisIndex = GetAxisIndex(axis);
    if (axisIndex >= 0) {
      if (acsc_Enable(m_controllerId, axisIndex, NULL)) {
        LOG_INFO("Enabled axis " + axis);
      }
      else {
        int error = acsc_GetLastError();
        LOG_ERROR("Failed to enable axis " + axis + ". Error: " + std::to_string(error));
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
      std::string posStr = "Initial positions: ";
      for (const auto& [axis, pos] : initialPositions) {
        posStr += axis + "=" + std::to_string(pos) + " ";
      }
      LOG_DEBUG(posStr);
    }
  }

  return true;
}

void ACSController::Disconnect() {
  if (!m_isConnected) return;

  LOG_INFO("Disconnecting");

  (void)StopAllAxes();
  acsc_CloseComm(m_controllerId);

  m_isConnected.store(false);
  m_controllerId = ACSC_INVALID;

  LOG_INFO("Disconnected");
}

bool ACSController::MoveToAbsolutePosition(double x, double y, double z, double /*velocity*/, bool blocking) {
  if (!m_isConnected) {
    LOG_ERROR("Cannot move - not connected");
    return false;
  }

  // Build axes/positions list based on configured available axes only
  // This handles single-axis or partial configurations (e.g., only Y installed)
  std::vector<std::string> axes;
  std::vector<double> positions;

  for (const auto& axis : m_availableAxes) {
    if (axis == "X") {
      axes.push_back("X");
      positions.push_back(x);
    }
    else if (axis == "Y") {
      axes.push_back("Y");
      positions.push_back(y);
    }
    else if (axis == "Z") {
      axes.push_back("Z");
      positions.push_back(z);
    }
  }

  if (axes.empty()) {
    LOG_ERROR("No valid axes configured for move");
    return false;
  }

  return MoveToPositionMultiAxis(axes, positions, blocking);
}

bool ACSController::MoveToPosition(const std::string& axis, double position, bool blocking) {
  // Clear previous error
  m_lastErrorCode = 0;
  m_lastErrorMessage.clear();

  LOG_DEBUG("MoveToPosition START - axis=" + axis + " position=" + std::to_string(position) + " blocking=" + (blocking ? "true" : "false"));

  if (!m_isConnected) {
    m_lastErrorCode = -1;
    m_lastErrorMessage = "Not connected";
    LOG_ERROR("Cannot move - not connected");
    return false;
  }

  int axisIndex = GetAxisIndex(axis);
  if (axisIndex < 0) {
    m_lastErrorCode = -2;
    m_lastErrorMessage = "Invalid axis: " + axis;
    LOG_ERROR("Invalid axis index for " + axis);
    return false;
  }

  // Wait for axis to be idle before starting new motion (prevents error 3065)
  constexpr int kMaxIdleIter = 500;  // 5000ms / 10ms poll
  constexpr int kIdlePollMs = 10;
  bool isEnabled = false;
  bool isMoving = true;

  for (int i = 0; i < kMaxIdleIter; ++i) {
    int motorState = 0;
    if (acsc_GetMotorState(m_controllerId, axisIndex, &motorState, NULL)) {
      isEnabled = (motorState & ACSC_MST_ENABLE) != 0;
      isMoving = (motorState & ACSC_MST_MOVE) != 0;

      if (!isMoving) {
        if (i > 0) {
          LOG_DEBUG("Axis " + axis + " became idle after " + std::to_string(i * kIdlePollMs) + "ms wait");
        }
        break;  // Axis is idle, proceed with move
      }
    } else {
      // State query failed, proceed anyway and let acsc_ToPointM handle it
      LOG_DEBUG("Motor state query failed, proceeding anyway");
      isMoving = false;
      break;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(kIdlePollMs));
  }

  if (isMoving) {
    m_lastErrorCode = -3;
    m_lastErrorMessage = "Timeout waiting for axis " + axis + " to become idle (waited " +
                         std::to_string(kMaxIdleIter * kIdlePollMs) + "ms)";
    LOG_ERROR(m_lastErrorMessage);
    return false;
  }

  LOG_INFO("Moving " + axis + " to " + std::to_string(position));

  int axes[2] = { axisIndex, -1 };
  double points[1] = { position };

  LOG_DEBUG("Calling acsc_ToPointM with controllerId=" + std::to_string(reinterpret_cast<uintptr_t>(m_controllerId)) + " axisIndex=" + std::to_string(axisIndex));

  if (!acsc_ToPointM(m_controllerId, ACSC_AMF_WAIT, axes, points, NULL)) {
    m_lastErrorCode = acsc_GetLastError();
    m_lastErrorMessage = "acsc_ToPointM failed: error=" + std::to_string(m_lastErrorCode) +
                         " (wasMoving=" + (isMoving ? "true" : "false") +
                         " wasEnabled=" + (isEnabled ? "true" : "false") + ")";
    LOG_ERROR("acsc_ToPointM failed. Error code: " + std::to_string(m_lastErrorCode));
    return false;
  }
  LOG_DEBUG("acsc_ToPointM succeeded");

  LOG_DEBUG("Calling StartMotion for axis " + axis);
  if (!StartMotion(axis)) {
    m_lastErrorCode = acsc_GetLastError();
    m_lastErrorMessage = "acsc_GoM (StartMotion) failed: error=" + std::to_string(m_lastErrorCode) +
                         " (wasMoving=" + (isMoving ? "true" : "false") +
                         " wasEnabled=" + (isEnabled ? "true" : "false") + ")";
    LOG_ERROR("StartMotion failed for axis " + axis);
    return false;
  }
  LOG_DEBUG("StartMotion succeeded");

  if (blocking) {
    LOG_DEBUG("Waiting for motion completion");
    return WaitForMotionCompletion(axis, 30.0, position);
  }

  LOG_DEBUG("MoveToPosition END - success");
  return true;
}

bool ACSController::MoveRelative(const std::string& axis, double distance, bool blocking) {
  // Clear previous error
  m_lastErrorCode = 0;
  m_lastErrorMessage.clear();

  LOG_DEBUG("MoveRelative START - axis=" + axis + " distance=" + std::to_string(distance) + " blocking=" + (blocking ? "true" : "false"));

  if (!m_isConnected) {
    m_lastErrorCode = -1;
    m_lastErrorMessage = "Not connected";
    LOG_ERROR("Cannot move - not connected");
    return false;
  }

  int axisIndex = GetAxisIndex(axis);
  if (axisIndex < 0) {
    m_lastErrorCode = -2;
    m_lastErrorMessage = "Invalid axis: " + axis;
    LOG_ERROR("Invalid axis index for " + axis);
    return false;
  }

  // Wait for axis to be idle before starting new motion (prevents error 3065)
  constexpr int kMaxIdleIter = 500;  // 5000ms / 10ms poll
  constexpr int kIdlePollMs = 10;
  bool isEnabled = false;
  bool isMoving = true;

  for (int i = 0; i < kMaxIdleIter; ++i) {
    int motorState = 0;
    if (acsc_GetMotorState(m_controllerId, axisIndex, &motorState, NULL)) {
      isEnabled = (motorState & ACSC_MST_ENABLE) != 0;
      isMoving = (motorState & ACSC_MST_MOVE) != 0;

      if (!isMoving) {
        if (i > 0) {
          LOG_DEBUG("Axis " + axis + " became idle after " + std::to_string(i * kIdlePollMs) + "ms wait");
        }
        break;  // Axis is idle, proceed with move
      }
    } else {
      // State query failed, proceed anyway and let acsc_ToPointM handle it
      LOG_DEBUG("Motor state query failed, proceeding anyway");
      isMoving = false;
      break;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(kIdlePollMs));
  }

  if (isMoving) {
    m_lastErrorCode = -3;
    m_lastErrorMessage = "Timeout waiting for axis " + axis + " to become idle (waited " +
                         std::to_string(kMaxIdleIter * kIdlePollMs) + "ms)";
    LOG_ERROR(m_lastErrorMessage);
    return false;
  }

  // Capture current position for target calculation (before issuing move)
  double currentPos = 0.0;
  GetPosition(axis, currentPos);
  double targetAbsPos = currentPos + distance;

  LOG_INFO("Moving " + axis + " relative " + std::to_string(distance));

  int axes[2] = { axisIndex, -1 };
  double distances[1] = { distance };

  LOG_DEBUG("Calling acsc_ToPointM with controllerId=" + std::to_string(reinterpret_cast<uintptr_t>(m_controllerId)) + " axisIndex=" + std::to_string(axisIndex));

  if (!acsc_ToPointM(m_controllerId, ACSC_AMF_WAIT | ACSC_AMF_RELATIVE, axes, distances, NULL)) {
    m_lastErrorCode = acsc_GetLastError();
    m_lastErrorMessage = "acsc_ToPointM failed: error=" + std::to_string(m_lastErrorCode) +
                         " (wasMoving=" + (isMoving ? "true" : "false") +
                         " wasEnabled=" + (isEnabled ? "true" : "false") + ")";
    LOG_ERROR("acsc_ToPointM failed. Error code: " + std::to_string(m_lastErrorCode));
    return false;
  }
  LOG_DEBUG("acsc_ToPointM succeeded");

  LOG_DEBUG("Calling StartMotion for axis " + axis);
  if (!StartMotion(axis)) {
    m_lastErrorCode = acsc_GetLastError();
    m_lastErrorMessage = "acsc_GoM (StartMotion) failed: error=" + std::to_string(m_lastErrorCode) +
                         " (wasMoving=" + (isMoving ? "true" : "false") +
                         " wasEnabled=" + (isEnabled ? "true" : "false") + ")";
    LOG_ERROR("StartMotion failed for axis " + axis);
    return false;
  }
  LOG_DEBUG("StartMotion succeeded");

  if (blocking) {
    LOG_DEBUG("Waiting for motion completion");
    return WaitForMotionCompletion(axis, 30.0, targetAbsPos);
  }

  LOG_DEBUG("MoveRelative END - success");
  return true;
}

bool ACSController::HomeAxis(const std::string& axis) {
  if (!m_isConnected) {
    LOG_ERROR("Cannot home - not connected");
    return false;
  }

  int axisIndex = GetAxisIndex(axis);
  if (axisIndex < 0) return false;

  LOG_INFO("Homing axis " + axis);

  if (!acsc_FaultClear(m_controllerId, axisIndex, NULL)) {
    int error = acsc_GetLastError();
    LOG_ERROR("Failed to clear faults. Error: " + std::to_string(error));
  }

  return WaitForMotionCompletion(axis);
}

bool ACSController::StopAxis(const std::string& axis) {
  if (!m_isConnected) {
    LOG_ERROR("Cannot stop - not connected");
    return false;
  }

  int axisIndex = GetAxisIndex(axis);
  if (axisIndex < 0) return false;

  LOG_INFO("Stopping axis " + axis);

  if (!acsc_Halt(m_controllerId, axisIndex, NULL)) {
    int error = acsc_GetLastError();
    LOG_ERROR("Failed to stop. Error: " + std::to_string(error));
    return false;
  }

  return true;
}

bool ACSController::StopAllMotion() {
  return StopAllAxes();
}

bool ACSController::StopAllAxes() {
  if (!m_isConnected) {
    LOG_ERROR("Cannot stop - not connected");
    return false;
  }

  LOG_INFO("Stopping all axes");

  if (!acsc_KillAll(m_controllerId, NULL)) {
    int error = acsc_GetLastError();
    LOG_ERROR("Failed to stop all. Error: " + std::to_string(error));
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
      LOG_DEBUG("Error getting position for " + axis + ": " + std::to_string(error));
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

  bool success = true;

  // Iterate over configured available axes only
  for (size_t i = 0; i < m_availableAxes.size(); ++i) {
    const std::string& axis = m_availableAxes[i];
    int axisIndex = static_cast<int>(i);  // Physical axis index = position in array
    double pos = 0.0;

    if (!acsc_GetFPosition(m_controllerId, axisIndex, &pos, NULL)) {
      success = false;
    }
    else {
      positions[axis] = pos;
    }
  }

  return success;
}

bool ACSController::EnableServo(const std::string& axis, bool enable) {
  if (!m_isConnected) {
    LOG_ERROR("Cannot change servo - not connected");
    return false;
  }

  int axisIndex = GetAxisIndex(axis);
  if (axisIndex < 0) return false;

  LOG_INFO("Setting servo " + axis + " to " + (enable ? "enabled" : "disabled"));

  bool result = false;
  if (enable) {
    // First, clear any faults on the axis before enabling
    // This is required by ACS controllers - motors cannot be enabled if faults are present
    if (acsc_FaultClear(m_controllerId, axisIndex, NULL) == 0) {
      LOG_WARNING("FaultClear returned 0 for axis " + axis);
      // Continue anyway - might not have any faults
    }

    // Small delay to ensure fault clear is processed
    Sleep(50);

    // Now try to enable the motor
    result = acsc_Enable(m_controllerId, axisIndex, NULL) != 0;
  }
  else {
    result = acsc_Disable(m_controllerId, axisIndex, NULL) != 0;
  }

  if (!result) {
    int error = acsc_GetLastError();
    LOG_ERROR("Failed to set servo. Error code: " + std::to_string(error));
  }
  else {
    LOG_INFO("Servo " + axis + " " + (enable ? "enabled" : "disabled") + " successfully");
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
    LOG_ERROR("Cannot set velocity - not connected");
    return false;
  }

  int axisIndex = GetAxisIndex(axis);
  if (axisIndex < 0) return false;

  LOG_DEBUG("Setting velocity " + axis + " to " + std::to_string(velocity));

  if (!acsc_SetVelocity(m_controllerId, axisIndex, velocity, NULL)) {
    int error = acsc_GetLastError();
    LOG_ERROR("Failed to set velocity. Error: " + std::to_string(error));
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

bool ACSController::WaitForMotionCompletion(const std::string& axis, double timeoutSeconds,
                                             double targetPosition) {
  if (!m_isConnected) {
    LOG_ERROR("Cannot wait - not connected");
    return false;
  }

  int axisIndex = GetAxisIndex(axis);
  if (axisIndex < 0) return false;

  LOG_DEBUG("WaitForMotionCompletion START for " + axis);

  auto startTime = std::chrono::steady_clock::now();

  // Phase 1: Wait for motion to START (motor flag becomes MOVING)
  // This prevents false "completed" when motion hasn't begun yet
  constexpr int kMaxStartIter = 50;  // 500ms / 10ms poll
  constexpr int kStartPollMs = 10;
  bool motionStarted = false;

  for (int i = 0; i < kMaxStartIter; ++i) {
    int motorState = 0;
    if (acsc_GetMotorState(m_controllerId, axisIndex, &motorState, NULL)) {
      if ((motorState & ACSC_MST_MOVE) != 0) {
        motionStarted = true;
        LOG_DEBUG("Axis " + axis + " motion started after " + std::to_string(i * kStartPollMs) + "ms");
        break;
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(kStartPollMs));
  }

  if (!motionStarted) {
    // Motion never started - check if we're already at target (position didn't change)
    LOG_DEBUG("Axis " + axis + " motion flag never set after " +
              std::to_string(kMaxStartIter * kStartPollMs) + "ms - checking if already at target");
    // Proceed to completion check anyway - might be a very short move
  }

  // Phase 2: Wait for motion to COMPLETE
  LOG_DEBUG("Waiting for " + axis + " motion completion");

  // Calculate remaining timeout for Phase 2
  auto phase1Elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now() - startTime).count();
  int remainingTimeoutMs = static_cast<int>(timeoutSeconds * 1000.0) - static_cast<int>(phase1Elapsed);
  if (remainingTimeoutMs < 100) {
    remainingTimeoutMs = 100;  // Minimum 100ms
  }

  // Try acsc_WaitMotionEnd first (SDK blocking wait)
  if (acsc_WaitMotionEnd(m_controllerId, axisIndex, remainingTimeoutMs)) {
    auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - startTime).count();
    LOG_DEBUG("Axis " + axis + " motion completed via SDK after " + std::to_string(totalMs) + "ms total");
    CheckInPosition(axis, axisIndex);
    VerifyFinalPosition(axis, axisIndex, targetPosition);
    return true;
  }

  // SDK wait failed — log and fall back to manual polling
  int sdkError = acsc_GetLastError();
  LOG_DEBUG("acsc_WaitMotionEnd failed (error=" + std::to_string(sdkError) +
            "), falling back to manual poll");

  // Fallback: bounded poll loop
  constexpr int kMaxPollIter = 1200;  // Safety bound: 30s / 50ms * 2x margin
  for (int iteration = 0; iteration < kMaxPollIter; ++iteration) {
    int motorState = 0;
    if (acsc_GetMotorState(m_controllerId, axisIndex, &motorState, NULL)) {
      if ((motorState & ACSC_MST_MOVE) == 0) {
        auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - startTime).count();
        LOG_DEBUG("Axis " + axis + " motion completed via fallback after " + std::to_string(totalMs) + "ms total");
        CheckInPosition(axis, axisIndex);
        VerifyFinalPosition(axis, axisIndex, targetPosition);
        return true;
      }
    }

    auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::steady_clock::now() - startTime).count();

    if (elapsedSeconds > timeoutSeconds) {
      LOG_WARNING("Timeout waiting for " + axis + " after " + std::to_string(timeoutSeconds) + " seconds");
      return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  LOG_ERROR("Loop safety bound reached in WaitForMotionCompletion (ACS)");
  return false;
}

bool ACSController::WaitForMultiAxisCompletion(const std::vector<std::string>& axes,
                                                double timeoutSeconds,
                                                const std::vector<double>& targetPositions) {
  if (!m_isConnected) {
    LOG_ERROR("Cannot wait - not connected");
    return false;
  }

  // Resolve all axis indices up front
  std::vector<int> axisIndices;
  for (const auto& axis : axes) {
    int idx = GetAxisIndex(axis);
    if (idx < 0) return false;
    axisIndices.push_back(idx);
  }

  LOG_DEBUG("WaitForMultiAxisCompletion START for " + std::to_string(axes.size()) + " axes");
  auto startTime = std::chrono::steady_clock::now();

  // Phase 1: Brief wait for ANY axis to show MOVING (confirms motion command was received)
  constexpr int kMaxMultiStartIter = 20;  // 100ms / 5ms poll
  constexpr int kMultiStartPollMs = 5;
  bool anyStarted = false;

  for (int i = 0; i < kMaxMultiStartIter; ++i) {
    for (int idx : axisIndices) {
      int motorState = 0;
      if (acsc_GetMotorState(m_controllerId, idx, &motorState, NULL)) {
        if ((motorState & ACSC_MST_MOVE) != 0) {
          anyStarted = true;
          break;
        }
      }
    }
    if (anyStarted) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(kMultiStartPollMs));
  }

  if (!anyStarted) {
    LOG_DEBUG("No axis showed MOVING flag after " +
              std::to_string(kMaxMultiStartIter * kMultiStartPollMs) + "ms - may already be at target");
  }

  // Phase 2: Wait for motion completion
  LOG_DEBUG("Waiting for multi-axis motion completion");

  // Calculate remaining timeout
  auto phase1Elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now() - startTime).count();
  int remainingTimeoutMs = static_cast<int>(timeoutSeconds * 1000.0) - static_cast<int>(phase1Elapsed);
  if (remainingTimeoutMs < 100) {
    remainingTimeoutMs = 100;
  }

  // Try acsc_WaitMotionEnd per axis sequentially (SDK blocking wait)
  bool sdkWaitSucceeded = true;
  for (size_t i = 0; i < axisIndices.size(); ++i) {
    if (!acsc_WaitMotionEnd(m_controllerId, axisIndices[i], remainingTimeoutMs)) {
      int sdkError = acsc_GetLastError();
      LOG_DEBUG("acsc_WaitMotionEnd failed for axis " + axes[i] +
                " (error=" + std::to_string(sdkError) + "), falling back to manual poll");
      sdkWaitSucceeded = false;
      break;
    }
    // Recalculate remaining timeout after each axis
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - startTime).count();
    remainingTimeoutMs = static_cast<int>(timeoutSeconds * 1000.0) - static_cast<int>(elapsed);
    if (remainingTimeoutMs < 100) {
      remainingTimeoutMs = 100;
    }
  }

  if (sdkWaitSucceeded) {
    auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - startTime).count();
    LOG_DEBUG("All " + std::to_string(axes.size()) + " axes completed via SDK after " + std::to_string(totalMs) + "ms");
    // Run diagnostics on all axes
    for (size_t i = 0; i < axes.size(); ++i) {
      CheckInPosition(axes[i], axisIndices[i]);
      if (i < targetPositions.size()) {
        VerifyFinalPosition(axes[i], axisIndices[i], targetPositions[i]);
      }
    }
    return true;
  }

  // Fallback: bounded poll loop — wait until ALL axes report NOT MOVING
  constexpr int kMaxPollIter = 1200;
  for (int iteration = 0; iteration < kMaxPollIter; ++iteration) {
    bool allStopped = true;
    for (int idx : axisIndices) {
      int motorState = 0;
      if (acsc_GetMotorState(m_controllerId, idx, &motorState, NULL)) {
        if ((motorState & ACSC_MST_MOVE) != 0) {
          allStopped = false;
          break;
        }
      }
    }

    if (allStopped) {
      auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime).count();
      LOG_DEBUG("All " + std::to_string(axes.size()) + " axes completed via fallback after " + std::to_string(totalMs) + "ms");
      // Run diagnostics on all axes
      for (size_t i = 0; i < axes.size(); ++i) {
        CheckInPosition(axes[i], axisIndices[i]);
        if (i < targetPositions.size()) {
          VerifyFinalPosition(axes[i], axisIndices[i], targetPositions[i]);
        }
      }
      return true;
    }

    auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::steady_clock::now() - startTime).count();
    if (elapsedSeconds > timeoutSeconds) {
      LOG_WARNING("Timeout waiting for multi-axis motion after " + std::to_string(timeoutSeconds) + "s");
      return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  LOG_ERROR("Loop safety bound reached in WaitForMultiAxisCompletion (ACS)");
  return false;
}

bool ACSController::ConfigureFromDevice(const MotionDevice& device) {
  if (m_isConnected) {
    LOG_WARNING("Cannot configure while connected");
    return false;
  }

  m_deviceName = device.Name;
  LOG_INFO("Configuring from device: " + device.Name);

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
    m_availableAxes = { "X", "Y", "Z" };
    LOG_INFO("Using default axes (X Y Z)");
  }

  return true;
}

bool ACSController::MoveToNamedPosition(const std::string& deviceName, const std::string& positionName) {
  LOG_INFO("Moving to named position " + positionName + " for " + deviceName);
  return true;
}

bool ACSController::StartMotion(const std::string& axis) {
  LOG_DEBUG("StartMotion START - axis=" + axis);

  if (!m_isConnected) {
    LOG_ERROR("Cannot start motion - not connected");
    return false;
  }

  int axisIndex = GetAxisIndex(axis);
  if (axisIndex < 0) {
    LOG_ERROR("Invalid axis: " + axis);
    return false;
  }

  int axes[2] = { axisIndex, -1 };

  LOG_DEBUG("Calling acsc_GoM with controllerId=" + std::to_string(reinterpret_cast<uintptr_t>(m_controllerId)) + " axisIndex=" + std::to_string(axisIndex));

  if (!acsc_GoM(m_controllerId, axes, NULL)) {
    int error = acsc_GetLastError();
    LOG_ERROR("acsc_GoM failed. Error code: " + std::to_string(error));
    return false;
  }

  LOG_DEBUG("StartMotion END - success");
  return true;
}

bool ACSController::MoveToPositionMultiAxis(const std::vector<std::string>& axes,
  const std::vector<double>& positions,
  bool blocking) {
  // Clear previous error
  m_lastErrorCode = 0;
  m_lastErrorMessage.clear();

  LOG_DEBUG("MoveToPositionMultiAxis START - blocking=" + std::string(blocking ? "true" : "false"));

  if (!m_isConnected) {
    m_lastErrorCode = -1;
    m_lastErrorMessage = "Not connected";
    LOG_ERROR("Cannot move - not connected");
    return false;
  }

  if (axes.size() != positions.size() || axes.empty()) {
    m_lastErrorCode = -2;
    m_lastErrorMessage = "Invalid axes/positions arrays";
    LOG_ERROR("Invalid axes/positions arrays");
    return false;
  }

  LOG_DEBUG("Moving multiple axes");

  std::vector<int> axisIndices;
  for (const auto& axis : axes) {
    int axisIndex = GetAxisIndex(axis);
    if (axisIndex < 0) {
      m_lastErrorCode = -2;
      m_lastErrorMessage = "Invalid axis: " + axis;
      LOG_ERROR("Invalid axis: " + axis);
      return false;
    }
    axisIndices.push_back(axisIndex);
  }

  // Wait for all axes to be idle before starting new motion (prevents error 3065)
  constexpr int kMaxIdleIter = 500;  // 5000ms / 10ms poll
  constexpr int kIdlePollMs = 10;

  for (size_t i = 0; i < axisIndices.size(); i++) {
    int axisIdx = axisIndices[i];
    const std::string& axisName = axes[i];
    bool isMoving = true;

    for (int j = 0; j < kMaxIdleIter; ++j) {
      int motorState = 0;
      if (acsc_GetMotorState(m_controllerId, axisIdx, &motorState, NULL)) {
        isMoving = (motorState & ACSC_MST_MOVE) != 0;

        if (!isMoving) {
          if (j > 0) {
            LOG_DEBUG("Axis " + axisName + " became idle after " + std::to_string(j * kIdlePollMs) + "ms wait");
          }
          break;  // Axis is idle, proceed to next axis
        }
      } else {
        // State query failed, proceed anyway and let acsc_ToPointM handle it
        LOG_DEBUG("Motor state query failed for " + axisName + ", proceeding anyway");
        isMoving = false;
        break;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(kIdlePollMs));
    }

    if (isMoving) {
      m_lastErrorCode = -3;
      m_lastErrorMessage = "Timeout waiting for axis " + axisName + " to become idle (waited " +
                           std::to_string(kMaxIdleIter * kIdlePollMs) + "ms)";
      LOG_ERROR(m_lastErrorMessage);
      return false;
    }
  }

  std::vector<int> axesArray(axisIndices.size() + 1);
  for (size_t i = 0; i < axisIndices.size(); i++) {
    axesArray[i] = axisIndices[i];
  }
  axesArray[axisIndices.size()] = -1;

  LOG_DEBUG("Calling acsc_ToPointM for multi-axis move");

  if (!acsc_ToPointM(m_controllerId, ACSC_AMF_WAIT, axesArray.data(),
    const_cast<double*>(positions.data()), NULL)) {
    m_lastErrorCode = acsc_GetLastError();
    m_lastErrorMessage = "acsc_ToPointM failed: error=" + std::to_string(m_lastErrorCode);
    LOG_ERROR("acsc_ToPointM failed. Error code: " + std::to_string(m_lastErrorCode));
    return false;
  }
  LOG_DEBUG("acsc_ToPointM succeeded");

  LOG_DEBUG("Calling acsc_GoM for multi-axis move");
  if (!acsc_GoM(m_controllerId, axesArray.data(), NULL)) {
    m_lastErrorCode = acsc_GetLastError();
    m_lastErrorMessage = "acsc_GoM failed: error=" + std::to_string(m_lastErrorCode);
    LOG_ERROR("acsc_GoM failed. Error code: " + std::to_string(m_lastErrorCode));
    return false;
  }
  LOG_DEBUG("acsc_GoM succeeded");

  if (blocking) {
    LOG_DEBUG("Waiting for motion completion on all axes");
    return WaitForMultiAxisCompletion(axes, 30.0, positions);
  }

  LOG_DEBUG("MoveToPositionMultiAxis END - success");
  return true;
}

void ACSController::CheckInPosition(const std::string& axis, int axisIndex) {
  int motorState = 0;
  if (acsc_GetMotorState(m_controllerId, axisIndex, &motorState, NULL)) {
    bool isMoving = (motorState & ACSC_MST_MOVE) != 0;
    bool isInPos  = (motorState & ACSC_MST_INPOS) != 0;

    if (!isMoving && !isInPos) {
      std::ostringstream oss;
      oss << "Axis " << axis << " stopped but INPOS flag not set (motor state=0x"
          << std::hex << motorState << ") - motor may not have reached target";
      LOG_WARNING(oss.str());
    }
  }
}

void ACSController::VerifyFinalPosition(const std::string& axis, int axisIndex, double targetPosition) {
  // Skip verification if target is NaN (e.g. HomeAxis calls)
  if (std::isnan(targetPosition)) {
    return;
  }

  double actualPosition = 0.0;
  if (acsc_GetFPosition(m_controllerId, axisIndex, &actualPosition, NULL)) {
    double error = std::abs(actualPosition - targetPosition);
    if (error > kPositionToleranceMm) {
      LOG_WARNING("Axis " + axis + " position error: target=" + std::to_string(targetPosition) +
                  " actual=" + std::to_string(actualPosition) +
                  " error=" + std::to_string(error) + "mm (tolerance=" +
                  std::to_string(kPositionToleranceMm) + "mm)");
    }
  }
}

bool ACSController::RunBuffer(int bufferNumber, const std::string& labelName) {
  if (!m_isConnected) {
    LOG_ERROR("Cannot run buffer - not connected");
    return false;
  }

  if (bufferNumber < 0 || bufferNumber > 63) {
    LOG_ERROR("Invalid buffer number " + std::to_string(bufferNumber));
    return false;
  }

  char* labelPtr = nullptr;
  char labelBuffer[256] = { 0 };

  if (!labelName.empty()) {
    std::string upperLabel = labelName;
    std::transform(upperLabel.begin(), upperLabel.end(), upperLabel.begin(), ::toupper);

    if (upperLabel[0] != '_' && (upperLabel[0] < 'A' || upperLabel[0] > 'Z')) {
      LOG_ERROR("Invalid label name: " + labelName);
      return false;
    }

    strncpy_s(labelBuffer, sizeof(labelBuffer), upperLabel.c_str(), _TRUNCATE);
    labelPtr = labelBuffer;

    LOG_INFO("Running buffer " + std::to_string(bufferNumber) + " from label " + labelName);
  }
  else {
    LOG_INFO("Running buffer " + std::to_string(bufferNumber));
  }

  if (!acsc_RunBuffer(m_controllerId, bufferNumber, labelPtr, ACSC_SYNCHRONOUS)) {
    int error = acsc_GetLastError();
    LOG_ERROR("Failed to run buffer. Error: " + std::to_string(error));
    return false;
  }

  return true;
}

bool ACSController::StopBuffer(int bufferNumber) {
  if (!m_isConnected) {
    LOG_ERROR("Cannot stop buffer - not connected");
    return false;
  }

  if (bufferNumber < 0 || bufferNumber > 63) {
    LOG_ERROR("Invalid buffer number");
    return false;
  }

  LOG_INFO("Stopping buffer " + std::to_string(bufferNumber));

  if (!acsc_StopBuffer(m_controllerId, bufferNumber, ACSC_SYNCHRONOUS)) {
    int error = acsc_GetLastError();
    LOG_ERROR("Failed to stop buffer. Error: " + std::to_string(error));
    return false;
  }

  return true;
}

bool ACSController::StopAllBuffers() {
  if (!m_isConnected) {
    LOG_ERROR("Cannot stop buffers - not connected");
    return false;
  }

  LOG_INFO("Stopping all buffers");

  if (!acsc_StopBuffer(m_controllerId, ACSC_NONE, ACSC_SYNCHRONOUS)) {
    int error = acsc_GetLastError();
    LOG_ERROR("Failed to stop all buffers. Error: " + std::to_string(error));
    return false;
  }

  return true;
}