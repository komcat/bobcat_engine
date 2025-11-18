// pi_controller_manager.cpp
#include "pi_controller_manager.h"
#include "PIPivotJsonLoader.h"
#include <algorithm>
#include <cmath>

PIControllerManager::PIControllerManager() {
  std::cout << "[INFO] PIControllerManager: Initialized" << std::endl;
}

PIControllerManager::~PIControllerManager() {
  std::cout << "[INFO] PIControllerManager: Shutting down" << std::endl;
  DisconnectAll();
}

// ============================================================================
// Interface Implementation - Device Management
// ============================================================================

void PIControllerManager::AddDevice(const MotionDevice& device) {
  if (!ValidateDeviceConfig(device)) {
    std::cout << "[ERROR] PIControllerManager: Invalid device configuration for "
      << device.Name << std::endl;
    return;
  }

  if (m_controllers.find(device.Name) != m_controllers.end()) {
    std::cout << "[WARNING] PIControllerManager: Device " << device.Name
      << " already exists. Updating configuration." << std::endl;
    UpdateDeviceConfig(device);
    return;
  }

  std::cout << "[INFO] PIControllerManager: Adding device " << device.Name << std::endl;

  // Store configuration
  m_deviceConfigs[device.Name] = device;

  // Create controller instance
  auto controller = std::unique_ptr<PIController>(CreateController(device));

  if (controller) {
    m_controllers[device.Name] = std::move(controller);
    m_connectionStatus[device.Name] = false;
    LogDeviceInfo(device.Name, "Device added successfully");
  }
  else {
    std::cout << "[ERROR] PIControllerManager: Failed to create controller for "
      << device.Name << std::endl;
  }
}

void PIControllerManager::RemoveDevice(const std::string& deviceName) {
  auto it = m_controllers.find(deviceName);
  if (it == m_controllers.end()) {
    std::cout << "[WARNING] PIControllerManager: Device " << deviceName
      << " not found" << std::endl;
    return;
  }

  std::cout << "[INFO] PIControllerManager: Removing device " << deviceName << std::endl;

  // Disconnect if connected
  if (m_connectionStatus[deviceName]) {
    DisconnectDevice(deviceName);
  }

  // Remove from all maps
  m_controllers.erase(deviceName);
  m_deviceConfigs.erase(deviceName);
  m_connectionStatus.erase(deviceName);
  m_connectionErrors.erase(deviceName);

  LogDeviceInfo(deviceName, "Device removed");
}

bool PIControllerManager::HasDevice(const std::string& deviceName) const {
  return m_controllers.find(deviceName) != m_controllers.end();
}

// ============================================================================
// Interface Implementation - Connection Management
// ============================================================================

bool PIControllerManager::ConnectAll() {
  std::cout << "[INFO] PIControllerManager: Connecting all devices" << std::endl;

  bool allSuccess = true;
  int successCount = 0;
  int failCount = 0;

  for (auto& [deviceName, controller] : m_controllers) {
    if (ConnectDevice(deviceName)) {
      successCount++;
    }
    else {
      allSuccess = false;
      failCount++;
    }
  }

  std::cout << "[INFO] PIControllerManager: Connect all complete. "
    << "Success: " << successCount << ", Failed: " << failCount << std::endl;

  return allSuccess;
}

void PIControllerManager::DisconnectAll() {
  std::cout << "[INFO] PIControllerManager: Disconnecting all devices" << std::endl;

  for (auto& [deviceName, controller] : m_controllers) {
    if (controller && controller->IsConnected()) {
      controller->Disconnect();
      m_connectionStatus[deviceName] = false;
      LogDeviceInfo(deviceName, "Disconnected");
    }
  }

  std::cout << "[INFO] PIControllerManager: All devices disconnected" << std::endl;
}

bool PIControllerManager::ConnectDevice(const std::string& deviceName) {
  auto it = m_controllers.find(deviceName);
  if (it == m_controllers.end()) {
    std::cout << "[ERROR] PIControllerManager: Device " << deviceName
      << " not found" << std::endl;
    return false;
  }

  PIController* controller = it->second.get();
  if (!controller) {
    std::cout << "[ERROR] PIControllerManager: Controller instance is null for "
      << deviceName << std::endl;
    return false;
  }

  // Already connected?
  if (controller->IsConnected()) {
    std::cout << "[INFO] PIControllerManager: Device " << deviceName
      << " already connected" << std::endl;
    m_connectionStatus[deviceName] = true;
    return true;
  }

  std::cout << "[INFO] PIControllerManager: Connecting device " << deviceName << std::endl;

  // Get device config
  auto configIt = m_deviceConfigs.find(deviceName);
  if (configIt == m_deviceConfigs.end()) {
    std::cout << "[ERROR] PIControllerManager: No configuration found for "
      << deviceName << std::endl;
    return false;
  }

  const MotionDevice& config = configIt->second;

  // Attempt connection
  bool success = controller->Connect(config.IpAddress, config.Port);

  if (success) {
    m_connectionStatus[deviceName] = true;
    m_connectionErrors.erase(deviceName);
    LogDeviceInfo(deviceName, "Connected successfully");

    // Verify and set pivot point after successful connection
    VerifyAndSetPivotPoint(deviceName);
  }
  else {
    m_connectionStatus[deviceName] = false;
    m_connectionErrors[deviceName] = "Connection failed";
    std::cout << "[ERROR] PIControllerManager: Failed to connect " << deviceName << std::endl;
  }

  return success;
}

bool PIControllerManager::DisconnectDevice(const std::string& deviceName) {
  auto it = m_controllers.find(deviceName);
  if (it == m_controllers.end()) {
    std::cout << "[ERROR] PIControllerManager: Device " << deviceName
      << " not found" << std::endl;
    return false;
  }

  PIController* controller = it->second.get();
  if (!controller) {
    return false;
  }

  std::cout << "[INFO] PIControllerManager: Disconnecting device " << deviceName << std::endl;

  controller->Disconnect();
  m_connectionStatus[deviceName] = false;

  LogDeviceInfo(deviceName, "Disconnected");
  return true;
}

bool PIControllerManager::ReconnectDevice(const std::string& deviceName) {
  std::cout << "[INFO] PIControllerManager: Reconnecting device " << deviceName << std::endl;

  // Disconnect first
  DisconnectDevice(deviceName);

  // Small delay before reconnecting (optional)
  // std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Connect again
  return ConnectDevice(deviceName);
}

int PIControllerManager::ConnectEnabledDevices() {
  std::cout << "[INFO] PIControllerManager: Connecting enabled devices" << std::endl;

  int connectedCount = 0;

  for (auto& [deviceName, config] : m_deviceConfigs) {
    if (config.Enabled) {
      if (ConnectDevice(deviceName)) {
        connectedCount++;
      }
    }
    else {
      std::cout << "[INFO] PIControllerManager: Skipping disabled device "
        << deviceName << std::endl;
    }
  }

  std::cout << "[INFO] PIControllerManager: Connected " << connectedCount
    << " enabled devices" << std::endl;

  return connectedCount;
}

// ============================================================================
// Interface Implementation - Device Queries
// ============================================================================

std::vector<std::string> PIControllerManager::GetDeviceNames() const {
  std::vector<std::string> names;
  names.reserve(m_controllers.size());

  for (const auto& [name, _] : m_controllers) {
    names.push_back(name);
  }

  return names;
}

std::vector<std::string> PIControllerManager::GetConnectedDeviceNames() const {
  std::vector<std::string> connectedNames;

  for (const auto& [name, status] : m_connectionStatus) {
    if (status) {
      connectedNames.push_back(name);
    }
  }

  return connectedNames;
}

std::map<std::string, bool> PIControllerManager::GetConnectionStatus() const {
  return m_connectionStatus;
}

// ============================================================================
// Interface Implementation - Statistics
// ============================================================================

size_t PIControllerManager::GetControllerCount() const {
  return m_controllers.size();
}

size_t PIControllerManager::GetConnectedCount() const {
  return std::count_if(m_connectionStatus.begin(), m_connectionStatus.end(),
    [](const auto& pair) { return pair.second; });
}

// ============================================================================
// Interface Implementation - Controller Access
// ============================================================================

void* PIControllerManager::GetController(const std::string& deviceName) {
  return GetPIController(deviceName);
}

// ============================================================================
// Type-Safe Controller Access
// ============================================================================

PIController* PIControllerManager::GetPIController(const std::string& deviceName) {
  auto it = m_controllers.find(deviceName);
  if (it != m_controllers.end()) {
    return it->second.get();
  }

  std::cout << "[WARNING] PIControllerManager: Controller " << deviceName
    << " not found" << std::endl;
  return nullptr;
}

const PIController* PIControllerManager::GetPIController(const std::string& deviceName) const {
  auto it = m_controllers.find(deviceName);
  if (it != m_controllers.end()) {
    return it->second.get();
  }

  return nullptr;
}

// ============================================================================
// Additional Management Methods
// ============================================================================

bool PIControllerManager::IsDeviceConnected(const std::string& deviceName) const {
  auto it = m_connectionStatus.find(deviceName);
  if (it != m_connectionStatus.end()) {
    return it->second;
  }
  return false;
}

MotionDevice PIControllerManager::GetDeviceConfig(const std::string& deviceName) const {
  auto it = m_deviceConfigs.find(deviceName);
  if (it != m_deviceConfigs.end()) {
    return it->second;
  }

  // Return empty config if not found
  return MotionDevice();
}

bool PIControllerManager::UpdateDeviceConfig(const MotionDevice& device) {
  if (!HasDevice(device.Name)) {
    std::cout << "[ERROR] PIControllerManager: Cannot update config for non-existent device "
      << device.Name << std::endl;
    return false;
  }

  if (!ValidateDeviceConfig(device)) {
    std::cout << "[ERROR] PIControllerManager: Invalid device configuration" << std::endl;
    return false;
  }

  std::cout << "[INFO] PIControllerManager: Updating configuration for "
    << device.Name << std::endl;

  // Update configuration
  m_deviceConfigs[device.Name] = device;

  // If connected, may need to reconnect with new config
  if (IsDeviceConnected(device.Name)) {
    std::cout << "[INFO] PIControllerManager: Device is connected. "
      << "Reconnection may be needed for changes to take effect." << std::endl;
  }

  return true;
}

std::vector<std::string> PIControllerManager::GetFailedDevices() const {
  std::vector<std::string> failedDevices;

  for (const auto& [deviceName, error] : m_connectionErrors) {
    failedDevices.push_back(deviceName);
  }

  return failedDevices;
}

void PIControllerManager::ClearConnectionError(const std::string& deviceName) {
  m_connectionErrors.erase(deviceName);
}

// ============================================================================
// Helper Methods
// ============================================================================

PIController* PIControllerManager::CreateController(const MotionDevice& device) {
  try {
    auto controller = new PIController();

    // Configure controller with device settings
    if (controller->ConfigureFromDevice(device)) {
      return controller;
    }
    else {
      delete controller;
      std::cout << "[ERROR] PIControllerManager: Failed to configure controller for "
        << device.Name << std::endl;
      return nullptr;
    }
  }
  catch (const std::exception& e) {
    std::cout << "[ERROR] PIControllerManager: Exception creating controller: "
      << e.what() << std::endl;
    return nullptr;
  }
}

bool PIControllerManager::ValidateDeviceConfig(const MotionDevice& device) const {
  if (device.Name.empty()) {
    std::cout << "[ERROR] PIControllerManager: Device name cannot be empty" << std::endl;
    return false;
  }

  if (device.TypeController != "PI" && device.TypeController != "pi") {
    std::cout << "[ERROR] PIControllerManager: Invalid controller type: "
      << device.TypeController << " (expected PI)" << std::endl;
    return false;
  }

  if (device.IpAddress.empty()) {
    std::cout << "[ERROR] PIControllerManager: IP address cannot be empty for "
      << device.Name << std::endl;
    return false;
  }

  if (device.Port <= 0 || device.Port > 65535) {
    std::cout << "[ERROR] PIControllerManager: Invalid port " << device.Port
      << " for " << device.Name << std::endl;
    return false;
  }

  return true;
}

void PIControllerManager::LogDeviceInfo(const std::string& deviceName,
  const std::string& message) const {
  std::cout << "[INFO] PIControllerManager [" << deviceName << "]: "
    << message << std::endl;
}

bool PIControllerManager::LoadPivotConfigurations(const std::string& filepath) {
  std::cout << "📂 [INFO] PIControllerManager: Loading pivot configurations from "
    << filepath << std::endl;

  m_pivotConfigs = PIPivotJsonLoader::LoadFromFile(filepath);

  if (m_pivotConfigs.empty()) {
    std::cout << "⚠️ [WARNING] PIControllerManager: No pivot configurations loaded" << std::endl;
    return false;
  }

  std::cout << "✅ [INFO] PIControllerManager: Loaded " << m_pivotConfigs.size()
    << " pivot configurations" << std::endl;

  return true;
}

bool PIControllerManager::VerifyAndSetPivotPoint(const std::string& deviceName) {
  // Check if device exists
  auto controllerIt = m_controllers.find(deviceName);
  if (controllerIt == m_controllers.end()) {
    std::cout << "❌ [ERROR] PIControllerManager: Device " << deviceName
      << " not found" << std::endl;
    return false;
  }

  PIController* controller = controllerIt->second.get();
  if (!controller || !controller->IsConnected()) {
    std::cout << "❌ [ERROR] PIControllerManager: Device " << deviceName
      << " is not connected" << std::endl;
    return false;
  }

  // Check if pivot configuration exists for this device
  auto pivotIt = m_pivotConfigs.find(deviceName);
  if (pivotIt == m_pivotConfigs.end()) {
    std::cout << "ℹ️ [INFO] PIControllerManager: No pivot configuration found for "
      << deviceName << ". Skipping pivot verification." << std::endl;
    return true; // Not an error - just no config for this device
  }

  const PIPivotConfig& expectedPivot = pivotIt->second;

  std::cout << "🔍 [INFO] PIControllerManager: Verifying pivot point for " << deviceName << std::endl;

  // Read current pivot point from controller
  double currentX = 0.0, currentY = 0.0, currentZ = 0.0;
  if (!controller->GetPivotPoint(currentX, currentY, currentZ)) {
    std::cout << "❌ [ERROR] PIControllerManager: Failed to read pivot point from "
      << deviceName << std::endl;
    return false;
  }

  // Compare with expected values (use small tolerance for floating point comparison)
  const double tolerance = 0.001;
  bool xMatch = std::abs(currentX - expectedPivot.PivotX) < tolerance;
  bool yMatch = std::abs(currentY - expectedPivot.PivotY) < tolerance;
  bool zMatch = std::abs(currentZ - expectedPivot.PivotZ) < tolerance;

  if (xMatch && yMatch && zMatch) {
    std::cout << "✅ [INFO] PIControllerManager: Pivot point for " << deviceName
      << " matches expected values" << std::endl;
    return true;
  }

  // Pivot doesn't match - set the correct values
  std::cout << "⚠️ [WARNING] PIControllerManager: Pivot point mismatch for " << deviceName << std::endl;
  std::cout << "📍 [INFO] Current: X=" << currentX << ", Y=" << currentY << ", Z=" << currentZ << std::endl;
  std::cout << "🎯 [INFO] Expected: X=" << expectedPivot.PivotX << ", Y=" << expectedPivot.PivotY
    << ", Z=" << expectedPivot.PivotZ << std::endl;
  std::cout << "📝 [INFO] PIControllerManager: Setting correct pivot point for " << deviceName << std::endl;

  if (!controller->SetPivotPoint(expectedPivot.PivotX, expectedPivot.PivotY, expectedPivot.PivotZ)) {
    std::cout << "❌ [ERROR] PIControllerManager: Failed to set pivot point for "
      << deviceName << std::endl;
    return false;
  }

  std::cout << "✅ [INFO] PIControllerManager: Pivot point updated successfully for "
    << deviceName << std::endl;

  return true;
}