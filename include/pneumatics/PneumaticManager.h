// PneumaticManager.h - Manages multiple pneumatic slides
#pragma once

#include "IPneumaticSlide.h"
#include "PneumaticSlide.h"
#include "Pneumatic_Types.h"
#include "EziIOManager.h"
#include <string>
#include <map>
#include <memory>
#include <vector>

/**
 * @brief Manages multiple pneumatic slides
 *
 * Provides centralized control and monitoring of pneumatic actuators.
 */
class PneumaticManager {
public:
  PneumaticManager(EziIOManager* ioManager);
  ~PneumaticManager();

  // ============================================================================
  // SLIDE MANAGEMENT
  // ============================================================================

  /**
   * @brief Add a pneumatic slide
   * @param config Slide configuration
   * @return True if added successfully
   */
  bool AddSlide(const PneumaticConfig& config);

  /**
   * @brief Remove a slide
   * @param slideName Name of the slide
   * @return True if removed successfully
   */
  bool RemoveSlide(const std::string& slideName);

  /**
   * @brief Check if slide exists
   */
  bool HasSlide(const std::string& slideName) const;

  /**
   * @brief Get slide by name
   */
  IPneumaticSlide* GetSlide(const std::string& slideName);

  // ============================================================================
  // NON-BLOCKING OPERATIONS
  // ============================================================================

  [[nodiscard]] PneumaticError Extend(const std::string& slideName);
  [[nodiscard]] PneumaticError Retract(const std::string& slideName);
  [[nodiscard]] PneumaticError Stop(const std::string& slideName);
  void StopAll();

  // ============================================================================
  // BLOCKING OPERATIONS
  // ============================================================================

  [[nodiscard]] PneumaticError ExtendAndWait(const std::string& slideName, uint32_t pollIntervalMs = 50);
  [[nodiscard]] PneumaticError RetractAndWait(const std::string& slideName, uint32_t pollIntervalMs = 50);

  // ============================================================================
  // STATUS & MONITORING
  // ============================================================================

  PneumaticPosition GetPosition(const std::string& slideName);
  PneumaticStatus GetStatus(const std::string& slideName);
  void UpdateAllPositions();

  /**
   * @brief Get all slide names
   */
  std::vector<std::string> GetAllSlideNames() const;

  /**
   * @brief Get slides by position
   */
  std::vector<std::string> GetSlidesByPosition(PneumaticPosition position);

  // ============================================================================
  // CALLBACKS
  // ============================================================================

  /**
   * @brief Set global position change callback (applies to all slides)
   */
  void SetGlobalPositionCallback(PneumaticPositionCallback callback);

  /**
   * @brief Set global error callback (applies to all slides)
   */
  void SetGlobalErrorCallback(PneumaticErrorCallback callback);

  /**
   * @brief Set position callback for specific slide
   */
  void SetSlidePositionCallback(const std::string& slideName, PneumaticPositionCallback callback);

  /**
   * @brief Set error callback for specific slide
   */
  void SetSlideErrorCallback(const std::string& slideName, PneumaticErrorCallback callback);

  // ============================================================================
  // CONFIGURATION
  // ============================================================================

  /**
   * @brief Get total slide count
   */
  size_t GetSlideCount() const { return m_slides.size(); }

private:
  EziIOManager* m_ioManager;
  std::map<std::string, std::shared_ptr<IPneumaticSlide>> m_slides;

  // Global callbacks
  PneumaticPositionCallback m_globalPositionCallback;
  PneumaticErrorCallback m_globalErrorCallback;

  // Helper methods
  PneumaticError ValidateSlide(const std::string& slideName, IPneumaticSlide** slide);
};