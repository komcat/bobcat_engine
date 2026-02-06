// src/motions/scanning/RotationScanController.cpp
#include "RotationScanController.h"
#include <iostream>
#include <sstream>
#include <iomanip>

RotationScanController::RotationScanController(
  UnifiedMotionManager& motionManager,
  DataStore& dataStore,
  const std::string& deviceName,
  const std::string& dataChannel)
  : m_motionManager(motionManager)
  , m_dataStore(dataStore)
  , m_deviceName(deviceName)
  , m_dataChannel(dataChannel)
  , m_state(RotationScanState::Idle)
  , m_haltRequested(false)
  , m_emergencyStopRequested(false)
  , m_progress(0.0)
{
}

RotationScanController::~RotationScanController() {
  // Stop any active scan
  if (IsScanActive()) {
    EmergencyStop();
  }

  // Wait for thread to finish
  if (m_scanThread.joinable()) {
    m_scanThread.join();
  }
}

bool RotationScanController::StartScan(const RotationScanParameters& params) {
  // Check if already scanning
  if (IsScanActive()) {
    NotifyError("Scan already in progress");
    return false;
  }

  // Validate parameters
  std::string validationError;
  if (!ValidateParameters(params, validationError)) {
    NotifyError("Invalid parameters: " + validationError);
    return false;
  }

  // Validate device
  if (!ValidateDeviceReady(validationError)) {
    NotifyError("Device not ready: " + validationError);
    return false;
  }

  // Store parameters
  m_parameters = params;

  // Reset control flags
  m_haltRequested = false;
  m_emergencyStopRequested = false;
  m_progress = 0.0;

  // Initialize results
  m_results = RotationScanResults();
  m_results.deviceName = m_deviceName;
  m_results.rotationAxis = params.rotationAxis;
  m_results.startTime = std::chrono::system_clock::now();

  // Generate scan ID
  auto now = std::chrono::system_clock::now();
  auto time_t_now = std::chrono::system_clock::to_time_t(now);
  std::tm tm_buf{};
  localtime_s(&tm_buf, &time_t_now);
  std::stringstream ss;
  ss << "rotation_" << m_deviceName << "_" << std::put_time(&tm_buf, "%Y%m%d_%H%M%S");
  m_results.scanId = ss.str();

  // Create strategy
  m_strategy = std::make_unique<RotationScanStrategy>();

  // Create context
  m_context = std::make_unique<RotationScanContext>(
    m_motionManager, m_dataStore, m_deviceName, m_dataChannel, m_parameters);

  // Set up context callbacks
  m_context->SetHaltFlag(&m_haltRequested);
  m_context->SetEmergencyStopFlag(&m_emergencyStopRequested);

  m_context->SetProgressCallback([this](double progress, const std::string& message) {
    NotifyProgress(progress, message);
  });

  m_context->SetPeakFoundCallback([this](const PeakInfo& peak) {
    if (m_peakFoundCallback) {
      m_peakFoundCallback(peak);
    }
  });

  // Wait for any previous thread to finish
  if (m_scanThread.joinable()) {
    m_scanThread.join();
  }

  // Start scan thread
  m_scanThread = std::thread(&RotationScanController::ScanThreadFunction, this);

  return true;
}

void RotationScanController::HaltScan() {
  if (!IsScanActive()) {
    return;
  }

  std::cout << "[RotationScanController] Halt requested - will return to peak" << std::endl;
  m_haltRequested = true;

  // Wait for thread to finish (it will return to peak before stopping)
  if (m_scanThread.joinable()) {
    m_scanThread.join();
  }
}

void RotationScanController::EmergencyStop() {
  if (!IsScanActive()) {
    return;
  }

  std::cout << "[RotationScanController] Emergency stop requested" << std::endl;
  m_emergencyStopRequested = true;
  m_haltRequested = true;

  // Wait for thread
  if (m_scanThread.joinable()) {
    m_scanThread.join();
  }
}

BaselineInfo RotationScanController::GetBaseline() const {
  if (m_context) {
    return m_context->GetBaseline();
  }
  return BaselineInfo();
}

PeakInfo RotationScanController::GetGlobalPeak() const {
  if (m_context) {
    return m_context->GetGlobalPeak();
  }
  return PeakInfo();
}

double RotationScanController::GetProgress() const {
  return m_progress.load();
}

RotationScanResults RotationScanController::GetResults() const {
  return m_results;
}

// ============================================================================
// SCAN EXECUTION
// ============================================================================

void RotationScanController::ScanThreadFunction() {
  try {
    ChangeState(RotationScanState::Initializing);

    bool success = ExecuteScan();

    if (m_emergencyStopRequested) {
      ChangeState(RotationScanState::Halted);
      HandleScanCompletion(false);
    } else if (m_haltRequested) {
      // Return to peak before reporting halted
      ChangeState(RotationScanState::ReturningToPeak);
      PeakInfo peak = m_context->GetGlobalPeak();
      if (peak.valid) {
        m_context->MoveToPosition(peak.position);
      }
      ChangeState(RotationScanState::Halted);
      HandleScanCompletion(false);
    } else if (success) {
      ChangeState(RotationScanState::Completed);
      HandleScanCompletion(true);
    } else {
      ChangeState(RotationScanState::Error);
      HandleScanCompletion(false);
    }
  }
  catch (const std::exception& e) {
    HandleScanError(std::string("Exception: ") + e.what());
    ChangeState(RotationScanState::Error);
  }
  catch (...) {
    HandleScanError("Unknown exception occurred");
    ChangeState(RotationScanState::Error);
  }
}

bool RotationScanController::ExecuteScan() {
  if (!m_strategy || !m_context) {
    NotifyError("Strategy or context not initialized");
    return false;
  }

  // Log scan start
  std::stringstream startMsg;
  startMsg << "Starting rotation scan: " << ToString(m_parameters.rotationAxis)
           << ", step=" << m_parameters.rotationStep << "°"
           << ", threshold=" << (m_parameters.improvementThreshold * 100) << "%";

  if (m_logger) {
    m_logger->LogInfo(startMsg.str(), "RotationScan");
  }
  std::cout << "[RotationScanController] " << startMsg.str() << std::endl;

  // Execute the strategy
  ChangeState(RotationScanState::RecordingBaseline);
  bool success = m_strategy->Execute(*m_context);

  // Collect results
  m_results.rotationSteps = m_strategy->GetRotationSteps();
  m_results.positiveDirectionImproved = m_strategy->PositiveDirectionImproved();
  m_results.negativeDirectionImproved = m_strategy->NegativeDirectionImproved();

  // Update results from context
  m_results.baseline = m_context->GetBaseline();
  m_results.globalPeak = m_context->GetGlobalPeak();

  // Calculate totals
  m_results.totalRotationSteps = static_cast<int>(m_results.rotationSteps.size());
  if (!m_results.rotationSteps.empty()) {
    m_results.totalRotation = m_results.rotationSteps.back().rotationAngle;
  }

  return success;
}

void RotationScanController::HandleScanCompletion(bool success) {
  m_results.endTime = std::chrono::system_clock::now();
  m_results.duration = std::chrono::duration_cast<std::chrono::seconds>(
    m_results.endTime - m_results.startTime);
  m_results.success = success;

  // Log completion
  std::stringstream completeMsg;
  completeMsg << "Rotation scan " << (success ? "completed" : "stopped")
              << " - Duration: " << m_results.duration.count() << "s";

  if (m_results.globalPeak.valid) {
    completeMsg << ", Peak: " << std::fixed << std::setprecision(6) << m_results.globalPeak.value;
  }

  if (m_logger) {
    m_logger->LogInfo(completeMsg.str(), "RotationScan");
  }
  std::cout << "[RotationScanController] " << completeMsg.str() << std::endl;

  // Reset state to idle
  ChangeState(RotationScanState::Idle);

  // Notify completion
  if (m_completionCallback) {
    m_completionCallback(m_results);
  }
}

void RotationScanController::HandleScanError(const std::string& message) {
  m_results.errorMessage = message;
  m_results.success = false;

  if (m_logger) {
    m_logger->LogError("Rotation scan error: " + message, "RotationScan");
  }
  std::cerr << "[RotationScanController] Error: " << message << std::endl;

  NotifyError(message);
}

// ============================================================================
// STATE MANAGEMENT
// ============================================================================

void RotationScanController::ChangeState(RotationScanState newState) {
  RotationScanState oldState = m_state.exchange(newState);

  if (oldState != newState) {
    std::cout << "[RotationScanController] State: " << ToString(oldState)
              << " -> " << ToString(newState) << std::endl;

    if (m_stateCallback) {
      m_stateCallback(oldState, newState);
    }
  }
}

void RotationScanController::NotifyProgress(double progress, const std::string& message) {
  m_progress = progress;

  if (m_progressCallback) {
    RotationScanProgressEvent event;
    event.overallProgress = progress;
    event.message = message;
    event.state = m_state.load();

    // Add rotation-specific info if available
    if (!m_results.rotationSteps.empty()) {
      const auto& lastStep = m_results.rotationSteps.back();
      event.currentDirection = lastStep.direction;
      event.currentRotation = lastStep.rotationAngle;
      event.rotationStepCount = static_cast<int>(m_results.rotationSteps.size());
    }

    m_progressCallback(event);
  }
}

void RotationScanController::NotifyError(const std::string& message) {
  if (m_errorCallback) {
    m_errorCallback(message);
  }
}

// ============================================================================
// VALIDATION
// ============================================================================

bool RotationScanController::ValidateParameters(const RotationScanParameters& params, std::string& error) {
  return params.Validate(error);
}

bool RotationScanController::ValidateDeviceReady(std::string& error) {
  // Check if device exists
  if (!m_motionManager.HasDevice(m_deviceName)) {
    error = "Device '" + m_deviceName + "' not found";
    return false;
  }

  // Check if data channel exists (just a warning, not an error)
  float testValue;
  if (!m_dataStore.TryGetValue(m_dataChannel, testValue)) {
    std::cout << "[RotationScanController] Warning: Data channel '" << m_dataChannel
              << "' not found in DataStore" << std::endl;
  }

  return true;
}

// ============================================================================
// JSON SERIALIZATION FOR RESULTS
// ============================================================================

nlohmann::json RotationScanResults::ToJson() const {
  nlohmann::json j;

  j["deviceName"] = deviceName;
  j["scanId"] = scanId;
  j["rotationAxis"] = ToString(rotationAxis);

  // Timestamps
  auto startTimeT = std::chrono::system_clock::to_time_t(startTime);
  auto endTimeT = std::chrono::system_clock::to_time_t(endTime);
  char startTimeBuf[26]{}, endTimeBuf[26]{};
  ctime_s(startTimeBuf, sizeof(startTimeBuf), &startTimeT);
  ctime_s(endTimeBuf, sizeof(endTimeBuf), &endTimeT);
  j["startTime"] = startTimeBuf;
  j["endTime"] = endTimeBuf;
  j["durationSeconds"] = duration.count();

  // Baseline
  j["baseline"]["value"] = baseline.value;
  j["baseline"]["valid"] = baseline.valid;
  if (baseline.valid) {
    j["baseline"]["position"]["x"] = baseline.position.x;
    j["baseline"]["position"]["y"] = baseline.position.y;
    j["baseline"]["position"]["z"] = baseline.position.z;
    j["baseline"]["position"]["u"] = baseline.position.u;
    j["baseline"]["position"]["v"] = baseline.position.v;
    j["baseline"]["position"]["w"] = baseline.position.w;
  }

  // Global peak
  j["globalPeak"]["value"] = globalPeak.value;
  j["globalPeak"]["valid"] = globalPeak.valid;
  j["globalPeak"]["context"] = globalPeak.context;
  if (globalPeak.valid) {
    j["globalPeak"]["position"]["x"] = globalPeak.position.x;
    j["globalPeak"]["position"]["y"] = globalPeak.position.y;
    j["globalPeak"]["position"]["z"] = globalPeak.position.z;
    j["globalPeak"]["position"]["u"] = globalPeak.position.u;
    j["globalPeak"]["position"]["v"] = globalPeak.position.v;
    j["globalPeak"]["position"]["w"] = globalPeak.position.w;
  }

  // Rotation steps
  j["rotationSteps"] = nlohmann::json::array();
  for (const auto& step : rotationSteps) {
    nlohmann::json stepJson;
    stepJson["rotationAngle"] = step.rotationAngle;
    stepJson["direction"] = ToString(step.direction);
    stepJson["peakValue"] = step.peakValue;
    stepJson["improvementFromBaseline"] = step.improvementFromBaseline;
    stepJson["improvementFromPrevious"] = step.improvementFromPrevious;
    stepJson["improved"] = step.improved;

    stepJson["position"]["x"] = step.peakPosition.x;
    stepJson["position"]["y"] = step.peakPosition.y;
    stepJson["position"]["z"] = step.peakPosition.z;
    stepJson["position"]["u"] = step.peakPosition.u;
    stepJson["position"]["v"] = step.peakPosition.v;
    stepJson["position"]["w"] = step.peakPosition.w;

    j["rotationSteps"].push_back(stepJson);
  }

  // Summary
  j["summary"]["totalRotation"] = totalRotation;
  j["summary"]["totalRotationSteps"] = totalRotationSteps;
  j["summary"]["positiveDirectionImproved"] = positiveDirectionImproved;
  j["summary"]["negativeDirectionImproved"] = negativeDirectionImproved;
  j["summary"]["success"] = success;
  j["summary"]["errorMessage"] = errorMessage;

  return j;
}
