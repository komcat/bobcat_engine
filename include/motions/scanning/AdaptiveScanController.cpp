// src/scanning/AdaptiveScanController.cpp
#include "AdaptiveScanController.h"
#include "ScanStrategy.h"
#include <sstream>
#include <iomanip>

AdaptiveScanController::AdaptiveScanController(
  UnifiedMotionManager& motionManager,
  DataStore& dataStore,
  const std::string& deviceName,
  const std::string& dataChannel)
  : m_motionManager(motionManager)
  , m_dataStore(dataStore)
  , m_deviceName(deviceName)
  , m_dataChannel(dataChannel)
  , m_state(ScanState::Idle)
  , m_logger(nullptr)  // ✅ Initialize to nullptr
  , m_haltRequested(false)
  , m_emergencyStopRequested(false)
{
  // ✅ Check before use
  if (m_logger) {
    m_logger->LogInfo("AdaptiveScanController created for device: " + deviceName);
  }

  m_stateMachine = std::make_unique<ScanStateMachine>();

  // Set state change callback
  m_stateMachine->SetStateChangeCallback(
    [this](ScanState oldState, ScanState newState) {
    m_state = newState;
    if (m_stateCallback) {
      m_stateCallback(oldState, newState);
    }
    if (m_logger) {  // ✅ Check before use
      m_logger->LogInfo("State changed: " + ToString(oldState) + " -> " + ToString(newState));
    }
  });
}

AdaptiveScanController::~AdaptiveScanController() {
  HaltScan();

  if (m_scanThread.joinable()) {
    m_scanThread.join();
  }

  if (m_logger) {  // ✅ Check before use
    m_logger->LogInfo("AdaptiveScanController destroyed for device: " + m_deviceName);
  }
}

// ============================================================================
// SCAN CONTROL
// ============================================================================

bool AdaptiveScanController::StartScan(
  const ScanParameters& params,
  std::unique_ptr<IScanStrategy> strategy) {

  // Check if already scanning
  if (m_state != ScanState::Idle) {
    if (m_logger) {  // ✅ Check before use
      m_logger->LogWarning("Cannot start scan - already active");
    }
    return false;
  }

  // Validate parameters
  std::string validationError;
  if (!ValidateParameters(params, validationError)) {
    NotifyError("Invalid parameters: " + validationError,
      ScanErrorType::InvalidParameters);
    return false;
  }

  // Validate device ready
  std::string deviceError;
  if (!ValidateDeviceReady(deviceError)) {
    NotifyError("Device not ready: " + deviceError,
      ScanErrorType::DeviceNotReady);
    return false;
  }

  // Store parameters
  m_parameters = params;

  // Set strategy (default to CoarseToFine if not provided)
  if (strategy) {
    m_strategy = std::move(strategy);
  }
  else {
    m_strategy = ScanStrategyFactory::CreateCoarseToFine();
  }

  if (m_logger) {  // ✅ Check before use
    m_logger->LogInfo("Starting scan with strategy: " + m_strategy->GetName());
  }

  // Create new data collector
  m_dataCollector = std::make_unique<ScanDataCollector>(m_deviceName);

  // Reset control flags
  m_haltRequested = false;
  m_emergencyStopRequested = false;

  // Start scan thread
  m_scanThread = std::thread(&AdaptiveScanController::ScanThreadFunction, this);

  return true;
}

void AdaptiveScanController::HaltScan() {
  if (m_state == ScanState::Idle) {
    return;
  }

  if (m_logger) {  // ✅ Check before use
    m_logger->LogInfo("Halt requested for device: " + m_deviceName);
  }
  m_haltRequested = true;

  if (m_scanThread.joinable()) {
    m_scanThread.join();
  }
}

void AdaptiveScanController::EmergencyStop() {
  if (m_logger) {  // ✅ Check before use
    m_logger->LogWarning("EMERGENCY STOP requested for device: " + m_deviceName);
  }
  m_emergencyStopRequested = true;
  m_haltRequested = true;

  // Stop motion immediately
  m_motionManager.EmergencyStop(m_deviceName);

  if (m_scanThread.joinable()) {
    m_scanThread.join();
  }
}

// ============================================================================
// SCAN EXECUTION
// ============================================================================

void AdaptiveScanController::ScanThreadFunction() {
  try {
    if (m_logger) {  // ✅ Check before use
      m_logger->LogInfo("Scan thread started");
    }

    // Initialize
    if (!m_stateMachine->TransitionTo(ScanState::Initializing)) {
      throw std::runtime_error("Failed to transition to Initializing state");
    }

    // Execute scan
    bool success = ExecuteScan();

    if (success && !m_haltRequested && !m_emergencyStopRequested) {
      HandleScanCompletion(true);
    }
    else if (m_emergencyStopRequested) {
      m_stateMachine->TransitionTo(ScanState::Error);
      HandleScanCompletion(false);
    }
    else {
      m_stateMachine->TransitionTo(ScanState::Halted);
      HandleScanCompletion(false);
    }

  }
  catch (const std::exception& e) {
    if (m_logger) {  // ✅ Check before use
      m_logger->LogError("Scan thread exception: " + std::string(e.what()));
    }
    HandleScanError(e.what());
  }
}

bool AdaptiveScanController::ExecuteScan() {
  // Record baseline
  if (!RecordBaseline()) {
    return false;
  }

  if (m_haltRequested || m_emergencyStopRequested) {
    return false;
  }

  // Execute strategy
  if (!ExecuteScanStrategy()) {
    return false;
  }

  if (m_haltRequested || m_emergencyStopRequested) {
    return false;
  }

  // Return to best position
  if (!ReturnToBestPosition()) {
    if (m_logger) {  // ✅ Check before use
      m_logger->LogWarning("Failed to return to best position");
    }
  }

  return true;
}

bool AdaptiveScanController::RecordBaseline() {
  if (!m_stateMachine->TransitionTo(ScanState::RecordingBaseline)) {
    return false;
  }

  if (m_logger) {  // ✅ Check before use
    m_logger->LogInfo("Recording baseline");
  }
  NotifyProgress(0.0, "Recording baseline");

  // Get current position
  PositionStruct currentPos = GetCurrentPosition();

  // Get averaged measurement
  double baselineValue = GetAveragedMeasurement(m_parameters.averagingCount);

  // Record in data collector
  m_dataCollector->RecordBaseline(baselineValue, currentPos);

  std::stringstream ss;
  ss << "Baseline: " << std::fixed << std::setprecision(6) << baselineValue
    << " at (" << currentPos.x << ", " << currentPos.y << ", " << currentPos.z << ")";

  if (m_logger) {  // ✅ Check before use
    m_logger->LogInfo(ss.str());
  }

  return true;
}

bool AdaptiveScanController::ExecuteScanStrategy() {
  if (!m_stateMachine->TransitionTo(ScanState::Scanning)) {
    return false;
  }

  if (m_logger) {  // ✅ Check before use
    m_logger->LogInfo("Executing scan strategy: " + m_strategy->GetName());
  }

  // Create scan context
  ScanContext context(
    m_motionManager,
    m_dataStore,
    *m_dataCollector,
    m_deviceName,
    m_dataChannel,
    m_parameters);

  // Set up context callbacks
  context.SetProgressCallback(
    [this](double progress, const std::string& message) {
    NotifyProgress(progress, message);
  });

  context.SetDataPointCallback(
    [this](const ScanDataPoint& dataPoint) {
    NotifyDataPoint(dataPoint);
  });

  context.SetPeakFoundCallback(
    [this](const PeakInfo& peak) {
    NotifyPeakFound(peak);
  });

  // Set control flags
  context.SetHaltFlag(&m_haltRequested);
  context.SetEmergencyStopFlag(&m_emergencyStopRequested);

  // Execute strategy
  bool success = m_strategy->Execute(context);

  return success;
}

bool AdaptiveScanController::ReturnToBestPosition() {
  if (!m_stateMachine->TransitionTo(ScanState::ReturningToPeak)) {
    return false;
  }

  PeakInfo peak = m_dataCollector->GetPeakPosition();

  if (!peak.valid) {
    if (m_logger) {  // ✅ Check before use
      m_logger->LogWarning("No valid peak found - staying at current position");
    }
    return true;
  }

  if (m_logger) {  // ✅ Check before use
    m_logger->LogInfo("Returning to global peak position");
  }
  NotifyProgress(0.95, "Returning to best position");

  bool success = MoveToPosition(peak.position);

  if (success) {
    // Verify position
    double verificationValue = GetMeasurement();
    std::stringstream ss;
    ss << "Returned to peak - Verification value: "
      << std::fixed << std::setprecision(6) << verificationValue;

    if (m_logger) {  // ✅ Check before use
      m_logger->LogInfo(ss.str());
    }
  }

  return success;
}

void AdaptiveScanController::HandleScanCompletion(bool success) {
  if (success) {
    m_stateMachine->TransitionTo(ScanState::Completed);
    NotifyProgress(1.0, "Scan completed successfully");
  }

  // Save results
  if (!m_dataCollector->SaveResults()) {
    if (m_logger) {  // ✅ Check before use
      m_logger->LogWarning("Failed to save scan results");
    }
  }

  // Notify completion
  if (m_completionCallback) {
    m_completionCallback(GetResults());
  }

  if (m_logger) {  // ✅ Check before use
    m_logger->LogInfo("Scan " + std::string(success ? "completed" : "finished with errors"));
  }
}

void AdaptiveScanController::HandleScanError(const std::string& message) {
  m_stateMachine->TransitionTo(ScanState::Error);
  NotifyError(message, ScanErrorType::Unknown);

  if (m_dataCollector) {
    m_dataCollector->SaveResults();
  }
}

// ============================================================================
// NOTIFICATION METHODS
// ============================================================================

void AdaptiveScanController::NotifyProgress(double progress, const std::string& message) {
  if (m_progressCallback) {
    ScanProgressEvent event;
    event.progress = progress;
    event.message = message;
    event.state = m_state;
    event.measurementCount = m_dataCollector ? m_dataCollector->GetMeasurementCount() : 0;
    m_progressCallback(event);
  }
}

void AdaptiveScanController::NotifyDataPoint(const ScanDataPoint& dataPoint) {
  if (m_dataPointCallback) {
    m_dataPointCallback(dataPoint);
  }
}

void AdaptiveScanController::NotifyPeakFound(const PeakInfo& peak) {
  if (m_peakFoundCallback) {
    m_peakFoundCallback(peak);
  }
}

void AdaptiveScanController::NotifyError(const std::string& message, ScanErrorType type) {
  if (m_errorCallback) {
    ScanError error;
    error.type = type;
    error.message = message;
    error.timestamp = std::chrono::system_clock::now();
    m_errorCallback(error);
  }

  if (m_logger) {  // ✅ Check before use
    m_logger->LogError(message);
  }
}

// ============================================================================
// MOTION HELPERS
// ============================================================================

bool AdaptiveScanController::MoveToPosition(const PositionStruct& position) {
  return m_motionManager.MoveToAbs(
    m_deviceName,
    position.x, position.y, position.z,
    m_parameters.velocityPercent,
    true);  // blocking
}

bool AdaptiveScanController::MoveRelative(const std::string& axis, double distance) {
  return m_motionManager.MoveRelative(m_deviceName, axis, distance, true);
}

bool AdaptiveScanController::WaitForMotionComplete() {
  // Motion manager uses blocking calls, so this is implicit
  return true;
}

// ============================================================================
// MEASUREMENT HELPERS
// ============================================================================

double AdaptiveScanController::GetMeasurement() {
  auto deadline = std::chrono::system_clock::now() + m_parameters.measurementTimeout;

  while (std::chrono::system_clock::now() < deadline && !m_haltRequested) {
    float value;
    if (m_dataStore.TryGetValue(m_dataChannel, value)) {
      if (value >= m_parameters.minValidValue &&
        value <= m_parameters.maxValidValue) {
        return static_cast<double>(value);
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  if (m_haltRequested) {
    throw std::runtime_error("Scan halted while waiting for measurement");
  }

  throw std::runtime_error("Measurement timeout");
}

double AdaptiveScanController::GetAveragedMeasurement(int numSamples) {
  double sum = 0.0;

  for (int i = 0; i < numSamples; i++) {
    sum += GetMeasurement();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  return sum / numSamples;
}

bool AdaptiveScanController::WaitForValidMeasurement(double& outValue) {
  try {
    outValue = GetMeasurement();
    return true;
  }
  catch (...) {
    return false;
  }
}

// ============================================================================
// QUERY METHODS
// ============================================================================

BaselineInfo AdaptiveScanController::GetBaseline() const {
  if (m_dataCollector) {
    return m_dataCollector->GetBaseline();
  }
  return BaselineInfo{};
}

PeakInfo AdaptiveScanController::GetGlobalPeak() const {
  if (m_dataCollector) {
    return m_dataCollector->GetPeakPosition();
  }
  return PeakInfo{};
}

double AdaptiveScanController::GetProgress() const {
  // Calculate based on state and measurements
  if (!m_dataCollector) {
    return 0.0;
  }

  // Estimate based on strategy
  int totalSteps = m_strategy ?
    m_strategy->EstimateTotalSteps(m_parameters) : 100;
  int currentSteps = m_dataCollector->GetMeasurementCount();

  return std::min(1.0, static_cast<double>(currentSteps) / totalSteps);
}

ScanResults AdaptiveScanController::GetResults() const {
  if (m_dataCollector) {
    return m_dataCollector->GetResults();
  }
  return ScanResults{};
}

PositionStruct AdaptiveScanController::GetCurrentPosition() const {
  return m_motionManager.GetCurrentPosition(m_deviceName);
}

// ============================================================================
// VALIDATION
// ============================================================================

bool AdaptiveScanController::ValidateParameters(
  const ScanParameters& params,
  std::string& error) {
  return params.Validate(error);
}

bool AdaptiveScanController::ValidateDeviceReady(std::string& error) {
  if (!m_motionManager.HasDevice(m_deviceName)) {
    error = "Device not found: " + m_deviceName;
    return false;
  }

  // Check if device is connected (implementation depends on your manager)
  auto connectionStatus = m_motionManager.GetConnectionStatus();
  auto it = connectionStatus.find(m_deviceName);

  if (it == connectionStatus.end() || !it->second) {
    error = "Device not connected: " + m_deviceName;
    return false;
  }

  // Check if data channel exists
  if (!m_dataStore.HasValue(m_dataChannel)) {
    if (m_logger) {  // ✅ Check before use
      m_logger->LogWarning("Data channel '" + m_dataChannel +
        "' not yet initialized - will wait for first value");
    }
  }

  return true;
}