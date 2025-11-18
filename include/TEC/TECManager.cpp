// TECManager.cpp
#include "TECManager.h"
#include "CLD101xAdapter.h"
#include "CLD101xDirectAdapter.h"
#include "TED4015DirectAdapter.h"
#include <iostream>
#include <algorithm>

TECManager::TECManager() {
  std::cout << "[INFO] TECManager: Initialized" << std::endl;
}

TECManager::~TECManager() {
  std::cout << "[INFO] TECManager: Shutting down" << std::endl;
  DisconnectAll();
}

// ============================================================================
// DEVICE MANAGEMENT
// ============================================================================

void TECManager::AddDevice(const TECDevice& device) {
  std::cout << "[INFO] TECManager: Adding device '" << device.Name
    << "' (Type: " << device.TypeController << ")" << std::endl;

  // Store configuration
  m_deviceConfigs[device.Name] = device;

  // Create device adapter
  auto adapter = CreateDeviceAdapter(device);

  if (adapter) {
    m_devices[device.Name] = std::move(adapter);
    std::cout << "[INFO] TECManager: Device '" << device.Name
      << "' added successfully" << std::endl;
  }
  else {
    std::cerr << "[ERROR] TECManager: Failed to create adapter for device '"
      << device.Name << "' (Type: " << device.TypeController << ")" << std::endl;
  }
}

void TECManager::RemoveDevice(const std::string& deviceName) {
  auto it = m_devices.find(deviceName);
  if (it != m_devices.end()) {
    // Disconnect before removing
    it->second->Disconnect();
    m_devices.erase(it);
    m_deviceConfigs.erase(deviceName);
    std::cout << "[INFO] TECManager: Device '" << deviceName << "' removed" << std::endl;
  }
  else {
    std::cerr << "[WARN] TECManager: Device '" << deviceName << "' not found" << std::endl;
  }
}

bool TECManager::HasDevice(const std::string& deviceName) const {
  return m_devices.find(deviceName) != m_devices.end();
}

bool TECManager::GetDeviceConfig(const std::string& deviceName, TECDevice& outDevice) const {
  auto it = m_deviceConfigs.find(deviceName);
  if (it != m_deviceConfigs.end()) {
    outDevice = it->second;
    return true;
  }
  return false;
}

// ============================================================================
// CONNECTION MANAGEMENT
// ============================================================================

int TECManager::ConnectAll() {
  std::cout << "[INFO] TECManager: Connecting all devices..." << std::endl;

  int connectedCount = 0;
  for (auto& [name, device] : m_devices) {
    if (device->Connect()) {
      connectedCount++;
    }
  }

  std::cout << "[INFO] TECManager: Connected " << connectedCount
    << " out of " << m_devices.size() << " devices" << std::endl;

  return connectedCount;
}

void TECManager::DisconnectAll() {
  std::cout << "[INFO] TECManager: Disconnecting all devices..." << std::endl;

  for (auto& [name, device] : m_devices) {
    device->Disconnect();
  }

  std::cout << "[INFO] TECManager: All devices disconnected" << std::endl;
}

bool TECManager::ConnectDevice(const std::string& deviceName) {
  auto it = m_devices.find(deviceName);
  if (it != m_devices.end()) {
    return it->second->Connect();
  }

  std::cerr << "[ERROR] TECManager: Device '" << deviceName << "' not found" << std::endl;
  return false;
}

bool TECManager::DisconnectDevice(const std::string& deviceName) {
  auto it = m_devices.find(deviceName);
  if (it != m_devices.end()) {
    it->second->Disconnect();
    return true;
  }

  std::cerr << "[ERROR] TECManager: Device '" << deviceName << "' not found" << std::endl;
  return false;
}

bool TECManager::ReconnectDevice(const std::string& deviceName) {
  auto it = m_devices.find(deviceName);
  if (it != m_devices.end()) {
    return it->second->Reconnect();
  }

  std::cerr << "[ERROR] TECManager: Device '" << deviceName << "' not found" << std::endl;
  return false;
}

int TECManager::ConnectEnabledDevices() {
  std::cout << "[INFO] TECManager: Connecting enabled devices..." << std::endl;

  int connectedCount = 0;
  for (auto& [name, config] : m_deviceConfigs) {
    if (config.Enabled) {
      auto it = m_devices.find(name);
      if (it != m_devices.end() && it->second->Connect()) {
        connectedCount++;
      }
    }
  }

  std::cout << "[INFO] TECManager: Connected " << connectedCount
    << " enabled devices" << std::endl;

  return connectedCount;
}

// ============================================================================
// TEC CONTROL
// ============================================================================

TECCommandResult TECManager::SetTemperature(const std::string& deviceName, double temperature) {
  auto it = m_devices.find(deviceName);
  if (it != m_devices.end()) {
    return it->second->SetTemperature(temperature);
  }

  TECCommandResult result;
  result.Success = false;
  result.Message = "Device not found: " + deviceName;
  return result;
}

TECCommandResult TECManager::TEC_On(const std::string& deviceName) {
  auto it = m_devices.find(deviceName);
  if (it != m_devices.end()) {
    return it->second->TEC_On();
  }

  TECCommandResult result;
  result.Success = false;
  result.Message = "Device not found: " + deviceName;
  return result;
}

TECCommandResult TECManager::TEC_Off(const std::string& deviceName) {
  auto it = m_devices.find(deviceName);
  if (it != m_devices.end()) {
    return it->second->TEC_Off();
  }

  TECCommandResult result;
  result.Success = false;
  result.Message = "Device not found: " + deviceName;
  return result;
}

TECCommandResult TECManager::ReadTemperature(const std::string& deviceName) {
  auto it = m_devices.find(deviceName);
  if (it != m_devices.end()) {
    return it->second->ReadTemperature();
  }

  TECCommandResult result;
  result.Success = false;
  result.Message = "Device not found: " + deviceName;
  return result;
}

TECCommandResult TECManager::ReadTECCurrent(const std::string& deviceName) {
  auto it = m_devices.find(deviceName);
  if (it != m_devices.end()) {
    return it->second->ReadTECCurrent();
  }

  TECCommandResult result;
  result.Success = false;
  result.Message = "Device not found: " + deviceName;
  return result;
}

// ============================================================================
// LASER CONTROL
// ============================================================================

TECCommandResult TECManager::SetLaserCurrent(const std::string& deviceName, double current) {
  auto it = m_devices.find(deviceName);
  if (it != m_devices.end()) {
    return it->second->SetLaserCurrent(current);
  }

  TECCommandResult result;
  result.Success = false;
  result.Message = "Device not found: " + deviceName;
  return result;
}

TECCommandResult TECManager::Laser_On(const std::string& deviceName) {
  auto it = m_devices.find(deviceName);
  if (it != m_devices.end()) {
    return it->second->Laser_On();
  }

  TECCommandResult result;
  result.Success = false;
  result.Message = "Device not found: " + deviceName;
  return result;
}

TECCommandResult TECManager::Laser_Off(const std::string& deviceName) {
  auto it = m_devices.find(deviceName);
  if (it != m_devices.end()) {
    return it->second->Laser_Off();
  }

  TECCommandResult result;
  result.Success = false;
  result.Message = "Device not found: " + deviceName;
  return result;
}

TECCommandResult TECManager::ReadLaserCurrent(const std::string& deviceName) {
  auto it = m_devices.find(deviceName);
  if (it != m_devices.end()) {
    return it->second->ReadLaserCurrent();
  }

  TECCommandResult result;
  result.Success = false;
  result.Message = "Device not found: " + deviceName;
  return result;
}

// ============================================================================
// STATUS QUERIES
// ============================================================================

TECStatus TECManager::GetDeviceStatus(const std::string& deviceName) const {
  auto it = m_devices.find(deviceName);
  if (it != m_devices.end()) {
    return it->second->GetStatus();
  }

  // Return empty status if not found
  TECStatus emptyStatus;
  return emptyStatus;
}

std::vector<std::string> TECManager::GetAllDeviceNames() const {
  std::vector<std::string> names;
  names.reserve(m_devices.size());

  for (const auto& [name, device] : m_devices) {
    names.push_back(name);
  }

  return names;
}

std::vector<std::string> TECManager::GetConnectedDeviceNames() const {
  std::vector<std::string> names;

  for (const auto& [name, device] : m_devices) {
    if (device->IsConnected()) {
      names.push_back(name);
    }
  }

  return names;
}

std::map<std::string, bool> TECManager::GetConnectionStatus() const {
  std::map<std::string, bool> status;

  for (const auto& [name, device] : m_devices) {
    status[name] = device->IsConnected();
  }

  return status;
}

size_t TECManager::GetTotalDeviceCount() const {
  return m_devices.size();
}

size_t TECManager::GetConnectedDeviceCount() const {
  size_t count = 0;

  for (const auto& [name, device] : m_devices) {
    if (device->IsConnected()) {
      count++;
    }
  }

  return count;
}

// ============================================================================
// DIRECT DEVICE ACCESS
// ============================================================================

ITECDevice* TECManager::GetDevice(const std::string& deviceName) {
  auto it = m_devices.find(deviceName);
  if (it != m_devices.end()) {
    return it->second.get();
  }
  return nullptr;
}

// ============================================================================
// PRIVATE METHODS
// ============================================================================

std::unique_ptr<ITECDevice> TECManager::CreateDeviceAdapter(const TECDevice& config) {
  std::string normalizedType = NormalizeType(config.TypeController);

  if (normalizedType == "CLD101X") {
    // Legacy TCP-based adapter (requires Python server)
    return std::make_unique<CLD101xAdapter>(config);
  }
  else if (normalizedType == "CLD101X_DIRECT") {
    // Direct VISA adapter (no Python server required)
    return std::make_unique<CLD101xDirectAdapter>(config);
  }
  else if (normalizedType == "TED4015" || normalizedType == "TED4015_DIRECT") {
    // TED4015 TEC-only controller (Direct VISA)
    return std::make_unique<TED4015DirectAdapter>(config);
  }

  // Add more device types here in the future:
  // else if (normalizedType == "OTHERTEC") {
  //     return std::make_unique<OtherTECAdapter>(config);
  // }

  std::cerr << "[ERROR] TECManager: Unknown controller type: "
    << config.TypeController << std::endl;
  return nullptr;
}

std::string TECManager::NormalizeType(const std::string& type) const {
  std::string normalized = type;

  // Convert to uppercase
  std::transform(normalized.begin(), normalized.end(),
    normalized.begin(), ::toupper);

  return normalized;
}