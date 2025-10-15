// ILogger.h - Abstract logging interface
#pragma once

#include <string>

/**
 * @brief Abstract logger interface
 *
 * Compatible with LogManager and allows dependency injection
 * for flexible logging across different components.
 */
class ILogger {
public:
  virtual ~ILogger() = default;

  // Core logging methods
  virtual void LogDebug(const std::string& message, const std::string& source = "") = 0;
  virtual void LogInfo(const std::string& message, const std::string& source = "") = 0;
  virtual void LogWarning(const std::string& message, const std::string& source = "") = 0;
  virtual void LogError(const std::string& message, const std::string& source = "") = 0;
  virtual void LogCritical(const std::string& message, const std::string& source = "") = 0;
};

/**
 * @brief Adapter to make LogManager compatible with ILogger interface
 */
class LogManagerAdapter : public ILogger {
public:
  explicit LogManagerAdapter(LogManager* logManager)
    : m_logManager(logManager) {
  }

  void LogDebug(const std::string& message, const std::string& source = "") override {
    if (m_logManager) m_logManager->Debug(message, source);
  }

  void LogInfo(const std::string& message, const std::string& source = "") override {
    if (m_logManager) m_logManager->Info(message, source);
  }

  void LogWarning(const std::string& message, const std::string& source = "") override {
    if (m_logManager) m_logManager->Warning(message, source);
  }

  void LogError(const std::string& message, const std::string& source = "") override {
    if (m_logManager) m_logManager->Error(message, source);
  }

  void LogCritical(const std::string& message, const std::string& source = "") override {
    if (m_logManager) m_logManager->Critical(message, source);
  }

private:
  LogManager* m_logManager;
};