// ConnectionConfig.h
#pragma once

#include <QString>
#include <QMap>
#include <chrono>
#include <string>

namespace Startup {

/**
 * @brief Configuration for a single manager's connection
 */
struct ManagerConnectionConfig {
  std::chrono::milliseconds timeout{10000};  // Default 10 seconds
  int phase = 0;  // Which phase this manager connects in
  bool enabled = true;  // Whether this manager should auto-connect

  ManagerConnectionConfig() = default;
  ManagerConnectionConfig(std::chrono::milliseconds t, int p, bool e = true)
    : timeout(t), phase(p), enabled(e) {}
};

/**
 * @brief Loads and provides connection timeout configuration from JSON
 *
 * Singleton pattern for easy access throughout the application.
 * Configuration file: config/connection_config.json
 */
class ConnectionConfig
{
public:
  /**
   * @brief Get the singleton instance
   */
  static ConnectionConfig& instance();

  /**
   * @brief Load configuration from JSON file
   * @param filePath Path to the configuration file
   * @return true if loaded successfully
   */
  bool loadFromFile(const std::string& filePath);

  /**
   * @brief Get the timeout for a specific manager
   * @param managerName The manager name (e.g., "CameraManager")
   * @return Timeout in milliseconds
   */
  std::chrono::milliseconds getTimeout(const QString& managerName) const;

  /**
   * @brief Get the phase for a specific manager
   * @param managerName The manager name
   * @return Phase number (0, 1, 2, etc.)
   */
  int getPhase(const QString& managerName) const;

  /**
   * @brief Check if a manager is enabled for auto-connect
   * @param managerName The manager name
   * @return true if enabled
   */
  bool isEnabled(const QString& managerName) const;

  /**
   * @brief Get the full configuration for a manager
   * @param managerName The manager name
   * @return Configuration struct
   */
  ManagerConnectionConfig getConfig(const QString& managerName) const;

  /**
   * @brief Get all configured managers sorted by phase
   * @return List of manager names
   */
  QStringList getManagersByPhase(int phase) const;

  /**
   * @brief Get the total number of phases
   */
  int phaseCount() const;

  /**
   * @brief Get the default timeout for unconfigured managers
   */
  std::chrono::milliseconds defaultTimeout() const { return m_defaultTimeout; }

private:
  ConnectionConfig();
  ~ConnectionConfig() = default;
  ConnectionConfig(const ConnectionConfig&) = delete;
  ConnectionConfig& operator=(const ConnectionConfig&) = delete;

  QMap<QString, ManagerConnectionConfig> m_managerConfigs;
  std::chrono::milliseconds m_defaultTimeout{10000};
  int m_phaseCount = 3;  // Default to 3 phases
  bool m_loaded = false;
};

} // namespace Startup
