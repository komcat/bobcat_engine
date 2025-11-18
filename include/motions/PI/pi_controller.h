// pi_controller.h
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

// Include PI GCS2 library
#include "PI_GCS2_DLL.h"

class PIController {
public:
  PIController();
  ~PIController();

  // Debug control
  void SetDebugVerbose(bool enabled) { m_debugVerbose = enabled; }
  bool GetDebugVerbose() const { return m_debugVerbose; }

  // Connection methods
  bool Connect(const std::string& ipAddress, int port = 50000);
  void Disconnect();
  bool IsConnected() const { return m_isConnected; }

  // Basic motion commands
  bool MoveToPosition(const std::string& axis, double position, bool blocking = true);
  bool MoveRelative(const std::string& axis, double distance, bool blocking = true);
  bool HomeAxis(const std::string& axis);
  bool StopAxis(const std::string& axis);
  bool StopAllAxes();

  // Status methods
  bool IsMoving(const std::string& axis);
  bool GetPosition(const std::string& axis, double& position);
  bool GetPositions(std::map<std::string, double>& positions);
  float GetAxisPositionFloat(const std::string& axis);

  // Servo control
  bool EnableServo(const std::string& axis, bool enable);
  bool IsServoEnabled(const std::string& axis, bool& enabled);

  // Motion configuration
  bool SetVelocity(const std::string& axis, double velocity);
  bool GetVelocity(const std::string& axis, double& velocity);
  bool SetSystemVelocity(double velocity);
  bool GetSystemVelocity(double& velocity);

  // Configuration
  bool ConfigureFromDevice(const MotionDevice& device);
  bool MoveToNamedPosition(const std::string& deviceName, const std::string& positionName);

  // Helper methods
  bool WaitForMotionCompletion(const std::string& axis, double timeoutSeconds = 30.0);

  // Multi-axis moves
  bool MoveToPositionAll(double x, double y, double z, double u, double v, double w, bool blocking = true);
  bool MoveToPositionMultiAxis(const std::vector<std::string>& axes,
    const std::vector<double>& positions,
    bool blocking = true);

  // Add to public section, around line 40 (after Status methods section)
  bool IsInMotion();
  PositionStruct GetCurrentPosition();

  // Home functions
  bool Home(const std::string& axis);
  bool HomeAll();
  bool HomeAxes(const std::vector<std::string>& axes);
  bool HomeAxes(const std::string& axesString);
  bool DefineHome(const std::string& axis);
  bool DefineHomeAll();

  // Scanning functions
  bool FSA(const std::string& axis1, double length1, const std::string& axis2, double length2,
    double threshold, double distance, double alignStep, int analogInput);
  bool FSC(const std::string& axis1, double length1, const std::string& axis2, double length2,
    double threshold, double distance, int analogInput);
  bool FSM(const std::string& axis1, double length1, const std::string& axis2, double length2,
    double threshold, double distance, int analogInput);

  // Analog reading
  bool GetAnalogChannelCount(int& numChannels);
  bool GetAnalogVoltage(int channel, double& voltage);
  bool GetAnalogVoltages(std::vector<int> channels, std::map<int, double>& voltages);
  void EnableAnalogReading(bool enable) { m_enableAnalogReading = enable; }
  bool IsAnalogReadingEnabled() const { return m_enableAnalogReading; }

  // Utility
  int GetControllerId() const { return m_controllerId; }
  const std::vector<std::string>& GetAvailableAxes() const { return m_availableAxes; }

  // Pivot point functions
  bool GetPivotPoint(double& pivotX, double& pivotY, double& pivotZ);
  bool SetPivotPoint(double pivotX, double pivotY, double pivotZ);

  // Utility for finding correct parameter IDs
  bool QueryAvailableParameters(int itemId = 1);
  bool GetParameterValue(int itemId, unsigned int parameterId, double& value);
  bool SetParameterValue(int itemId, unsigned int parameterId, double value);

  // Communication thread control
  void StartCommunicationThread();
  void StopCommunicationThread();

private:
  // Communication thread
  void CommunicationThreadFunc();
  void UpdateAnalogReadings();
  void InitializeAnalogChannels();

  // Helper
  std::string AxesToString(const std::vector<std::string>& axes) const;

  // Debug flags
  bool m_debugVerbose = false;
  bool m_enableDebug = false;

  // Thread-related members
  std::thread m_communicationThread;
  std::mutex m_mutex;
  std::condition_variable m_condVar;
  std::atomic<bool> m_threadRunning{ false };
  std::atomic<bool> m_terminateThread{ false };
  std::atomic<bool> m_isConnected{ false };

  // Controller
  int m_controllerId;

  // Configuration
  std::string m_ipAddress;
  int m_port;
  std::string m_deviceName;
  std::vector<std::string> m_availableAxes;

  // Motion status (cached)
  std::map<std::string, double> m_axisPositions;
  std::map<std::string, bool> m_axisMoving;
  std::map<std::string, bool> m_axisServoEnabled;

  // Analog reading
  std::atomic<bool> m_enableAnalogReading{ true };
  int m_numAnalogChannels = 0;
  std::map<int, double> m_analogVoltages;
  std::vector<int> m_activeAnalogChannels = { 5, 6 };

  // Performance optimization
  std::chrono::steady_clock::time_point m_lastStatusUpdate;
  std::chrono::steady_clock::time_point m_lastPositionUpdate;
  const int m_statusUpdateInterval = 200;
};