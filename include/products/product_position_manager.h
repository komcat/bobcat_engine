// product_position_manager.h
#pragma once

#include "MotionTypes.h"
#include <string>
#include <map>
#include <vector>

// Forward declaration
class UnifiedMotionManager;

/**
 * @brief Manages product-specific position configurations
 *
 * Handles loading, saving, and editing of position sets for different products.
 * Keeps position management separate from motion control.
 */
class ProductPositionManager {
public:
  ProductPositionManager();
  ~ProductPositionManager();

  // ============================================================================
  // PRODUCT FILE MANAGEMENT
  // ============================================================================

  /**
   * @brief Load positions from a product file
   * @param productFilePath Full path to positions.productX.json
   * @return true if loaded successfully
   */
  bool LoadProduct(const std::string& productFilePath);

  /**
   * @brief Save positions to the current product file
   * @return true if saved successfully
   */
  bool SaveProduct();

  /**
   * @brief Save positions to a new product file
   * @param productName Product name (will create positions.productName.json)
   * @return true if saved successfully
   */
  bool SaveProductAs(const std::string& productName);

  /**
   * @brief Get the current loaded product name
   */
  std::string GetCurrentProductName() const { return m_currentProductName; }

  /**
   * @brief Get the current product file path
   */
  std::string GetCurrentProductPath() const { return m_currentProductPath; }

  /**
   * @brief Check if a product is currently loaded
   */
  bool HasLoadedProduct() const { return !m_currentProductName.empty(); }

  // ============================================================================
  // POSITION MANAGEMENT
  // ============================================================================

  /**
   * @brief Add or update a position
   */
  void AddPosition(const std::string& deviceName,
    const std::string& positionName,
    const PositionStruct& position);

  /**
   * @brief Add or update a position with individual coordinates
   */
  void AddPosition(const std::string& deviceName,
    const std::string& positionName,
    double x, double y, double z,
    double u = 0.0, double v = 0.0, double w = 0.0);

  /**
   * @brief Update an existing position (fails if position doesn't exist)
   * @return true if position existed and was updated
   */
  bool UpdatePosition(const std::string& deviceName,
    const std::string& positionName,
    const PositionStruct& position);

  /**
   * @brief Remove a position
   * @return true if position existed and was removed
   */
  bool RemovePosition(const std::string& deviceName,
    const std::string& positionName);

  /**
   * @brief Get a specific position
   * @return true if position was found
   */
  bool GetPosition(const std::string& deviceName,
    const std::string& positionName,
    PositionStruct& outPosition) const;

  /**
   * @brief Get list of all position names for a device
   */
  std::vector<std::string> GetPositionList(const std::string& deviceName) const;

  /**
   * @brief Get list of all device names that have positions
   */
  std::vector<std::string> GetAllDeviceNames() const;

  /**
   * @brief Get all positions for a device
   */
  std::map<std::string, PositionStruct> GetAllPositions(
    const std::string& deviceName) const;

  /**
   * @brief Check if a device has any positions
   */
  bool HasDevice(const std::string& deviceName) const;

  /**
   * @brief Check if a specific position exists
   */
  bool HasPosition(const std::string& deviceName,
    const std::string& positionName) const;

  // ============================================================================
  // BULK OPERATIONS
  // ============================================================================

  /**
   * @brief Clear all positions for all devices
   */
  void ClearAllPositions();

  /**
   * @brief Clear all positions for a specific device
   */
  void ClearDevicePositions(const std::string& deviceName);

  /**
   * @brief Get total number of positions across all devices
   */
  size_t GetTotalPositionCount() const;

  /**
   * @brief Get number of positions for a specific device
   */
  size_t GetPositionCount(const std::string& deviceName) const;

  // ============================================================================
  // INTEGRATION WITH MOTION MANAGER
  // ============================================================================

  /**
   * @brief Export all positions to UnifiedMotionManager
   * This loads the positions into the motion manager for use
   */
  void ExportToMotionManager(UnifiedMotionManager& manager) const;

  /**
   * @brief Import current positions from UnifiedMotionManager
   * This captures the current state from the motion manager
   */
  void ImportFromMotionManager(const UnifiedMotionManager& manager);

private:
  // Storage: deviceName -> (positionName -> PositionStruct)
  std::map<std::string, std::map<std::string, PositionStruct>> m_positions;

  std::string m_currentProductName;
  std::string m_currentProductPath;

  // Extract product name from file path
  std::string ExtractProductName(const std::string& filePath) const;
};