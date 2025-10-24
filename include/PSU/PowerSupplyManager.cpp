// PowerSupplyManager.cpp
#include "PowerSupplyManager.h"
#include <iostream>

PowerSupplyManager::PowerSupplyManager(QObject* parent)
  : QObject(parent)
{
}

void PowerSupplyManager::addPowerSupply(const std::string& name, PowerSupplyPtr psu)
{
  if (!psu) {
    std::cerr << "[PowerSupplyManager] Cannot add null power supply" << std::endl;
    return;
  }

  if (m_devices.find(name) != m_devices.end()) {
    std::cerr << "[PowerSupplyManager] Device '" << name << "' already exists" << std::endl;
    return;
  }

  m_devices[name] = psu;
  emit deviceAdded(QString::fromStdString(name));

  std::cout << "[PowerSupplyManager] Added device: " << name << std::endl;
}

void PowerSupplyManager::removePowerSupply(const std::string& name)
{
  auto it = m_devices.find(name);
  if (it == m_devices.end()) {
    std::cerr << "[PowerSupplyManager] Device '" << name << "' not found" << std::endl;
    return;
  }

  // Disconnect before removing
  if (it->second->isConnected()) {
    it->second->disconnect();
  }

  m_devices.erase(it);
  emit deviceRemoved(QString::fromStdString(name));

  std::cout << "[PowerSupplyManager] Removed device: " << name << std::endl;
}

void PowerSupplyManager::clearAllDevices()
{
  disconnectAll();
  m_devices.clear();
  std::cout << "[PowerSupplyManager] Cleared all devices" << std::endl;
}

PowerSupplyPtr PowerSupplyManager::getPowerSupply(const std::string& name) const
{
  auto it = m_devices.find(name);
  if (it != m_devices.end()) {
    return it->second;
  }
  return nullptr;
}

std::vector<std::string> PowerSupplyManager::getDeviceNames() const
{
  std::vector<std::string> names;
  names.reserve(m_devices.size());

  for (const auto& pair : m_devices) {
    names.push_back(pair.first);
  }

  return names;
}

bool PowerSupplyManager::hasDevice(const std::string& name) const
{
  return m_devices.find(name) != m_devices.end();
}

bool PowerSupplyManager::connectAll()
{
  bool allConnected = true;

  for (auto& pair : m_devices) {
    const std::string& name = pair.first;
    PowerSupplyPtr& psu = pair.second;

    if (!psu->isConnected()) {
      std::cout << "[PowerSupplyManager] Connecting " << name << "..." << std::endl;

      if (psu->connect()) {
        emit deviceConnectionChanged(QString::fromStdString(name), true);
      }
      else {
        std::cerr << "[PowerSupplyManager] Failed to connect " << name
          << ": " << psu->getLastError() << std::endl;
        allConnected = false;
      }
    }
  }

  return allConnected;
}

void PowerSupplyManager::disconnectAll()
{
  for (auto& pair : m_devices) {
    const std::string& name = pair.first;
    PowerSupplyPtr& psu = pair.second;

    if (psu->isConnected()) {
      std::cout << "[PowerSupplyManager] Disconnecting " << name << "..." << std::endl;
      psu->disconnect();
      emit deviceConnectionChanged(QString::fromStdString(name), false);
    }
  }
}