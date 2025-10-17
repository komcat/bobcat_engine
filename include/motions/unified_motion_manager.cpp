// unified_motion_manager.cpp - Complete Implementation
#include "unified_motion_manager.h"
#include "MotionJsonLoader.h"
#include <iostream>
#include <algorithm>

UnifiedMotionManager::UnifiedMotionManager() {
  std::cout << "[INFO] UnifiedMotionManager: Initializing" << std::endl;

  // Register ACS manager
  RegisterManager("ACS", std::make_unique<ACSControllerManager>());

  // Register PI manager
  RegisterManager("PI", std::make_unique<PIControllerManager>());

  // Easy to add more in the future:
  // RegisterManager("NEWPORT", std::make_unique<NewportControllerManager>());
  // RegisterManager("THORLABS", std::make_unique<ThorlabsControllerManager>());

  std::cout << "[INFO] UnifiedMotionManager: Initialized with "
    << m_managers.size() << " controller managers" << std::endl;
}

UnifiedMotionManager::~UnifiedMotionManager() {
  std::cout << "[INFO] UnifiedMotionManager: Shutting down" << std::endl;
  DisconnectAll();
}

void UnifiedMotionManager::RegisterManager(const std::string& type,
  std::unique_ptr<IMotionControllerManager> manager) {
  std::string normalizedType = NormalizeType(type);
  m_managers[normalizedType] = std::move(manager);
  std::cout << "[INFO] UnifiedMotionManager: Registered " << type << " manager" << std::endl;
}

std::string UnifiedMotionManager::NormalizeType(const std::string& type) const {
  std::string normalized = type;
  std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::toupper);
  return normalized;
}

IMotionControllerManager* UnifiedMotionManager::GetManagerForDevice(const std::string& deviceName) {
  auto it = m_deviceTypes.find(deviceName);
  if (it == m_deviceTypes.end()) {
    return nullptr;
  }

  std::string normalizedType = NormalizeType(it->second);
  auto managerIt = m_managers.find(normalizedType);
  if (managerIt == m_managers.end()) {
    return nullptr;
  }

  return managerIt->second.get();
}

std::string UnifiedMotionManager::GetDeviceType(const std::string& deviceName) const {
  auto it = m_deviceTypes.find(deviceName);
  if (it != m_deviceTypes.end()) {
    return it->second;
  }
  return "";
}

// ============================================================================
// Device Management
// ============================================================================

void UnifiedMotionManager::AddDevice(const MotionDevice& device) {
  std::cout << "[INFO] UnifiedMotionManager: Adding device " << device.Name
    << " (Type: " << device.TypeController << ")" << std::endl;

  // Store full device configuration
  m_deviceConfigs[device.Name] = device;

  // Store device type for routing
  m_deviceTypes[device.Name] = device.TypeController;

  // Get the appropriate manager
  std::string normalizedType = NormalizeType(device.TypeController);
  auto managerIt = m_managers.find(normalizedType);

  if (managerIt != m_managers.end()) {
    managerIt->second->AddDevice(device);
    std::cout << "[INFO] UnifiedMotionManager: Routed " << device.Name
      << " to " << normalizedType << " manager" << std::endl;
  }
  else {
    std::cout << "[ERROR] UnifiedMotionManager: No manager found for type '"
      << device.TypeController << "'" << std::endl;
    std::cout << "[ERROR] UnifiedMotionManager: Supported types: ";
    for (const auto& [type, _] : m_managers) {
      std::cout << type << " ";
    }
    std::cout << std::endl;
  }
}


void UnifiedMotionManager::RemoveDevice(const std::string& deviceName) {
  IMotionControllerManager* manager = GetManagerForDevice(deviceName);
  if (!manager) {
    std::cout << "[WARNING] UnifiedMotionManager: Device " << deviceName
      << " not found" << std::endl;
    return;
  }

  std::string type = m_deviceTypes[deviceName];
  std::cout << "[INFO] UnifiedMotionManager: Removing device " << deviceName
    << " (Type: " << type << ")" << std::endl;

  manager->RemoveDevice(deviceName);
  m_deviceTypes.erase(deviceName);
  m_deviceConfigs.erase(deviceName);  // Remove config
  m_namedPositions.erase(deviceName); // Remove named positions

  std::cout << "[INFO] UnifiedMotionManager: Removed device " << deviceName << std::endl;
}


bool UnifiedMotionManager::GetDeviceConfig(const std::string& deviceName,
  MotionDevice& outDevice) const {
  auto it = m_deviceConfigs.find(deviceName);
  if (it != m_deviceConfigs.end()) {
    outDevice = it->second;
    return true;
  }
  return false;
}

std::string UnifiedMotionManager::GetInstalledAxes(const std::string& deviceName) const {
  auto it = m_deviceConfigs.find(deviceName);
  if (it != m_deviceConfigs.end()) {
    return it->second.InstalledAxes;
  }
  return "";
}

bool UnifiedMotionManager::HasDevice(const std::string& deviceName) const {
  return m_deviceTypes.find(deviceName) != m_deviceTypes.end();
}

// ============================================================================
// Connection Management
// ============================================================================

bool UnifiedMotionManager::ConnectAll() {
  std::cout << "[INFO] UnifiedMotionManager: Connecting all enabled devices" << std::endl;

  bool allSuccess = true;
  for (auto& [type, manager] : m_managers) {
    bool result = manager->ConnectAll();
    if (!result) {
      allSuccess = false;
    }
  }

  std::cout << "[INFO] UnifiedMotionManager: Connect all "
    << (allSuccess ? "succeeded" : "completed with errors") << std::endl;

  return allSuccess;
}

void UnifiedMotionManager::DisconnectAll() {
  std::cout << "[INFO] UnifiedMotionManager: Disconnecting all devices" << std::endl;

  for (auto& [type, manager] : m_managers) {
    manager->DisconnectAll();
  }

  std::cout << "[INFO] UnifiedMotionManager: All devices disconnected" << std::endl;
}

bool UnifiedMotionManager::ConnectDevice(const std::string& deviceName) {
  IMotionControllerManager* manager = GetManagerForDevice(deviceName);
  if (!manager) {
    std::cout << "[ERROR] UnifiedMotionManager: Device " << deviceName
      << " not found" << std::endl;
    return false;
  }

  std::cout << "[INFO] UnifiedMotionManager: Connecting device " << deviceName << std::endl;
  return manager->ConnectDevice(deviceName);
}

bool UnifiedMotionManager::DisconnectDevice(const std::string& deviceName) {
  IMotionControllerManager* manager = GetManagerForDevice(deviceName);
  if (!manager) {
    std::cout << "[ERROR] UnifiedMotionManager: Device " << deviceName
      << " not found" << std::endl;
    return false;
  }

  return manager->DisconnectDevice(deviceName);
}

bool UnifiedMotionManager::ReconnectDevice(const std::string& deviceName) {
  IMotionControllerManager* manager = GetManagerForDevice(deviceName);
  if (!manager) {
    std::cout << "[ERROR] UnifiedMotionManager: Device " << deviceName
      << " not found" << std::endl;
    return false;
  }

  return manager->ReconnectDevice(deviceName);
}

int UnifiedMotionManager::ConnectEnabledDevices() {
  std::cout << "[INFO] UnifiedMotionManager: Connecting enabled devices" << std::endl;

  int totalCount = 0;
  for (auto& [type, manager] : m_managers) {
    int count = manager->ConnectEnabledDevices();
    std::cout << "[INFO] UnifiedMotionManager: Connected " << count
      << " " << type << " devices" << std::endl;
    totalCount += count;
  }

  std::cout << "[INFO] UnifiedMotionManager: Total connected: " << totalCount << std::endl;
  return totalCount;
}

// ============================================================================
// MOTION CONTROL METHODS
// ============================================================================

bool UnifiedMotionManager::MoveToAbs(const std::string& deviceName,
  double x, double y, double z,
  double velocity,
  bool blocking) {
  std::string type = GetDeviceType(deviceName);
  if (type.empty()) {
    std::cout << "[ERROR] UnifiedMotionManager: Device " << deviceName
      << " not found" << std::endl;
    return false;
  }

  std::cout << "[INFO] UnifiedMotionManager: MoveToAbs on " << deviceName
    << " to (" << x << ", " << y << ", " << z << ")" << std::endl;

  if (NormalizeType(type) == "ACS") {
    ACSController* controller = GetACSController(deviceName);
    if (!controller) return false;
    return controller->MoveToAbsolutePosition(x, y, z, velocity, blocking);
  }
  else if (NormalizeType(type) == "PI") {
    PIController* controller = GetPIController(deviceName);
    if (!controller) return false;
    bool success = true;
    success &= controller->MoveToPosition("X", x, blocking);
    success &= controller->MoveToPosition("Y", y, blocking);
    success &= controller->MoveToPosition("Z", z, blocking);
    return success;
  }

  return false;
}

bool UnifiedMotionManager::MoveToPosition(const std::string& deviceName,
  const std::string& axis,
  double position,
  bool blocking) {
  std::string type = GetDeviceType(deviceName);
  if (type.empty()) {
    std::cout << "[ERROR] UnifiedMotionManager: Device " << deviceName
      << " not found" << std::endl;
    return false;
  }

  std::cout << "[INFO] UnifiedMotionManager: MoveToPosition on " << deviceName
    << " axis " << axis << " to " << position << std::endl;

  if (NormalizeType(type) == "ACS") {
    ACSController* controller = GetACSController(deviceName);
    if (!controller) return false;
    return controller->MoveToPosition(axis, position, blocking);
  }
  else if (NormalizeType(type) == "PI") {
    PIController* controller = GetPIController(deviceName);
    if (!controller) return false;
    return controller->MoveToPosition(axis, position, blocking);
  }

  return false;
}

bool UnifiedMotionManager::MoveToNamedPosition(const std::string& deviceName,
  const std::string& positionName,
  bool blocking) {
  // Check if device exists
  if (!HasDevice(deviceName)) {
    std::cout << "[ERROR] UnifiedMotionManager: Device " << deviceName
      << " not found" << std::endl;
    return false;
  }

  // Find the named position
  auto deviceIt = m_namedPositions.find(deviceName);
  if (deviceIt == m_namedPositions.end()) {
    std::cout << "[ERROR] UnifiedMotionManager: No named positions defined for device "
      << deviceName << std::endl;
    return false;
  }

  auto posIt = deviceIt->second.find(positionName);
  if (posIt == deviceIt->second.end()) {
    std::cout << "[ERROR] UnifiedMotionManager: Position '" << positionName
      << "' not found for device " << deviceName << std::endl;

    // List available positions
    std::vector<std::string> available = GetNamedPositionList(deviceName);
    if (!available.empty()) {
      std::cout << "[INFO] Available positions: ";
      for (const auto& name : available) {
        std::cout << "'" << name << "' ";
      }
      std::cout << std::endl;
    }
    return false;
  }

  // Get the position and device config
  PositionStruct pos = posIt->second;
  std::string installedAxes = GetInstalledAxes(deviceName);

  std::cout << "[INFO] UnifiedMotionManager: Moving " << deviceName
    << " to named position '" << positionName << "'"
    << (blocking ? " (blocking)" : " (non-blocking)") << std::endl;
  std::cout << "[INFO] Target position: X=" << pos.x << " Y=" << pos.y
    << " Z=" << pos.z << " U=" << pos.u << " V=" << pos.v << " W=" << pos.w << std::endl;

  // Get device type to determine how to move
  std::string type = GetDeviceType(deviceName);
  if (type.empty()) {
    std::cout << "[ERROR] UnifiedMotionManager: Cannot determine device type" << std::endl;
    return false;
  }

  bool success = true;

  if (NormalizeType(type) == "ACS") {
    ACSController* controller = GetACSController(deviceName);
    if (!controller) return false;

    // Move XYZ axes first
    success = controller->MoveToAbsolutePosition(pos.x, pos.y, pos.z, 100.0, blocking);

    // Move UVW axes individually if they exist
    if (installedAxes.find('U') != std::string::npos) {
      success &= controller->MoveToPosition("U", pos.u, blocking);
    }
    if (installedAxes.find('V') != std::string::npos) {
      success &= controller->MoveToPosition("V", pos.v, blocking);
    }
    if (installedAxes.find('W') != std::string::npos) {
      success &= controller->MoveToPosition("W", pos.w, blocking);
    }

  }
  else if (NormalizeType(type) == "PI") {
    PIController* controller = GetPIController(deviceName);
    if (!controller) return false;

    // PI moves all 6 axes at once using MoveToPositionAll
    success = controller->MoveToPositionAll(pos.x, pos.y, pos.z,
      pos.u, pos.v, pos.w,
      blocking);  // ✅ Use blocking parameter
  }
  else {
    std::cout << "[ERROR] UnifiedMotionManager: Unsupported controller type '"
      << type << "' for MoveToNamedPosition" << std::endl;
    return false;
  }

  if (success) {
    std::cout << "[INFO] UnifiedMotionManager: Successfully "
      << (blocking ? "moved" : "started moving")
      << " to position '" << positionName << "'" << std::endl;
  }
  else {
    std::cout << "[ERROR] UnifiedMotionManager: Failed to move to position '"
      << positionName << "'" << std::endl;
  }

  return success;
}


bool UnifiedMotionManager::MoveRelative(const std::string& deviceName,
  const std::string& axis,
  double distance,
  bool blocking) {
  std::string type = GetDeviceType(deviceName);
  if (type.empty()) {
    std::cout << "[ERROR] UnifiedMotionManager: Device " << deviceName
      << " not found" << std::endl;
    return false;
  }

  std::cout << "[INFO] UnifiedMotionManager: MoveRelative on " << deviceName
    << " axis " << axis << " by " << distance << std::endl;

  if (NormalizeType(type) == "ACS") {
    ACSController* controller = GetACSController(deviceName);
    if (!controller) return false;
    return controller->MoveRelative(axis, distance, blocking);
  }
  else if (NormalizeType(type) == "PI") {
    PIController* controller = GetPIController(deviceName);
    if (!controller) return false;
    return controller->MoveRelative(axis, distance, blocking);
  }

  return false;
}

bool UnifiedMotionManager::EmergencyStop(const std::string& deviceName) {
  std::string type = GetDeviceType(deviceName);
  if (type.empty()) {
    std::cout << "[ERROR] UnifiedMotionManager: Device " << deviceName
      << " not found" << std::endl;
    return false;
  }

  std::cout << "[WARNING] UnifiedMotionManager: EMERGENCY STOP on " << deviceName << std::endl;

  if (NormalizeType(type) == "ACS") {
    ACSController* controller = GetACSController(deviceName);
    if (!controller) return false;
    return controller->StopAllMotion();
  }
  else if (NormalizeType(type) == "PI") {
    PIController* controller = GetPIController(deviceName);
    if (!controller) return false;
    return controller->StopAllAxes();
  }

  return false;
}

bool UnifiedMotionManager::EmergencyStopAll() {
  std::cout << "[WARNING] UnifiedMotionManager: EMERGENCY STOP ALL DEVICES" << std::endl;

  bool success = true;

  // Stop all devices across all managers
  for (auto& [deviceName, type] : m_deviceTypes) {
    if (!EmergencyStop(deviceName)) {
      success = false;
    }
  }

  return success;
}

bool UnifiedMotionManager::StopAxis(const std::string& deviceName, const std::string& axis) {
  std::string type = GetDeviceType(deviceName);
  if (type.empty()) return false;

  if (NormalizeType(type) == "ACS") {
    ACSController* controller = GetACSController(deviceName);
    if (!controller) return false;
    return controller->StopAxis(axis);
  }
  else if (NormalizeType(type) == "PI") {
    PIController* controller = GetPIController(deviceName);
    if (!controller) return false;
    return controller->StopAxis(axis);
  }

  return false;
}

bool UnifiedMotionManager::HomeAxis(const std::string& deviceName, const std::string& axis) {
  std::string type = GetDeviceType(deviceName);
  if (type.empty()) return false;

  if (NormalizeType(type) == "ACS") {
    ACSController* controller = GetACSController(deviceName);
    if (!controller) return false;
    return controller->HomeAxis(axis);
  }
  else if (NormalizeType(type) == "PI") {
    PIController* controller = GetPIController(deviceName);
    if (!controller) return false;
    return controller->HomeAxis(axis);
  }

  return false;
}

bool UnifiedMotionManager::IsMoving(const std::string& deviceName, const std::string& axis) {
  std::string type = GetDeviceType(deviceName);
  if (type.empty()) return false;

  if (NormalizeType(type) == "ACS") {
    ACSController* controller = GetACSController(deviceName);
    if (!controller) return false;
    return controller->IsMoving(axis);
  }
  else if (NormalizeType(type) == "PI") {
    PIController* controller = GetPIController(deviceName);
    if (!controller) return false;
    return controller->IsMoving(axis);
  }

  return false;
}

bool UnifiedMotionManager::IsDeviceInMotion(const std::string& deviceName) {
  std::string type = GetDeviceType(deviceName);
  if (type.empty()) return false;

  if (NormalizeType(type) == "ACS") {
    ACSController* controller = GetACSController(deviceName);
    if (!controller) return false;
    return controller->IsInMotion();
  }
  else if (NormalizeType(type) == "PI") {
    PIController* controller = GetPIController(deviceName);
    if (!controller) return false;
    return controller->IsInMotion();
  }

  return false;
}

bool UnifiedMotionManager::GetPosition(const std::string& deviceName,
  const std::string& axis,
  double& position) {
  std::string type = GetDeviceType(deviceName);
  if (type.empty()) return false;

  if (NormalizeType(type) == "ACS") {
    ACSController* controller = GetACSController(deviceName);
    if (!controller) return false;
    return controller->GetPosition(axis, position);
  }
  else if (NormalizeType(type) == "PI") {
    PIController* controller = GetPIController(deviceName);
    if (!controller) return false;
    return controller->GetPosition(axis, position);
  }

  return false;
}

PositionStruct UnifiedMotionManager::GetCurrentPosition(const std::string& deviceName) {
  std::string type = GetDeviceType(deviceName);
  PositionStruct defaultPos = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };  // All 6 axes

  if (type.empty()) return defaultPos;

  if (NormalizeType(type) == "ACS") {
    ACSController* controller = GetACSController(deviceName);
    if (!controller) return defaultPos;
    return controller->GetCurrentPosition();
  }
  else if (NormalizeType(type) == "PI") {
    PIController* controller = GetPIController(deviceName);
    if (!controller) return defaultPos;
    return controller->GetCurrentPosition();
  }

  return defaultPos;
}

bool UnifiedMotionManager::SetVelocity(const std::string& deviceName,
  const std::string& axis,
  double velocity) {
  std::string type = GetDeviceType(deviceName);
  if (type.empty()) return false;

  if (NormalizeType(type) == "ACS") {
    ACSController* controller = GetACSController(deviceName);
    if (!controller) return false;
    return controller->SetVelocity(axis, velocity);
  }
  else if (NormalizeType(type) == "PI") {
    PIController* controller = GetPIController(deviceName);
    if (!controller) return false;
    return controller->SetVelocity(axis, velocity);
  }

  return false;
}

bool UnifiedMotionManager::GetVelocity(const std::string& deviceName,
  const std::string& axis,
  double& velocity) {
  std::string type = GetDeviceType(deviceName);
  if (type.empty()) return false;

  if (NormalizeType(type) == "ACS") {
    ACSController* controller = GetACSController(deviceName);
    if (!controller) return false;
    return controller->GetVelocity(axis, velocity);
  }
  else if (NormalizeType(type) == "PI") {
    PIController* controller = GetPIController(deviceName);
    if (!controller) return false;
    return controller->GetVelocity(axis, velocity);
  }

  return false;
}

bool UnifiedMotionManager::EnableServo(const std::string& deviceName,
  const std::string& axis,
  bool enable) {
  std::string type = GetDeviceType(deviceName);
  if (type.empty()) return false;

  if (NormalizeType(type) == "ACS") {
    ACSController* controller = GetACSController(deviceName);
    if (!controller) return false;
    return controller->EnableServo(axis, enable);
  }
  else if (NormalizeType(type) == "PI") {
    PIController* controller = GetPIController(deviceName);
    if (!controller) return false;
    // PI may not have servo enable/disable - adjust as needed
    return true;
  }

  return false;
}

// ============================================================================
// Device Queries
// ============================================================================

std::vector<std::string> UnifiedMotionManager::GetAllDeviceNames() const {
  std::vector<std::string> allNames;

  for (const auto& [type, manager] : m_managers) {
    std::vector<std::string> names = manager->GetDeviceNames();
    allNames.insert(allNames.end(), names.begin(), names.end());
  }

  return allNames;
}

std::vector<std::string> UnifiedMotionManager::GetDeviceNamesByType(const std::string& type) const {
  std::string normalizedType = NormalizeType(type);
  auto it = m_managers.find(normalizedType);

  if (it != m_managers.end()) {
    return it->second->GetDeviceNames();
  }

  return std::vector<std::string>();
}

std::vector<std::string> UnifiedMotionManager::GetConnectedDeviceNames() const {
  std::vector<std::string> connectedNames;

  for (const auto& [type, manager] : m_managers) {
    std::vector<std::string> names = manager->GetConnectedDeviceNames();
    connectedNames.insert(connectedNames.end(), names.begin(), names.end());
  }

  return connectedNames;
}

std::map<std::string, bool> UnifiedMotionManager::GetConnectionStatus() const {
  std::map<std::string, bool> allStatus;

  for (const auto& [type, manager] : m_managers) {
    std::map<std::string, bool> status = manager->GetConnectionStatus();
    allStatus.insert(status.begin(), status.end());
  }

  return allStatus;
}

size_t UnifiedMotionManager::GetTotalDeviceCount() const {
  size_t total = 0;
  for (const auto& [type, manager] : m_managers) {
    total += manager->GetControllerCount();
  }
  return total;
}

size_t UnifiedMotionManager::GetDeviceCountByType(const std::string& type) const {
  std::string normalizedType = NormalizeType(type);
  auto it = m_managers.find(normalizedType);

  if (it != m_managers.end()) {
    return it->second->GetControllerCount();
  }

  return 0;
}

size_t UnifiedMotionManager::GetConnectedDeviceCount() const {
  return GetConnectedDeviceNames().size();
}

std::vector<std::string> UnifiedMotionManager::GetSupportedControllerTypes() const {
  std::vector<std::string> types;
  for (const auto& [type, _] : m_managers) {
    types.push_back(type);
  }
  return types;
}

// ============================================================================
// Manager Access
// ============================================================================

IMotionControllerManager* UnifiedMotionManager::GetManagerByType(const std::string& type) {
  std::string normalizedType = NormalizeType(type);
  auto it = m_managers.find(normalizedType);

  if (it != m_managers.end()) {
    return it->second.get();
  }

  return nullptr;
}

ACSControllerManager* UnifiedMotionManager::GetACSManager() {
  auto it = m_managers.find("ACS");
  if (it != m_managers.end()) {
    return static_cast<ACSControllerManager*>(it->second.get());
  }
  return nullptr;
}

PIControllerManager* UnifiedMotionManager::GetPIManager() {
  auto it = m_managers.find("PI");
  if (it != m_managers.end()) {
    return static_cast<PIControllerManager*>(it->second.get());
  }
  return nullptr;
}

ACSController* UnifiedMotionManager::GetACSController(const std::string& deviceName) {
  std::string type = GetDeviceType(deviceName);
  if (NormalizeType(type) != "ACS") {
    if (!type.empty()) {
      std::cout << "[WARNING] UnifiedMotionManager: Device " << deviceName
        << " is not an ACS controller (Type: " << type << ")" << std::endl;
    }
    return nullptr;
  }

  ACSControllerManager* manager = GetACSManager();
  if (!manager) return nullptr;

  return manager->GetACSController(deviceName);
}

PIController* UnifiedMotionManager::GetPIController(const std::string& deviceName) {
  std::string type = GetDeviceType(deviceName);
  if (NormalizeType(type) != "PI") {
    if (!type.empty()) {
      std::cout << "[WARNING] UnifiedMotionManager: Device " << deviceName
        << " is not a PI controller (Type: " << type << ")" << std::endl;
    }
    return nullptr;
  }

  PIControllerManager* manager = GetPIManager();
  if (!manager) return nullptr;

  return manager->GetPIController(deviceName);
}

void* UnifiedMotionManager::GetController(const std::string& deviceName, std::string& outType) {
  outType = GetDeviceType(deviceName);
  if (outType.empty()) {
    return nullptr;
  }

  std::string normalizedType = NormalizeType(outType);

  if (normalizedType == "ACS") {
    return GetACSController(deviceName);
  }
  else if (normalizedType == "PI") {
    return GetPIController(deviceName);
  }

  return nullptr;
}

// ============================================================================
// NAMED POSITION MANAGEMENT
// ============================================================================

void UnifiedMotionManager::AddNamedPosition(const std::string& deviceName,
  const std::string& positionName,
  const PositionStruct& position) {
  m_namedPositions[deviceName][positionName] = position;
  std::cout << "[INFO] UnifiedMotionManager: Added named position '" << positionName
    << "' for device " << deviceName
    << " -> (" << position.x << ", " << position.y << ", " << position.z << ")"
    << std::endl;
}

void UnifiedMotionManager::AddNamedPosition(const std::string& deviceName,
  const std::string& positionName,
  double x, double y, double z,
  double u, double v, double w) {
  PositionStruct pos = { x, y, z, u, v, w };
  AddNamedPosition(deviceName, positionName, pos);
}

bool UnifiedMotionManager::RemoveNamedPosition(const std::string& deviceName,
  const std::string& positionName) {
  auto deviceIt = m_namedPositions.find(deviceName);
  if (deviceIt == m_namedPositions.end()) {
    std::cout << "[WARNING] UnifiedMotionManager: No named positions for device "
      << deviceName << std::endl;
    return false;
  }

  auto posIt = deviceIt->second.find(positionName);
  if (posIt == deviceIt->second.end()) {
    std::cout << "[WARNING] UnifiedMotionManager: Position '" << positionName
      << "' not found for device " << deviceName << std::endl;
    return false;
  }

  deviceIt->second.erase(posIt);
  std::cout << "[INFO] UnifiedMotionManager: Removed named position '" << positionName
    << "' from device " << deviceName << std::endl;
  return true;
}

bool UnifiedMotionManager::GetNamedPosition(const std::string& deviceName,
  const std::string& positionName,
  PositionStruct& outPosition) const {
  auto deviceIt = m_namedPositions.find(deviceName);
  if (deviceIt == m_namedPositions.end()) {
    return false;
  }

  auto posIt = deviceIt->second.find(positionName);
  if (posIt == deviceIt->second.end()) {
    return false;
  }

  outPosition = posIt->second;
  return true;
}

std::vector<std::string> UnifiedMotionManager::GetNamedPositionList(const std::string& deviceName) const {
  std::vector<std::string> names;

  auto deviceIt = m_namedPositions.find(deviceName);
  if (deviceIt != m_namedPositions.end()) {
    for (const auto& [name, pos] : deviceIt->second) {
      names.push_back(name);
    }
  }

  return names;
}

// In unified_motion_manager.cpp
bool UnifiedMotionManager::ReloadNamedPositions(const std::string& configPath) {
  std::cout << "[INFO] UnifiedMotionManager: Reloading named positions from "
    << configPath << std::endl;

  // Clear existing positions
  m_namedPositions.clear();

  // Reload using MotionJsonLoader
  return MotionJsonLoader::LoadFromFile(*this, configPath) > 0;
}

void UnifiedMotionManager::ClearNamedPositions(const std::string& deviceName) {
  auto it = m_namedPositions.find(deviceName);
  if (it != m_namedPositions.end()) {
    m_namedPositions.erase(it);
    std::cout << "[INFO] Cleared named positions for " << deviceName << std::endl;
  }
}