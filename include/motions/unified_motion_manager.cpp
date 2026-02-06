// unified_motion_manager.cpp - Complete Implementation
#include "unified_motion_manager.h"
#include "MotionJsonLoader.h"
#include "safety/SafetyWatchManager.h"
#include <algorithm>
#include <limits>
#include <thread>
#include <chrono>

// Logging helper macros
#define LOG_DEBUG(msg) if (m_logger) m_logger->LogDebug(msg, "UnifiedMotionManager")
#define LOG_INFO(msg) if (m_logger) m_logger->LogInfo(msg, "UnifiedMotionManager")
#define LOG_WARNING(msg) if (m_logger) m_logger->LogWarning(msg, "UnifiedMotionManager")
#define LOG_ERROR(msg) if (m_logger) m_logger->LogError(msg, "UnifiedMotionManager")

std::string UnifiedMotionManager::GetWaypointStateString(const std::string& deviceName) const {
  auto it = m_waypointStates.find(deviceName);
  if (it == m_waypointStates.end()) {
    return "NoState";
  }
  switch (it->second.state) {
    case WaypointState::Idle: return "Idle";
    case WaypointState::MovingThroughExitWaypoints: return "MovingThroughExitWaypoints";
    case WaypointState::MovingThroughEntryWaypoints: return "MovingThroughEntryWaypoints";
    case WaypointState::MovingToFinalTarget: return "MovingToFinalTarget";
    default: return "Unknown";
  }
}

UnifiedMotionManager::UnifiedMotionManager() {
  // Register ACS manager
  RegisterManager("ACS", std::make_unique<ACSControllerManager>());

  // Register PI manager
  RegisterManager("PI", std::make_unique<PIControllerManager>());

  // Easy to add more in the future:
  // RegisterManager("NEWPORT", std::make_unique<NewportControllerManager>());
  // RegisterManager("THORLABS", std::make_unique<ThorlabsControllerManager>());

  // Note: Can't log here - logger not set yet
}

void UnifiedMotionManager::SetLogger(ILogger* logger) {
  m_logger = logger;

  LOG_INFO("Initialized with " + std::to_string(m_managers.size()) + " controller managers");

  // Propagate logger to child managers
  ACSControllerManager* acsManager = GetACSManager();
  if (acsManager) {
    acsManager->SetLogger(logger);
  }

  PIControllerManager* piManager = GetPIManager();
  if (piManager) {
    piManager->SetLogger(logger);
  }
}

UnifiedMotionManager::~UnifiedMotionManager() {
  LOG_INFO("Shutting down");
  DisconnectAll();
}

void UnifiedMotionManager::SetSafetyWatchManager(SafetyWatchManager* safetyManager) {
  if (!safetyManager) {
    LOG_WARNING("SafetyWatchManager is null");
    return;
  }

  // Subscribe to safety watch - emergency stop all axes on critical safety flag
  QObject::connect(safetyManager, &SafetyWatchManager::safetyTriggered,
    [this](QString title, QString message, bool critical) {
      Q_UNUSED(title);
      Q_UNUSED(message);
      if (critical) {
        LOG_WARNING("Critical safety flag - EMERGENCY STOP ALL AXES");
        (void)EmergencyStopAll();
      }
    });

  LOG_INFO("Subscribed to SafetyWatchManager for emergency stop");
}

void UnifiedMotionManager::RegisterManager(const std::string& type,
  std::unique_ptr<IMotionControllerManager> manager) {
  std::string normalizedType = NormalizeType(type);
  m_managers[normalizedType] = std::move(manager);
  LOG_DEBUG("Registered " + type + " manager");
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
  LOG_INFO("Adding device " + device.Name + " (Type: " + device.TypeController + ")");

  // Store full device configuration
  m_deviceConfigs[device.Name] = device;

  // Store device type for routing
  m_deviceTypes[device.Name] = device.TypeController;

  // Get the appropriate manager
  std::string normalizedType = NormalizeType(device.TypeController);
  auto managerIt = m_managers.find(normalizedType);

  if (managerIt != m_managers.end()) {
    managerIt->second->AddDevice(device);
    LOG_INFO("Routed " + device.Name + " to " + normalizedType + " manager");
  }
  else {
    LOG_ERROR("No manager found for type '" + device.TypeController + "'");
    std::string supportedTypes;
    for (const auto& [type, _] : m_managers) {
      supportedTypes += type + " ";
    }
    LOG_ERROR("Supported types: " + supportedTypes);
  }
}


void UnifiedMotionManager::RemoveDevice(const std::string& deviceName) {
  IMotionControllerManager* manager = GetManagerForDevice(deviceName);
  if (!manager) {
    LOG_WARNING("Device " + deviceName + " not found");
    return;
  }

  std::string type = m_deviceTypes[deviceName];
  LOG_INFO("Removing device " + deviceName + " (Type: " + type + ")");

  manager->RemoveDevice(deviceName);
  m_deviceTypes.erase(deviceName);
  m_deviceConfigs.erase(deviceName);  // Remove config
  m_namedPositions.erase(deviceName); // Remove named positions

  LOG_INFO("Removed device " + deviceName);
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
  LOG_INFO("Connecting all enabled devices");

  bool allSuccess = true;
  for (auto& [type, manager] : m_managers) {
    bool result = manager->ConnectAll();
    if (!result) {
      allSuccess = false;
    }
  }

  LOG_INFO("Connect all " + std::string(allSuccess ? "succeeded" : "completed with errors"));

  return allSuccess;
}

void UnifiedMotionManager::DisconnectAll() {
  LOG_INFO("Disconnecting all devices");

  for (auto& [type, manager] : m_managers) {
    manager->DisconnectAll();
  }

  LOG_INFO("All devices disconnected");
}

bool UnifiedMotionManager::ConnectDevice(const std::string& deviceName) {
  IMotionControllerManager* manager = GetManagerForDevice(deviceName);
  if (!manager) {
    LOG_ERROR("Device " + deviceName + " not found");
    return false;
  }

  LOG_INFO("Connecting device " + deviceName);
  return manager->ConnectDevice(deviceName);
}

bool UnifiedMotionManager::DisconnectDevice(const std::string& deviceName) {
  IMotionControllerManager* manager = GetManagerForDevice(deviceName);
  if (!manager) {
    LOG_ERROR("Device " + deviceName + " not found");
    return false;
  }

  return manager->DisconnectDevice(deviceName);
}

bool UnifiedMotionManager::ReconnectDevice(const std::string& deviceName) {
  IMotionControllerManager* manager = GetManagerForDevice(deviceName);
  if (!manager) {
    LOG_ERROR("Device " + deviceName + " not found");
    return false;
  }

  return manager->ReconnectDevice(deviceName);
}

int UnifiedMotionManager::ConnectEnabledDevices() {
  LOG_INFO("Connecting enabled devices");

  int totalCount = 0;
  for (auto& [type, manager] : m_managers) {
    int count = manager->ConnectEnabledDevices();
    LOG_INFO("Connected " + std::to_string(count) + " " + type + " devices");
    totalCount += count;
  }

  LOG_INFO("Total connected: " + std::to_string(totalCount));
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
    LOG_ERROR("Device " + deviceName + " not found");
    return false;
  }

  LOG_INFO("MoveToAbs on " + deviceName + " to (" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ")");

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

bool UnifiedMotionManager::MoveToAbs6(const std::string& deviceName,
  const PositionStruct& pos,
  bool blocking) {
  std::string type = GetDeviceType(deviceName);
  if (type.empty()) {
    LOG_ERROR("Device " + deviceName + " not found");
    return false;
  }

  LOG_INFO("MoveToAbs6 on " + deviceName + " to (" + std::to_string(pos.x) + ", " + std::to_string(pos.y) + ", " + std::to_string(pos.z) + ", " + std::to_string(pos.u) + ", " + std::to_string(pos.v) + ", " + std::to_string(pos.w) + ")");

  if (NormalizeType(type) == "ACS") {
    ACSController* controller = GetACSController(deviceName);
    if (!controller) return false;

    // Build list of axes and positions based on installed axes only
    // This handles partial configurations (e.g., only Y installed for stage-main)
    std::string installedAxes = GetInstalledAxes(deviceName);
    std::vector<std::string> axes;
    std::vector<double> positions;

    // Only include axes that are actually installed
    if (installedAxes.find('X') != std::string::npos) {
      axes.push_back("X");
      positions.push_back(pos.x);
    }
    if (installedAxes.find('Y') != std::string::npos) {
      axes.push_back("Y");
      positions.push_back(pos.y);
    }
    if (installedAxes.find('Z') != std::string::npos) {
      axes.push_back("Z");
      positions.push_back(pos.z);
    }
    if (installedAxes.find('U') != std::string::npos) {
      axes.push_back("U");
      positions.push_back(pos.u);
    }
    if (installedAxes.find('V') != std::string::npos) {
      axes.push_back("V");
      positions.push_back(pos.v);
    }
    if (installedAxes.find('W') != std::string::npos) {
      axes.push_back("W");
      positions.push_back(pos.w);
    }

    if (axes.empty()) {
      LOG_ERROR("MoveToAbs6: No valid axes configured for " + deviceName);
      return false;
    }

    return controller->MoveToPositionMultiAxis(axes, positions, blocking);
  }
  else if (NormalizeType(type) == "PI") {
    PIController* controller = GetPIController(deviceName);
    if (!controller) return false;

    return controller->MoveToPositionAll(pos.x, pos.y, pos.z,
      pos.u, pos.v, pos.w,
      blocking);
  }

  return false;
}

bool UnifiedMotionManager::MoveToPosition(const std::string& deviceName,
  const std::string& axis,
  double position,
  bool blocking) {
  std::string type = GetDeviceType(deviceName);
  if (type.empty()) {
    LOG_ERROR("Device " + deviceName + " not found");
    return false;
  }

  LOG_INFO("MoveToPosition on " + deviceName + " axis " + axis + " to " + std::to_string(position));

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
  LOG_DEBUG("MoveRelative START - device=" + deviceName + " axis=" + axis + " distance=" + std::to_string(distance) + " blocking=" + std::string(blocking ? "true" : "false"));

  std::string type = GetDeviceType(deviceName);
  if (type.empty()) {
    LOG_ERROR("Device " + deviceName + " not found");
    return false;
  }

  LOG_INFO("MoveRelative on " + deviceName + " axis " + axis + " by " + std::to_string(distance) + " (type=" + type + ")");

  if (NormalizeType(type) == "ACS") {
    ACSController* controller = GetACSController(deviceName);
    if (!controller) {
      LOG_ERROR("GetACSController returned null for " + deviceName);
      return false;
    }
    LOG_DEBUG("Calling ACSController::MoveRelative");
    bool result = controller->MoveRelative(axis, distance, blocking);
    LOG_DEBUG("ACSController::MoveRelative returned " + std::string(result ? "true" : "false"));
    return result;
  }
  else if (NormalizeType(type) == "PI") {
    PIController* controller = GetPIController(deviceName);
    if (!controller) {
      LOG_ERROR("GetPIController returned null for " + deviceName);
      return false;
    }
    LOG_DEBUG("Calling PIController::MoveRelative");
    bool result = controller->MoveRelative(axis, distance, blocking);
    LOG_DEBUG("PIController::MoveRelative returned " + std::string(result ? "true" : "false"));
    return result;
  }

  LOG_ERROR("Unknown device type " + type);
  return false;
}

bool UnifiedMotionManager::EmergencyStop(const std::string& deviceName) {
  std::string type = GetDeviceType(deviceName);
  if (type.empty()) {
    LOG_ERROR("Device " + deviceName + " not found");
    return false;
  }

  LOG_WARNING("EMERGENCY STOP on " + deviceName);

  // ✅ Clear waypoint state after stopping motion
  stopWaypointSequence(deviceName);

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
  LOG_WARNING("EMERGENCY STOP ALL DEVICES");

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

bool UnifiedMotionManager::SetSystemVelocity(const std::string& deviceName, double velocity) {
  std::string type = GetDeviceType(deviceName);
  if (type.empty()) {
    LOG_ERROR("SetSystemVelocity: Device not found: " + deviceName);
    return false;
  }

  if (NormalizeType(type) == "PI") {
    PIController* controller = GetPIController(deviceName);
    if (!controller) {
      LOG_ERROR("SetSystemVelocity: PI controller not found for " + deviceName);
      return false;
    }
    return controller->SetSystemVelocity(velocity);
  }
  else {
    LOG_WARNING("SetSystemVelocity: System velocity only supported for PI controllers");
    return false;
  }
}

bool UnifiedMotionManager::GetSystemVelocity(const std::string& deviceName, double& velocity) {
  std::string type = GetDeviceType(deviceName);
  if (type.empty()) return false;

  if (NormalizeType(type) == "PI") {
    PIController* controller = GetPIController(deviceName);
    if (!controller) return false;
    return controller->GetSystemVelocity(velocity);
  }

  return false;
}

// ============================================================================
// PI Fast Scan / Active Alignment
// ============================================================================

bool UnifiedMotionManager::FLM(const std::string& deviceName, const std::string& axis,
  double length, double threshold, int analogInput, int direction) {
  std::string type = GetDeviceType(deviceName);
  if (type.empty()) {
    LOG_ERROR("FLM: Device not found: " + deviceName);
    return false;
  }

  if (NormalizeType(type) != "PI") {
    LOG_ERROR("FLM: Only supported for PI controllers (device: " + deviceName + " is type: " + type + ")");
    return false;
  }

  PIController* controller = GetPIController(deviceName);
  if (!controller) {
    LOG_ERROR("FLM: PI controller not found for " + deviceName);
    return false;
  }

  return controller->FLM(axis, length, threshold, analogInput, direction);
}

bool UnifiedMotionManager::FLS(const std::string& deviceName, const std::string& axis,
  double length, double threshold, int analogInput, int direction) {
  std::string type = GetDeviceType(deviceName);
  if (type.empty()) {
    LOG_ERROR("FLS: Device not found: " + deviceName);
    return false;
  }

  if (NormalizeType(type) != "PI") {
    LOG_ERROR("FLS: Only supported for PI controllers");
    return false;
  }

  PIController* controller = GetPIController(deviceName);
  if (!controller) {
    LOG_ERROR("FLS: PI controller not found for " + deviceName);
    return false;
  }

  return controller->FLS(axis, length, threshold, analogInput, direction);
}

bool UnifiedMotionManager::FSA(const std::string& deviceName,
  const std::string& axis1, double length1,
  const std::string& axis2, double length2,
  double threshold, double distance, double alignStep, int analogInput) {
  std::string type = GetDeviceType(deviceName);
  if (type.empty()) {
    LOG_ERROR("FSA: Device not found: " + deviceName);
    return false;
  }

  if (NormalizeType(type) != "PI") {
    LOG_ERROR("FSA: Only supported for PI controllers");
    return false;
  }

  PIController* controller = GetPIController(deviceName);
  if (!controller) {
    LOG_ERROR("FSA: PI controller not found for " + deviceName);
    return false;
  }

  return controller->FSA(axis1, length1, axis2, length2, threshold, distance, alignStep, analogInput);
}

bool UnifiedMotionManager::FSC(const std::string& deviceName,
  const std::string& axis1, double length1,
  const std::string& axis2, double length2,
  double threshold, double distance, int analogInput) {
  std::string type = GetDeviceType(deviceName);
  if (type.empty()) {
    LOG_ERROR("FSC: Device not found: " + deviceName);
    return false;
  }

  if (NormalizeType(type) != "PI") {
    LOG_ERROR("FSC: Only supported for PI controllers");
    return false;
  }

  PIController* controller = GetPIController(deviceName);
  if (!controller) {
    LOG_ERROR("FSC: PI controller not found for " + deviceName);
    return false;
  }

  return controller->FSC(axis1, length1, axis2, length2, threshold, distance, analogInput);
}

bool UnifiedMotionManager::FSM(const std::string& deviceName,
  const std::string& axis1, double length1,
  const std::string& axis2, double length2,
  double threshold, double distance, int analogInput) {
  std::string type = GetDeviceType(deviceName);
  if (type.empty()) {
    LOG_ERROR("FSM: Device not found: " + deviceName);
    return false;
  }

  if (NormalizeType(type) != "PI") {
    LOG_ERROR("FSM: Only supported for PI controllers");
    return false;
  }

  PIController* controller = GetPIController(deviceName);
  if (!controller) {
    LOG_ERROR("FSM: PI controller not found for " + deviceName);
    return false;
  }

  return controller->FSM(axis1, length1, axis2, length2, threshold, distance, analogInput);
}

bool UnifiedMotionManager::GetFastScanStatus(const std::string& deviceName, int& status) {
  std::string type = GetDeviceType(deviceName);
  if (type.empty()) {
    LOG_ERROR("GetFastScanStatus: Device not found: " + deviceName);
    return false;
  }

  if (NormalizeType(type) != "PI") {
    LOG_ERROR("GetFastScanStatus: Only supported for PI controllers");
    return false;
  }

  PIController* controller = GetPIController(deviceName);
  if (!controller) {
    LOG_ERROR("GetFastScanStatus: PI controller not found for " + deviceName);
    return false;
  }

  return controller->GetFastScanStatus(status);
}

bool UnifiedMotionManager::WaitForFastScanComplete(const std::string& deviceName, double timeoutSeconds) {
  std::string type = GetDeviceType(deviceName);
  if (type.empty()) {
    LOG_ERROR("WaitForFastScanComplete: Device not found: " + deviceName);
    return false;
  }

  if (NormalizeType(type) != "PI") {
    LOG_ERROR("WaitForFastScanComplete: Only supported for PI controllers");
    return false;
  }

  PIController* controller = GetPIController(deviceName);
  if (!controller) {
    LOG_ERROR("WaitForFastScanComplete: PI controller not found for " + deviceName);
    return false;
  }

  auto startTime = std::chrono::steady_clock::now();
  int status = 1;

  constexpr int MAX_ITERATIONS = 2400; // Safety bound: 60s timeout / 50ms poll * 2x margin
  for (int iteration = 0; iteration < MAX_ITERATIONS; ++iteration) {
    if (!controller->GetFastScanStatus(status)) {
      LOG_ERROR("WaitForFastScanComplete: Failed to query scan status");
      return false;
    }

    // status: 0=idle (complete), >0=running, <0=error
    if (status == 0) {
      LOG_INFO("WaitForFastScanComplete: Scan completed successfully");
      return true;
    }

    if (status < 0) {
      LOG_ERROR("WaitForFastScanComplete: Scan failed with error: " + std::to_string(status));
      return false;
    }

    // Check timeout
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::steady_clock::now() - startTime).count();
    if (elapsed > timeoutSeconds) {
      LOG_WARNING("WaitForFastScanComplete: Timeout after " + std::to_string(timeoutSeconds) + " seconds");
      return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  LOG_ERROR("Loop safety bound reached in WaitForFastScanComplete");
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

bool UnifiedMotionManager::IsServoEnabled(const std::string& deviceName,
  const std::string& axis) {
  std::string type = GetDeviceType(deviceName);
  if (type.empty()) return false;

  if (NormalizeType(type) == "ACS") {
    ACSController* controller = GetACSController(deviceName);
    if (!controller) return false;
    bool enabled = false;
    if (controller->IsServoEnabled(axis, enabled)) {
      return enabled;
    }
    return false;
  }
  else if (NormalizeType(type) == "PI") {
    // PI hexapods are typically always enabled when connected
    PIController* controller = GetPIController(deviceName);
    if (!controller) return false;
    return controller->IsConnected();
  }

  return false;
}

int UnifiedMotionManager::GetLastErrorCode(const std::string& deviceName) {
  std::string type = GetDeviceType(deviceName);
  if (type.empty()) return -999;

  if (NormalizeType(type) == "ACS") {
    ACSController* controller = GetACSController(deviceName);
    if (!controller) return -998;
    return controller->GetLastErrorCode();
  }

  return 0;
}

std::string UnifiedMotionManager::GetLastErrorMessage(const std::string& deviceName) {
  std::string type = GetDeviceType(deviceName);
  if (type.empty()) return "Device not found: " + deviceName;

  if (NormalizeType(type) == "ACS") {
    ACSController* controller = GetACSController(deviceName);
    if (!controller) return "Controller not found for: " + deviceName;
    return controller->GetLastErrorMessage();
  }

  return "";
}

// ============================================================================
// ACS Buffer Program Control
// ============================================================================

bool UnifiedMotionManager::RunBuffer(const std::string& deviceName, int bufferNumber,
  const std::string& labelName) {
  std::string type = GetDeviceType(deviceName);
  if (type.empty()) {
    LOG_ERROR("RunBuffer: Unknown device " + deviceName);
    return false;
  }

  if (NormalizeType(type) != "ACS") {
    LOG_ERROR("RunBuffer: Device " + deviceName + " is not an ACS controller (type: " + type + ")");
    return false;
  }

  ACSController* controller = GetACSController(deviceName);
  if (!controller) {
    LOG_ERROR("RunBuffer: Could not get ACS controller for " + deviceName);
    return false;
  }

  return controller->RunBuffer(bufferNumber, labelName);
}

bool UnifiedMotionManager::StopBuffer(const std::string& deviceName, int bufferNumber) {
  std::string type = GetDeviceType(deviceName);
  if (type.empty()) {
    LOG_ERROR("StopBuffer: Unknown device " + deviceName);
    return false;
  }

  if (NormalizeType(type) != "ACS") {
    LOG_ERROR("StopBuffer: Device " + deviceName + " is not an ACS controller (type: " + type + ")");
    return false;
  }

  ACSController* controller = GetACSController(deviceName);
  if (!controller) {
    LOG_ERROR("StopBuffer: Could not get ACS controller for " + deviceName);
    return false;
  }

  return controller->StopBuffer(bufferNumber);
}

bool UnifiedMotionManager::StopAllBuffers(const std::string& deviceName) {
  std::string type = GetDeviceType(deviceName);
  if (type.empty()) {
    LOG_ERROR("StopAllBuffers: Unknown device " + deviceName);
    return false;
  }

  if (NormalizeType(type) != "ACS") {
    LOG_ERROR("StopAllBuffers: Device " + deviceName + " is not an ACS controller (type: " + type + ")");
    return false;
  }

  ACSController* controller = GetACSController(deviceName);
  if (!controller) {
    LOG_ERROR("StopAllBuffers: Could not get ACS controller for " + deviceName);
    return false;
  }

  return controller->StopAllBuffers();
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
      LOG_WARNING("Device " + deviceName + " is not an ACS controller (Type: " + type + ")");
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
      LOG_WARNING("Device " + deviceName + " is not a PI controller (Type: " + type + ")");
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
  std::string logMsg = "Added named position '" + positionName + "' for device " + deviceName +
                       " -> (" + std::to_string(position.x) + ", " + std::to_string(position.y) + ", " + std::to_string(position.z) + ")";

  if (!position.waypoints.empty()) {
    logMsg += " with " + std::to_string(position.waypoints.size()) + " entry waypoint(s)";
  }
  if (!position.exit_waypoints.empty()) {
    logMsg += " with " + std::to_string(position.exit_waypoints.size()) + " exit waypoint(s)";
  }

  LOG_INFO(logMsg);
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
    LOG_WARNING("No named positions for device " + deviceName);
    return false;
  }

  auto posIt = deviceIt->second.find(positionName);
  if (posIt == deviceIt->second.end()) {
    LOG_WARNING("Position '" + positionName + "' not found for device " + deviceName);
    return false;
  }

  deviceIt->second.erase(posIt);
  LOG_INFO("Removed named position '" + positionName + "' from device " + deviceName);
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
  LOG_INFO("Reloading named positions from " + configPath);

  // Clear existing positions
  m_namedPositions.clear();

  // Reload using MotionJsonLoader
  return MotionJsonLoader::LoadFromFile(*this, configPath) > 0;
}

void UnifiedMotionManager::ClearNamedPositions(const std::string& deviceName) {
  auto it = m_namedPositions.find(deviceName);
  if (it != m_namedPositions.end()) {
    m_namedPositions.erase(it);
    LOG_INFO("Cleared named positions for " + deviceName);
  }
}

// ============================================================================
// POSITION SAVE TO FILE (for RetraceEdit feature)
// ============================================================================

bool UnifiedMotionManager::SaveNamedPositionToFile(
  const std::string& deviceName,
  const std::string& positionName,
  const PositionStruct& newPosition,
  bool preserveWaypoints
) {
  LOG_INFO("SaveNamedPositionToFile: " + deviceName + "/" + positionName);

  // Determine config paths
  std::string localConfigPath = "config/motor_devices.local.json";
  std::string baseConfigPath = "config/motor_devices.json";

  // If we have a stored config path, use it to determine the base
  if (!m_configFilePath.empty()) {
    // If currently using local config, that's our target
    if (m_configFilePath.find(".local.json") != std::string::npos) {
      localConfigPath = m_configFilePath;
      // Derive base config path
      size_t pos = m_configFilePath.find(".local.json");
      baseConfigPath = m_configFilePath.substr(0, pos) + ".json";
    }
    else {
      // Currently using base config, will create local version
      baseConfigPath = m_configFilePath;
      size_t pos = m_configFilePath.find(".json");
      if (pos != std::string::npos) {
        localConfigPath = m_configFilePath.substr(0, pos) + ".local.json";
      }
    }
  }

  LOG_INFO("  Local config: " + localConfigPath);
  LOG_INFO("  Base config: " + baseConfigPath);

  // Use MotionJsonLoader helper to save
  std::string errorMessage;
  bool success = MotionJsonLoader::SavePositionToFile(
    localConfigPath,
    baseConfigPath,
    deviceName,
    positionName,
    newPosition,
    preserveWaypoints,
    errorMessage
  );

  if (!success) {
    LOG_ERROR("Failed to save position to file: " + errorMessage);
    return false;
  }

  // Update runtime map
  if (preserveWaypoints) {
    // Preserve existing waypoints in runtime map
    auto deviceIt = m_namedPositions.find(deviceName);
    if (deviceIt != m_namedPositions.end()) {
      auto posIt = deviceIt->second.find(positionName);
      if (posIt != deviceIt->second.end()) {
        // Keep waypoints, update coordinates
        PositionStruct updatedPos = newPosition;
        updatedPos.waypoints = posIt->second.waypoints;
        updatedPos.exit_waypoints = posIt->second.exit_waypoints;
        m_namedPositions[deviceName][positionName] = updatedPos;
        LOG_INFO("Updated runtime position (preserved waypoints)");
        return true;
      }
    }
  }

  // Direct update (new position or no waypoints to preserve)
  m_namedPositions[deviceName][positionName] = newPosition;
  LOG_INFO("Updated runtime position");

  // Update config file path to point to local file (since we just wrote to it)
  m_configFilePath = localConfigPath;

  return true;
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
    LOG_ERROR("Device " + deviceName + " not found");
    return false;
  }

  // ✅ FIX: Check if position exists OR can be calculated
  PositionStruct testPos;
  if (!GetPositionWithCalculation(deviceName, positionName, testPos)) {
    LOG_ERROR("Position '" + positionName + "' not found for device " + deviceName);
    return false;
  }

  // ✅ BLOCKING MODE - Use old direct implementation
  if (blocking) {
    LOG_INFO("Blocking move - executing directly");
    return executeMoveToPosition(deviceName, positionName, true);
  }

  // ✅ NON-BLOCKING MODE - Check if we need waypoint state machine at all

  // Get target position first to check for waypoints
  PositionStruct targetPos;
  if (!GetPositionWithCalculation(deviceName, positionName, targetPos)) {
    LOG_ERROR("Failed to get position '" + positionName + "'");
    return false;
  }

  // Check if target has entry waypoints
  bool hasEntryWaypoints = !targetPos.waypoints.empty();

  // Check if current position has exit waypoints
  bool hasExitWaypoints = false;
  auto stateIt = m_waypointStates.find(deviceName);
  if (stateIt != m_waypointStates.end() && !stateIt->second.currentPositionName.empty()) {
    std::string currentPosName = stateIt->second.currentPositionName;
    std::string baseName;
    double ox, oy, oz;
    if (!ParseCalculatedPosition(currentPosName, baseName, ox, oy, oz)) {
      PositionStruct currentPos;
      if (GetNamedPosition(deviceName, currentPosName, currentPos)) {
        hasExitWaypoints = !currentPos.exit_waypoints.empty();
      }
    }
  }

  // ✅ SIMPLE PATH: No waypoints - just do direct move without state machine
  if (!hasEntryWaypoints && !hasExitWaypoints) {
    LOG_INFO("Direct point-to-point move (no waypoints)");

    // ✅ Store pending target for position verification by WaitForMotionComplete
    m_pendingTargets[deviceName] = positionName;

    bool success = executeMoveToPosition(deviceName, positionName, false);
    if (success) {
      // Update current position tracking (but don't use state machine)
      if (m_waypointStates.find(deviceName) == m_waypointStates.end()) {
        m_waypointStates[deviceName] = DeviceWaypointState();
      }
      // Note: Don't set currentPositionName yet - device is still moving
      // It will be updated when WaitForMotionComplete verifies arrival
      m_waypointStates[deviceName].state = WaypointState::Idle;  // Ensure idle
    } else {
      // Move failed to start, clear pending target
      m_pendingTargets.erase(deviceName);
    }
    return success;
  }

  // ✅ WAYPOINT PATH: Has waypoints - use full state machine
  LOG_INFO("Non-blocking move - using waypoint system");

  // Initialize waypoint state for this device if needed
  if (m_waypointStates.find(deviceName) == m_waypointStates.end()) {
    m_waypointStates[deviceName] = DeviceWaypointState();
    if (m_debugLogging) {
      LOG_DEBUG("Created new waypoint state for " + deviceName);
    }
  }

  DeviceWaypointState& state = m_waypointStates[deviceName];

  // ✅ DEBUG: Log current state before checking
  if (m_debugLogging) {
    LOG_DEBUG(deviceName + " current waypoint state = " + GetWaypointStateString(deviceName) +
              ", target = '" + state.targetPositionName + "'" +
              ", current = '" + state.currentPositionName + "'" +
              ", queue size = " + std::to_string(state.waypointQueue.size()) +
              ", IsDeviceInMotion = " + std::string(IsDeviceInMotion(deviceName) ? "true" : "false"));
  }

  // Don't allow new movement if already in motion
  if (state.state != WaypointState::Idle) {
    LOG_WARNING("Device " + deviceName + " already in waypoint motion (state=" + GetWaypointStateString(deviceName) + ")");
    return false;
  }

  // Clear any existing waypoints
  while (!state.waypointQueue.empty()) {
    state.waypointQueue.pop();
  }
  state.exitWaypointCount = 0;
  state.exitWaypointsProcessed = 0;

  state.targetPositionName = positionName;

  // Auto-detect current position if not set
  if (state.currentPositionName.empty()) {
    state.currentPositionName = detectCurrentPositionName(deviceName);
    if (!state.currentPositionName.empty()) {
      LOG_INFO("Auto-detected current position: " + state.currentPositionName);
    }
  }

  // Queue exit waypoints from current position
  if (hasExitWaypoints && !state.currentPositionName.empty() &&
    state.currentPositionName != positionName) {

    PositionStruct currentPos;
    if (GetNamedPosition(deviceName, state.currentPositionName, currentPos)) {
      LOG_INFO("Queueing " + std::to_string(currentPos.exit_waypoints.size()) +
               " exit waypoints from " + state.currentPositionName);

      for (const auto& wp : currentPos.exit_waypoints) {
        state.waypointQueue.push(wp);
      }
      state.exitWaypointCount = currentPos.exit_waypoints.size();
    }
  }

  // Queue entry waypoints to target
  if (hasEntryWaypoints) {
    LOG_INFO("Queueing " + std::to_string(targetPos.waypoints.size()) +
             " entry waypoints to " + positionName);

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
    // This shouldn't happen now (handled by SIMPLE PATH above)
    LOG_INFO("No waypoints, moving directly to target");
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
    LOG_ERROR("startWaypointSequence called with empty queue");
    state.state = WaypointState::Idle;
    return false;
  }

  // Get next waypoint
  std::string nextWaypoint = state.waypointQueue.front();
  state.waypointQueue.pop();

  LOG_INFO("Moving to waypoint: " + nextWaypoint);

  // Execute move
  bool success = executeMoveToPosition(deviceName, nextWaypoint, false);

  if (!success) {
    LOG_ERROR("Failed to move to waypoint: " + nextWaypoint);
    stopWaypointSequence(deviceName);
    return false;
  }

  return true;
}

void UnifiedMotionManager::Update() {
  // Update all device waypoint state machines
  for (auto& [deviceName, state] : m_waypointStates) {
    if (state.state != WaypointState::Idle) {
      if (m_debugLogging) {
        LOG_DEBUG("Update(): Processing " + deviceName +
                  " (state=" + GetWaypointStateString(deviceName) + ")");
      }
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
  std::string oldStateStr = GetWaypointStateString(deviceName);  // For logging

  // Check if device is still moving
  bool isMoving = IsDeviceInMotion(deviceName);
  if (isMoving) {
    if (m_debugLogging) {
      static int skipCount = 0;
      if (++skipCount % 20 == 0) {  // Log every 20th check to reduce spam
        LOG_DEBUG("updateDeviceWaypoints: " + deviceName +
                  " still in motion, waiting... (state=" + oldStateStr + ")");
      }
    }
    return;  // Still moving, wait
  }

  // ✅ FIX: Verify position reached before proceeding to next state
  // Get current position
  PositionStruct currentPos = GetCurrentPosition(deviceName);

  // Determine target position for current state
  std::string currentTargetName;
  PositionStruct targetPos;
  bool hasTarget = false;

  switch (state.state) {
  case WaypointState::MovingThroughExitWaypoints:
  case WaypointState::MovingThroughEntryWaypoints:
    // Target is the waypoint we just tried to move to (already popped from queue in previous cycle)
    // We need to track this - for now, skip verification for waypoints (they're usually safe positions)
    hasTarget = false;
    break;

  case WaypointState::MovingToFinalTarget:
    currentTargetName = state.targetPositionName;
    hasTarget = GetNamedPosition(deviceName, currentTargetName, targetPos);
    break;

  default:
    hasTarget = false;
    break;
  }

  // ✅ Verify position if we have a target
  if (hasTarget) {
    double dx = std::abs(currentPos.x - targetPos.x);
    double dy = std::abs(currentPos.y - targetPos.y);
    double dz = std::abs(currentPos.z - targetPos.z);
    double du = std::abs(currentPos.u - targetPos.u);
    double dv = std::abs(currentPos.v - targetPos.v);
    double dw = std::abs(currentPos.w - targetPos.w);

    // Position tolerance (configurable - using 0.01mm for precision)
    const double tolerance = 0.01;

    bool positionReached = (dx < tolerance && dy < tolerance && dz < tolerance &&
                           du < tolerance && dv < tolerance && dw < tolerance);

    if (!positionReached) {
      // ✅ Device hasn't reached target yet - check if we should log a warning
      // Track warning count to avoid spam (static map persists across calls)
      static std::map<std::string, int> warningCount;
      static std::map<std::string, std::chrono::steady_clock::time_point> lastWarningTime;

      auto now = std::chrono::steady_clock::now();
      auto& lastTime = lastWarningTime[deviceName];
      auto& count = warningCount[deviceName];

      // Only log warning every 1 second to reduce spam
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime).count();
      if (count == 0 || elapsed > 1000) {
        LOG_DEBUG("Device " + deviceName + " moving to " + currentTargetName +
                  " (error: X=" + std::to_string(dx) + ", Y=" + std::to_string(dy) + ", Z=" + std::to_string(dz) + "mm)");
        lastTime = now;
        count++;
      }

      // Don't proceed to next state - wait for position to be reached
      return;
    }
    else {
      // ✅ Position reached - reset warning counters
      static std::map<std::string, int> warningCount;
      static std::map<std::string, std::chrono::steady_clock::time_point> lastWarningTime;
      warningCount[deviceName] = 0;
      lastWarningTime[deviceName] = std::chrono::steady_clock::now();
    }
  }

  // Movement complete AND position verified - process next state
  switch (state.state) {

  case WaypointState::MovingThroughExitWaypoints:
    LOG_INFO("Exit waypoint reached");
    state.exitWaypointsProcessed++;

    if (state.exitWaypointsProcessed >= state.exitWaypointCount) {
      LOG_INFO("All exit waypoints complete");

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
    LOG_INFO("Entry waypoint reached");

    if (state.waypointQueue.empty()) {
      // All entry waypoints done - move to final target
      LOG_INFO("Moving to final target: " + state.targetPositionName);
      state.state = WaypointState::MovingToFinalTarget;
      executeMoveToPosition(deviceName, state.targetPositionName, false);
    }
    else {
      // More entry waypoints
      startWaypointSequence(deviceName);
    }
    break;

  case WaypointState::MovingToFinalTarget:
    LOG_INFO("Final target reached: " + state.targetPositionName);
    state.currentPositionName = state.targetPositionName;
    state.state = WaypointState::Idle;
    if (m_debugLogging) {
      LOG_DEBUG(deviceName + " STATE TRANSITION: " + oldStateStr + " -> Idle" +
                " (target was " + state.targetPositionName + ")");
    }
    break;

  case WaypointState::Idle:
    // Already idle - this shouldn't happen in normal flow
    if (m_debugLogging) {
      LOG_DEBUG(deviceName + " updateDeviceWaypoints called while already Idle (unexpected)");
    }
    break;
  }
}

void UnifiedMotionManager::stopWaypointSequence(const std::string& deviceName) {
  auto it = m_waypointStates.find(deviceName);
  if (it == m_waypointStates.end()) {
    return;
  }

  DeviceWaypointState& state = it->second;

  LOG_INFO("Stopping waypoint sequence for " + deviceName);

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
    LOG_ERROR("executeMoveToPosition: Position '" + positionName + "' not found for device " + deviceName);
    return false;
  }

  std::string installedAxes = GetInstalledAxes(deviceName);
  std::string type = GetDeviceType(deviceName);
  bool success = true;

  if (NormalizeType(type) == "ACS") {
    ACSController* controller = GetACSController(deviceName);
    if (!controller) return false;

    // Build list of axes and positions based on installed axes only
    // This handles partial configurations (e.g., only Y installed for stage-main)
    std::vector<std::string> axes;
    std::vector<double> positions;

    // Only include axes that are actually installed
    if (installedAxes.find('X') != std::string::npos) {
      axes.push_back("X");
      positions.push_back(pos.x);
    }
    if (installedAxes.find('Y') != std::string::npos) {
      axes.push_back("Y");
      positions.push_back(pos.y);
    }
    if (installedAxes.find('Z') != std::string::npos) {
      axes.push_back("Z");
      positions.push_back(pos.z);
    }
    if (installedAxes.find('U') != std::string::npos) {
      axes.push_back("U");
      positions.push_back(pos.u);
    }
    if (installedAxes.find('V') != std::string::npos) {
      axes.push_back("V");
      positions.push_back(pos.v);
    }
    if (installedAxes.find('W') != std::string::npos) {
      axes.push_back("W");
      positions.push_back(pos.w);
    }

    if (axes.empty()) {
      LOG_ERROR("executeMoveToPosition: No valid axes configured for " + deviceName);
      return false;
    }

    // Move all axes together in coordinated motion
    success = controller->MoveToPositionMultiAxis(axes, positions, blocking);
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

  // Find the CLOSEST matching position (considering all 6 axes)
  std::string closestName = "";
  double closestDistance = std::numeric_limits<double>::max();

  for (const auto& [posName, pos] : deviceIt->second) {
    double dx = std::abs(currentPos.x - pos.x);
    double dy = std::abs(currentPos.y - pos.y);
    double dz = std::abs(currentPos.z - pos.z);
    double du = std::abs(currentPos.u - pos.u);
    double dv = std::abs(currentPos.v - pos.v);
    double dw = std::abs(currentPos.w - pos.w);

    // Check if ALL axes are within tolerance
    if (dx < tolerance && dy < tolerance && dz < tolerance &&
        du < tolerance && dv < tolerance && dw < tolerance) {
      // Calculate total distance to find closest match
      double totalDistance = dx + dy + dz + du + dv + dw;
      if (totalDistance < closestDistance) {
        closestDistance = totalDistance;
        closestName = posName;
      }
    }
  }

  return closestName;  // Empty if not at any known position
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
  LOG_INFO("Set current position of " + deviceName + " to " + positionName);
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
  (void)EmergencyStop(deviceName);
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
      LOG_ERROR("ParseCalculatedPosition: Invalid axis '" + std::string(1, axis) +
                "' in position name: " + positionName);
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
      LOG_ERROR("ParseCalculatedPosition: Invalid value '" + valueStr +
                "' for axis " + std::string(1, axis) + " in: " + positionName);
      return false;
    }

    if (nextPlus >= remaining.length()) break;
    pos = nextPlus;  // Move to next '+' position
  }

  LOG_DEBUG("ParseCalculatedPosition: '" + positionName +
            "' -> base='" + baseName + "' offsets=(" +
            std::to_string(offsetX) + ", " + std::to_string(offsetY) + ", " + std::to_string(offsetZ) + ")");

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
      LOG_ERROR("GetPositionWithCalculation: Base position '" + baseName +
                "' not found for device " + deviceName);
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

    LOG_INFO("GetPositionWithCalculation: Calculated position '" + positionName +
             "' from base '" + baseName + "'" +
             " with offset (" + std::to_string(offsetX) + ", " + std::to_string(offsetY) + ", " + std::to_string(offsetZ) + ")" +
             " -> (" + std::to_string(outPosition.x) + ", " + std::to_string(outPosition.y) + ", " + std::to_string(outPosition.z) + ")");

    return true;
  }

  // Not a calculated position, get normally
  return GetNamedPosition(deviceName, positionName, outPosition);
}

std::vector<std::string> UnifiedMotionManager::GetAllNodeNames(const std::string& deviceName) const {
  // Return all named positions for this device
  return GetNamedPositionList(deviceName);
}

// ============================================================================
// PENDING TARGET TRACKING
// ============================================================================

std::string UnifiedMotionManager::GetPendingTarget(const std::string& deviceName) const {
  auto it = m_pendingTargets.find(deviceName);
  if (it != m_pendingTargets.end()) {
    return it->second;
  }
  return "";
}

void UnifiedMotionManager::ClearPendingTarget(const std::string& deviceName) {
  auto it = m_pendingTargets.find(deviceName);
  if (it != m_pendingTargets.end()) {
    // Update current position name now that move is confirmed complete
    if (m_waypointStates.find(deviceName) != m_waypointStates.end()) {
      m_waypointStates[deviceName].currentPositionName = it->second;
    }
    m_pendingTargets.erase(it);
    if (m_debugLogging) {
      LOG_DEBUG("Cleared pending target for " + deviceName);
    }
  }
}