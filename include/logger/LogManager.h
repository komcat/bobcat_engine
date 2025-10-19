// LogManager.h - Thread-safe logging system
#pragma once

#include "ILogger.h"  // ✅ ADD THIS
#include <string>
#include <vector>
#include <mutex>
#include <functional>
#include <fstream>
#include <chrono>

/**
 * @brief Centralized logging system
 *
 * Features:
 * - Multiple log levels
 * - Thread-safe
 * - Multiple outputs (console, file, UI)
 * - History management
 * - Implements ILogger interface
 */
class LogManager : public ILogger  // ✅ ADD INHERITANCE
{
public:
  enum class Level {
    DEBUG,
    INFO,
    WARNING,
    FAILURE,
    CRITICAL
  };

  struct LogEntry {
    Level level;
    std::string message;
    std::string timestamp;
    std::string source;  // Optional: which component logged this
  };

  using LogCallback = std::function<void(const LogEntry&)>;

  LogManager();
  ~LogManager();

  // ✅ ILogger interface implementation
  void LogDebug(const std::string& message, const std::string& source = "") override;
  void LogInfo(const std::string& message, const std::string& source = "") override;
  void LogWarning(const std::string& message, const std::string& source = "") override;
  void LogError(const std::string& message, const std::string& source = "") override;
  void LogCritical(const std::string& message, const std::string& source = "") override;

  // Original logging methods (kept for backward compatibility)
  void Debug(const std::string& message, const std::string& source = "");
  void Info(const std::string& message, const std::string& source = "");
  void Warning(const std::string& message, const std::string& source = "");
  void Error(const std::string& message, const std::string& source = "");
  void Critical(const std::string& message, const std::string& source = "");

  // Generic log method
  void Log(Level level, const std::string& message, const std::string& source = "");

  // Output configuration
  void EnableConsoleOutput(bool enable) { m_consoleEnabled = enable; }
  void EnableFileOutput(bool enable, const std::string& filePath = "logs/bobcat.log");
  void SetMinimumLevel(Level level) { m_minimumLevel = level; }

  // UI callback for real-time updates
  void SetUICallback(LogCallback callback);
  void ClearUICallback();

  // History access
  std::vector<LogEntry> GetRecentEntries(size_t count = 100) const;
  std::vector<LogEntry> GetAllEntries() const;
  void ClearHistory();

  // Statistics
  size_t GetTotalCount() const;
  size_t GetErrorCount() const;
  size_t GetWarningCount() const;

  // Level utilities
  static std::string LevelToString(Level level);
  static std::string LevelToColorCode(Level level); // For UI styling

private:
  void WriteToConsole(const LogEntry& entry);
  void WriteToFile(const LogEntry& entry);
  std::string GetTimestamp() const;

  mutable std::mutex m_mutex;
  std::vector<LogEntry> m_history;
  LogCallback m_uiCallback;

  bool m_consoleEnabled;
  bool m_fileEnabled;
  std::ofstream m_logFile;
  std::string m_logFilePath;
  Level m_minimumLevel;

  size_t m_maxHistorySize;
  size_t m_errorCount;
  size_t m_warningCount;
};