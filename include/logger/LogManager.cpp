// LogManager.cpp - spdlog-based logging implementation
#include "LogManager.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <algorithm>

// spdlog includes
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/ansicolor_sink.h>

#ifdef _WIN32
#include <windows.h>
#endif

// Constants for file rotation
constexpr size_t MAX_FILE_SIZE = 3 * 1024 * 1024;  // 3 MB
constexpr size_t MAX_ROTATING_FILES = 5;           // Keep 5 rotated files per day

LogManager::LogManager()
  : m_consoleEnabled(true)
  , m_fileEnabled(false)
  , m_minimumLevel(Level::DEBUG)
  , m_maxHistorySize(2000)
  , m_errorCount(0)
  , m_warningCount(0)
{
  InitializeSpdlog();
}

LogManager::~LogManager()
{
  if (m_logger) {
    m_logger->flush();
  }
  spdlog::drop("bobcat");
  std::cout << "[LogManager] Shutdown" << std::endl;
}

void LogManager::InitializeSpdlog()
{
  try {
#ifdef _WIN32
    // Enable ANSI escape codes (Virtual Terminal Processing) on Windows 10+
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
      DWORD dwMode = 0;
      if (GetConsoleMode(hOut, &dwMode)) {
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, dwMode);
      }
    }

    // Use ANSI color sink (works with VT processing enabled)
    auto ansiSink = std::make_shared<spdlog::sinks::ansicolor_stdout_sink_mt>();
    m_consoleSink = ansiSink;
#else
    // Create console sink with colors for non-Windows
    m_consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
#endif

    // Create logger with console sink only (file sink added later via EnableFileOutput)
    m_logger = std::make_shared<spdlog::logger>("bobcat", m_consoleSink);
    m_logger->set_level(spdlog::level::trace);
    m_logger->set_pattern("[%H:%M:%S.%e] [%l] %v");

    spdlog::register_logger(m_logger);
    std::cout << "[LogManager] Initialized with spdlog backend" << std::endl;
  }
  catch (const spdlog::spdlog_ex& ex) {
    std::cerr << "[LogManager] spdlog initialization failed: " << ex.what() << std::endl;
  }
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

  // Check for date rotation if file logging is enabled
  if (m_fileEnabled) {
    CheckAndRotateLogFile();
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

    // Copy callback while still locked
    callbackCopy = m_uiCallback;
  }
  // Mutex is NOW unlocked

  // Log via spdlog (thread-safe internally)
  if (m_logger) {
    std::string formattedMsg = source.empty() ? message : "[" + source + "] " + message;

    switch (level) {
      case Level::DEBUG:    m_logger->debug(formattedMsg); break;
      case Level::INFO:     m_logger->info(formattedMsg); break;
      case Level::WARNING:  m_logger->warn(formattedMsg); break;
      case Level::FAILURE:  m_logger->error(formattedMsg); break;
      case Level::CRITICAL: m_logger->critical(formattedMsg); break;
    }
  }

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

    // Get current date for date-stamped filename
    m_currentLogDate = GetDateString();
    CreateFileSink();
    m_fileEnabled = true;
  }
  else {
    // Remove file sink from logger
    if (m_fileSink && m_logger) {
      auto& sinks = m_logger->sinks();
      sinks.erase(std::remove(sinks.begin(), sinks.end(), m_fileSink), sinks.end());
      m_fileSink.reset();
    }
    m_fileEnabled = false;
  }
}

void LogManager::CreateFileSink()
{
  try {
    std::string logFileName = m_logDirectory + "/bobcat_" + m_currentLogDate + ".log";

    // Remove old file sink if exists
    if (m_fileSink && m_logger) {
      auto& sinks = m_logger->sinks();
      sinks.erase(std::remove(sinks.begin(), sinks.end(), m_fileSink), sinks.end());
    }

    // Create rotating file sink: 3MB max, 5 backup files
    m_fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
      logFileName, MAX_FILE_SIZE, MAX_ROTATING_FILES);

    m_logger->sinks().push_back(m_fileSink);
    std::cout << "[LogManager] File logging enabled: " << logFileName
              << " (max 3MB, " << MAX_ROTATING_FILES << " rolling files)" << std::endl;
  }
  catch (const spdlog::spdlog_ex& ex) {
    std::cerr << "[LogManager] Failed to create file sink: " << ex.what() << std::endl;
    m_fileEnabled = false;
  }
}

void LogManager::CheckAndRotateLogFile()
{
  // Check if date has changed since last log
  std::string currentDate = GetDateString();

  if (currentDate != m_currentLogDate && m_fileEnabled) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Log rotation message before switching
    if (m_logger) {
      m_logger->info("Log file rotating to new date: {}", currentDate);
    }

    m_currentLogDate = currentDate;
    CreateFileSink();

    if (m_logger) {
      m_logger->info("Log file started (rotated from previous date)");
    }

    std::cout << "[LogManager] Rotated to new log file for date: " << currentDate << std::endl;
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
  // This method is kept for backward compatibility but spdlog handles console output now
  // Only used if spdlog is not initialized
  std::cout << "[" << entry.timestamp << "] "
    << "[" << LevelToString(entry.level) << "] ";

  if (!entry.source.empty()) {
    std::cout << "[" << entry.source << "] ";
  }

  std::cout << entry.message << std::endl;
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
