// pi_controller_manager.cpp
#include "pi_controller_manager.h"
#include "PIPivotJsonLoader.h"
#include <algorithm>
#include <cmath>

#define LOG_DEBUG(msg) if (m_logger) m_logger->LogDebug(msg, "PIControllerManager")
#define LOG_INFO(msg) if (m_logger) m_logger->LogInfo(msg, "PIControllerManager")
#define LOG_WARNING(msg) if (m_logger) m_logger->LogWarning(msg, "PIControllerManager")
#define LOG_ERROR(msg) if (m_logger) m_logger->LogError(msg, "PIControllerManager")

PIControllerManager::PIControllerManager() {
  LOG_INFO("Initialized");
}

PIControllerManager::~PIControllerManager() {
  LOG_INFO("Shutting down");
  DisconnectAll();
}

void PIControllerManager::SetLogger(ILogger* logger)
{
    m_logger = logger;
    // Propagate to all controllers
    for (auto& [name, controller] : m_controllers) {
        if (controller) {
            controller->SetLogger(logger);
        }
    }
    LOG_INFO("Logger initialized");
}

// ============================================================================
// Interface Implementation - Device Management
// ============================================================================

void PIControllerManager::AddDevice(const MotionDevice& device) {
  if (!ValidateDeviceConfig(device)) {
    LOG_ERROR("Invalid device configuration for " + device.Name);
    return;
  }

  if (m_controllers.find(device.Name) != m_controllers.end()) {
    LOG_WARNING("Device " + device.Name + " already exists. Updating configuration.");
    UpdateDeviceConfig(device);
    return;
  }

  LOG_INFO("Adding device " + device.Name);

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
    LOG_ERROR("Failed to create controller for " + device.Name);
  }
}

void PIControllerManager::RemoveDevice(const std::string& deviceName) {
  auto it = m_controllers.find(deviceName);
  if (it == m_controllers.end()) {
    LOG_WARNING("Device " + deviceName + " not found");
    return;
  }

  LOG_INFO("Removing device " + deviceName);

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
  LOG_INFO("Connecting all devices");

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

  LOG_INFO("Connect all complete. Success: " + std::to_string(successCount) + ", Failed: " + std::to_string(failCount));

  return allSuccess;
}

void PIControllerManager::DisconnectAll() {
  LOG_INFO("Disconnecting all devices");

  for (auto& [deviceName, controller] : m_controllers) {
    if (controller && controller->IsConnected()) {
      controller->Disconnect();
      m_connectionStatus[deviceName] = false;
      LogDeviceInfo(deviceName, "Disconnected");
    }
  }

  LOG_INFO("All devices disconnected");
}

bool PIControllerManager::ConnectDevice(const std::string& deviceName) {
  auto it = m_controllers.find(deviceName);
  if (it == m_controllers.end()) {
    LOG_ERROR("Device " + deviceName + " not found");
    return false;
  }

  PIController* controller = it->second.get();
  if (!controller) {
    LOG_ERROR("Controller instance is null for " + deviceName);
    return false;
  }

  // Already connected?
  if (controller->IsConnected()) {
    LOG_INFO("Device " + deviceName + " already connected");
    m_connectionStatus[deviceName] = true;
    return true;
  }

  LOG_INFO("Connecting device " + deviceName);

  // Get device config
  auto configIt = m_deviceConfigs.find(deviceName);
  if (configIt == m_deviceConfigs.end()) {
    LOG_ERROR("No configuration found for " + deviceName);
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
    LOG_ERROR("Failed to connect " + deviceName);
  }

  return success;
}

bool PIControllerManager::DisconnectDevice(const std::string& deviceName) {
  auto it = m_controllers.find(deviceName);
  if (it == m_controllers.end()) {
    LOG_ERROR("Device " + deviceName + " not found");
    return false;
  }

  PIController* controller = it->second.get();
  if (!controller) {
    return false;
  }

  LOG_INFO("Disconnecting device " + deviceName);

  controller->Disconnect();
  m_connectionStatus[deviceName] = false;

  LogDeviceInfo(deviceName, "Disconnected");
  return true;
}

bool PIControllerManager::ReconnectDevice(const std::string& deviceName) {
  LOG_INFO("Reconnecting device " + deviceName);

  // Disconnect first
  DisconnectDevice(deviceName);

  // Small delay before reconnecting (optional)
  // std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Connect again
  return ConnectDevice(deviceName);
}

int PIControllerManager::ConnectEnabledDevices() {
  LOG_INFO("Connecting enabled devices");

  int connectedCount = 0;

  for (auto& [deviceName, config] : m_deviceConfigs) {
    if (config.Enabled) {
      if (ConnectDevice(deviceName)) {
        connectedCount++;
      }
    }
    else {
      LOG_INFO("Skipping disabled device " + deviceName);
    }
  }

  LOG_INFO("Connected " + std::to_string(connectedCount) + " enabled devices");

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

  LOG_WARNING("Controller " + deviceName + " not found");
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
    LOG_ERROR("Cannot update config for non-existent device " + device.Name);
    return false;
  }

  if (!ValidateDeviceConfig(device)) {
    LOG_ERROR("Invalid device configuration");
    return false;
  }

  LOG_INFO("Updating configuration for " + device.Name);

  // Update configuration
  m_deviceConfigs[device.Name] = device;

  // If connected, may need to reconnect with new config
  if (IsDeviceConnected(device.Name)) {
    LOG_INFO("Device is connected. Reconnection may be needed for changes to take effect.");
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
      LOG_ERROR("Failed to configure controller for " + device.Name);
      return nullptr;
    }
  }
  catch (const std::exception& e) {
    LOG_ERROR("Exception creating controller: " + std::string(e.what()));
    return nullptr;
  }
}

bool PIControllerManager::ValidateDeviceConfig(const MotionDevice& device) const {
  if (device.Name.empty()) {
    LOG_ERROR("Device name cannot be empty");
    return false;
  }

  if (device.TypeController != "PI" && device.TypeController != "pi") {
    LOG_ERROR("Invalid controller type: " + device.TypeController + " (expected PI)");
    return false;
  }

  if (device.IpAddress.empty()) {
    LOG_ERROR("IP address cannot be empty for " + device.Name);
    return false;
  }

  if (device.Port <= 0 || device.Port > 65535) {
    LOG_ERROR("Invalid port " + std::to_string(device.Port) + " for " + device.Name);
    return false;
  }

  return true;
}

void PIControllerManager::LogDeviceInfo(const std::string& deviceName,
  const std::string& message) const {
  LOG_INFO("[" + deviceName + "]: " + message);
}

bool PIControllerManager::LoadPivotConfigurations(const std::string& filepath) {
  LOG_INFO("Loading pivot configurations from " + filepath);

  m_pivotConfigs = PIPivotJsonLoader::LoadFromFile(filepath);

  if (m_pivotConfigs.empty()) {
    LOG_WARNING("No pivot configurations loaded");
    return false;
  }

  LOG_INFO("Loaded " + std::to_string(m_pivotConfigs.size()) + " pivot configurations");

  return true;
}

bool PIControllerManager::VerifyAndSetPivotPoint(const std::string& deviceName) {
  // Check if device exists
  auto controllerIt = m_controllers.find(deviceName);
  if (controllerIt == m_controllers.end()) {
    LOG_ERROR("Device " + deviceName + " not found");
    return false;
  }

  PIController* controller = controllerIt->second.get();
  if (!controller || !controller->IsConnected()) {
    LOG_ERROR("Device " + deviceName + " is not connected");
    return false;
  }

  // Check if pivot configuration exists for this device
  auto pivotIt = m_pivotConfigs.find(deviceName);
  if (pivotIt == m_pivotConfigs.end()) {
    LOG_INFO("No pivot configuration found for " + deviceName + ". Skipping pivot verification.");
    return true; // Not an error - just no config for this device
  }

  const PIPivotConfig& expectedPivot = pivotIt->second;

  LOG_INFO("Verifying pivot point for " + deviceName);

  // Read current pivot point from controller
  double currentX = 0.0, currentY = 0.0, currentZ = 0.0;
  if (!controller->GetPivotPoint(currentX, currentY, currentZ)) {
    LOG_ERROR("Failed to read pivot point from " + deviceName);
    return false;
  }

  // Compare with expected values (use small tolerance for floating point comparison)
  const double tolerance = 0.001;
  bool xMatch = std::abs(currentX - expectedPivot.PivotX) < tolerance;
  bool yMatch = std::abs(currentY - expectedPivot.PivotY) < tolerance;
  bool zMatch = std::abs(currentZ - expectedPivot.PivotZ) < tolerance;

  if (xMatch && yMatch && zMatch) {
    LOG_INFO("Pivot point for " + deviceName + " matches expected values");
    return true;
  }

  // Pivot doesn't match - set the correct values
  LOG_WARNING("Pivot point mismatch for " + deviceName);
  LOG_INFO("Current: X=" + std::to_string(currentX) + ", Y=" + std::to_string(currentY) + ", Z=" + std::to_string(currentZ));
  LOG_INFO("Expected: X=" + std::to_string(expectedPivot.PivotX) + ", Y=" + std::to_string(expectedPivot.PivotY) + ", Z=" + std::to_string(expectedPivot.PivotZ));
  LOG_INFO("Setting correct pivot point for " + deviceName);

  if (!controller->SetPivotPoint(expectedPivot.PivotX, expectedPivot.PivotY, expectedPivot.PivotZ)) {
    LOG_ERROR("Failed to set pivot point for " + deviceName);
    return false;
  }

  LOG_INFO("Pivot point updated successfully for " + deviceName);

  return true;
}