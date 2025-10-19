// ILogger.cpp
#include "ILogger.h"
#include "LogManager.h"

LogManagerAdapter::LogManagerAdapter(LogManager* logManager)
  : m_logManager(logManager) {
}

void LogManagerAdapter::LogDebug(const std::string& message, const std::string& source) {
  if (m_logManager) m_logManager->Debug(message, source);
}

void LogManagerAdapter::LogInfo(const std::string& message, const std::string& source) {
  if (m_logManager) m_logManager->Info(message, source);
}

void LogManagerAdapter::LogWarning(const std::string& message, const std::string& source) {
  if (m_logManager) m_logManager->Warning(message, source);
}

void LogManagerAdapter::LogError(const std::string& message, const std::string& source) {
  if (m_logManager) m_logManager->Error(message, source);
}

void LogManagerAdapter::LogCritical(const std::string& message, const std::string& source) {
  if (m_logManager) m_logManager->Critical(message, source);
}