// acs_controller_manager.cpp
#include "acs_controller_manager.h"
#include <algorithm>

#define LOG_DEBUG(msg) if (m_logger) m_logger->LogDebug(msg, "ACSControllerManager")
#define LOG_INFO(msg) if (m_logger) m_logger->LogInfo(msg, "ACSControllerManager")
#define LOG_WARNING(msg) if (m_logger) m_logger->LogWarning(msg, "ACSControllerManager")
#define LOG_ERROR(msg) if (m_logger) m_logger->LogError(msg, "ACSControllerManager")

ACSControllerManager::ACSControllerManager() {
  LOG_INFO("Initialized");
}

ACSControllerManager::~ACSControllerManager() {
  LOG_INFO("Shutting down");
  DisconnectAll();
}

// ============================================================================
// Interface Implementation - Device Management
// ============================================================================

void ACSControllerManager::AddDevice(const MotionDevice& device) {
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
  auto controller = std::unique_ptr<ACSController>(CreateController(device));

  if (controller) {
    m_controllers[device.Name] = std::move(controller);
    m_connectionStatus[device.Name] = false;
    LogDeviceInfo(device.Name, "Device added successfully");
  }
  else {
    LOG_ERROR("Failed to create controller for " + device.Name);
  }
}

void ACSControllerManager::RemoveDevice(const std::string& deviceName) {
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

bool ACSControllerManager::HasDevice(const std::string& deviceName) const {
  return m_controllers.find(deviceName) != m_controllers.end();
}

// ============================================================================
// Interface Implementation - Connection Management
// ============================================================================

bool ACSControllerManager::ConnectAll() {
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

void ACSControllerManager::DisconnectAll() {
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

bool ACSControllerManager::ConnectDevice(const std::string& deviceName) {
  auto it = m_controllers.find(deviceName);
  if (it == m_controllers.end()) {
    LOG_ERROR("Device " + deviceName + " not found");
    return false;
  }

  ACSController* controller = it->second.get();
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
  }
  else {
    m_connectionStatus[deviceName] = false;
    m_connectionErrors[deviceName] = "Connection failed";
    LOG_ERROR("Failed to connect " + deviceName);
  }

  return success;
}

bool ACSControllerManager::DisconnectDevice(const std::string& deviceName) {
  auto it = m_controllers.find(deviceName);
  if (it == m_controllers.end()) {
    LOG_ERROR("Device " + deviceName + " not found");
    return false;
  }

  ACSController* controller = it->second.get();
  if (!controller) {
    return false;
  }

  LOG_INFO("Disconnecting device " + deviceName);

  controller->Disconnect();
  m_connectionStatus[deviceName] = false;

  LogDeviceInfo(deviceName, "Disconnected");
  return true;
}

bool ACSControllerManager::ReconnectDevice(const std::string& deviceName) {
  LOG_INFO("Reconnecting device " + deviceName);

  // Disconnect first
  DisconnectDevice(deviceName);

  // Small delay before reconnecting (optional)
  // std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Connect again
  return ConnectDevice(deviceName);
}

int ACSControllerManager::ConnectEnabledDevices() {
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

std::vector<std::string> ACSControllerManager::GetDeviceNames() const {
  std::vector<std::string> names;
  names.reserve(m_controllers.size());

  for (const auto& [name, _] : m_controllers) {
    names.push_back(name);
  }

  return names;
}

std::vector<std::string> ACSControllerManager::GetConnectedDeviceNames() const {
  std::vector<std::string> connectedNames;

  for (const auto& [name, status] : m_connectionStatus) {
    if (status) {
      connectedNames.push_back(name);
    }
  }

  return connectedNames;
}

std::map<std::string, bool> ACSControllerManager::GetConnectionStatus() const {
  return m_connectionStatus;
}

// ============================================================================
// Interface Implementation - Statistics
// ============================================================================

size_t ACSControllerManager::GetControllerCount() const {
  return m_controllers.size();
}

size_t ACSControllerManager::GetConnectedCount() const {
  return std::count_if(m_connectionStatus.begin(), m_connectionStatus.end(),
    [](const auto& pair) { return pair.second; });
}

// ============================================================================
// Interface Implementation - Controller Access
// ============================================================================

void* ACSControllerManager::GetController(const std::string& deviceName) {
  return GetACSController(deviceName);
}

// ============================================================================
// Type-Safe Controller Access
// ============================================================================

ACSController* ACSControllerManager::GetACSController(const std::string& deviceName) {
  auto it = m_controllers.find(deviceName);
  if (it != m_controllers.end()) {
    return it->second.get();
  }

  LOG_WARNING("Controller " + deviceName + " not found");
  return nullptr;
}

const ACSController* ACSControllerManager::GetACSController(const std::string& deviceName) const {
  auto it = m_controllers.find(deviceName);
  if (it != m_controllers.end()) {
    return it->second.get();
  }

  return nullptr;
}

// ============================================================================
// Additional Management Methods
// ============================================================================

bool ACSControllerManager::IsDeviceConnected(const std::string& deviceName) const {
  auto it = m_connectionStatus.find(deviceName);
  if (it != m_connectionStatus.end()) {
    return it->second;
  }
  return false;
}

MotionDevice ACSControllerManager::GetDeviceConfig(const std::string& deviceName) const {
  auto it = m_deviceConfigs.find(deviceName);
  if (it != m_deviceConfigs.end()) {
    return it->second;
  }

  // Return empty config if not found
  return MotionDevice();
}

bool ACSControllerManager::UpdateDeviceConfig(const MotionDevice& device) {
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

std::vector<std::string> ACSControllerManager::GetFailedDevices() const {
  std::vector<std::string> failedDevices;

  for (const auto& [deviceName, error] : m_connectionErrors) {
    failedDevices.push_back(deviceName);
  }

  return failedDevices;
}

void ACSControllerManager::ClearConnectionError(const std::string& deviceName) {
  m_connectionErrors.erase(deviceName);
}

bool ACSControllerManager::EmergencyStopAll() {
  LOG_WARNING("EMERGENCY STOP ALL DEVICES");

  bool allSuccess = true;

  for (auto& [deviceName, controller] : m_controllers) {
    if (controller && controller->IsConnected()) {
      if (!controller->StopAllMotion()) {
        allSuccess = false;
        LOG_ERROR("Failed to stop " + deviceName);
      }
      else {
        LogDeviceInfo(deviceName, "Emergency stopped");
      }
    }
  }

  return allSuccess;
}

// ============================================================================
// Helper Methods
// ============================================================================

ACSController* ACSControllerManager::CreateController(const MotionDevice& device) {
  try {
    auto controller = new ACSController();

    // Propagate logger to new controller
    if (m_logger) {
      controller->SetLogger(m_logger);
    }

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

bool ACSControllerManager::ValidateDeviceConfig(const MotionDevice& device) const {
  if (device.Name.empty()) {
    LOG_ERROR("Device name cannot be empty");
    return false;
  }

  if (device.TypeController != "ACS" && device.TypeController != "acs") {
    LOG_ERROR("Invalid controller type: " + device.TypeController + " (expected ACS)");
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

void ACSControllerManager::LogDeviceInfo(const std::string& deviceName,
  const std::string& message) const {
  LOG_INFO("[" + deviceName + "]: " + message);
}

void ACSControllerManager::SetLogger(ILogger* logger) {
  m_logger = logger;
  // Propagate to all controllers
  for (auto& [name, controller] : m_controllers) {
    controller->SetLogger(logger);
  }
  LOG_INFO("Logger set for ACSControllerManager");
}