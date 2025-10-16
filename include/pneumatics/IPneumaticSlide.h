// IPneumaticSlide.h - Interface for pneumatic slides
#pragma once

#include "Pneumatic_Types.h"
#include <string>

/**
 * @brief Interface for pneumatic slide control
 *
 * Provides both blocking and non-blocking operations for pneumatic slides.
 * Monitors position sensors and handles timeouts.
 */
class IPneumaticSlide {
public:
  virtual ~IPneumaticSlide() = default;

  // ============================================================================
  // NON-BLOCKING OPERATIONS
  // ============================================================================

  /**
   * @brief Start extending (non-blocking)
   * @return Error code
   */
  virtual PneumaticError Extend() = 0;

  /**
   * @brief Start retracting (non-blocking)
   * @return Error code
   */
  virtual PneumaticError Retract() = 0;

  /**
   * @brief Stop movement and turn off output
   * @return Error code
   */
  virtual PneumaticError Stop() = 0;

  // ============================================================================
  // BLOCKING OPERATIONS
  // ============================================================================

  /**
   * @brief Extend and wait until extended or timeout (blocking)
   * @param pollIntervalMs Polling interval in milliseconds (default 50ms)
   * @return Error code
   */
  virtual PneumaticError ExtendAndWait(uint32_t pollIntervalMs = 50) = 0;

  /**
   * @brief Retract and wait until retracted or timeout (blocking)
   * @param pollIntervalMs Polling interval in milliseconds (default 50ms)
   * @return Error code
   */
  virtual PneumaticError RetractAndWait(uint32_t pollIntervalMs = 50) = 0;

  // ============================================================================
  // STATUS & MONITORING
  // ============================================================================

  /**
   * @brief Update position by reading sensors
   * @return Current position
   */
  virtual PneumaticPosition UpdatePosition() = 0;

  /**
   * @brief Get current position (cached)
   * @return Current position
   */
  virtual PneumaticPosition GetPosition() const = 0;

  /**
   * @brief Get full status information
   * @return Status struct
   */
  virtual PneumaticStatus GetStatus() const = 0;

  /**
   * @brief Check if slide is currently moving
   * @return True if extending or retracting
   */
  virtual bool IsMoving() const = 0;

  /**
   * @brief Check if timeout has occurred during movement
   * @return True if timeout occurred
   */
  virtual bool HasTimedOut() const = 0;

  // ============================================================================
  // CONFIGURATION
  // ============================================================================

  /**
   * @brief Get slide name
   */
  virtual std::string GetName() const = 0;

  /**
   * @brief Set timeout duration
   * @param timeoutMs Timeout in milliseconds
   */
  virtual void SetTimeout(uint32_t timeoutMs) = 0;

  /**
   * @brief Get timeout duration
   * @return Timeout in milliseconds
   */
  virtual uint32_t GetTimeout() const = 0;

  // ============================================================================
  // CALLBACKS
  // ============================================================================

  /**
   * @brief Set position change callback
   */
  virtual void SetPositionCallback(PneumaticPositionCallback callback) = 0;

  /**
   * @brief Set error callback
   */
  virtual void SetErrorCallback(PneumaticErrorCallback callback) = 0;
};