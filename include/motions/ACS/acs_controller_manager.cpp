// acs_controller_manager.cpp
#include "acs_controller_manager.h"
#include <algorithm>

ACSControllerManager::ACSControllerManager() {
  std::cout << "[INFO] ACSControllerManager: Initialized" << std::endl;
}

ACSControllerManager::~ACSControllerManager() {
  std::cout << "[INFO] ACSControllerManager: Shutting down" << std::endl;
  DisconnectAll();
}

// ============================================================================
// Interface Implementation - Device Management
// ============================================================================

void ACSControllerManager::AddDevice(const MotionDevice& device) {
  if (!ValidateDeviceConfig(device)) {
    std::cout << "[ERROR] ACSControllerManager: Invalid device configuration for "
      << device.Name << std::endl;
    return;
  }

  if (m_controllers.find(device.Name) != m_controllers.end()) {
    std::cout << "[WARNING] ACSControllerManager: Device " << device.Name
      << " already exists. Updating configuration." << std::endl;
    UpdateDeviceConfig(device);
    return;
  }

  std::cout << "[INFO] ACSControllerManager: Adding device " << device.Name << std::endl;

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
    std::cout << "[ERROR] ACSControllerManager: Failed to create controller for "
      << device.Name << std::endl;
  }
}

void ACSControllerManager::RemoveDevice(const std::string& deviceName) {
  auto it = m_controllers.find(deviceName);
  if (it == m_controllers.end()) {
    std::cout << "[WARNING] ACSControllerManager: Device " << deviceName
      << " not found" << std::endl;
    return;
  }

  std::cout << "[INFO] ACSControllerManager: Removing device " << deviceName << std::endl;

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
  std::cout << "[INFO] ACSControllerManager: Connecting all devices" << std::endl;

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

  std::cout << "[INFO] ACSControllerManager: Connect all complete. "
    << "Success: " << successCount << ", Failed: " << failCount << std::endl;

  return allSuccess;
}

void ACSControllerManager::DisconnectAll() {
  std::cout << "[INFO] ACSControllerManager: Disconnecting all devices" << std::endl;

  for (auto& [deviceName, controller] : m_controllers) {
    if (controller && controller->IsConnected()) {
      controller->Disconnect();
      m_connectionStatus[deviceName] = false;
      LogDeviceInfo(deviceName, "Disconnected");
    }
  }

  std::cout << "[INFO] ACSControllerManager: All devices disconnected" << std::endl;
}

bool ACSControllerManager::ConnectDevice(const std::string& deviceName) {
  auto it = m_controllers.find(deviceName);
  if (it == m_controllers.end()) {
    std::cout << "[ERROR] ACSControllerManager: Device " << deviceName
      << " not found" << std::endl;
    return false;
  }

  ACSController* controller = it->second.get();
  if (!controller) {
    std::cout << "[ERROR] ACSControllerManager: Controller instance is null for "
      << deviceName << std::endl;
    return false;
  }

  // Already connected?
  if (controller->IsConnected()) {
    std::cout << "[INFO] ACSControllerManager: Device " << deviceName
      << " already connected" << std::endl;
    m_connectionStatus[deviceName] = true;
    return true;
  }

  std::cout << "[INFO] ACSControllerManager: Connecting device " << deviceName << std::endl;

  // Get device config
  auto configIt = m_deviceConfigs.find(deviceName);
  if (configIt == m_deviceConfigs.end()) {
    std::cout << "[ERROR] ACSControllerManager: No configuration found for "
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
  }
  else {
    m_connectionStatus[deviceName] = false;
    m_connectionErrors[deviceName] = "Connection failed";
    std::cout << "[ERROR] ACSControllerManager: Failed to connect " << deviceName << std::endl;
  }

  return success;
}

bool ACSControllerManager::DisconnectDevice(const std::string& deviceName) {
  auto it = m_controllers.find(deviceName);
  if (it == m_controllers.end()) {
    std::cout << "[ERROR] ACSControllerManager: Device " << deviceName
      << " not found" << std::endl;
    return false;
  }

  ACSController* controller = it->second.get();
  if (!controller) {
    return false;
  }

  std::cout << "[INFO] ACSControllerManager: Disconnecting device " << deviceName << std::endl;

  controller->Disconnect();
  m_connectionStatus[deviceName] = false;

  LogDeviceInfo(deviceName, "Disconnected");
  return true;
}

bool ACSControllerManager::ReconnectDevice(const std::string& deviceName) {
  std::cout << "[INFO] ACSControllerManager: Reconnecting device " << deviceName << std::endl;

  // Disconnect first
  DisconnectDevice(deviceName);

  // Small delay before reconnecting (optional)
  // std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Connect again
  return ConnectDevice(deviceName);
}

int ACSControllerManager::ConnectEnabledDevices() {
  std::cout << "[INFO] ACSControllerManager: Connecting enabled devices" << std::endl;

  int connectedCount = 0;

  for (auto& [deviceName, config] : m_deviceConfigs) {
    if (config.Enabled) {
      if (ConnectDevice(deviceName)) {
        connectedCount++;
      }
    }
    else {
      std::cout << "[INFO] ACSControllerManager: Skipping disabled device "
        << deviceName << std::endl;
    }
  }

  std::cout << "[INFO] ACSControllerManager: Connected " << connectedCount
    << " enabled devices" << std::endl;

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

  std::cout << "[WARNING] ACSControllerManager: Controller " << deviceName
    << " not found" << std::endl;
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
    std::cout << "[ERROR] ACSControllerManager: Cannot update config for non-existent device "
      << device.Name << std::endl;
    return false;
  }

  if (!ValidateDeviceConfig(device)) {
    std::cout << "[ERROR] ACSControllerManager: Invalid device configuration" << std::endl;
    return false;
  }

  std::cout << "[INFO] ACSControllerManager: Updating configuration for "
    << device.Name << std::endl;

  // Update configuration
  m_deviceConfigs[device.Name] = device;

  // If connected, may need to reconnect with new config
  if (IsDeviceConnected(device.Name)) {
    std::cout << "[INFO] ACSControllerManager: Device is connected. "
      << "Reconnection may be needed for changes to take effect." << std::endl;
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
  std::cout << "[WARNING] ACSControllerManager: EMERGENCY STOP ALL DEVICES" << std::endl;

  bool allSuccess = true;

  for (auto& [deviceName, controller] : m_controllers) {
    if (controller && controller->IsConnected()) {
      if (!controller->StopAllMotion()) {
        allSuccess = false;
        std::cout << "[ERROR] ACSControllerManager: Failed to stop " << deviceName << std::endl;
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

    // Configure controller with device settings
    if (controller->ConfigureFromDevice(device)) {
      return controller;
    }
    else {
      delete controller;
      std::cout << "[ERROR] ACSControllerManager: Failed to configure controller for "
        << device.Name << std::endl;
      return nullptr;
    }
  }
  catch (const std::exception& e) {
    std::cout << "[ERROR] ACSControllerManager: Exception creating controller: "
      << e.what() << std::endl;
    return nullptr;
  }
}

bool ACSControllerManager::ValidateDeviceConfig(const MotionDevice& device) const {
  if (device.Name.empty()) {
    std::cout << "[ERROR] ACSControllerManager: Device name cannot be empty" << std::endl;
    return false;
  }

  if (device.TypeController != "ACS" && device.TypeController != "acs") {
    std::cout << "[ERROR] ACSControllerManager: Invalid controller type: "
      << device.TypeController << " (expected ACS)" << std::endl;
    return false;
  }

  if (device.IpAddress.empty()) {
    std::cout << "[ERROR] ACSControllerManager: IP address cannot be empty for "
      << device.Name << std::endl;
    return false;
  }

  if (device.Port <= 0 || device.Port > 65535) {
    std::cout << "[ERROR] ACSControllerManager: Invalid port " << device.Port
      << " for " << device.Name << std::endl;
    return false;
  }

  return true;
}

void ACSControllerManager::LogDeviceInfo(const std::string& deviceName,
  const std::string& message) const {
  std::cout << "[INFO] ACSControllerManager [" << deviceName << "]: "
    << message << std::endl;
}