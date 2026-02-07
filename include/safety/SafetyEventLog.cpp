// SafetyEventLog.cpp
#include "SafetyEventLog.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

using json = nlohmann::json;

namespace {

std::string SeverityToString(SafetySeverity sev) {
  switch (sev) {
    case SafetySeverity::INFO:      return "INFO";
    case SafetySeverity::WARNING:   return "WARNING";
    case SafetySeverity::CRITICAL:  return "CRITICAL";
    case SafetySeverity::EMERGENCY: return "EMERGENCY";
    default:                        return "UNKNOWN";
  }
}

std::string TimePointToISO8601(std::chrono::system_clock::time_point tp) {
  auto time_t_val = std::chrono::system_clock::to_time_t(tp);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
    tp.time_since_epoch()) % 1000;

  std::tm tm_val{};
#ifdef _WIN32
  localtime_s(&tm_val, &time_t_val);
#else
  localtime_r(&time_t_val, &tm_val);
#endif

  std::ostringstream oss;
  oss << std::put_time(&tm_val, "%Y-%m-%dT%H:%M:%S")
      << '.' << std::setfill('0') << std::setw(3) << ms.count();
  return oss.str();
}

} // anonymous namespace

SafetyEventLog::~SafetyEventLog() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_fileStream.is_open()) {
    m_fileStream.close();
  }
}

bool SafetyEventLog::Initialize(const std::string& logDirectory) {
  std::lock_guard<std::mutex> lock(m_mutex);

  try {
    std::filesystem::create_directories(logDirectory);
  } catch (const std::filesystem::filesystem_error& e) {
    std::cerr << "[SafetyEventLog] Failed to create log directory: " << e.what() << std::endl;
    return false;
  }

  m_filePath = logDirectory + "/safety_events.jsonl";
  m_fileStream.open(m_filePath, std::ios::app);
  if (!m_fileStream.is_open()) {
    std::cerr << "[SafetyEventLog] Failed to open log file: " << m_filePath << std::endl;
    return false;
  }

  std::cout << "[SafetyEventLog] Initialized, writing to: " << m_filePath << std::endl;
  return true;
}

void SafetyEventLog::LogSafetyTrigger(const std::string& source, const std::string& message,
                                       bool critical, ErrorCode code) {
  LogEvent(
    critical ? SafetySeverity::CRITICAL : SafetySeverity::WARNING,
    source,
    critical ? "SAFETY_CRITICAL_TRIGGER" : "SAFETY_TRIGGER",
    message,
    code
  );
}

void SafetyEventLog::LogSequenceStart(const std::string& sequenceName) {
  LogEvent(SafetySeverity::INFO, "SequenceEngine", "SEQUENCE_START",
           "Sequence started: " + sequenceName);
}

void SafetyEventLog::LogSequenceStop(const std::string& sequenceName, bool success,
                                      const std::string& reason) {
  std::string msg = "Sequence " + std::string(success ? "completed" : "stopped") +
                    ": " + sequenceName;
  if (!reason.empty()) {
    msg += " (" + reason + ")";
  }
  LogEvent(
    success ? SafetySeverity::INFO : SafetySeverity::WARNING,
    "SequenceEngine",
    success ? "SEQUENCE_COMPLETE" : "SEQUENCE_STOPPED",
    msg,
    success ? ErrorCode::Success : ErrorCode::SequenceStoppedByUser
  );
}

void SafetyEventLog::LogSequenceError(const std::string& sequenceName, const std::string& error,
                                       ErrorCode code) {
  LogEvent(SafetySeverity::WARNING, "SequenceEngine", "SEQUENCE_ERROR",
           "Sequence error in " + sequenceName + ": " + error, code);
}

void SafetyEventLog::LogEvent(SafetySeverity severity, const std::string& source,
                               const std::string& eventType, const std::string& message,
                               ErrorCode code) {
  std::lock_guard<std::mutex> lock(m_mutex);

  SafetyEvent event;
  event.id = m_nextId++;
  event.timestamp = std::chrono::system_clock::now();
  event.severity = severity;
  event.source = source;
  event.eventType = eventType;
  event.errorCode = code;
  event.message = message;
  event.acknowledged = false;

  // Ring buffer: remove oldest if at capacity
  if (m_events.size() >= MAX_EVENTS) {
    m_events.erase(m_events.begin());
  }
  m_events.push_back(event);

  WriteToFile(event);
}

std::vector<SafetyEvent> SafetyEventLog::GetRecentEvents(size_t count) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (count >= m_events.size()) {
    return m_events;
  }
  return std::vector<SafetyEvent>(m_events.end() - static_cast<ptrdiff_t>(count), m_events.end());
}

std::vector<SafetyEvent> SafetyEventLog::GetEventsSince(
    std::chrono::system_clock::time_point since) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::vector<SafetyEvent> result;
  for (const auto& event : m_events) {
    if (event.timestamp >= since) {
      result.push_back(event);
    }
  }
  return result;
}

void SafetyEventLog::AcknowledgeEvent(uint64_t eventId) {
  std::lock_guard<std::mutex> lock(m_mutex);

  for (auto& event : m_events) {
    if (event.id == eventId) {
      event.acknowledged = true;
      break;
    }
  }
}

std::string SafetyEvent::ToJsonLine() const {
  json j;
  j["id"] = id;
  j["timestamp"] = TimePointToISO8601(timestamp);
  j["severity"] = SeverityToString(severity);
  j["source"] = source;
  j["eventType"] = eventType;
  j["errorCode"] = ErrorCodeToString(errorCode);
  j["errorCodeHex"] = static_cast<uint16_t>(errorCode);
  j["message"] = message;
  j["acknowledged"] = acknowledged;
  return j.dump();
}

void SafetyEventLog::WriteToFile(const SafetyEvent& event) {
  RotateIfNeeded();
  if (m_fileStream.is_open()) {
    m_fileStream << event.ToJsonLine() << '\n';
    m_fileStream.flush();
  }
}

void SafetyEventLog::RotateIfNeeded() {
  // m_mutex already held by caller (LogEvent holds the lock)
  if (!m_fileStream.is_open()) return;

  auto pos = m_fileStream.tellp();
  if (pos < 0 || static_cast<size_t>(pos) < MAX_FILE_SIZE_BYTES) return;

  m_fileStream.close();

  std::string backupPath = m_filePath + ".1";
  std::error_code ec;
  std::filesystem::remove(backupPath, ec);                  // Remove old backup
  std::filesystem::rename(m_filePath, backupPath, ec);      // Current -> .1

  m_fileStream.open(m_filePath, std::ios::app);             // Open fresh file
}
