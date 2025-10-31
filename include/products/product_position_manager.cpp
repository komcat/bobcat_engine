// product_position_manager.cpp
#include "product_position_manager.h"
#include "product_json_loader.h"
#include "unified_motion_manager.h"
#include <iostream>
#include <algorithm>
#include <filesystem>  // ✅ For directory creation

namespace fs = std::filesystem;

ProductPositionManager::ProductPositionManager()
  : m_currentProductName("")
  , m_currentProductPath("")
{
  std::cout << "[INFO] ProductPositionManager: Initialized" << std::endl;
}

ProductPositionManager::~ProductPositionManager() {
  std::cout << "[INFO] ProductPositionManager: Shutdown" << std::endl;
}

// ============================================================================
// PRODUCT FILE MANAGEMENT
// ============================================================================

bool ProductPositionManager::LoadProduct(const std::string& productFilePath) {
  std::cout << "[INFO] ProductPositionManager: Loading product from "
    << productFilePath << std::endl;

  // Clear existing positions and graphs
  m_positions.clear();
  m_graphManager.clearAll();  // ✅ NEW: Clear graphs

  // Load using ProductJsonLoader
  int count = ProductJsonLoader::LoadFromFile(*this, productFilePath);

  if (count < 0) {
    std::cout << "[ERROR] ProductPositionManager: Failed to load product" << std::endl;
    return false;
  }

  m_currentProductPath = productFilePath;
  m_currentProductName = ExtractProductName(productFilePath);

  std::cout << "[INFO] ProductPositionManager: Loaded product '"
    << m_currentProductName << "' with " << count << " positions" << std::endl;

  return true;
}

bool ProductPositionManager::SaveProduct() {
  if (m_currentProductPath.empty()) {
    std::cout << "[ERROR] ProductPositionManager: No product file loaded" << std::endl;
    return false;
  }

  std::cout << "[INFO] ProductPositionManager: Saving product to "
    << m_currentProductPath << std::endl;

  return ProductJsonLoader::SaveToFile(*this, m_currentProductPath);
}

bool ProductPositionManager::SaveProductAs(const std::string& productName) {
  // ✅ Create product directory structure: config/productName/
  std::string productDir = "config/" + productName;
  // ✅ FIXED: Use correct format positions.[productName].json
  std::string filePath = productDir + "/positions." + productName + ".json";

  std::cout << "[INFO] ProductPositionManager: Saving product as "
    << filePath << std::endl;

  // ✅ Create directory if it doesn't exist
  try {
    if (!fs::exists(productDir)) {
      fs::create_directories(productDir);
      std::cout << "[INFO] ProductPositionManager: Created directory " << productDir << std::endl;
    }
  }
  catch (const std::exception& e) {
    std::cout << "[ERROR] ProductPositionManager: Failed to create directory: "
      << e.what() << std::endl;
    return false;
  }

  // ✅ Copy files from original product folder if one is loaded
  if (HasLoadedProduct() && !m_currentProductName.empty()) {
    std::string sourceDir = "config/" + m_currentProductName;

    std::cout << "[INFO] ProductPositionManager: Copying files from "
      << sourceDir << " to " << productDir << std::endl;

    try {
      // Copy all files and subdirectories from source to destination
      if (fs::exists(sourceDir) && fs::is_directory(sourceDir)) {
        for (const auto& entry : fs::recursive_directory_iterator(sourceDir)) {
          // Get relative path from source directory
          std::string relativePath = entry.path().string().substr(sourceDir.length() + 1);
          std::string destPath = productDir + "/" + relativePath;

          if (entry.is_directory()) {
            // Create subdirectory in destination
            fs::create_directories(destPath);
            std::cout << "[INFO] ProductPositionManager: Created subdirectory "
              << relativePath << std::endl;
          }
          else if (entry.is_regular_file()) {
            // Check if this is the positions file that needs renaming
            std::string filename = entry.path().filename().string();

            if (filename == "positions." + m_currentProductName + ".json") {
              // This is the old positions file - we'll save new one instead
              std::cout << "[INFO] ProductPositionManager: Skipping old positions file "
                << filename << " (will save new one)" << std::endl;
              continue;
            }

            // Copy regular file
            fs::copy_file(entry.path(), destPath, fs::copy_options::overwrite_existing);
            std::cout << "[INFO] ProductPositionManager: Copied file "
              << relativePath << std::endl;
          }
        }
      }
    }
    catch (const std::exception& e) {
      std::cout << "[WARNING] ProductPositionManager: Error copying files: "
        << e.what() << std::endl;
      // Continue anyway - we can still save the positions file
    }
  }

  // ✅ Save positions file with new product name
  if (ProductJsonLoader::SaveToFile(*this, filePath)) {
    m_currentProductPath = filePath;
    m_currentProductName = productName;
    std::cout << "[SUCCESS] ProductPositionManager: Product saved as '"
      << productName << "'" << std::endl;
    return true;
  }

  return false;
}

std::string ProductPositionManager::ExtractProductName(const std::string& filePath) const {
  // Extract product name from path like:
  //   "config/SIPHOG101/positions.SIPHOG101.json" (correct format)
  //   "config/SIPHOG101/positions.json" (legacy format - folder name used)
  //   "config/SIPHOG101/SIPHOG101.positions.json" (old wrong format)

  // Find the last directory separator
  size_t lastSlash = filePath.find_last_of("/\\");
  if (lastSlash == std::string::npos) {
    return "unknown";
  }

  // Get the filename
  std::string filename = filePath.substr(lastSlash + 1);

  // Try to extract from filename pattern "positions.[name].json"
  if (filename.find("positions.") == 0 && filename.find(".json") != std::string::npos) {
    // Extract name between "positions." and ".json"
    size_t start = 10; // Length of "positions."
    size_t end = filename.find(".json");
    if (end > start) {
      std::string productName = filename.substr(start, end - start);
      std::cout << "[DEBUG] ProductPositionManager: Extracted product name '"
        << productName << "' from filename: " << filename << std::endl;
      return productName;
    }
  }

  // Fallback: Use directory name (for legacy "positions.json" files)
  size_t secondLastSlash = filePath.find_last_of("/\\", lastSlash - 1);
  if (secondLastSlash == std::string::npos) {
    return "unknown";
  }

  std::string productName = filePath.substr(secondLastSlash + 1, lastSlash - secondLastSlash - 1);

  std::cout << "[DEBUG] ProductPositionManager: Extracted product name '"
    << productName << "' from directory path: " << filePath << std::endl;

  return productName;
}

// ============================================================================
// POSITION MANAGEMENT
// ============================================================================

void ProductPositionManager::AddPosition(const std::string& deviceName,
  const std::string& positionName,
  const PositionStruct& position) {
  m_positions[deviceName][positionName] = position;

  std::cout << "[INFO] ProductPositionManager: Added/Updated position '"
    << positionName << "' for device " << deviceName
    << " -> (" << position.x << ", " << position.y << ", "
    << position.z << ")" << std::endl;
}

void ProductPositionManager::AddPosition(const std::string& deviceName,
  const std::string& positionName,
  double x, double y, double z,
  double u, double v, double w) {
  PositionStruct pos = { x, y, z, u, v, w };
  AddPosition(deviceName, positionName, pos);
}

bool ProductPositionManager::UpdatePosition(const std::string& deviceName,
  const std::string& positionName,
  const PositionStruct& position) {
  auto deviceIt = m_positions.find(deviceName);
  if (deviceIt == m_positions.end()) {
    std::cout << "[WARNING] ProductPositionManager: Device " << deviceName
      << " not found" << std::endl;
    return false;
  }

  auto posIt = deviceIt->second.find(positionName);
  if (posIt == deviceIt->second.end()) {
    std::cout << "[WARNING] ProductPositionManager: Position '" << positionName
      << "' not found for device " << deviceName << std::endl;
    return false;
  }

  deviceIt->second[positionName] = position;

  std::cout << "[INFO] ProductPositionManager: Updated position '"
    << positionName << "' for device " << deviceName << std::endl;

  return true;
}

bool ProductPositionManager::RemovePosition(const std::string& deviceName,
  const std::string& positionName) {
  auto deviceIt = m_positions.find(deviceName);
  if (deviceIt == m_positions.end()) {
    std::cout << "[WARNING] ProductPositionManager: Device " << deviceName
      << " not found" << std::endl;
    return false;
  }

  auto posIt = deviceIt->second.find(positionName);
  if (posIt == deviceIt->second.end()) {
    std::cout << "[WARNING] ProductPositionManager: Position '" << positionName
      << "' not found for device " << deviceName << std::endl;
    return false;
  }

  deviceIt->second.erase(posIt);

  // Remove device entry if no positions left
  if (deviceIt->second.empty()) {
    m_positions.erase(deviceIt);
  }

  std::cout << "[INFO] ProductPositionManager: Removed position '"
    << positionName << "' from device " << deviceName << std::endl;

  return true;
}

bool ProductPositionManager::GetPosition(const std::string& deviceName,
  const std::string& positionName,
  PositionStruct& outPosition) const {
  auto deviceIt = m_positions.find(deviceName);
  if (deviceIt == m_positions.end()) {
    return false;
  }

  auto posIt = deviceIt->second.find(positionName);
  if (posIt == deviceIt->second.end()) {
    return false;
  }

  outPosition = posIt->second;
  return true;
}

std::vector<std::string> ProductPositionManager::GetPositionList(
  const std::string& deviceName) const {
  std::vector<std::string> positions;

  auto deviceIt = m_positions.find(deviceName);
  if (deviceIt != m_positions.end()) {
    for (const auto& [posName, pos] : deviceIt->second) {
      positions.push_back(posName);
    }
  }

  return positions;
}

std::vector<std::string> ProductPositionManager::GetAllDeviceNames() const {
  std::vector<std::string> devices;

  for (const auto& [deviceName, positions] : m_positions) {
    devices.push_back(deviceName);
  }

  return devices;
}

std::map<std::string, PositionStruct> ProductPositionManager::GetAllPositions(
  const std::string& deviceName) const {
  auto deviceIt = m_positions.find(deviceName);
  if (deviceIt != m_positions.end()) {
    return deviceIt->second;
  }

  return std::map<std::string, PositionStruct>();
}

bool ProductPositionManager::HasDevice(const std::string& deviceName) const {
  return m_positions.find(deviceName) != m_positions.end();
}

bool ProductPositionManager::HasPosition(const std::string& deviceName,
  const std::string& positionName) const {
  auto deviceIt = m_positions.find(deviceName);
  if (deviceIt == m_positions.end()) {
    return false;
  }

  return deviceIt->second.find(positionName) != deviceIt->second.end();
}

// ============================================================================
// BULK OPERATIONS
// ============================================================================

void ProductPositionManager::ClearAllPositions() {
  std::cout << "[INFO] ProductPositionManager: Clearing all positions" << std::endl;
  m_positions.clear();
}

void ProductPositionManager::ClearDevicePositions(const std::string& deviceName) {
  auto it = m_positions.find(deviceName);
  if (it != m_positions.end()) {
    m_positions.erase(it);
    std::cout << "[INFO] ProductPositionManager: Cleared positions for device "
      << deviceName << std::endl;
  }
}

size_t ProductPositionManager::GetTotalPositionCount() const {
  size_t count = 0;
  for (const auto& [device, positions] : m_positions) {
    count += positions.size();
  }
  return count;
}

size_t ProductPositionManager::GetPositionCount(const std::string& deviceName) const {
  auto it = m_positions.find(deviceName);
  if (it != m_positions.end()) {
    return it->second.size();
  }
  return 0;
}

// ============================================================================
// INTEGRATION WITH MOTION MANAGER
// ============================================================================

void ProductPositionManager::ExportToMotionManager(UnifiedMotionManager& manager) const {
  std::cout << "[INFO] ProductPositionManager: Exporting positions to motion manager"
    << std::endl;

  // Clear existing positions in motion manager
  for (const auto& deviceName : manager.GetAllDeviceNames()) {
    manager.ClearNamedPositions(deviceName);
  }

  // Export all positions
  int count = 0;
  for (const auto& [deviceName, positions] : m_positions) {
    for (const auto& [posName, pos] : positions) {
      manager.AddNamedPosition(deviceName, posName, pos);
      count++;
    }
  }

  std::cout << "[INFO] ProductPositionManager: Exported " << count
    << " positions to motion manager" << std::endl;
}

void ProductPositionManager::ImportFromMotionManager(const UnifiedMotionManager& manager) {
  std::cout << "[INFO] ProductPositionManager: Importing positions from motion manager"
    << std::endl;

  // Clear current positions
  m_positions.clear();

  // Import positions from each device
  int count = 0;
  for (const auto& deviceName : manager.GetAllDeviceNames()) {
    std::vector<std::string> positionNames = manager.GetNamedPositionList(deviceName);

    for (const auto& posName : positionNames) {
      PositionStruct pos;
      if (manager.GetNamedPosition(deviceName, posName, pos)) {
        AddPosition(deviceName, posName, pos);
        count++;
      }
    }
  }

  std::cout << "[INFO] ProductPositionManager: Imported " << count
    << " positions from motion manager" << std::endl;
}

// ============================================================================
// GRAPH QUERY HELPERS (for UI components)
// ============================================================================

std::vector<std::string> ProductPositionManager::GetAllDevicesWithGraphs() const {
  QList<QString> qDevices = m_graphManager.getAllDevices();
  std::vector<std::string> devices;
  devices.reserve(qDevices.size());

  for (const QString& qDevice : qDevices) {
    devices.push_back(qDevice.toStdString());
  }

  return devices;
}

std::vector<std::string> ProductPositionManager::GetGraphNamesForDevice(
  const std::string& deviceName) const {

  QString qDevice = QString::fromStdString(deviceName);
  QList<QString> qGraphNames = m_graphManager.getGraphNames(qDevice);

  std::vector<std::string> graphNames;
  graphNames.reserve(qGraphNames.size());

  for (const QString& qName : qGraphNames) {
    graphNames.push_back(qName.toStdString());
  }

  return graphNames;
}

std::vector<std::string> ProductPositionManager::GetNodeNamesForGraph(
  const std::string& deviceName,
  const std::string& graphName) const {

  QString qDevice = QString::fromStdString(deviceName);
  QString qGraph = QString::fromStdString(graphName);

  const MotionGraph* graph = m_graphManager.getGraph(qDevice, qGraph);

  std::vector<std::string> nodeNames;

  if (graph) {
    QList<MotionNode> nodes = graph->getAllNodes();
    nodeNames.reserve(nodes.size());

    for (const MotionNode& node : nodes) {
      nodeNames.push_back(node.position.toStdString());
    }
  }

  return nodeNames;
}

bool ProductPositionManager::HasGraphsForDevice(const std::string& deviceName) const {
  return m_graphManager.hasDevice(QString::fromStdString(deviceName));
}

bool ProductPositionManager::HasGraph(const std::string& deviceName,
                                       const std::string& graphName) const {
  return m_graphManager.hasGraph(
    QString::fromStdString(deviceName),
    QString::fromStdString(graphName)
  );
}