// EziIODevice.cpp - Implementation
#include "EziIODevice.h"
#include <sstream>
#include <chrono>
#include <thread>
#include <iostream>

// ============================================================================
// STATIC PIN MASKS (uint64_t for 48+ I/O support)
// ============================================================================

const uint64_t EziIODevice::OUTPUT_PIN_MASKS_16[16] = {
  0x10000ULL, 0x20000ULL, 0x40000ULL, 0x80000ULL,
  0x100000ULL, 0x200000ULL, 0x400000ULL, 0x800000ULL,
  0x1000000ULL, 0x2000000ULL, 0x4000000ULL, 0x8000000ULL,
  0x10000000ULL, 0x20000000ULL, 0x40000000ULL, 0x80000000ULL
};

const uint64_t EziIODevice::OUTPUT_PIN_MASKS_8[8] = {
  0x100ULL, 0x200ULL, 0x400ULL, 0x800ULL,
  0x1000ULL, 0x2000ULL, 0x4000ULL, 0x8000ULL
};

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

EziIODevice::EziIODevice(const EziIODeviceConfig& config)
  : m_config(config),
  m_isConnected(false)
{
  ParseIpAddress();

  // Select appropriate mask array based on output count
  m_currentOutputMasks = (m_config.OutputCount <= 8) ? OUTPUT_PIN_MASKS_8 : OUTPUT_PIN_MASKS_16;

  std::cout << "[EziIODevice] Created device: " << m_config.Name
    << " (ID: " << m_config.DeviceId << ")" << std::endl;
}

EziIODevice::~EziIODevice() {
  if (m_isConnected) {
    Disconnect();
  }
}

// ============================================================================
// CONNECTION MANAGEMENT
// ============================================================================

bool EziIODevice::Connect() {
  if (m_isConnected) {
    std::cout << "[EziIODevice] " << m_config.Name << " already connected" << std::endl;
    return true;
  }

  std::cout << "[EziIODevice] Connecting to " << m_config.Name
    << " at " << m_config.IpAddress
    << " (ID: " << m_config.DeviceId << ")" << std::endl;

  if (PE::FAS_ConnectTCP(m_ipBytes[0], m_ipBytes[1], m_ipBytes[2], m_ipBytes[3],
    m_config.DeviceId)) {
    m_isConnected = true;
    std::cout << "[EziIODevice] ✓ Connected successfully!" << std::endl;
    return true;
  }

  std::cerr << "[EziIODevice] ✗ Connection failed for " << m_config.Name << std::endl;
  return false;
}

bool EziIODevice::Disconnect() {
  if (!m_isConnected) {
    return true;
  }

  PE::FAS_Close(m_config.DeviceId);
  m_isConnected = false;

  std::cout << "[EziIODevice] Disconnected from " << m_config.Name << std::endl;
  return true;
}

// ============================================================================
// INPUT OPERATIONS
// ============================================================================

EziIOError EziIODevice::ReadInputs(uint64_t& inputs, uint64_t& latch) {
  if (!m_isConnected) {
    std::cerr << "[EziIODevice] " << m_config.Name << " not connected" << std::endl;
    return EziIOError::DEVICE_DISCONNECTED;
  }

  unsigned long temp_inputs = 0;
  unsigned long temp_latch = 0;

  int result = PE::FAS_GetInput(m_config.DeviceId, &temp_inputs, &temp_latch);

  if (result == FMM_OK) {
    inputs = static_cast<uint64_t>(temp_inputs);
    latch = static_cast<uint64_t>(temp_latch);

    // Update cached status
    {
      std::lock_guard<std::mutex> lock(m_statusMutex);
      m_lastStatus.Inputs = inputs;
      m_lastStatus.Latch = latch;
      m_lastStatus.Valid = true;
    }

    return EziIOError::SUCCESS;
  }

  std::cerr << "[EziIODevice] Failed to read inputs from " << m_config.Name
    << ", error: " << result << std::endl;
  return EziIOError::OPERATION_FAILED;
}

EziIOError EziIODevice::GetLastInputStatus(uint64_t& inputs, uint64_t& latch) {
  std::lock_guard<std::mutex> lock(m_statusMutex);
  inputs = m_lastStatus.Inputs;
  latch = m_lastStatus.Latch;
  return m_lastStatus.Valid ? EziIOError::SUCCESS : EziIOError::OPERATION_FAILED;
}

EziIOError EziIODevice::ClearLatch(uint64_t latchMask) {
  if (!m_isConnected) {
    return EziIOError::DEVICE_DISCONNECTED;
  }

  unsigned long temp_latchMask = static_cast<unsigned long>(latchMask);
  int result = PE::FAS_ClearLatch(m_config.DeviceId, temp_latchMask);

  if (result == FMM_OK) {
    return EziIOError::SUCCESS;
  }

  std::cerr << "[EziIODevice] Failed to clear latch for " << m_config.Name
    << ", error: " << result << std::endl;
  return EziIOError::OPERATION_FAILED;
}

// ============================================================================
// OUTPUT OPERATIONS
// ============================================================================

EziIOError EziIODevice::ReadOutputs(uint64_t& outputs, uint64_t& status) {
  if (!m_isConnected) {
    return EziIOError::DEVICE_DISCONNECTED;
  }

  unsigned long temp_outputs = 0;
  unsigned long temp_status = 0;

  int result = PE::FAS_GetOutput(m_config.DeviceId, &temp_outputs, &temp_status);

  if (result == FMM_OK) {
    outputs = static_cast<uint64_t>(temp_outputs);
    status = static_cast<uint64_t>(temp_status);

    // Update cached status
    {
      std::lock_guard<std::mutex> lock(m_statusMutex);
      m_lastStatus.Outputs = outputs;
      m_lastStatus.OutputStatus = status;
    }

    return EziIOError::SUCCESS;
  }

  std::cerr << "[EziIODevice] Failed to read outputs from " << m_config.Name
    << ", error: " << result << std::endl;
  return EziIOError::OPERATION_FAILED;
}

EziIOError EziIODevice::GetLastOutputStatus(uint64_t& outputs, uint64_t& status) {
  std::lock_guard<std::mutex> lock(m_statusMutex);
  outputs = m_lastStatus.Outputs;
  status = m_lastStatus.OutputStatus;
  return EziIOError::SUCCESS;
}

EziIOError EziIODevice::SetOutputs(uint64_t setMask, uint64_t clearMask) {
  if (!m_isConnected) {
    return EziIOError::DEVICE_DISCONNECTED;
  }

  unsigned long temp_setMask = static_cast<unsigned long>(setMask);
  unsigned long temp_clearMask = static_cast<unsigned long>(clearMask);

  std::cout << "[EziIODevice] " << m_config.Name << " - Set: 0x" << std::hex
    << temp_setMask << ", Clear: 0x" << temp_clearMask << std::dec << std::endl;

  int result = PE::FAS_SetOutput(m_config.DeviceId, temp_setMask, temp_clearMask);

  if (result == FMM_OK) {
    // Small delay to ensure operation completes
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Read back to update status
    uint64_t outputs = 0, status = 0;
    ReadOutputs(outputs, status);

    return EziIOError::SUCCESS;
  }

  std::cerr << "[EziIODevice] Failed to set outputs for " << m_config.Name
    << ", error: " << result << std::endl;
  return EziIOError::OPERATION_FAILED;
}

EziIOError EziIODevice::SetOutput(int pin, bool state) {
  if (!m_isConnected) {
    return EziIOError::DEVICE_DISCONNECTED;
  }

  if (pin < 0 || pin >= m_config.OutputCount) {
    std::cerr << "[EziIODevice] Invalid pin: " << pin << std::endl;
    return EziIOError::INVALID_PIN;
  }

  uint64_t mask = GetOutputPinMask(pin);

  std::cout << "[EziIODevice] " << m_config.Name << " - Pin " << pin
    << " -> " << (state ? "ON" : "OFF") << std::endl;

  return state ? SetOutputs(mask, 0) : SetOutputs(0, mask);
}

// ============================================================================
// PIN NAMING
// ============================================================================

void EziIODevice::MapInputPinName(const std::string& pinName, int pinNumber) {
  if (pinNumber >= 0 && pinNumber < m_config.InputCount) {
    m_inputPinNames[pinName] = pinNumber;
    m_inputPinNumbers[pinNumber] = pinName;  // ✅ ADD THIS LINE!
  }
}

void EziIODevice::MapOutputPinName(const std::string& pinName, int pinNumber) {
  if (pinNumber >= 0 && pinNumber < m_config.OutputCount) {
    m_outputPinNames[pinName] = pinNumber;
    m_outputPinNumbers[pinNumber] = pinName;  // ✅ ADD THIS LINE!
  }
}

int EziIODevice::GetInputPinByName(const std::string& pinName) const {
  auto it = m_inputPinNames.find(pinName);
  return (it != m_inputPinNames.end()) ? it->second : -1;
}

int EziIODevice::GetOutputPinByName(const std::string& pinName) const {
  auto it = m_outputPinNames.find(pinName);
  return (it != m_outputPinNames.end()) ? it->second : -1;
}


// NEW: Reverse lookup methods
std::string EziIODevice::GetInputPinName(int pinNumber) const {
  auto it = m_inputPinNumbers.find(pinNumber);
  if (it != m_inputPinNumbers.end()) {
    return it->second;
  }
  // Return default name if not mapped
  return "Input" + std::to_string(pinNumber);
}

std::string EziIODevice::GetOutputPinName(int pinNumber) const {
  auto it = m_outputPinNumbers.find(pinNumber);
  if (it != m_outputPinNumbers.end()) {
    return it->second;
  }
  // Return default name if not mapped
  return "Output" + std::to_string(pinNumber);
}

// ============================================================================
// GET ALL PIN NAMES (for UI dropdowns)
// ============================================================================

std::vector<std::string> EziIODevice::GetAllInputPinNames() const {
  std::vector<std::string> pinNames;
  pinNames.reserve(m_inputPinNames.size());

  // Collect all pin names from the map
  for (const auto& pair : m_inputPinNames) {
    pinNames.push_back(pair.first);  // pair.first is the pin name
  }

  return pinNames;
}

std::vector<std::string> EziIODevice::GetAllOutputPinNames() const {
  std::vector<std::string> pinNames;
  pinNames.reserve(m_outputPinNames.size());

  // Collect all pin names from the map
  for (const auto& pair : m_outputPinNames) {
    pinNames.push_back(pair.first);  // pair.first is the pin name
  }

  return pinNames;
}

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

void EziIODevice::ParseIpAddress() {
  std::istringstream iss(m_config.IpAddress);
  std::string token;
  int i = 0;
  while (std::getline(iss, token, '.') && i < 4) {
    m_ipBytes[i++] = static_cast<uint8_t>(std::stoi(token));
  }
}

uint64_t EziIODevice::GetOutputPinMask(int pin) const {
  if (pin < 0 || pin >= m_config.OutputCount) {
    return 0;
  }
  return m_currentOutputMasks[pin];
}