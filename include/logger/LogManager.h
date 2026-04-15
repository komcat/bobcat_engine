// LogManager.h - Thread-safe logging system with spdlog backend
#pragma once

#include "ILogger.h"
#include <string>
#include <vector>
#include <mutex>
#include <functional>
#include <chrono>
#include <memory>

// Forward declarations for spdlog
namespace spdlog {
    class logger;
    namespace sinks {
        class sink;
    }
}

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
  // Sets the minimum level for both the custom history filter and the
  // underlying spdlog backend (they must stay in sync — spdlog silently
  // drops anything below its own level, so updating just m_minimumLevel
  // isn't enough).
  void SetMinimumLevel(Level level);

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
  std::string GetTimestamp() const;
  std::string GetDateString() const;  // Returns YYYY-MM-DD for log file naming
  void CheckAndRotateLogFile();       // Rotate log file if date changed
  void InitializeSpdlog();            // Initialize spdlog backend
  void CreateFileSink();              // Create/recreate rotating file sink

  mutable std::mutex m_mutex;
  std::vector<LogEntry> m_history;
  LogCallback m_uiCallback;

  // spdlog components
  std::shared_ptr<spdlog::logger> m_logger;
  std::shared_ptr<spdlog::sinks::sink> m_consoleSink;
  std::shared_ptr<spdlog::sinks::sink> m_fileSink;

  bool m_consoleEnabled;
  bool m_fileEnabled;
  std::string m_logDirectory;     // Base directory for log files
  std::string m_currentLogDate;   // Current log file date (YYYY-MM-DD)
  Level m_minimumLevel;

  size_t m_maxHistorySize;
  size_t m_errorCount;
  size_t m_warningCount;
};