// src/scanning/ScanContext.cpp
#include "ScanContext.h"
#include <thread>
#include <cmath>
#include <iostream>
#include <sstream>
#include <iomanip>

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
      // Return 0.0 to indicate failure - caller should check halt flags
      return 0.0;
    }

    float value;
    if (m_dataStore.TryGetValue(m_dataChannel, value)) {
      if (ValidateMeasurement(static_cast<double>(value))) {
        return static_cast<double>(value);
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // Return 0.0 to indicate timeout - caller should check halt flags
  return 0.0;
}

double ScanContext::GetFreshMeasurement() {
  const auto& params = m_parameters;

  // Trim disabled - fall back to the original blind settle sleep.
  if (params.freshSamples <= 0) {
    if (!SleepInterruptible(std::chrono::milliseconds(params.motionSettleTimeMs))) {
      return 0.0;
    }
    return GetMeasurement();
  }

  // Snapshot the publish counter BEFORE waiting. Value and counter come from
  // the same lock, so the counter we compare against is exactly the one that
  // produced the sample present at the moment the move finished.
  float ignoredValue = 0.0f;
  uint64_t startCount = 0;
  m_dataStore.TryGetValue(m_dataChannel, ignoredValue, startCount);

  // Precautionary floor for the analogue chain in front of the instrument
  // (photodiode + TIA), which the publish counter cannot see.
  if (params.settleMinMs > 0) {
    if (!SleepInterruptible(std::chrono::milliseconds(params.settleMinMs))) {
      return 0.0;
    }
  }

  const uint64_t targetCount = startCount + static_cast<uint64_t>(params.freshSamples);
  const int pollMs = 10;
  const int timeoutMs = (params.freshSampleTimeoutMs > 0) ? params.freshSampleTimeoutMs : 300;
  const int maxIterations = (timeoutMs / pollMs) + 2;  // bounded, never while(true)

  auto gateStart = std::chrono::steady_clock::now();
  bool gotFresh = false;

  for (int iteration = 0; iteration < maxIterations; ++iteration) {
    if (IsHaltRequested() || IsEmergencyStopRequested()) {
      return 0.0;
    }

    float value = 0.0f;
    uint64_t count = 0;
    if (m_dataStore.TryGetValue(m_dataChannel, value, count) && count >= targetCount) {
      gotFresh = true;
      break;
    }

    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - gateStart).count();

    if (elapsedMs >= timeoutMs) {
      break;
    }

    if (!SleepInterruptible(std::chrono::milliseconds(pollMs))) {
      return 0.0;
    }
  }

  if (!gotFresh && !m_freshGateWarned) {
    // Degrade to "use the latest value" - the same behaviour as before this
    // gate existed. Warn once so a genuinely stalled stream is visible.
    m_freshGateWarned = true;
    std::cout << "[ScanContext] Fresh-sample gate timed out on channel '"
      << m_dataChannel << "' after " << timeoutMs
      << " ms - falling back to latest value. Is the instrument still polling?"
      << std::endl;
  }

  return GetMeasurement();
}

void ScanContext::TraceSample(const std::string& phase,
  const std::string& axis,
  int direction,
  double stepSize,
  const PositionStruct& position,
  double value,
  const std::string& note) {

  if (!m_parameters.traceEnabled) {
    return;
  }

  const double elapsedSec = std::chrono::duration<double>(
    std::chrono::steady_clock::now() - m_traceStart).count();

  const double dv = m_haveLastTraceValue ? (value - m_lastTraceValue) : 0.0;

  std::ostringstream oss;
  oss << "xcan," << (++m_traceSeq)
    << ',' << std::fixed << std::setprecision(3) << elapsedSec
    << ',' << phase
    << ',' << axis
    << ',' << direction
    << ',' << std::setprecision(3) << (stepSize * 1000.0)
    << ",x=" << std::setprecision(6) << position.x
    << ",y=" << position.y
    << ",z=" << position.z
    << ",v=" << std::scientific << std::setprecision(6) << value
    << ",dv=" << dv
    << ',' << note;

  std::cout << oss.str() << std::endl;

  m_lastTraceValue = value;
  m_haveLastTraceValue = true;
}

double ScanContext::GetAveragedMeasurement(int numSamples) {
  double sum = 0.0;

  for (int i = 0; i < numSamples; i++) {
    if (IsHaltRequested() || IsEmergencyStopRequested()) {
      // Return 0.0 to indicate failure - caller should check halt flags
      return 0.0;
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