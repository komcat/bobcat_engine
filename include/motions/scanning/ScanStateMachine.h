// include/scanning/ScanStateMachine.h
#pragma once

#include "ScanTypes.h"
#include <functional>
#include <string>

/**
 * @brief State machine for managing scan phases
 *
 * Valid state transitions:
 * Idle -> Initializing -> RecordingBaseline -> Scanning -> ReturningToPeak -> Completed
 *                                                       |
 *                                                       +-> Halted
 *                                                       |
 *                                                       +-> Error
 *
 * Any state can transition to Error or Halted
 */
class ScanStateMachine {
public:
  using StateChangeCallback = std::function<void(ScanState oldState, ScanState newState)>;

  ScanStateMachine();

  /**
   * @brief Set callback for state changes
   */
  void SetStateChangeCallback(StateChangeCallback callback) {
    m_stateChangeCallback = callback;
  }

  /**
   * @brief Get current state
   */
  ScanState GetState() const { return m_currentState; }

  /**
   * @brief Attempt state transition
   * @return true if transition is valid and was performed
   */
  bool TransitionTo(ScanState newState);

  /**
   * @brief Check if transition is valid
   */
  bool IsValidTransition(ScanState from, ScanState to) const;

  /**
   * @brief Reset to idle state
   */
  void Reset();

  /**
   * @brief Get human-readable state description
   */
  std::string GetStateDescription() const;

  /**
   * @brief Check if in terminal state (Completed, Error, Halted)
   */
  bool IsTerminalState() const;

  /**
   * @brief Check if in active scanning state
   */
  bool IsActiveState() const;

private:
  ScanState m_currentState;
  StateChangeCallback m_stateChangeCallback;

  // Validation helpers
  bool CanTransitionToInitializing(ScanState from) const;
  bool CanTransitionToRecordingBaseline(ScanState from) const;
  bool CanTransitionToScanning(ScanState from) const;
  bool CanTransitionToReturningToPeak(ScanState from) const;
  bool CanTransitionToCompleted(ScanState from) const;
  bool CanTransitionToError(ScanState from) const;
  bool CanTransitionToHalted(ScanState from) const;
  bool CanTransitionToIdle(ScanState from) const;
};

// ============================================================================
// INLINE IMPLEMENTATIONS
// ============================================================================

inline ScanStateMachine::ScanStateMachine()
  : m_currentState(ScanState::Idle) {
}

inline void ScanStateMachine::Reset() {
  ScanState oldState = m_currentState;
  m_currentState = ScanState::Idle;

  if (m_stateChangeCallback) {
    m_stateChangeCallback(oldState, ScanState::Idle);
  }
}

inline std::string ScanStateMachine::GetStateDescription() const {
  return ToString(m_currentState);
}

inline bool ScanStateMachine::IsTerminalState() const {
  return m_currentState == ScanState::Completed ||
    m_currentState == ScanState::Error ||
    m_currentState == ScanState::Halted;
}

inline bool ScanStateMachine::IsActiveState() const {
  return m_currentState == ScanState::Initializing ||
    m_currentState == ScanState::RecordingBaseline ||
    m_currentState == ScanState::Scanning ||
    m_currentState == ScanState::ReturningToPeak;
}

inline bool ScanStateMachine::TransitionTo(ScanState newState) {
  if (!IsValidTransition(m_currentState, newState)) {
    return false;
  }

  ScanState oldState = m_currentState;
  m_currentState = newState;

  if (m_stateChangeCallback) {
    m_stateChangeCallback(oldState, newState);
  }

  return true;
}

inline bool ScanStateMachine::IsValidTransition(ScanState from, ScanState to) const {
  switch (to) {
  case ScanState::Idle:
    return CanTransitionToIdle(from);
  case ScanState::Initializing:
    return CanTransitionToInitializing(from);
  case ScanState::RecordingBaseline:
    return CanTransitionToRecordingBaseline(from);
  case ScanState::Scanning:
    return CanTransitionToScanning(from);
  case ScanState::ReturningToPeak:
    return CanTransitionToReturningToPeak(from);
  case ScanState::Completed:
    return CanTransitionToCompleted(from);
  case ScanState::Error:
    return CanTransitionToError(from);
  case ScanState::Halted:
    return CanTransitionToHalted(from);
  default:
    return false;
  }
}

inline bool ScanStateMachine::CanTransitionToIdle(ScanState from) const {
  // Can only return to Idle from terminal states
  return from == ScanState::Completed ||
    from == ScanState::Error ||
    from == ScanState::Halted;
}

inline bool ScanStateMachine::CanTransitionToInitializing(ScanState from) const {
  return from == ScanState::Idle;
}

inline bool ScanStateMachine::CanTransitionToRecordingBaseline(ScanState from) const {
  return from == ScanState::Initializing;
}

inline bool ScanStateMachine::CanTransitionToScanning(ScanState from) const {
  return from == ScanState::RecordingBaseline;
}

inline bool ScanStateMachine::CanTransitionToReturningToPeak(ScanState from) const {
  return from == ScanState::Scanning;
}

inline bool ScanStateMachine::CanTransitionToCompleted(ScanState from) const {
  return from == ScanState::ReturningToPeak ||
    from == ScanState::Scanning;
}

inline bool ScanStateMachine::CanTransitionToError(ScanState from) const {
  // Can transition to error from any non-terminal state
  return from != ScanState::Completed &&
    from != ScanState::Error &&
    from != ScanState::Halted;
}

inline bool ScanStateMachine::CanTransitionToHalted(ScanState from) const {
  // Can halt from any active state
  return from == ScanState::Initializing ||
    from == ScanState::RecordingBaseline ||
    from == ScanState::Scanning ||
    from == ScanState::ReturningToPeak;
}