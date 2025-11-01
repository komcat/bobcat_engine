// SMUManager.cpp - Implementation of SMU Manager
#include "SMUManager.h"
#include <iostream>
#include <algorithm>

SMUManager::SMUManager(QObject* parent)
  : QObject(parent)
{
  std::cout << "[SMUManager] Initialized" << std::endl;
}

void SMUManager::AddDevice(const std::string& name, SMUDevicePtr device)
{
  if (!device) {
    std::cerr << "[SMUManager] Cannot add null device: " << name << std::endl;
    return;
  }

  if (m_devices.find(name) != m_devices.end()) {
    std::cout << "[SMUManager] Replacing existing device: " << name << std::endl;
    RemoveDevice(name);
  }

  m_devices[name] = device;
  std::cout << "[SMUManager] Added device: " << name << std::endl;

  emit deviceAdded(QString::fromStdString(name));
}

void SMUManager::RemoveDevice(const std::string& name)
{
  auto it = m_devices.find(name);
  if (it != m_devices.end()) {
    // Disconnect before removing
    if (it->second && it->second->IsConnected()) {
      it->second->Disconnect();
    }

    m_devices.erase(it);
    std::cout << "[SMUManager] Removed device: " << name << std::endl;

    emit deviceRemoved(QString::fromStdString(name));
  }
}

void SMUManager::ClearAllDevices()
{
  std::cout << "[SMUManager] Clearing all devices..." << std::endl;

  // Disconnect all first
  DisconnectAll();

  m_devices.clear();
  std::cout << "[SMUManager] All devices cleared" << std::endl;
}

SMUDevicePtr SMUManager::GetDevice(const std::string& name) const
{
  auto it = m_devices.find(name);
  if (it != m_devices.end()) {
    return it->second;
  }

  std::cerr << "[SMUManager] Device not found: " << name << std::endl;
  return nullptr;
}

std::vector<std::string> SMUManager::GetDeviceNames() const
{
  std::vector<std::string> names;
  names.reserve(m_devices.size());

  for (const auto& pair : m_devices) {
    names.push_back(pair.first);
  }

  return names;
}

bool SMUManager::HasDevice(const std::string& name) const
{
  return m_devices.find(name) != m_devices.end();
}

bool SMUManager::ConnectAll()
{
  std::cout << "[SMUManager] Connecting all devices..." << std::endl;

  bool allSuccess = true;
  for (auto& pair : m_devices) {
    const std::string& name = pair.first;
    SMUDevicePtr device = pair.second;

    if (!device) continue;

    std::cout << "[SMUManager] Connecting: " << name << std::endl;

    if (device->Connect()) {
      std::cout << "[SMUManager] Connected: " << name << std::endl;
      emit deviceConnectionChanged(QString::fromStdString(name), true);
    }
    else {
      std::cerr << "[SMUManager] Failed to connect: " << name << std::endl;
      allSuccess = false;
      emit deviceConnectionChanged(QString::fromStdString(name), false);
    }
  }

  if (allSuccess) {
    std::cout << "[SMUManager] All devices connected successfully" << std::endl;
  }
  else {
    std::cerr << "[SMUManager] Some devices failed to connect" << std::endl;
  }

  return allSuccess;
}

void SMUManager::DisconnectAll()
{
  std::cout << "[SMUManager] Disconnecting all devices..." << std::endl;

  for (auto& pair : m_devices) {
    const std::string& name = pair.first;
    SMUDevicePtr device = pair.second;

    if (!device) continue;

    if (device->IsConnected()) {
      std::cout << "[SMUManager] Disconnecting: " << name << std::endl;
      device->Disconnect();
      emit deviceConnectionChanged(QString::fromStdString(name), false);
    }
  }

  std::cout << "[SMUManager] All devices disconnected" << std::endl;
}

void SMUManager::OutputOffAll()
{
  std::cout << "[SMUManager] Turning off all outputs (safety)..." << std::endl;

  for (auto& pair : m_devices) {
    const std::string& name = pair.first;
    SMUDevicePtr device = pair.second;

    if (!device || !device->IsConnected()) continue;

    std::cout << "[SMUManager] Turning off output: " << name << std::endl;
    device->SetOutputOff();
  }

  std::cout << "[SMUManager] All outputs turned off" << std::endl;
}
