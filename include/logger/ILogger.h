// ILogger.h - Abstract logging interface
#pragma once

#include <string>

class LogManager;  // Forward declaration

/**
 * @brief Abstract logger interface
 */
class ILogger {
public:
  virtual ~ILogger() = default;

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
  explicit LogManagerAdapter(LogManager* logManager);

  void LogDebug(const std::string& message, const std::string& source = "") override;
  void LogInfo(const std::string& message, const std::string& source = "") override;
  void LogWarning(const std::string& message, const std::string& source = "") override;
  void LogError(const std::string& message, const std::string& source = "") override;
  void LogCritical(const std::string& message, const std::string& source = "") override;

private:
  LogManager* m_logManager;
};