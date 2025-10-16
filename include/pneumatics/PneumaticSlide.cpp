// PneumaticSlide.cpp - Implementation
#include "PneumaticSlide.h"
#include <iostream>
#include <thread>
#include <chrono>

PneumaticSlide::PneumaticSlide(const PneumaticConfig& config, EziIOManager* ioManager)
  : m_config(config)
  , m_ioManager(ioManager)
  , m_currentPosition(PneumaticPosition::UNKNOWN)
  , m_targetPosition(PneumaticPosition::UNKNOWN)
{
  std::cout << "[PneumaticSlide] Created: " << m_config.Name << std::endl;

  // Initial position update
  UpdatePosition();
}

PneumaticSlide::~PneumaticSlide() {
  Stop();
}

// ============================================================================
// NON-BLOCKING OPERATIONS
// ============================================================================

PneumaticError PneumaticSlide::Extend() {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::cout << "[PneumaticSlide] " << m_config.Name << " - Extend()" << std::endl;

  // Check if already extended - use internal version (no lock)
  UpdatePositionInternal();
  if (m_currentPosition == PneumaticPosition::EXTENDED) {
    std::cout << "[PneumaticSlide] " << m_config.Name << " - Already extended" << std::endl;
    return PneumaticError::ALREADY_AT_POSITION;
  }

  // Set output HIGH to extend
  PneumaticError result = SetOutput(true);
  if (result != PneumaticError::SUCCESS) {
    return result;
  }

  // Update state
  m_targetPosition = PneumaticPosition::EXTENDED;
  m_moveStartTime = std::chrono::steady_clock::now();

  PneumaticPosition oldPosition = m_currentPosition;
  m_currentPosition = PneumaticPosition::EXTENDING;
  NotifyPositionChange(m_currentPosition);

  return PneumaticError::SUCCESS;
}

PneumaticError PneumaticSlide::Retract() {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::cout << "[PneumaticSlide] " << m_config.Name << " - Retract()" << std::endl;

  // Check if already retracted - use internal version (no lock)
  UpdatePositionInternal();
  if (m_currentPosition == PneumaticPosition::RETRACTED) {
    std::cout << "[PneumaticSlide] " << m_config.Name << " - Already retracted" << std::endl;
    return PneumaticError::ALREADY_AT_POSITION;
  }

  // Set output LOW to retract
  PneumaticError result = SetOutput(false);
  if (result != PneumaticError::SUCCESS) {
    return result;
  }

  // Update state
  m_targetPosition = PneumaticPosition::RETRACTED;
  m_moveStartTime = std::chrono::steady_clock::now();

  PneumaticPosition oldPosition = m_currentPosition;
  m_currentPosition = PneumaticPosition::RETRACTING;
  NotifyPositionChange(m_currentPosition);

  return PneumaticError::SUCCESS;
}

PneumaticError PneumaticSlide::Stop() {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::cout << "[PneumaticSlide] " << m_config.Name << " - Stop()" << std::endl;

  m_targetPosition = PneumaticPosition::UNKNOWN;
  return SetOutput(false);
}

// ============================================================================
// BLOCKING OPERATIONS
// ============================================================================

PneumaticError PneumaticSlide::ExtendAndWait(uint32_t pollIntervalMs) {
  std::cout << "[PneumaticSlide] " << m_config.Name << " - ExtendAndWait()" << std::endl;

  PneumaticError result = Extend();
  if (result != PneumaticError::SUCCESS && result != PneumaticError::ALREADY_AT_POSITION) {
    return result;
  }

  if (result == PneumaticError::ALREADY_AT_POSITION) {
    return PneumaticError::SUCCESS;
  }

  return WaitForPosition(PneumaticPosition::EXTENDED, pollIntervalMs);
}

PneumaticError PneumaticSlide::RetractAndWait(uint32_t pollIntervalMs) {
  std::cout << "[PneumaticSlide] " << m_config.Name << " - RetractAndWait()" << std::endl;

  PneumaticError result = Retract();
  if (result != PneumaticError::SUCCESS && result != PneumaticError::ALREADY_AT_POSITION) {
    return result;
  }

  if (result == PneumaticError::ALREADY_AT_POSITION) {
    return PneumaticError::SUCCESS;
  }

  return WaitForPosition(PneumaticPosition::RETRACTED, pollIntervalMs);
}

// ============================================================================
// STATUS & MONITORING
// ============================================================================

// Public version WITH lock
PneumaticPosition PneumaticSlide::UpdatePosition() {
  std::lock_guard<std::mutex> lock(m_mutex);
  return UpdatePositionInternal();
}

// Internal version WITHOUT lock (called when already holding lock)
PneumaticPosition PneumaticSlide::UpdatePositionInternal() {
  bool extended = ReadExtendedSensor();
  bool retracted = ReadRetractedSensor();

  PneumaticPosition newPosition = DeterminePosition(extended, retracted);

  // Check for timeout during movement - use internal version (no lock)
  if (IsMovingInternal() && GetElapsedMs() > m_config.TimeoutMs) {
    std::cerr << "[PneumaticSlide] " << m_config.Name << " - TIMEOUT!" << std::endl;
    newPosition = PneumaticPosition::TIMEOUT;
    NotifyError(PneumaticError::TIMEOUT, "Movement timeout occurred");
  }

  // Notify if position changed
  if (newPosition != m_currentPosition) {
    PneumaticPosition oldPosition = m_currentPosition;
    m_currentPosition = newPosition;
    NotifyPositionChange(newPosition);
  }

  return m_currentPosition;
}

PneumaticStatus PneumaticSlide::GetStatus() const {
  std::lock_guard<std::mutex> lock(m_mutex);

  PneumaticStatus status;
  status.Position = m_currentPosition;
  status.ExtendedSensorActive = const_cast<PneumaticSlide*>(this)->ReadExtendedSensor();
  status.RetractedSensorActive = const_cast<PneumaticSlide*>(this)->ReadRetractedSensor();
  status.LastMoveTimeMs = GetElapsedMs();
  status.Valid = true;

  // Get output state
  if (m_ioManager) {
    uint32_t outputs = 0, ioStatus = 0;
    if (m_ioManager->ReadOutputs(m_config.Output.DeviceName, outputs, ioStatus) == EziIOError::SUCCESS) {
      IEziIODevice* device = m_ioManager->GetDevice(m_config.Output.DeviceName);
      if (device) {
        int pin = device->GetOutputPinByName(m_config.Output.PinName);
        if (pin >= 0) {
          bool is16Pin = (device->GetOutputCount() > 8);
          int bitOffset = is16Pin ? 16 : 8;
          status.OutputActive = (outputs & (1U << (bitOffset + pin))) != 0;
        }
      }
    }
  }

  return status;
}

// Internal version WITHOUT lock (called when already holding lock)
bool PneumaticSlide::IsMovingInternal() const {
  return (m_currentPosition == PneumaticPosition::EXTENDING ||
    m_currentPosition == PneumaticPosition::RETRACTING);
}

// Public version WITH lock
bool PneumaticSlide::IsMoving() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return IsMovingInternal();
}

bool PneumaticSlide::HasTimedOut() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_currentPosition == PneumaticPosition::TIMEOUT;
}

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

PneumaticError PneumaticSlide::SetOutput(bool active) {
  if (!m_ioManager) {
    return PneumaticError::IO_MANAGER_NULL;
  }

  EziIOError result = m_ioManager->SetOutputByName(
    m_config.Output.DeviceName,
    m_config.Output.PinName,
    active
  );

  if (result != EziIOError::SUCCESS) {
    std::cerr << "[PneumaticSlide] " << m_config.Name
      << " - Failed to set output: " << GetErrorString(result) << std::endl;
    return PneumaticError::OPERATION_FAILED;
  }

  return PneumaticError::SUCCESS;
}

bool PneumaticSlide::ReadExtendedSensor() {
  if (!m_ioManager) {
    return false;
  }

  bool state = false;
  m_ioManager->GetInputByName(
    m_config.ExtendedSensor.DeviceName,
    m_config.ExtendedSensor.PinName,
    state
  );

  return state;
}

bool PneumaticSlide::ReadRetractedSensor() {
  if (!m_ioManager) {
    return false;
  }

  bool state = false;
  m_ioManager->GetInputByName(
    m_config.RetractedSensor.DeviceName,
    m_config.RetractedSensor.PinName,
    state
  );

  return state;
}

PneumaticPosition PneumaticSlide::DeterminePosition(bool extended, bool retracted) {
  // Error: Both sensors active
  if (extended && retracted) {
    std::cerr << "[PneumaticSlide] " << m_config.Name
      << " - HARDWARE ERROR: Both sensors active!" << std::endl;
    NotifyError(PneumaticError::HARDWARE_ERROR, "Both sensors active simultaneously");
    return PneumaticPosition::HARDWARE_ERROR;
  }

  // Extended position
  if (extended && !retracted) {
    return PneumaticPosition::EXTENDED;
  }

  // Retracted position
  if (!extended && retracted) {
    return PneumaticPosition::RETRACTED;
  }

  // No sensors active - check if we're moving
  if (m_targetPosition == PneumaticPosition::EXTENDED) {
    return PneumaticPosition::EXTENDING;
  }
  else if (m_targetPosition == PneumaticPosition::RETRACTED) {
    return PneumaticPosition::RETRACTING;
  }

  // Unknown position
  return PneumaticPosition::UNKNOWN;
}

void PneumaticSlide::NotifyPositionChange(PneumaticPosition newPosition) {
  if (m_positionCallback) {
    PneumaticPosition oldPos = m_currentPosition;
    m_positionCallback(m_config.Name, oldPos, newPosition);
  }

  std::cout << "[PneumaticSlide] " << m_config.Name
    << " - Position: " << GetPositionString(newPosition) << std::endl;
}

void PneumaticSlide::NotifyError(PneumaticError error, const std::string& message) {
  if (m_errorCallback) {
    m_errorCallback(m_config.Name, error, message);
  }
}

uint32_t PneumaticSlide::GetElapsedMs() const {
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_moveStartTime);
  return static_cast<uint32_t>(elapsed.count());
}

PneumaticError PneumaticSlide::WaitForPosition(PneumaticPosition targetPos, uint32_t pollIntervalMs) {
  while (true) {
    UpdatePosition();

    // Success - reached target position
    if (m_currentPosition == targetPos) {
      std::cout << "[PneumaticSlide] " << m_config.Name
        << " - Reached " << GetPositionString(targetPos) << std::endl;
      return PneumaticError::SUCCESS;
    }

    // Error conditions
    if (m_currentPosition == PneumaticPosition::TIMEOUT) {
      return PneumaticError::TIMEOUT;
    }
    if (m_currentPosition == PneumaticPosition::HARDWARE_ERROR) {
      return PneumaticError::HARDWARE_ERROR;
    }

    // Wait before next poll
    std::this_thread::sleep_for(std::chrono::milliseconds(pollIntervalMs));
  }
}