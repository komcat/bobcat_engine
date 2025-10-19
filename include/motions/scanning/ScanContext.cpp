// src/scanning/ScanContext.cpp
#include "ScanContext.h"
#include <thread>
#include <cmath>

ScanContext::ScanContext(
  UnifiedMotionManager& motionManager,
  DataStore& dataStore,
  ScanDataCollector& dataCollector,
  const std::string& deviceName,
  const std::string& dataChannel,
  const ScanParameters& parameters)
  : m_motionManager(motionManager)
  , m_dataStore(dataStore)
  , m_dataCollector(dataCollector)
  , m_deviceName(deviceName)
  , m_dataChannel(dataChannel)
  , m_parameters(parameters)
{
}

// ============================================================================
// MOTION CONTROL
// ============================================================================

bool ScanContext::MoveToPosition(const PositionStruct& position) {
  return m_motionManager.MoveToAbs(
    m_deviceName,
    position.x, position.y, position.z,
    m_parameters.velocityPercent,
    true);  // blocking
}

bool ScanContext::MoveRelative(const std::string& axis, double distance) {
  return m_motionManager.MoveRelative(m_deviceName, axis, distance, true);
}

PositionStruct ScanContext::GetCurrentPosition() const {
  return m_motionManager.GetCurrentPosition(m_deviceName);
}

bool ScanContext::IsMoving() const {
  return m_motionManager.IsDeviceInMotion(m_deviceName);
}

bool ScanContext::WaitForMotionComplete(std::chrono::milliseconds timeout) {
  auto deadline = std::chrono::system_clock::now() + timeout;

  while (IsMoving() && std::chrono::system_clock::now() < deadline) {
    if (IsHaltRequested() || IsEmergencyStopRequested()) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  return !IsMoving();
}

// ============================================================================
// MEASUREMENTS
// ============================================================================

double ScanContext::GetMeasurement() {
  auto deadline = std::chrono::system_clock::now() + m_parameters.measurementTimeout;

  while (std::chrono::system_clock::now() < deadline) {
    if (IsHaltRequested() || IsEmergencyStopRequested()) {
      throw std::runtime_error("Measurement interrupted by halt/stop");
    }

    float value;
    if (m_dataStore.TryGetValue(m_dataChannel, value)) {
      if (ValidateMeasurement(static_cast<double>(value))) {
        return static_cast<double>(value);
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  throw std::runtime_error("Measurement timeout");
}

double ScanContext::GetAveragedMeasurement(int numSamples) {
  double sum = 0.0;

  for (int i = 0; i < numSamples; i++) {
    if (IsHaltRequested() || IsEmergencyStopRequested()) {
      throw std::runtime_error("Averaging interrupted");
    }

    sum += GetMeasurement();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  return sum / numSamples;
}

bool ScanContext::WaitForValidMeasurement(double& outValue) {
  try {
    outValue = GetMeasurement();
    return true;
  }
  catch (...) {
    return false;
  }
}

// ============================================================================
// DATA COLLECTION
// ============================================================================

void ScanContext::RecordDataPoint(const ScanDataPoint& dataPoint) {
  // Convert to legacy format for data collector
  m_dataCollector.RecordMeasurement(
    dataPoint.value,
    dataPoint.position,
    dataPoint.axis,
    dataPoint.stepSize,
    dataPoint.direction);

  // Notify callback
  if (m_dataPointCallback) {
    m_dataPointCallback(dataPoint);
  }
}

bool ScanContext::UpdateGlobalPeak(
  double value,
  const PositionStruct& position,
  const std::string& context) {

  bool isNewPeak = false;

  if (!m_globalPeak.valid || value > m_globalPeak.value) {
    m_globalPeak.value = value;
    m_globalPeak.position = position;
    m_globalPeak.timestamp = std::chrono::system_clock::now();
    m_globalPeak.context = context;
    m_globalPeak.valid = true;
    isNewPeak = true;

    // Notify callback
    if (m_peakFoundCallback) {
      m_peakFoundCallback(m_globalPeak);
    }
  }

  return isNewPeak;
}

PeakInfo ScanContext::GetGlobalPeak() const {
  return m_globalPeak;
}

BaselineInfo ScanContext::GetBaseline() const {
  return m_baseline;
}

// ============================================================================
// PROGRESS & CONTROL
// ============================================================================

void ScanContext::ReportProgress(double progress, const std::string& message) {
  if (m_progressCallback) {
    m_progressCallback(progress, message);
  }
}

bool ScanContext::IsHaltRequested() const {
  return m_haltRequested && m_haltRequested->load();
}

bool ScanContext::IsEmergencyStopRequested() const {
  return m_emergencyStopRequested && m_emergencyStopRequested->load();
}

bool ScanContext::SleepInterruptible(std::chrono::milliseconds duration) {
  auto deadline = std::chrono::system_clock::now() + duration;

  while (std::chrono::system_clock::now() < deadline) {
    if (IsHaltRequested() || IsEmergencyStopRequested()) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  return true;
}

// ============================================================================
// HELPER METHODS
// ============================================================================

bool ScanContext::ValidateMeasurement(double value) const {
  return value >= m_parameters.minValidValue &&
    value <= m_parameters.maxValidValue &&
    std::isfinite(value);
}

void ScanContext::WaitForSettling() {
  SleepInterruptible(std::chrono::milliseconds(m_parameters.motionSettleTimeMs));
}