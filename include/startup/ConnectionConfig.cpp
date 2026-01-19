// ConnectionConfig.cpp
#include "ConnectionConfig.h"
#include <fstream>
#include <iostream>
#include "nlohmann/json.hpp"

using json = nlohmann::json;

namespace Startup {

ConnectionConfig& ConnectionConfig::instance()
{
  static ConnectionConfig instance;
  return instance;
}

ConnectionConfig::ConnectionConfig()
{
  // Set up default configurations
  // These will be overridden if config file is loaded

  // Phase 0: Independent, can run in parallel
  m_managerConfigs["TcpClientManager"] = ManagerConnectionConfig(
    std::chrono::milliseconds(5000), 0);
  m_managerConfigs["CameraManager"] = ManagerConnectionConfig(
    std::chrono::milliseconds(15000), 0);

  // Phase 1: Motion and IO (can be parallel, IO needed before safety watch)
  m_managerConfigs["MotionManager"] = ManagerConnectionConfig(
    std::chrono::milliseconds(20000), 1);
  m_managerConfigs["IOManager"] = ManagerConnectionConfig(
    std::chrono::milliseconds(10000), 1);

  // Phase 2: Devices that depend on IO for safety watch
  m_managerConfigs["TECManager"] = ManagerConnectionConfig(
    std::chrono::milliseconds(10000), 2);
  m_managerConfigs["LightingManager"] = ManagerConnectionConfig(
    std::chrono::milliseconds(8000), 2);

  m_phaseCount = 3;
}

bool ConnectionConfig::loadFromFile(const std::string& filePath)
{
  try {
    std::ifstream file(filePath);
    if (!file.is_open()) {
      std::cout << "[ConnectionConfig] Config file not found: " << filePath
                << " (using defaults)" << std::endl;
      return false;
    }

    json config;
    file >> config;

    // Load default timeout
    if (config.contains("default_timeout_ms")) {
      m_defaultTimeout = std::chrono::milliseconds(config["default_timeout_ms"].get<int>());
    }

    // Load manager configurations
    if (config.contains("managers")) {
      for (auto& [name, managerConfig] : config["managers"].items()) {
        ManagerConnectionConfig cfg;

        if (managerConfig.contains("timeout_ms")) {
          cfg.timeout = std::chrono::milliseconds(managerConfig["timeout_ms"].get<int>());
        }
        else {
          cfg.timeout = m_defaultTimeout;
        }

        if (managerConfig.contains("phase")) {
          cfg.phase = managerConfig["phase"].get<int>();
          if (cfg.phase >= m_phaseCount) {
            m_phaseCount = cfg.phase + 1;
          }
        }

        if (managerConfig.contains("enabled")) {
          cfg.enabled = managerConfig["enabled"].get<bool>();
        }

        m_managerConfigs[QString::fromStdString(name)] = cfg;
      }
    }

    m_loaded = true;
    std::cout << "[ConnectionConfig] Loaded " << m_managerConfigs.size()
              << " manager configurations from " << filePath << std::endl;
    return true;
  }
  catch (const json::exception& e) {
    std::cerr << "[ConnectionConfig] JSON parse error: " << e.what() << std::endl;
    return false;
  }
  catch (const std::exception& e) {
    std::cerr << "[ConnectionConfig] Error loading config: " << e.what() << std::endl;
    return false;
  }
}

std::chrono::milliseconds ConnectionConfig::getTimeout(const QString& managerName) const
{
  auto it = m_managerConfigs.find(managerName);
  if (it != m_managerConfigs.end()) {
    return it.value().timeout;
  }
  return m_defaultTimeout;
}

int ConnectionConfig::getPhase(const QString& managerName) const
{
  auto it = m_managerConfigs.find(managerName);
  if (it != m_managerConfigs.end()) {
    return it.value().phase;
  }
  return 0;  // Default to phase 0
}

bool ConnectionConfig::isEnabled(const QString& managerName) const
{
  auto it = m_managerConfigs.find(managerName);
  if (it != m_managerConfigs.end()) {
    return it.value().enabled;
  }
  return true;  // Default to enabled
}

ManagerConnectionConfig ConnectionConfig::getConfig(const QString& managerName) const
{
  auto it = m_managerConfigs.find(managerName);
  if (it != m_managerConfigs.end()) {
    return it.value();
  }
  return ManagerConnectionConfig(m_defaultTimeout, 0, true);
}

QStringList ConnectionConfig::getManagersByPhase(int phase) const
{
  QStringList managers;
  for (auto it = m_managerConfigs.begin(); it != m_managerConfigs.end(); ++it) {
    if (it.value().phase == phase && it.value().enabled) {
      managers.append(it.key());
    }
  }
  return managers;
}

int ConnectionConfig::phaseCount() const
{
  return m_phaseCount;
}

} // namespace Startup
