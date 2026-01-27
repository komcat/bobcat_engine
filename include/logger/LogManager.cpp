// LogManager.cpp
#include "LogManager.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <filesystem>

LogManager::LogManager()
  : m_consoleEnabled(true)
  , m_fileEnabled(false)
  , m_minimumLevel(Level::DEBUG)
  , m_maxHistorySize(2000)  // Reduced from 5000 to prevent memory issues in long loops
  , m_errorCount(0)
  , m_warningCount(0)
{
  std::cout << "[LogManager] Initialized" << std::endl;
}

LogManager::~LogManager()
{
  if (m_logFile.is_open()) {
    m_logFile.close();
  }
  std::cout << "[LogManager] Shutdown" << std::endl;
}

// ============================================================================
// ILogger Interface Implementation
// ============================================================================

void LogManager::LogDebug(const std::string& message, const std::string& source)
{
  Debug(message, source);
}

void LogManager::LogInfo(const std::string& message, const std::string& source)
{
  Info(message, source);
}

void LogManager::LogWarning(const std::string& message, const std::string& source)
{
  Warning(message, source);
}

void LogManager::LogError(const std::string& message, const std::string& source)
{
  Error(message, source);
}

void LogManager::LogCritical(const std::string& message, const std::string& source)
{
  Critical(message, source);
}

// ============================================================================
// Original Methods (for backward compatibility)
// ============================================================================

void LogManager::Debug(const std::string& message, const std::string& source)
{
  Log(Level::DEBUG, message, source);
}

void LogManager::Info(const std::string& message, const std::string& source)
{
  Log(Level::INFO, message, source);
}

void LogManager::Warning(const std::string& message, const std::string& source)
{
  Log(Level::WARNING, message, source);
}

void LogManager::Error(const std::string& message, const std::string& source)
{
  Log(Level::FAILURE, message, source);
}

void LogManager::Critical(const std::string& message, const std::string& source)
{
  Log(Level::CRITICAL, message, source);
}

// ============================================================================
// Core Logging
// ============================================================================

void LogManager::Log(Level level, const std::string& message, const std::string& source)
{
  if (level < m_minimumLevel) {
    return;
  }

  LogEntry entry;
  entry.level = level;
  entry.message = message;
  entry.timestamp = GetTimestamp();
  entry.source = source;

  // Copy callback BEFORE locking
  LogCallback callbackCopy;

  {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Add to history
    m_history.push_back(entry);

    if (m_history.size() > m_maxHistorySize) {
      m_history.erase(m_history.begin());
    }

    // Update statistics
    if (level == Level::FAILURE || level == Level::CRITICAL) {
      m_errorCount++;
    }
    else if (level == Level::WARNING) {
      m_warningCount++;
    }

    // Output to console
    if (m_consoleEnabled) {
      WriteToConsole(entry);
    }

    // Output to file
    if (m_fileEnabled && m_logFile.is_open()) {
      WriteToFile(entry);
    }

    // Copy callback while still locked
    callbackCopy = m_uiCallback;
  }
  // Mutex is NOW unlocked - safe to call callback

  // Call UI callback OUTSIDE the lock
  if (callbackCopy) {
    callbackCopy(entry);
  }
}

void LogManager::EnableFileOutput(bool enable, const std::string& filePath)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (enable) {
    // Extract directory from the provided path (e.g., "logs/bobcat.log" -> "logs")
    std::filesystem::path path(filePath);
    m_logDirectory = path.parent_path().string();
    if (m_logDirectory.empty()) {
      m_logDirectory = "logs";
    }

    // Create directory if needed
    std::filesystem::create_directories(m_logDirectory);

    // Get current date and create date-stamped filename
    m_currentLogDate = GetDateString();
    m_logFilePath = m_logDirectory + "/bobcat_" + m_currentLogDate + ".log";

    // Open log file
    m_logFile.open(m_logFilePath, std::ios::app);
    if (m_logFile.is_open()) {
      m_fileEnabled = true;
      std::cout << "[LogManager] File logging enabled: " << m_logFilePath << std::endl;
    }
    else {
      std::cerr << "[LogManager] Failed to open log file: " << m_logFilePath << std::endl;
    }
  }
  else {
    if (m_logFile.is_open()) {
      m_logFile.close();
    }
    m_fileEnabled = false;
  }
}

void LogManager::SetUICallback(LogCallback callback)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_uiCallback = callback;
}

void LogManager::ClearUICallback()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_uiCallback = nullptr;
}

std::vector<LogManager::LogEntry> LogManager::GetRecentEntries(size_t count) const
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_history.empty()) {
    return {};
  }

  size_t start = (m_history.size() > count) ? (m_history.size() - count) : 0;
  return std::vector<LogEntry>(m_history.begin() + start, m_history.end());
}

std::vector<LogManager::LogEntry> LogManager::GetAllEntries() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_history;
}

void LogManager::ClearHistory()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_history.clear();
  m_errorCount = 0;
  m_warningCount = 0;
}

size_t LogManager::GetTotalCount() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_history.size();
}

size_t LogManager::GetErrorCount() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_errorCount;
}

size_t LogManager::GetWarningCount() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_warningCount;
}

std::string LogManager::LevelToString(Level level)
{
  switch (level) {
  case Level::DEBUG:    return "DEBUG";
  case Level::INFO:     return "INFO";
  case Level::WARNING:  return "WARNING";
  case Level::FAILURE:  return "ERROR";
  case Level::CRITICAL: return "CRITICAL";
  default:              return "UNKNOWN";
  }
}

std::string LogManager::LevelToColorCode(Level level)
{
  switch (level) {
  case Level::DEBUG:    return "#888888";  // Gray
  case Level::INFO:     return "#00ff00";  // Green
  case Level::WARNING:  return "#ffaa00";  // Orange
  case Level::FAILURE:  return "#ff4444";  // Red
  case Level::CRITICAL: return "#ff0000";  // Bright Red
  default:              return "#ffffff";  // White
  }
}

void LogManager::WriteToConsole(const LogEntry& entry)
{
  std::cout << "[" << entry.timestamp << "] "
    << "[" << LevelToString(entry.level) << "] ";

  if (!entry.source.empty()) {
    std::cout << "[" << entry.source << "] ";
  }

  std::cout << entry.message << std::endl;
}

void LogManager::WriteToFile(const LogEntry& entry)
{
  // Check if we need to rotate to a new date-based log file
  CheckAndRotateLogFile();

  if (!m_logFile.is_open()) {
    return;
  }

  m_logFile << "[" << entry.timestamp << "] "
    << "[" << LevelToString(entry.level) << "] ";

  if (!entry.source.empty()) {
    m_logFile << "[" << entry.source << "] ";
  }

  m_logFile << entry.message << std::endl;
  m_logFile.flush();
}

std::string LogManager::GetTimestamp() const
{
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
    now.time_since_epoch()) % 1000;

  std::tm timeinfo;
  localtime_s(&timeinfo, &time_t);

  std::stringstream ss;
  ss << std::put_time(&timeinfo, "%H:%M:%S");
  ss << "." << std::setfill('0') << std::setw(3) << ms.count();

  return ss.str();
}

std::string LogManager::GetDateString() const
{
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);

  std::tm timeinfo;
  localtime_s(&timeinfo, &time_t);

  std::stringstream ss;
  ss << std::put_time(&timeinfo, "%Y-%m-%d");

  return ss.str();
}

void LogManager::CheckAndRotateLogFile()
{
  // Check if date has changed since last log
  std::string currentDate = GetDateString();

  if (currentDate != m_currentLogDate) {
    // Date changed - close old file and open new one
    if (m_logFile.is_open()) {
      m_logFile << "[" << GetTimestamp() << "] [INFO] Log file rotating to new date: " << currentDate << std::endl;
      m_logFile.close();
    }

    m_currentLogDate = currentDate;
    m_logFilePath = m_logDirectory + "/bobcat_" + m_currentLogDate + ".log";

    m_logFile.open(m_logFilePath, std::ios::app);
    if (m_logFile.is_open()) {
      std::cout << "[LogManager] Rotated to new log file: " << m_logFilePath << std::endl;
      m_logFile << "[" << GetTimestamp() << "] [INFO] Log file started (rotated from previous date)" << std::endl;
    }
    else {
      std::cerr << "[LogManager] Failed to open rotated log file: " << m_logFilePath << std::endl;
      m_fileEnabled = false;
    }
  }
}