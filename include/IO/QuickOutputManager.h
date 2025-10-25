// QuickOutputManager.h - Backend module for managing quick output controls
#pragma once

#include "EziIO_Types.h"
#include <string>
#include <vector>
#include <memory>

class EziIOManager;

/**
 * @brief Single output pin definition
 */
struct OutputPinDefinition {
  std::string deviceName;     // Name of the IO device
  std::string pinName;        // Optional pin name (e.g., "GRIPPER_OPEN")
  int pinNumber;              // Pin number (0-based)
  std::string displayName;    // User-friendly display name

  OutputPinDefinition()
    : pinNumber(-1) {}

  OutputPinDefinition(const std::string& device, int pin, const std::string& display)
    : deviceName(device), pinNumber(pin), displayName(display) {}

  OutputPinDefinition(const std::string& device, const std::string& name, const std::string& display)
    : deviceName(device), pinName(name), pinNumber(-1), displayName(display) {}
};

/**
 * @brief Current output pin status
 */
struct OutputPinStatus {
  std::string displayName;
  bool isOn;
  bool isValid;  // False if communication error

  OutputPinStatus() : isOn(false), isValid(false) {}
  OutputPinStatus(const std::string& name, bool on, bool valid)
    : displayName(name), isOn(on), isValid(valid) {}
};

/**
 * @brief Quick Output Manager - Backend logic for output control
 *
 * Manages a collection of output pins from various IO devices.
 * Provides methods to turn outputs on/off and retrieve their status.
 * Completely UI-agnostic - can be used with any frontend.
 */
class QuickOutputManager {
public:
  explicit QuickOutputManager(EziIOManager* ioManager);
  ~QuickOutputManager();

  // ============================================================================
  // PIN MANAGEMENT
  // ============================================================================

  /**
   * @brief Add an output pin to the quick access list
   */
  void AddOutputPin(const OutputPinDefinition& pinDef);

  /**
   * @brief Remove an output pin by index
   */
  bool RemoveOutputPin(size_t index);

  /**
   * @brief Clear all output pins
   */
  void ClearAllPins();

  /**
   * @brief Get number of registered output pins
   */
  size_t GetPinCount() const { return m_outputPins.size(); }

  /**
   * @brief Get output pin definition by index
   */
  const OutputPinDefinition* GetPinDefinition(size_t index) const;

  // ============================================================================
  // OUTPUT CONTROL
  // ============================================================================

  /**
   * @brief Turn output ON by index
   */
  EziIOError TurnOn(size_t index);

  /**
   * @brief Turn output OFF by index
   */
  EziIOError TurnOff(size_t index);

  /**
   * @brief Set output state by index
   */
  EziIOError SetOutput(size_t index, bool state);

  /**
   * @brief Toggle output state by index
   */
  EziIOError ToggleOutput(size_t index);

  /**
   * @brief Turn all outputs OFF (emergency stop)
   */
  void TurnOffAll();

  // ============================================================================
  // STATUS RETRIEVAL
  // ============================================================================

  /**
   * @brief Get status of a specific output pin
   */
  OutputPinStatus GetPinStatus(size_t index) const;

  /**
   * @brief Get status of all output pins
   */
  std::vector<OutputPinStatus> GetAllPinStatuses() const;

  /**
   * @brief Refresh status from all devices (force read)
   */
  void RefreshAllStatuses();

private:
  EziIOManager* m_ioManager;
  std::vector<OutputPinDefinition> m_outputPins;

  // Helper to resolve pin number from name if needed
  int ResolvePinNumber(const OutputPinDefinition& pinDef) const;
};
