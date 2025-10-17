// product_json_loader.h
#pragma once

#include <string>

// Forward declarations
class ProductPositionManager;

/**
 * @brief JSON loader/saver for product position files
 *
 * Handles serialization and deserialization of product position configurations.
 * Separates JSON handling from business logic in ProductPositionManager.
 */
class ProductJsonLoader {
public:
  /**
   * @brief Load positions from JSON file into ProductPositionManager
   * @param manager The ProductPositionManager to load into
   * @param filePath Path to positions JSON file
   * @return Number of positions loaded, or -1 on error
   */
  static int LoadFromFile(ProductPositionManager& manager,
    const std::string& filePath);

  /**
   * @brief Save positions from ProductPositionManager to JSON file
   * @param manager The ProductPositionManager to save from
   * @param filePath Path to save JSON file
   * @return true if saved successfully
   */
  static bool SaveToFile(const ProductPositionManager& manager,
    const std::string& filePath);

  /**
   * @brief Validate a product JSON file format
   * @param filePath Path to JSON file
   * @return true if file is valid product positions format
   */
  static bool ValidateFile(const std::string& filePath);

  /**
   * @brief Get a preview of what's in a product file without loading
   * @param filePath Path to JSON file
   * @param outDeviceCount Output: number of devices
   * @param outPositionCount Output: total number of positions
   * @return true if preview was successful
   */
  static bool GetFileInfo(const std::string& filePath,
    int& outDeviceCount,
    int& outPositionCount);

private:
  // No instances allowed - static utility class
  ProductJsonLoader() = delete;
  ~ProductJsonLoader() = delete;
};