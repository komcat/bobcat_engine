// PneumaticSlide.h - Concrete pneumatic slide implementation
#pragma once

#include "IPneumaticSlide.h"
#include "Pneumatic_Types.h"
#include "EziIOManager.h"
#include <mutex>
#include <chrono>

/**
 * @brief Concrete implementation of pneumatic slide control
 *
 * Manages a single pneumatic slide/cylinder with position feedback.
 * Handles state transitions, timeouts, and callbacks.
 */
class PneumaticSlide : public IPneumaticSlide {
public:
  PneumaticSlide(const PneumaticConfig& config, EziIOManager* ioManager);
  virtual ~PneumaticSlide();

  // ============================================================================
  // NON-BLOCKING OPERATIONS
  // ============================================================================
  PneumaticError Extend() override;
  PneumaticError Retract() override;
  PneumaticError Stop() override;

  // ============================================================================
  // BLOCKING OPERATIONS
  // ============================================================================
  PneumaticError ExtendAndWait(uint32_t pollIntervalMs = 50) override;
  PneumaticError RetractAndWait(uint32_t pollIntervalMs = 50) override;

  // ============================================================================
  // STATUS & MONITORING
  // ============================================================================
  PneumaticPosition UpdatePosition() override;
  PneumaticPosition GetPosition() const override { return m_currentPosition; }
  PneumaticStatus GetStatus() const override;
  bool IsMoving() const override;
  bool HasTimedOut() const override;

  // ============================================================================
  // CONFIGURATION
  // ============================================================================
  std::string GetName() const override { return m_config.Name; }
  void SetTimeout(uint32_t timeoutMs) override { m_config.TimeoutMs = timeoutMs; }
  uint32_t GetTimeout() const override { return m_config.TimeoutMs; }

  // ============================================================================
  // CALLBACKS
  // ============================================================================
  void SetPositionCallback(PneumaticPositionCallback callback) override {
    m_positionCallback = callback;
  }
  void SetErrorCallback(PneumaticErrorCallback callback) override {
    m_errorCallback = callback;
  }

private:
  // Configuration
  PneumaticConfig m_config;
  EziIOManager* m_ioManager;

  // State
  mutable std::mutex m_mutex;
  PneumaticPosition m_currentPosition;
  PneumaticPosition m_targetPosition;
  std::chrono::steady_clock::time_point m_moveStartTime;

  // Callbacks
  PneumaticPositionCallback m_positionCallback;
  PneumaticErrorCallback m_errorCallback;

  // ============================================================================
  // PRIVATE HELPERS
  // ============================================================================
  PneumaticError SetOutput(bool active);
  bool ReadExtendedSensor();
  bool ReadRetractedSensor();
  PneumaticPosition DeterminePosition(bool extended, bool retracted);
  void NotifyPositionChange(PneumaticPosition newPosition);
  void NotifyError(PneumaticError error, const std::string& message);
  uint32_t GetElapsedMs() const;
  PneumaticError WaitForPosition(PneumaticPosition targetPos, uint32_t pollIntervalMs);

  // Internal versions without mutex locks (called when already holding lock)
  PneumaticPosition UpdatePositionInternal();
  bool IsMovingInternal() const;
};