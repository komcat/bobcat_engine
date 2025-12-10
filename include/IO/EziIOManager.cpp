// EziIOManager.cpp - Implementation
#include "EziIOManager.h"
#include "FAS_EziMOTIONPlusE.h"
#include <iostream>
#include <thread>
#include <chrono>

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

EziIOManager::EziIOManager()
  : m_initialized(false),
  m_enableLogging(true),
  m_pollingThread(nullptr),
  m_stopPolling(false),
  m_pollingInterval(100),
  m_connectedDeviceCount(0)
{
  std::cout << "[EziIOManager] Manager created" << std::endl;
}

EziIOManager::~EziIOManager() {
  try {
    Shutdown();
  }
  catch (const std::exception& e) {
    std::cerr << "[EziIOManager] Exception during destruction: " << e.what() << std::endl;
  }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

bool EziIOManager::Initialize() {
  if (m_initialized) {
    if (m_enableLogging) {
      std::cout << "[EziIOManager] Already initialized" << std::endl;
    }
    return true;
  }

  // Enable auto-reconnect for better connection stability
  PE::FAS_SetAutoReconnect(1);

  m_initialized = true;

  if (m_enableLogging) {
    std::cout << "[EziIOManager] Initialized successfully" << std::endl;
  }

  return true;
}

void EziIOManager::Shutdown() {
  if (!m_initialized) {
    return;
  }

  if (m_enableLogging) {
    std::cout << "[EziIOManager] Shutting down..." << std::endl;
  }

  // Stop polling thread
  try {
    StopPolling();
  }
  catch (...) {
    std::cerr << "[EziIOManager] Error stopping polling during shutdown" << std::endl;
  }

  // Disconnect all devices
  try {
    DisconnectAll();
  }
  catch (...) {
    std::cerr << "[EziIOManager] Error disconnecting devices during shutdown" << std::endl;
  }

  // Clear collections
  m_devices.clear();
  m_deviceNameMap.clear();
  m_deviceIdMap.clear();
  m_initialized = false;

  if (m_enableLogging) {
    std::cout << "[EziIOManager] Shutdown complete" << std::endl;
  }
}

// ============================================================================
// DEVICE MANAGEMENT
// ============================================================================

bool EziIOManager::AddDevice(const EziIODeviceConfig& config) {
  // Check if device with this name already exists
  if (m_deviceNameMap.find(config.Name) != m_deviceNameMap.end()) {
    std::cerr << "[EziIOManager] Device '" << config.Name << "' already exists" << std::endl;
    return false;
  }

  // Check if device with this ID already exists
  if (m_deviceIdMap.find(config.DeviceId) != m_deviceIdMap.end()) {
    std::cerr << "[EziIOManager] Device ID " << config.DeviceId << " already exists" << std::endl;
    return false;
  }

  // Create new device
  auto device = std::make_shared<EziIODevice>(config);

  // Add to collections
  m_devices.push_back(device);
  m_deviceNameMap[config.Name] = device;
  m_deviceIdMap[config.DeviceId] = device;

  if (m_enableLogging) {
    std::cout << "[EziIOManager] Added device: " << config.Name
      << " (ID: " << config.DeviceId << ", IP: " << config.IpAddress << ")" << std::endl;
  }

  return true;
}


bool EziIOManager::AddWagoDevice(const EziIODeviceConfig& config) {
  // Check if device with this name already exists
  if (m_deviceNameMap.find(config.Name) != m_deviceNameMap.end()) {
    std::cerr << "[EziIOManager] Device '" << config.Name << "' already exists" << std::endl;
    return false;
  }

  // Check if device with this ID already exists
  if (m_deviceIdMap.find(config.DeviceId) != m_deviceIdMap.end()) {
    std::cerr << "[EziIOManager] Device ID " << config.DeviceId << " already exists" << std::endl;
    return false;
  }

  // Create new WAGO device
  auto device = std::make_shared<WagoIODevice>(config);

  // Add to collections
  m_devices.push_back(device);
  m_deviceNameMap[config.Name] = device;
  m_deviceIdMap[config.DeviceId] = device;

  if (m_enableLogging) {
    std::cout << "[EziIOManager] Added WAGO device: " << config.Name
      << " (ID: " << config.DeviceId << ", IP: " << config.IpAddress << ")" << std::endl;
  }

  return true;
}

bool EziIOManager::RegisterDevice(std::shared_ptr<IEziIODevice> device) {
  if (!device) {
    std::cerr << "[EziIOManager] Cannot register null device" << std::endl;
    return false;
  }

  const std::string& name = device->GetName();
  int deviceId = device->GetDeviceId();

  // Check if device with this name already exists
  if (m_deviceNameMap.find(name) != m_deviceNameMap.end()) {
    std::cerr << "[EziIOManager] Device '" << name << "' already exists" << std::endl;
    return false;
  }

  // Check if device with this ID already exists
  if (m_deviceIdMap.find(deviceId) != m_deviceIdMap.end()) {
    std::cerr << "[EziIOManager] Device ID " << deviceId << " already exists" << std::endl;
    return false;
  }

  // Add to collections
  m_devices.push_back(device);
  m_deviceNameMap[name] = device;
  m_deviceIdMap[deviceId] = device;

  if (m_enableLogging) {
    std::cout << "[EziIOManager] Registered device: " << name
      << " (ID: " << deviceId << ", IP: " << device->GetIpAddress() << ")" << std::endl;
  }

  return true;
}

bool EziIOManager::RemoveDevice(const std::string& deviceName) {
  auto it = m_deviceNameMap.find(deviceName);
  if (it == m_deviceNameMap.end()) {
    std::cerr << "[EziIOManager] Device '" << deviceName << "' not found" << std::endl;
    return false;
  }

  auto device = it->second;
  int deviceId = device->GetDeviceId();

  // Disconnect if connected
  if (device->IsConnected()) {
    device->Disconnect();
    m_connectedDeviceCount--;
  }

  // Remove from all collections
  m_deviceNameMap.erase(deviceName);
  m_deviceIdMap.erase(deviceId);
  m_devices.erase(
    std::remove_if(m_devices.begin(), m_devices.end(),
      [&device](const std::shared_ptr<IEziIODevice>& d) { return d == device; }),
    m_devices.end()
  );

  if (m_enableLogging) {
    std::cout << "[EziIOManager] Removed device: " << deviceName << std::endl;
  }

  return true;
}

bool EziIOManager::HasDevice(const std::string& deviceName) const {
  return m_deviceNameMap.find(deviceName) != m_deviceNameMap.end();
}

IEziIODevice* EziIOManager::GetDevice(const std::string& deviceName) {
  auto it = m_deviceNameMap.find(deviceName);
  return (it != m_deviceNameMap.end()) ? it->second.get() : nullptr;
}

IEziIODevice* EziIOManager::GetDeviceById(int deviceId) {
  auto it = m_deviceIdMap.find(deviceId);
  return (it != m_deviceIdMap.end()) ? it->second.get() : nullptr;
}

// ============================================================================
// CONNECTION MANAGEMENT
// ============================================================================

bool EziIOManager::ConnectAll() {
  if (m_enableLogging) {
    std::cout << "[EziIOManager] Connecting all devices..." << std::endl;
  }

  bool allSuccess = true;
  int connectedCount = 0;

  for (auto& device : m_devices) {
    if (device->Connect()) {
      connectedCount++;
    }
    else {
      allSuccess = false;
    }
  }

  m_connectedDeviceCount = connectedCount;

  if (m_enableLogging) {
    std::cout << "[EziIOManager] Connected " << connectedCount << "/"
      << m_devices.size() << " devices" << std::endl;
  }

  return allSuccess;
}

void EziIOManager::DisconnectAll() {
  if (m_enableLogging) {
    std::cout << "[EziIOManager] Disconnecting all devices..." << std::endl;
  }

  for (auto& device : m_devices) {
    device->Disconnect();
  }

  m_connectedDeviceCount = 0;

  if (m_enableLogging) {
    std::cout << "[EziIOManager] All devices disconnected" << std::endl;
  }
}

bool EziIOManager::ConnectDevice(const std::string& deviceName) {
  IEziIODevice* device = GetDevice(deviceName);
  if (!device) {
    std::cerr << "[EziIOManager] Device '" << deviceName << "' not found" << std::endl;
    return false;
  }

  bool wasConnected = device->IsConnected();
  bool result = device->Connect();

  if (result && !wasConnected) {
    m_connectedDeviceCount++;
  }

  return result;
}

bool EziIOManager::DisconnectDevice(const std::string& deviceName) {
  IEziIODevice* device = GetDevice(deviceName);
  if (!device) {
    std::cerr << "[EziIOManager] Device '" << deviceName << "' not found" << std::endl;
    return false;
  }

  bool wasConnected = device->IsConnected();
  bool result = device->Disconnect();

  if (result && wasConnected) {
    m_connectedDeviceCount--;
  }

  return result;
}

int EziIOManager::ConnectEnabledDevices() {
  if (m_enableLogging) {
    std::cout << "[EziIOManager] Connecting enabled devices..." << std::endl;
  }

  int connectedCount = 0;

  for (auto& device : m_devices) {
    if (device->Connect()) {
      connectedCount++;
    }
  }

  m_connectedDeviceCount = connectedCount;

  if (m_enableLogging) {
    std::cout << "[EziIOManager] Connected " << connectedCount << " devices" << std::endl;
  }

  return connectedCount;
}

// ============================================================================
// INPUT OPERATIONS
// ============================================================================

EziIOError EziIOManager::ReadInputs(const std::string& deviceName,
  uint32_t& inputs, uint32_t& latch) {
  IEziIODevice* device = nullptr;
  EziIOError validationResult = ValidateDevice(deviceName, &device, "ReadInputs");
  if (validationResult != EziIOError::SUCCESS) {
    return validationResult;
  }

  return device->ReadInputs(inputs, latch);
}

EziIOError EziIOManager::GetLastInputStatus(const std::string& deviceName,
  uint32_t& inputs, uint32_t& latch) {
  IEziIODevice* device = GetDevice(deviceName);
  if (!device) {
    return EziIOError::DEVICE_NOT_FOUND;
  }

  return device->GetLastInputStatus(inputs, latch);
}

EziIOError EziIOManager::ClearLatch(const std::string& deviceName, uint32_t latchMask) {
  IEziIODevice* device = nullptr;
  EziIOError validationResult = ValidateDevice(deviceName, &device, "ClearLatch");
  if (validationResult != EziIOError::SUCCESS) {
    return validationResult;
  }

  return device->ClearLatch(latchMask);
}

// ============================================================================
// OUTPUT OPERATIONS
// ============================================================================

EziIOError EziIOManager::ReadOutputs(const std::string& deviceName,
  uint32_t& outputs, uint32_t& status) {
  IEziIODevice* device = nullptr;
  EziIOError validationResult = ValidateDevice(deviceName, &device, "ReadOutputs");
  if (validationResult != EziIOError::SUCCESS) {
    return validationResult;
  }

  return device->ReadOutputs(outputs, status);
}

EziIOError EziIOManager::GetLastOutputStatus(const std::string& deviceName,
  uint32_t& outputs, uint32_t& status) {
  IEziIODevice* device = GetDevice(deviceName);
  if (!device) {
    return EziIOError::DEVICE_NOT_FOUND;
  }

  return device->GetLastOutputStatus(outputs, status);
}

EziIOError EziIOManager::SetOutputs(const std::string& deviceName,
  uint32_t setMask, uint32_t clearMask) {
  IEziIODevice* device = nullptr;
  EziIOError validationResult = ValidateDevice(deviceName, &device, "SetOutputs");
  if (validationResult != EziIOError::SUCCESS) {
    return validationResult;
  }

  return device->SetOutputs(setMask, clearMask);
}

EziIOError EziIOManager::SetOutput(const std::string& deviceName, int pin, bool state) {
  IEziIODevice* device = nullptr;
  EziIOError validationResult = ValidateDevice(deviceName, &device, "SetOutput");
  if (validationResult != EziIOError::SUCCESS) {
    return validationResult;
  }

  return device->SetOutput(pin, state);
}

// ============================================================================
// PIN NAME CONVENIENCE METHODS
// ============================================================================

EziIOError EziIOManager::SetOutputByName(const std::string& deviceName,
  const std::string& pinName,
  bool state) {
  IEziIODevice* device = GetDevice(deviceName);
  if (!device) {
    if (m_enableLogging) {
      std::cerr << "[EziIOManager] SetOutputByName failed: Device '"
        << deviceName << "' not found" << std::endl;
    }
    return EziIOError::DEVICE_NOT_FOUND;
  }

  if (!device->IsConnected()) {
    if (m_enableLogging) {
      std::cerr << "[EziIOManager] SetOutputByName failed: Device '"
        << deviceName << "' is disconnected" << std::endl;
    }
    return EziIOError::DEVICE_DISCONNECTED;
  }

  // Get pin number from name
  int pin = device->GetOutputPinByName(pinName);
  if (pin < 0) {
    if (m_enableLogging) {
      std::cerr << "[EziIOManager] SetOutputByName failed: Pin name '"
        << pinName << "' not found on device '" << deviceName << "'" << std::endl;
    }
    return EziIOError::INVALID_PIN;
  }

  return device->SetOutput(pin, state);
}

bool EziIOManager::GetInputByName(const std::string& deviceName,
  const std::string& pinName,
  bool& state) {
  IEziIODevice* device = GetDevice(deviceName);
  if (!device || !device->IsConnected()) {
    return false;
  }

  // Get pin number from name
  int pin = device->GetInputPinByName(pinName);
  if (pin < 0) {
    if (m_enableLogging) {
      std::cerr << "[EziIOManager] GetInputByName failed: Pin name '"
        << pinName << "' not found on device '" << deviceName << "'" << std::endl;
    }
    return false;
  }

  // Read current inputs
  uint32_t inputs = 0, latch = 0;
  if (device->ReadInputs(inputs, latch) != EziIOError::SUCCESS) {
    return false;
  }

  // Extract the specific pin state
  state = (inputs & (1U << pin)) != 0;
  return true;
}

// ============================================================================
// POLLING THREAD
// ============================================================================

void EziIOManager::StartPolling(unsigned int intervalMs) {
  if (m_pollingThread) {
    if (m_enableLogging) {
      std::cout << "[EziIOManager] Polling already running" << std::endl;
    }
    return;
  }

  m_pollingInterval = intervalMs;
  m_stopPolling = false;
  m_pollingThread = new std::thread(&EziIOManager::PollingThreadFunc, this);

  if (m_enableLogging) {
    std::cout << "[EziIOManager] Polling started with interval "
      << intervalMs << "ms" << std::endl;
  }
}

void EziIOManager::StopPolling() {
  if (!m_pollingThread) {
    return;
  }

  m_stopPolling = true;

  if (m_pollingThread->joinable()) {
    m_pollingThread->join();
  }

  delete m_pollingThread;
  m_pollingThread = nullptr;

  if (m_enableLogging) {
    std::cout << "[EziIOManager] Polling stopped" << std::endl;
  }
}

void EziIOManager::SetPollingInterval(unsigned int intervalMs) {
  m_pollingInterval = intervalMs;
}

bool EziIOManager::IsPolling() const {
  return m_pollingThread != nullptr && !m_stopPolling;
}

void EziIOManager::PollingThreadFunc() {
  while (!m_stopPolling) {
    int connectedCount = 0;

    // Poll all connected devices
    for (auto& device : m_devices) {
      if (device->IsConnected()) {
        connectedCount++;

        uint32_t inputs = 0, latch = 0;
        uint32_t outputs = 0, status = 0;

        // Read inputs and outputs (updates cached values)
        device->ReadInputs(inputs, latch);
        device->ReadOutputs(outputs, status);
      }
    }

    // Sleep longer if no devices connected
    if (connectedCount == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    else {
      std::this_thread::sleep_for(std::chrono::milliseconds(m_pollingInterval));
    }
  }
}

// ============================================================================
// DEVICE QUERIES
// ============================================================================

std::vector<std::string> EziIOManager::GetAllDeviceNames() const {
  std::vector<std::string> names;
  for (const auto& [name, device] : m_deviceNameMap) {
    names.push_back(name);
  }
  return names;
}

std::vector<std::string> EziIOManager::GetConnectedDeviceNames() const {
  std::vector<std::string> names;
  for (const auto& [name, device] : m_deviceNameMap) {
    if (device->IsConnected()) {
      names.push_back(name);
    }
  }
  return names;
}

std::map<std::string, bool> EziIOManager::GetConnectionStatus() const {
  std::map<std::string, bool> status;
  for (const auto& [name, device] : m_deviceNameMap) {
    status[name] = device->IsConnected();
  }
  return status;
}

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

EziIOError EziIOManager::ValidateDevice(const std::string& deviceName,
  IEziIODevice** device,
  const std::string& operation) {
  *device = GetDevice(deviceName);

  if (!*device) {
    if (m_enableLogging) {
      std::cerr << "[EziIOManager] " << operation << " failed: Device '"
        << deviceName << "' not found" << std::endl;
    }
    return EziIOError::DEVICE_NOT_FOUND;
  }

  if (!(*device)->IsConnected()) {
    if (m_enableLogging) {
      std::cerr << "[EziIOManager] " << operation << " failed: Device '"
        << deviceName << "' is disconnected" << std::endl;
    }
    return EziIOError::DEVICE_DISCONNECTED;
  }

  return EziIOError::SUCCESS;
}