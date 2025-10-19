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

  // ✅ UPDATED - Log waypoints if present
  std::cout << "[INFO] UnifiedMotionManager: Added named position '" << positionName
    << "' for device " << deviceName
    << " -> (" << position.x << ", " << position.y << ", " << position.z << ")";

  if (!position.waypoints.empty()) {
    std::cout << " with " << position.waypoints.size() << " entry waypoint(s)";
  }
  if (!position.exit_waypoints.empty()) {
    std::cout << " with " << position.exit_waypoints.size() << " exit waypoint(s)";
  }
  std::cout << std::endl;
}

void UnifiedMotionManager::AddNamedPosition(const std::string& deviceName,
  const std::string& positionName,
  double x, double y, double z,
  double u, double v, double w) {
  // ✅ IMPORTANT - This overload creates position WITHOUT waypoints
  // If you need to preserve existing waypoints when updating coordinates only,
  // use the PositionStruct overload instead
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


// unified_motion_manager.cpp
// ✅ ADD THESE IMPLEMENTATIONS AT THE END OF YOUR FILE

// ============================================================================
// WAYPOINT SYSTEM IMPLEMENTATION
// ============================================================================

// REPLACE the existing MoveToNamedPosition() with this version:

bool UnifiedMotionManager::MoveToNamedPosition(const std::string& deviceName,
  const std::string& positionName,
  bool blocking) {
  // Check if device exists
  if (!HasDevice(deviceName)) {
    std::cout << "[ERROR] UnifiedMotionManager: Device " << deviceName
      << " not found" << std::endl;
    return false;
  }

  // ✅ FIX: Check if position exists OR can be calculated
  PositionStruct testPos;
  if (!GetPositionWithCalculation(deviceName, positionName, testPos)) {
    std::cout << "[ERROR] UnifiedMotionManager: Position '" << positionName
      << "' not found for device " << deviceName << std::endl;
    return false;
  }

  // ✅ BLOCKING MODE - Use old direct implementation
  if (blocking) {
    std::cout << "[INFO] UnifiedMotionManager: Blocking move - executing directly" << std::endl;
    return executeMoveToPosition(deviceName, positionName, true);
  }

  // ✅ NON-BLOCKING MODE - Use waypoint system
  std::cout << "[INFO] UnifiedMotionManager: Non-blocking move - using waypoint system" << std::endl;

  // Initialize waypoint state for this device if needed
  if (m_waypointStates.find(deviceName) == m_waypointStates.end()) {
    m_waypointStates[deviceName] = DeviceWaypointState();
  }

  DeviceWaypointState& state = m_waypointStates[deviceName];

  // Don't allow new movement if already in motion
  if (state.state != WaypointState::Idle) {
    std::cout << "[WARNING] UnifiedMotionManager: Device " << deviceName
      << " already in waypoint motion" << std::endl;
    return false;
  }

  // Clear any existing waypoints
  while (!state.waypointQueue.empty()) {
    state.waypointQueue.pop();
  }
  state.exitWaypointCount = 0;
  state.exitWaypointsProcessed = 0;

  // ✅ Get target position (may be calculated)
  PositionStruct targetPos;
  if (!GetPositionWithCalculation(deviceName, positionName, targetPos)) {
    std::cout << "[ERROR] UnifiedMotionManager: Failed to get position" << std::endl;
    return false;
  }

  state.targetPositionName = positionName;

  // Auto-detect current position if not set
  if (state.currentPositionName.empty()) {
    state.currentPositionName = detectCurrentPositionName(deviceName);
    if (!state.currentPositionName.empty()) {
      std::cout << "[INFO] UnifiedMotionManager: Auto-detected current position: "
        << state.currentPositionName << std::endl;
    }
  }

  bool hasExitWaypoints = false;
  bool hasEntryWaypoints = !targetPos.waypoints.empty();  // ✅ Calculated positions have no waypoints

  // Queue exit waypoints from current position (only if current is NOT calculated)
  if (!state.currentPositionName.empty() &&
    state.currentPositionName != positionName) {

    // ✅ Only get exit waypoints if current position is NOT a calculated one
    std::string baseName;
    double ox, oy, oz;
    if (!ParseCalculatedPosition(state.currentPositionName, baseName, ox, oy, oz)) {
      // Current position is not calculated, can have exit waypoints
      PositionStruct currentPos;
      if (GetNamedPosition(deviceName, state.currentPositionName, currentPos)) {
        if (!currentPos.exit_waypoints.empty()) {
          std::cout << "[INFO] UnifiedMotionManager: Queueing "
            << currentPos.exit_waypoints.size()
            << " exit waypoints from " << state.currentPositionName << std::endl;

          for (const auto& wp : currentPos.exit_waypoints) {
            state.waypointQueue.push(wp);
          }
          state.exitWaypointCount = currentPos.exit_waypoints.size();
          hasExitWaypoints = true;
        }
      }
    }
    else {
      std::cout << "[INFO] UnifiedMotionManager: Current position is calculated, skipping exit waypoints" << std::endl;
    }
  }

  // Queue entry waypoints to target (calculated positions won't have any)
  if (hasEntryWaypoints) {
    std::cout << "[INFO] UnifiedMotionManager: Queueing "
      << targetPos.waypoints.size()
      << " entry waypoints to " << positionName << std::endl;

    for (const auto& wp : targetPos.waypoints) {
      state.waypointQueue.push(wp);
    }
  }

  // Determine initial state and start
  if (hasExitWaypoints) {
    state.state = WaypointState::MovingThroughExitWaypoints;
    return startWaypointSequence(deviceName);
  }
  else if (hasEntryWaypoints) {
    state.state = WaypointState::MovingThroughEntryWaypoints;
    return startWaypointSequence(deviceName);
  }
  else {
    // No waypoints - move directly to target
    std::cout << "[INFO] UnifiedMotionManager: No waypoints, moving directly to target" << std::endl;
    state.state = WaypointState::MovingToFinalTarget;
    bool success = executeMoveToPosition(deviceName, positionName, false);
    if (success) {
      state.currentPositionName = positionName;
    }
    return success;
  }
}



bool UnifiedMotionManager::startWaypointSequence(const std::string& deviceName) {
  auto it = m_waypointStates.find(deviceName);
  if (it == m_waypointStates.end()) {
    return false;
  }

  DeviceWaypointState& state = it->second;

  if (state.waypointQueue.empty()) {
    std::cout << "[ERROR] UnifiedMotionManager: startWaypointSequence called with empty queue"
      << std::endl;
    state.state = WaypointState::Idle;
    return false;
  }

  // Get next waypoint
  std::string nextWaypoint = state.waypointQueue.front();
  state.waypointQueue.pop();

  std::cout << "[INFO] UnifiedMotionManager: Moving to waypoint: " << nextWaypoint << std::endl;

  // Execute move
  bool success = executeMoveToPosition(deviceName, nextWaypoint, false);

  if (!success) {
    std::cout << "[ERROR] UnifiedMotionManager: Failed to move to waypoint: "
      << nextWaypoint << std::endl;
    stopWaypointSequence(deviceName);
    return false;
  }

  return true;
}

void UnifiedMotionManager::Update() {
  // Update all device waypoint state machines
  for (auto& [deviceName, state] : m_waypointStates) {
    if (state.state != WaypointState::Idle) {
      updateDeviceWaypoints(deviceName);
    }
  }
}

void UnifiedMotionManager::updateDeviceWaypoints(const std::string& deviceName) {
  auto it = m_waypointStates.find(deviceName);
  if (it == m_waypointStates.end()) {
    return;
  }

  DeviceWaypointState& state = it->second;

  // Check if device is still moving
  if (IsDeviceInMotion(deviceName)) {
    return;  // Still moving, wait
  }

  // Movement complete - process next state
  switch (state.state) {

  case WaypointState::MovingThroughExitWaypoints:
    std::cout << "[INFO] UnifiedMotionManager: Exit waypoint reached" << std::endl;
    state.exitWaypointsProcessed++;

    if (state.exitWaypointsProcessed >= state.exitWaypointCount) {
      std::cout << "[INFO] UnifiedMotionManager: All exit waypoints complete" << std::endl;

      if (state.waypointQueue.empty()) {
        // No entry waypoints - go to final target
        state.state = WaypointState::MovingToFinalTarget;
        executeMoveToPosition(deviceName, state.targetPositionName, false);
      }
      else {
        // Switch to entry waypoints
        state.state = WaypointState::MovingThroughEntryWaypoints;
        startWaypointSequence(deviceName);
      }
    }
    else {
      // More exit waypoints
      startWaypointSequence(deviceName);
    }
    break;

  case WaypointState::MovingThroughEntryWaypoints:
    std::cout << "[INFO] UnifiedMotionManager: Entry waypoint reached" << std::endl;

    if (state.waypointQueue.empty()) {
      // All entry waypoints done - move to final target
      std::cout << "[INFO] UnifiedMotionManager: Moving to final target: "
        << state.targetPositionName << std::endl;
      state.state = WaypointState::MovingToFinalTarget;
      executeMoveToPosition(deviceName, state.targetPositionName, false);
    }
    else {
      // More entry waypoints
      startWaypointSequence(deviceName);
    }
    break;

  case WaypointState::MovingToFinalTarget:
    std::cout << "[INFO] UnifiedMotionManager: Final target reached: "
      << state.targetPositionName << std::endl;
    state.currentPositionName = state.targetPositionName;
    state.state = WaypointState::Idle;
    break;

  case WaypointState::Idle:
    break;
  }
}

void UnifiedMotionManager::stopWaypointSequence(const std::string& deviceName) {
  auto it = m_waypointStates.find(deviceName);
  if (it == m_waypointStates.end()) {
    return;
  }

  DeviceWaypointState& state = it->second;

  std::cout << "[INFO] UnifiedMotionManager: Stopping waypoint sequence for "
    << deviceName << std::endl;

  // Clear queue
  while (!state.waypointQueue.empty()) {
    state.waypointQueue.pop();
  }

  state.state = WaypointState::Idle;
}

bool UnifiedMotionManager::executeMoveToPosition(const std::string& deviceName,
  const std::string& positionName,
  bool blocking) {
  // ✅ Use GetPositionWithCalculation instead of GetNamedPosition
  PositionStruct pos;
  if (!GetPositionWithCalculation(deviceName, positionName, pos)) {
    std::cout << "[ERROR] executeMoveToPosition: Position '" << positionName
      << "' not found for device " << deviceName << std::endl;
    return false;
  }

  std::string installedAxes = GetInstalledAxes(deviceName);
  std::string type = GetDeviceType(deviceName);
  bool success = true;

  if (NormalizeType(type) == "ACS") {
    ACSController* controller = GetACSController(deviceName);
    if (!controller) return false;

    success = controller->MoveToAbsolutePosition(pos.x, pos.y, pos.z, 100.0, blocking);

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

    success = controller->MoveToPositionAll(pos.x, pos.y, pos.z,
      pos.u, pos.v, pos.w,
      blocking);
  }

  return success;
}


std::string UnifiedMotionManager::detectCurrentPositionName(const std::string& deviceName,
  double tolerance) {
  PositionStruct currentPos = GetCurrentPosition(deviceName);

  // Check all saved positions
  auto deviceIt = m_namedPositions.find(deviceName);
  if (deviceIt == m_namedPositions.end()) {
    return "";
  }

  for (const auto& [posName, pos] : deviceIt->second) {
    double dx = std::abs(currentPos.x - pos.x);
    double dy = std::abs(currentPos.y - pos.y);
    double dz = std::abs(currentPos.z - pos.z);

    if (dx < tolerance && dy < tolerance && dz < tolerance) {
      return posName;
    }
  }

  return "";  // Not at any known position
}

std::string UnifiedMotionManager::GetCurrentPositionName(const std::string& deviceName) const {
  auto it = m_waypointStates.find(deviceName);
  if (it != m_waypointStates.end()) {
    return it->second.currentPositionName;
  }
  return "";
}

void UnifiedMotionManager::SetCurrentPositionName(const std::string& deviceName,
  const std::string& positionName) {
  if (m_waypointStates.find(deviceName) == m_waypointStates.end()) {
    m_waypointStates[deviceName] = DeviceWaypointState();
  }
  m_waypointStates[deviceName].currentPositionName = positionName;
  std::cout << "[INFO] UnifiedMotionManager: Set current position of "
    << deviceName << " to " << positionName << std::endl;
}

bool UnifiedMotionManager::IsWaypointSystemActive(const std::string& deviceName) const {
  auto it = m_waypointStates.find(deviceName);
  if (it != m_waypointStates.end()) {
    return it->second.state != WaypointState::Idle;
  }
  return false;
}

void UnifiedMotionManager::CancelWaypointSequence(const std::string& deviceName) {
  stopWaypointSequence(deviceName);
  EmergencyStop(deviceName);
}

// ============================================================================
// CALCULATED POSITIONS IMPLEMENTATION
// ============================================================================

bool UnifiedMotionManager::ParseCalculatedPosition(const std::string& positionName,
  std::string& baseName,
  double& offsetX,
  double& offsetY,
  double& offsetZ) {
  // Check for '+' character indicating offset
  size_t plusPos = positionName.find('+');
  if (plusPos == std::string::npos) {
    return false;  // Not a calculated position
  }

  // Extract base name
  baseName = positionName.substr(0, plusPos);

  // Initialize offsets
  offsetX = 0.0;
  offsetY = 0.0;
  offsetZ = 0.0;

  // Parse offset string (e.g. "+Y1" or "+X-2+Z5.5" or "+Y-1.00")
  std::string remaining = positionName.substr(plusPos);

  size_t pos = 0;
  while (pos < remaining.length()) {
    // Skip '+' at start of new offset
    if (remaining[pos] == '+') {
      pos++;
    }

    // Get axis (X, Y, or Z)
    if (pos >= remaining.length()) break;

    char axis = std::toupper(remaining[pos]);
    if (axis != 'X' && axis != 'Y' && axis != 'Z') {
      std::cout << "[ERROR] ParseCalculatedPosition: Invalid axis '" << axis
        << "' in position name: " << positionName << std::endl;
      return false;
    }
    pos++;

    // ✅ FIX: Get value - find next '+' that's not part of the number
    // We need to handle cases like "+Y-1.00+Z2" correctly
    size_t nextPlus = pos;

    // Skip the first character if it's a minus sign (part of the value)
    if (nextPlus < remaining.length() && remaining[nextPlus] == '-') {
      nextPlus++;
    }

    // Find the next '+' after any leading minus sign
    while (nextPlus < remaining.length() && remaining[nextPlus] != '+') {
      nextPlus++;
    }

    std::string valueStr;
    if (nextPlus < remaining.length()) {
      valueStr = remaining.substr(pos, nextPlus - pos);
    }
    else {
      valueStr = remaining.substr(pos);  // Rest of string
    }

    try {
      double value = std::stod(valueStr);

      if (axis == 'X') offsetX = value;
      else if (axis == 'Y') offsetY = value;
      else if (axis == 'Z') offsetZ = value;

    }
    catch (const std::exception& e) {
      std::cout << "[ERROR] ParseCalculatedPosition: Invalid value '" << valueStr
        << "' for axis " << axis << " in: " << positionName << std::endl;
      return false;
    }

    if (nextPlus >= remaining.length()) break;
    pos = nextPlus;  // Move to next '+' position
  }

  std::cout << "[DEBUG] ParseCalculatedPosition: '" << positionName
    << "' -> base='" << baseName << "' offsets=("
    << offsetX << ", " << offsetY << ", " << offsetZ << ")" << std::endl;

  return true;
}


bool UnifiedMotionManager::GetPositionWithCalculation(const std::string& deviceName,
  const std::string& positionName,
  PositionStruct& outPosition) {
  std::string baseName;
  double offsetX, offsetY, offsetZ;

  // Check if this is a calculated position
  if (ParseCalculatedPosition(positionName, baseName, offsetX, offsetY, offsetZ)) {
    // Get base position
    PositionStruct basePos;
    if (!GetNamedPosition(deviceName, baseName, basePos)) {
      std::cout << "[ERROR] GetPositionWithCalculation: Base position '"
        << baseName << "' not found for device " << deviceName << std::endl;
      return false;
    }

    // Calculate offset position
    outPosition = basePos;
    outPosition.x += offsetX;
    outPosition.y += offsetY;
    outPosition.z += offsetZ;

    // ✅ IMPORTANT: Clear waypoints for calculated positions
    // Calculated positions shouldn't inherit waypoints from base
    outPosition.waypoints.clear();
    outPosition.exit_waypoints.clear();

    std::cout << "[INFO] GetPositionWithCalculation: Calculated position '"
      << positionName << "' from base '" << baseName << "'"
      << " with offset (" << offsetX << ", " << offsetY << ", " << offsetZ << ")"
      << " -> (" << outPosition.x << ", " << outPosition.y << ", " << outPosition.z << ")"
      << std::endl;

    return true;
  }

  // Not a calculated position, get normally
  return GetNamedPosition(deviceName, positionName, outPosition);
}

std::vector<std::string> UnifiedMotionManager::GetAllNodeNames(const std::string& deviceName) const {
  // Return all named positions for this device
  return GetNamedPositionList(deviceName);
}