// QuickOutputManager.cpp
#include "QuickOutputManager.h"
#include "EziIOManager.h"
#include "IEziIODevice.h"

QuickOutputManager::QuickOutputManager(EziIOManager* ioManager)
  : m_ioManager(ioManager)
{
}

QuickOutputManager::~QuickOutputManager()
{
}

void QuickOutputManager::AddOutputPin(const OutputPinDefinition& pinDef)
{
  m_outputPins.push_back(pinDef);
}

bool QuickOutputManager::RemoveOutputPin(size_t index)
{
  if (index >= m_outputPins.size()) {
    return false;
  }

  m_outputPins.erase(m_outputPins.begin() + index);
  return true;
}

void QuickOutputManager::ClearAllPins()
{
  m_outputPins.clear();
}

const OutputPinDefinition* QuickOutputManager::GetPinDefinition(size_t index) const
{
  if (index >= m_outputPins.size()) {
    return nullptr;
  }

  return &m_outputPins[index];
}

int QuickOutputManager::ResolvePinNumber(const OutputPinDefinition& pinDef) const
{
  // If pin number is already set, use it
  if (pinDef.pinNumber >= 0) {
    return pinDef.pinNumber;
  }

  // Otherwise, try to resolve by name
  if (!pinDef.pinName.empty() && m_ioManager) {
    IEziIODevice* device = m_ioManager->GetDevice(pinDef.deviceName);
    if (device) {
      return device->GetOutputPinByName(pinDef.pinName);
    }
  }

  return -1;  // Invalid
}

EziIOError QuickOutputManager::TurnOn(size_t index)
{
  return SetOutput(index, true);
}

EziIOError QuickOutputManager::TurnOff(size_t index)
{
  return SetOutput(index, false);
}

EziIOError QuickOutputManager::SetOutput(size_t index, bool state)
{
  if (!m_ioManager) {
    return EziIOError::NOT_INITIALIZED;
  }

  if (index >= m_outputPins.size()) {
    return EziIOError::INVALID_PIN;
  }

  const OutputPinDefinition& pinDef = m_outputPins[index];
  int pinNumber = ResolvePinNumber(pinDef);

  if (pinNumber < 0) {
    return EziIOError::INVALID_PIN;
  }

  // Set the output
  EziIOError error = m_ioManager->SetOutput(pinDef.deviceName, pinNumber, state);

  // Force a read to update the cached status
  if (error == EziIOError::SUCCESS) {
    uint64_t outputs = 0;
    uint64_t status = 0;
    m_ioManager->ReadOutputs(pinDef.deviceName, outputs, status);
  }

  return error;
}

EziIOError QuickOutputManager::ToggleOutput(size_t index)
{
  if (!m_ioManager) {
    return EziIOError::NOT_INITIALIZED;
  }

  if (index >= m_outputPins.size()) {
    return EziIOError::INVALID_PIN;
  }

  // Get current status
  OutputPinStatus status = GetPinStatus(index);
  if (!status.isValid) {
    return EziIOError::OPERATION_FAILED;
  }

  // Toggle
  return SetOutput(index, !status.isOn);
}

void QuickOutputManager::TurnOffAll()
{
  if (!m_ioManager) {
    return;
  }

  // Track which devices we've refreshed to avoid redundant reads
  std::vector<std::string> refreshedDevices;

  for (size_t i = 0; i < m_outputPins.size(); ++i) {
    const OutputPinDefinition& pinDef = m_outputPins[i];
    int pinNumber = ResolvePinNumber(pinDef);

    if (pinNumber >= 0) {
      // Turn off the output
      (void)m_ioManager->SetOutput(pinDef.deviceName, pinNumber, false);

      // Check if we've already refreshed this device
      bool alreadyRefreshed = false;
      for (const auto& deviceName : refreshedDevices) {
        if (deviceName == pinDef.deviceName) {
          alreadyRefreshed = true;
          break;
        }
      }

      // Refresh device status if not done yet
      if (!alreadyRefreshed) {
        uint64_t outputs = 0;
        uint64_t status = 0;
        m_ioManager->ReadOutputs(pinDef.deviceName, outputs, status);
        refreshedDevices.push_back(pinDef.deviceName);
      }
    }
  }
}

OutputPinStatus QuickOutputManager::GetPinStatus(size_t index) const
{
  if (!m_ioManager || index >= m_outputPins.size()) {
    return OutputPinStatus();
  }

  const OutputPinDefinition& pinDef = m_outputPins[index];
  int pinNumber = ResolvePinNumber(pinDef);

  if (pinNumber < 0) {
    return OutputPinStatus(pinDef.displayName, false, false);
  }

  // Force a fresh read from the device (not cached)
  uint64_t outputs = 0;
  uint64_t status = 0;
  EziIOError error = m_ioManager->ReadOutputs(pinDef.deviceName, outputs, status);

  if (error != EziIOError::SUCCESS) {
    return OutputPinStatus(pinDef.displayName, false, false);
  }

  // Get device to determine output count and bit offset
  IEziIODevice* device = m_ioManager->GetDevice(pinDef.deviceName);
  if (!device) {
    return OutputPinStatus(pinDef.displayName, false, false);
  }

  int outputCount = device->GetOutputCount();

  // Outputs use bits 8-15 for 8-pin, or bits 16-31 for 16-pin devices
  int bitOffset = (outputCount > 8) ? 16 : 8;

  // Check if bit is set (with proper offset)
  bool isOn = (outputs & (1ULL << (bitOffset + pinNumber))) != 0;
  return OutputPinStatus(pinDef.displayName, isOn, true);
}

std::vector<OutputPinStatus> QuickOutputManager::GetAllPinStatuses() const
{
  std::vector<OutputPinStatus> statuses;
  statuses.reserve(m_outputPins.size());

  if (!m_ioManager) {
    return statuses;
  }

  // Track which devices we've read to avoid redundant reads
  std::vector<std::string> readDevices;

  for (size_t i = 0; i < m_outputPins.size(); ++i) {
    const OutputPinDefinition& pinDef = m_outputPins[i];

    // Check if we need to read this device
    bool needsRead = true;
    for (const auto& deviceName : readDevices) {
      if (deviceName == pinDef.deviceName) {
        needsRead = false;
        break;
      }
    }

    // Read the device if we haven't yet
    if (needsRead) {
      uint64_t outputs = 0;
      uint64_t status = 0;
      m_ioManager->ReadOutputs(pinDef.deviceName, outputs, status);
      readDevices.push_back(pinDef.deviceName);
    }

    // Now get the status (will use cached value we just read)
    int pinNumber = ResolvePinNumber(pinDef);
    if (pinNumber < 0) {
      statuses.push_back(OutputPinStatus(pinDef.displayName, false, false));
      continue;
    }

    // Get device to determine bit offset
    IEziIODevice* device = m_ioManager->GetDevice(pinDef.deviceName);
    if (!device) {
      statuses.push_back(OutputPinStatus(pinDef.displayName, false, false));
      continue;
    }

    uint64_t outputs = 0;
    uint64_t status = 0;
    EziIOError error = m_ioManager->GetLastOutputStatus(pinDef.deviceName, outputs, status);

    if (error != EziIOError::SUCCESS) {
      statuses.push_back(OutputPinStatus(pinDef.displayName, false, false));
      continue;
    }

    // Outputs use bits 8-15 for 8-pin, or bits 16-31 for 16-pin devices
    int outputCount = device->GetOutputCount();
    int bitOffset = (outputCount > 8) ? 16 : 8;

    // Check if bit is set (with proper offset)
    bool isOn = (outputs & (1ULL << (bitOffset + pinNumber))) != 0;
    statuses.push_back(OutputPinStatus(pinDef.displayName, isOn, true));
  }

  return statuses;
}

void QuickOutputManager::RefreshAllStatuses()
{
  if (!m_ioManager) {
    return;
  }

  // Trigger read for each unique device
  std::vector<std::string> devicesRead;

  for (const auto& pinDef : m_outputPins) {
    // Check if we've already read this device
    bool alreadyRead = false;
    for (const auto& deviceName : devicesRead) {
      if (deviceName == pinDef.deviceName) {
        alreadyRead = true;
        break;
      }
    }

    if (!alreadyRead) {
      uint64_t outputs = 0;
      uint64_t status = 0;
      m_ioManager->ReadOutputs(pinDef.deviceName, outputs, status);
      devicesRead.push_back(pinDef.deviceName);
    }
  }
}
