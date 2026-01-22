// acs_controller.h
#pragma once

// Prevent Windows.h conflicts
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <map>
#include "MotionTypes.h"

// Include ACS controller library
#include "ACSC_wrapper.h"

class ACSController {
public:
  ACSController();
  ~ACSController();

  // Connection methods
  bool Connect(const std::string& ipAddress, int port = ACSC_SOCKET_STREAM_PORT);
  void Disconnect();
  bool IsConnected() const { return m_isConnected; }

  // Basic motion commands
  bool MoveToPosition(const std::string& axis, double position, bool blocking = true);
  bool MoveRelative(const std::string& axis, double distance, bool blocking = true);
  bool MoveToAbsolutePosition(double x, double y, double z, double velocity, bool blocking = true);
  bool MoveToPositionMultiAxis(const std::vector<std::string>& axes,
    const std::vector<double>& positions,
    bool blocking = true);
  bool HomeAxis(const std::string& axis);
  bool StopAxis(const std::string& axis);
  bool StopAllAxes();
  bool StopAllMotion();

  // Status methods
  bool IsMoving(const std::string& axis);
  bool IsInMotion();
  bool GetPosition(const std::string& axis, double& position);
  float GetAxisPositionFloat(const std::string& axis);
  bool GetPositions(std::map<std::string, double>& positions);
  PositionStruct GetCurrentPosition();

  // Servo control
  bool EnableServo(const std::string& axis, bool enable);
  bool IsServoEnabled(const std::string& axis, bool& enabled);

  // Motion configuration
  bool SetVelocity(const std::string& axis, double velocity);
  bool GetVelocity(const std::string& axis, double& velocity);

  // Configuration from MotionDevice
  bool ConfigureFromDevice(const MotionDevice& device);

  // Moving to named positions from MotionTypes
  bool MoveToNamedPosition(const std::string& deviceName, const std::string& positionName);

  // Helper methods
  bool WaitForMotionCompletion(const std::string& axis, double timeoutSeconds = 30.0);

  // Buffer program control
  bool RunBuffer(int bufferNumber, const std::string& labelName = "");
  bool StopBuffer(int bufferNumber);
  bool StopAllBuffers();

  // Expose available axes
  const std::vector<std::string>& GetAvailableAxes() const { return m_availableAxes; }

  // Error tracking for debugging
  int GetLastErrorCode() const { return m_lastErrorCode; }
  std::string GetLastErrorMessage() const { return m_lastErrorMessage; }

  // Communication thread control
  void StartCommunicationThread();
  void StopCommunicationThread();

private:
  // Communication thread function
  void CommunicationThreadFunc();

  // Helper methods
  void ProcessCommandQueue();
  void UpdatePositions();
  void UpdateMotorStatus();
  bool StartMotion(const std::string& axis);
  int GetAxisIndex(const std::string& axis);

  // Command queue structure
  struct MotorCommand {
    std::string axis;
    double distance;
    bool executed;
  };

  // Thread-related members
  std::thread m_communicationThread;
  std::mutex m_mutex;
  std::condition_variable m_condVar;
  std::atomic<bool> m_threadRunning{ false };
  std::atomic<bool> m_terminateThread{ false };
  std::atomic<bool> m_isConnected{ false };

  // Device info
  std::string m_deviceName;

  // Command queue
  std::vector<MotorCommand> m_commandQueue;
  std::mutex m_commandMutex;

  // Controller handle
  HANDLE m_controllerId;

  // Configuration
  std::string m_ipAddress;
  int m_port;
  std::vector<std::string> m_availableAxes;

  // Status monitoring (cached values)
  std::map<std::string, double> m_axisPositions;
  std::map<std::string, bool> m_axisMoving;
  std::map<std::string, bool> m_axisServoEnabled;

  // Cache timing
  std::chrono::steady_clock::time_point m_lastStatusUpdate;
  std::chrono::steady_clock::time_point m_lastPositionUpdate;
  const int m_statusUpdateInterval = 200;  // milliseconds

  // Debug flag
  bool m_enableDebug = false;

  // Error tracking
  mutable int m_lastErrorCode = 0;
  mutable std::string m_lastErrorMessage;
};