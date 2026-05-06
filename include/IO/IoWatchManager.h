// IoWatchManager.h
#pragma once

#include <QObject>
#include <thread>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <map>

class EziIOManager;
class LogManager;

/**
 * @brief Manual-override IO watch layer.
 *
 * Background thread that polls configured input pins and acts on the bound
 * output pin per the binding's `action`:
 *   - TOGGLE: on a qualifying edge, flip the output (OFF -> ON, ON -> OFF).
 *   - FOLLOW: output mirrors input level — rising edge sets ON, falling
 *             edge sets OFF (momentary / hold-while-pressed). The `trigger`
 *             field is ignored in FOLLOW mode (both edges always fire).
 *
 * Per-binding debounce suppresses switch bounce. This is a pure background
 * override — it does not interact with the recipe system or the safety cascade.
 */
class IoWatchManager : public QObject {
  Q_OBJECT

public:
  enum class TriggerEdge {
    RISING,   // OFF -> ON
    FALLING,  // ON  -> OFF
    BOTH
  };

  enum class Action {
    TOGGLE,  // flip output on qualifying edge (legacy default)
    FOLLOW   // output mirrors input level (rising=ON, falling=OFF)
  };

  struct Binding {
    std::string name;
    std::string inputDevice;
    std::string inputPinName;
    std::string outputDevice;
    std::string outputPinName;
    TriggerEdge trigger;
    Action action;
    int debounceMs;
    bool enabled;
  };

  explicit IoWatchManager(QObject* parent = nullptr);
  ~IoWatchManager();

  void SetIOManager(EziIOManager* ioManager);
  void SetLogManager(LogManager* logManager);

  [[nodiscard]] bool LoadConfig(const std::string& configPath);

  // Stop watching, reload config from disk, and restart watching if enabled.
  // Intended for UI-driven hot reload after editing io_watch.local.json.
  [[nodiscard]] bool Reload(const std::string& configPath);

  bool IsEnabled() const { return m_enabled; }
  const std::vector<Binding>& GetBindings() const { return m_bindings; }

  [[nodiscard]] bool StartWatching(int intervalMs = -1);
  void StopWatching();
  bool IsWatching() const { return m_isRunning; }

signals:
  void bindingTriggered(QString name, QString outputDevice, QString outputPin, bool newState);
  void watchingStarted();
  void watchingStopped();
  void watchError(QString errorMessage);

private:
  EziIOManager* m_ioManager;
  LogManager* m_logManager;

  bool m_enabled;
  int m_pollingIntervalMs;
  std::vector<Binding> m_bindings;

  std::map<std::string, bool> m_lastInputStates;         // key: "device:pin"
  std::map<std::string, int64_t> m_lastTriggerTimeMs;    // key: binding name

  std::unique_ptr<std::thread> m_pollingThread;
  std::atomic<bool> m_stopRequested;
  std::atomic<bool> m_isRunning;
  mutable std::mutex m_mutex;

  int m_pollFailCount = 0;

  void PollingThreadFunction();
  bool ShouldTrigger(bool prevState, bool currState, TriggerEdge edge) const;
  void ExecuteToggle(const Binding& binding);
  void ExecuteSet(const Binding& binding, bool desiredState);
  std::string MakeInputKey(const std::string& deviceName, const std::string& pinName) const;
  void LogEvent(const std::string& message, bool isError = false);

  static TriggerEdge ParseTriggerEdge(const std::string& edgeStr);
  static std::string TriggerEdgeToString(TriggerEdge edge);
  static Action ParseAction(const std::string& actionStr);
  static std::string ActionToString(Action action);
};
