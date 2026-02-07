// SafetyWatchManager.h
#pragma once

#include <QObject>
#include <QTimer>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>
#include <map>

class EziIOManager;
class LogManager;
class SafetyEventLog;

/**
 * @brief Background safety monitoring layer
 *
 * Continuously polls configured IO inputs and detects state changes.
 * When a safety condition is triggered (rising/falling edge), raises
 * a safety flag, emits signals for UI notification and worker stopping.
 *
 * Architecture:
 *   - Uses std::thread for background polling (like SMUPollingManager)
 *   - Thread-safe interface with atomic flags
 *   - Manual acknowledgment required to clear flags
 *   - Configurable trigger edges per input
 *
 * Usage:
 *   SafetyWatchManager* safety = new SafetyWatchManager();
 *   safety->SetIOManager(ioManager);
 *   safety->SetLogManager(logManager);
 *   safety->LoadConfig("config/safety_watch.json");
 *   safety->StartWatching();
 *
 *   // Connect to signals for UI and worker integration
 *   connect(safety, &SafetyWatchManager::safetyTriggered, ...);
 */
class SafetyWatchManager : public QObject
{
  Q_OBJECT

public:
  /**
   * @brief Edge detection trigger types
   */
  enum class TriggerEdge {
    RISING,   // Trigger on off->on transition
    FALLING,  // Trigger on on->off transition
    BOTH      // Trigger on any state change
  };

  /**
   * @brief Configuration for a watched input pin
   */
  struct WatchedInput {
    std::string deviceName;     // IO device name (e.g., "IOTop")
    std::string pinName;        // Pin name (e.g., "E_Stop")
    int pinNumber;              // Pin number (for reference)
    TriggerEdge trigger;        // When to trigger
    std::string alertTitle;     // Popup title
    std::string alertMessage;   // Popup message
    bool critical;              // If true, auto-stops workers
    bool enabled;               // If false, skip this input
  };

  explicit SafetyWatchManager(QObject* parent = nullptr);
  ~SafetyWatchManager();

  // ========== Dependencies ==========

  /**
   * @brief Set the IO manager for reading input states
   * @param ioManager Pointer to initialized EziIOManager
   */
  void SetIOManager(EziIOManager* ioManager);

  /**
   * @brief Set the log manager for event logging
   * @param logManager Pointer to LogManager
   */
  void SetLogManager(LogManager* logManager);

  /**
   * @brief Set the safety event log for structured audit trail
   * @param eventLog Pointer to SafetyEventLog
   */
  void SetSafetyEventLog(SafetyEventLog* eventLog);

  // ========== Configuration ==========

  /**
   * @brief Load safety watch configuration from JSON file
   * @param configPath Path to safety_watch.json
   * @return true if loaded successfully
   */
  [[nodiscard]] bool LoadConfig(const std::string& configPath);

  /**
   * @brief Save current configuration to JSON file
   * @param configPath Path to save to
   * @return true if saved successfully
   */
  bool SaveConfig(const std::string& configPath);

  /**
   * @brief Check if safety watch is enabled in config
   */
  bool IsEnabled() const { return m_enabled; }

  /**
   * @brief Get all watched inputs
   */
  const std::vector<WatchedInput>& GetWatchedInputs() const { return m_watchedInputs; }

  // ========== Watching Control ==========

  /**
   * @brief Start the background polling thread
   * @param intervalMs Polling interval in milliseconds (default from config)
   * @return true if started successfully
   */
  [[nodiscard]] bool StartWatching(int intervalMs = -1);

  /**
   * @brief Stop the polling thread
   */
  void StopWatching();

  /**
   * @brief Check if watching is currently active
   */
  bool IsWatching() const { return m_isRunning; }

  // ========== Safety Flag Management ==========

  /**
   * @brief Check if any safety flag is currently raised
   */
  bool IsSafetyFlagRaised() const { return m_safetyFlagRaised; }

  /**
   * @brief Check if a critical safety flag is raised
   */
  bool IsCriticalFlagRaised() const { return m_criticalFlagRaised; }

  /**
   * @brief Acknowledge and clear the safety flag
   * Called when user dismisses the alert dialog
   */
  void AcknowledgeSafetyFlag();

  /**
   * @brief Get the current active alert info
   */
  std::string GetActiveAlertTitle() const;
  std::string GetActiveAlertMessage() const;
  bool IsActiveAlertCritical() const;

  /**
   * @brief Store/retrieve E-Stop cascade result summary
   * Set by Application::performEStopCascade(), read by MainWindow for dialog display
   */
  void SetEStopSummary(const std::string& summary);
  std::string GetEStopSummary() const;

signals:
  /**
   * @brief Emitted when a safety condition is triggered
   * @param title Alert title
   * @param message Alert message
   * @param critical True if this is a critical alert (should stop workers)
   */
  void safetyTriggered(QString title, QString message, bool critical);

  /**
   * @brief Emitted when safety flag is acknowledged/cleared
   */
  void safetyCleared();

  /**
   * @brief Emitted when watching starts
   */
  void watchingStarted();

  /**
   * @brief Emitted when watching stops
   */
  void watchingStopped();

  /**
   * @brief Emitted when any watched input changes state
   * @param deviceName Device name
   * @param pinName Pin name
   * @param newState New state (true = on)
   */
  void inputStateChanged(QString deviceName, QString pinName, bool newState);

  /**
   * @brief Emitted when an error occurs during watching
   * @param errorMessage Description of the error
   */
  void watchError(QString errorMessage);

  /**
   * @brief Emitted when the polling thread appears stalled (no heartbeat)
   * @param message Description of the stall condition
   */
  void pollingThreadStalled(QString message);

private:
  // Dependencies
  EziIOManager* m_ioManager;
  LogManager* m_logManager;
  SafetyEventLog* m_safetyEventLog;

  // Configuration
  bool m_enabled;
  int m_pollingIntervalMs;
  std::vector<WatchedInput> m_watchedInputs;

  // State tracking
  std::map<std::string, bool> m_lastInputStates;  // Key: "deviceName:pinName"

  // Active alert info
  std::string m_activeAlertTitle;
  std::string m_activeAlertMessage;
  bool m_activeAlertCritical;

  // E-Stop cascade result summary
  std::string m_eStopSummary;

  // IO read failure tracking (moved from static to member for thread safety)
  int m_pollFailCount = 0;

  // Thread management
  std::unique_ptr<std::thread> m_pollingThread;
  std::atomic<bool> m_stopRequested;
  std::atomic<bool> m_isRunning;
  std::atomic<bool> m_safetyFlagRaised;
  std::atomic<bool> m_criticalFlagRaised;
  mutable std::mutex m_mutex;

  // Health monitor: heartbeat from polling thread, checked by QTimer on Qt event loop
  std::atomic<int64_t> m_lastPollTickMs{0};
  QTimer* m_healthTimer = nullptr;

  // Private methods
  void CheckPollingHealth();
  void PollingThreadFunction();
  bool ShouldTrigger(bool prevState, bool currState, TriggerEdge edge) const;
  void RaiseSafetyFlag(const WatchedInput& input, bool prevState, bool currState);
  std::string MakeInputKey(const std::string& deviceName, const std::string& pinName) const;
  void LogEvent(const std::string& message, bool isError = false);

  // JSON helpers
  static TriggerEdge ParseTriggerEdge(const std::string& edgeStr);
  static std::string TriggerEdgeToString(TriggerEdge edge);
};
