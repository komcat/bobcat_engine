// SafetyEventLog.h
// Structured safety event logging with audit trail.
#pragma once

#include "common/ErrorCode.h"
#include <chrono>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

enum class SafetySeverity : uint8_t { INFO, WARNING, CRITICAL, EMERGENCY };

struct SafetyEvent {
  uint64_t id = 0;
  std::chrono::system_clock::time_point timestamp;
  SafetySeverity severity = SafetySeverity::INFO;
  std::string source;
  std::string eventType;
  ErrorCode errorCode = ErrorCode::Success;
  std::string message;
  bool acknowledged = false;

  [[nodiscard]] std::string ToJsonLine() const;
};

class SafetyEventLog {
public:
  SafetyEventLog() = default;
  ~SafetyEventLog();

  SafetyEventLog(const SafetyEventLog&) = delete;
  SafetyEventLog& operator=(const SafetyEventLog&) = delete;

  [[nodiscard]] bool Initialize(const std::string& logDirectory = "logs");

  // Convenience methods
  void LogSafetyTrigger(const std::string& source, const std::string& message,
                        bool critical, ErrorCode code = ErrorCode::SafetyFlagRaised);
  void LogSequenceStart(const std::string& sequenceName);
  void LogSequenceStop(const std::string& sequenceName, bool success,
                       const std::string& reason = "");
  void LogSequenceError(const std::string& sequenceName, const std::string& error,
                        ErrorCode code = ErrorCode::SequenceExecutionFailed);

  // General event logging
  void LogEvent(SafetySeverity severity, const std::string& source,
                const std::string& eventType, const std::string& message,
                ErrorCode code = ErrorCode::Success);

  // Query
  [[nodiscard]] std::vector<SafetyEvent> GetRecentEvents(size_t count = 50) const;
  [[nodiscard]] std::vector<SafetyEvent> GetEventsSince(
    std::chrono::system_clock::time_point since) const;
  void AcknowledgeEvent(uint64_t eventId);

private:
  static constexpr size_t MAX_EVENTS = 1000;

  mutable std::mutex m_mutex;
  std::vector<SafetyEvent> m_events;
  uint64_t m_nextId = 1;
  std::ofstream m_fileStream;

  void WriteToFile(const SafetyEvent& event);
};
