// IoWatchManager.cpp
#include "IoWatchManager.h"
#include "EziIOManager.h"
#include "../logger/LogManager.h"
#include "common/ThreadPriority.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <chrono>

using json = nlohmann::json;

namespace {
int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
}
}  // namespace

IoWatchManager::IoWatchManager(QObject* parent)
  : QObject(parent)
  , m_ioManager(nullptr)
  , m_logManager(nullptr)
  , m_enabled(false)
  , m_pollingIntervalMs(50)
  , m_stopRequested(false)
  , m_isRunning(false)
{
  std::cout << "[IoWatchManager] Created" << std::endl;
}

IoWatchManager::~IoWatchManager() {
  std::cout << "[IoWatchManager] Destroying - stopping watching" << std::endl;
  StopWatching();
}

void IoWatchManager::SetIOManager(EziIOManager* ioManager) {
  m_ioManager = ioManager;
}

void IoWatchManager::SetLogManager(LogManager* logManager) {
  m_logManager = logManager;
}

bool IoWatchManager::LoadConfig(const std::string& configPath) {
  std::ifstream file(configPath);
  if (!file.is_open()) {
    std::cerr << "[IoWatchManager] Failed to open config: " << configPath << std::endl;
    LogEvent("Failed to load IO watch config: " + configPath, true);
    return false;
  }

  try {
    json config = json::parse(file);

    m_enabled = config.value("enabled", true);
    m_pollingIntervalMs = config.value("polling_interval_ms", 50);

    m_bindings.clear();

    if (config.contains("bindings") && config["bindings"].is_array()) {
      for (const auto& bJson : config["bindings"]) {
        Binding b;
        b.name = bJson.value("name", "");

        if (bJson.contains("input") && bJson["input"].is_object()) {
          b.inputDevice = bJson["input"].value("device", "");
          b.inputPinName = bJson["input"].value("pin_name", "");
        }
        if (bJson.contains("output") && bJson["output"].is_object()) {
          b.outputDevice = bJson["output"].value("device", "");
          b.outputPinName = bJson["output"].value("pin_name", "");
        }

        b.trigger = ParseTriggerEdge(bJson.value("trigger", "rising"));
        b.debounceMs = bJson.value("debounce_ms", 100);
        b.enabled = bJson.value("enabled", true);

        if (b.name.empty()) {
          b.name = b.inputDevice + ":" + b.inputPinName + " -> " +
                   b.outputDevice + ":" + b.outputPinName;
        }

        if (b.inputDevice.empty() || b.inputPinName.empty() ||
            b.outputDevice.empty() || b.outputPinName.empty()) {
          std::cerr << "[IoWatchManager] Skipping binding '" << b.name
                    << "' - missing input/output device or pin name" << std::endl;
          continue;
        }

        m_bindings.push_back(b);
        LogEvent("Loaded binding: " + b.name + " (trigger=" + TriggerEdgeToString(b.trigger)
                 + ", debounce=" + std::to_string(b.debounceMs) + "ms"
                 + ", enabled=" + (b.enabled ? "yes" : "no") + ")");
      }
    }

    LogEvent("Config loaded: " + std::to_string(m_bindings.size()) + " bindings"
             + ", interval=" + std::to_string(m_pollingIntervalMs) + "ms"
             + ", enabled=" + (m_enabled ? "yes" : "no"));
    return true;

  } catch (const json::exception& e) {
    std::cerr << "[IoWatchManager] JSON parse error: " << e.what() << std::endl;
    LogEvent("IO watch config parse error: " + std::string(e.what()), true);
    return false;
  }
}

bool IoWatchManager::Reload(const std::string& configPath) {
  LogEvent("Reload requested: " + configPath);

  StopWatching();

  if (!LoadConfig(configPath)) {
    LogEvent("Reload failed: LoadConfig returned false", true);
    return false;
  }

  if (!m_enabled) {
    LogEvent("Reload: config loaded but disabled - not starting watch");
    return true;
  }

  if (m_bindings.empty()) {
    LogEvent("Reload: config loaded with no bindings - not starting watch");
    return true;
  }

  return StartWatching();
}

bool IoWatchManager::StartWatching(int intervalMs) {
  if (!m_enabled) {
    LogEvent("Disabled in config");
    return false;
  }

  if (!m_ioManager) {
    LogEvent("No IO manager set", true);
    emit watchError("No IO manager configured");
    return false;
  }

  if (m_isRunning) {
    LogEvent("Already watching");
    return true;
  }

  if (m_bindings.empty()) {
    LogEvent("No bindings configured - skipping start");
    return false;
  }

  if (intervalMs > 0) {
    m_pollingIntervalMs = intervalMs;
  }

  // Seed last-known input states so the first poll doesn't fire on already-high inputs
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lastInputStates.clear();
    m_lastTriggerTimeMs.clear();

    for (const auto& b : m_bindings) {
      if (!b.enabled) continue;

      bool state = false;
      if (m_ioManager->GetInputByName(b.inputDevice, b.inputPinName, state)) {
        m_lastInputStates[MakeInputKey(b.inputDevice, b.inputPinName)] = state;
        LogEvent("Initial input " + b.inputDevice + ":" + b.inputPinName +
                 " = " + (state ? "ON" : "OFF"));
      } else {
        LogEvent("Failed to read initial state for " + b.inputDevice + ":" + b.inputPinName, true);
      }
    }
  }

  m_stopRequested = false;
  m_pollingThread = std::make_unique<std::thread>(&IoWatchManager::PollingThreadFunction, this);

  LogEvent("Started watching (interval=" + std::to_string(m_pollingIntervalMs) +
           "ms, " + std::to_string(m_bindings.size()) + " bindings)");

  emit watchingStarted();
  return true;
}

void IoWatchManager::StopWatching() {
  if (!m_isRunning && !m_pollingThread) {
    return;
  }

  LogEvent("Stopping watch...");

  m_stopRequested = true;

  if (m_pollingThread && m_pollingThread->joinable()) {
    m_pollingThread->join();
  }
  m_pollingThread.reset();

  LogEvent("Watch stopped");

  emit watchingStopped();
}

void IoWatchManager::PollingThreadFunction() {
  m_isRunning = true;
  ThreadUtil::SetCurrentThreadPriority(ThreadUtil::Priority::AboveNormal, "IoWatch");
  LogEvent("Polling thread started - " + std::to_string(m_bindings.size()) + " bindings");

  auto nextPollTime = std::chrono::steady_clock::now();

  while (!m_stopRequested) {
    for (const auto& b : m_bindings) {
      if (!b.enabled) continue;
      if (m_stopRequested) break;

      bool currentState = false;
      if (!m_ioManager->GetInputByName(b.inputDevice, b.inputPinName, currentState)) {
        if (m_pollFailCount++ % 50 == 0) {
          LogEvent("Failed to read " + b.inputDevice + ":" + b.inputPinName, true);
          emit watchError(QString::fromStdString(
            "Failed to read " + b.inputDevice + ":" + b.inputPinName));
        }
        continue;
      }

      const std::string key = MakeInputKey(b.inputDevice, b.inputPinName);
      bool prevState = false;

      {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_lastInputStates.find(key);
        if (it != m_lastInputStates.end()) {
          prevState = it->second;
        }
        m_lastInputStates[key] = currentState;
      }

      if (currentState == prevState) continue;

      if (!ShouldTrigger(prevState, currentState, b.trigger)) continue;

      // Debounce: require elapsed time since last trigger exceeds window
      const int64_t now = NowMs();
      int64_t lastTrigger = 0;
      {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_lastTriggerTimeMs.find(b.name);
        if (it != m_lastTriggerTimeMs.end()) {
          lastTrigger = it->second;
        }
      }

      if (lastTrigger > 0 && (now - lastTrigger) < b.debounceMs) {
        LogEvent("Debounced '" + b.name + "' (" + std::to_string(now - lastTrigger) +
                 "ms since last, debounce=" + std::to_string(b.debounceMs) + "ms)");
        continue;
      }

      ExecuteToggle(b);

      {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastTriggerTimeMs[b.name] = now;
      }
    }

    nextPollTime += std::chrono::milliseconds(m_pollingIntervalMs);
    std::this_thread::sleep_until(nextPollTime);
  }

  m_isRunning = false;
  LogEvent("Polling thread stopped");
}

void IoWatchManager::ExecuteToggle(const Binding& b) {
  bool currentOut = false;
  if (!m_ioManager->GetOutputByName(b.outputDevice, b.outputPinName, currentOut)) {
    const std::string msg = "'" + b.name + "': failed to read output " +
                            b.outputDevice + ":" + b.outputPinName;
    LogEvent(msg, true);
    emit watchError(QString::fromStdString(msg));
    return;
  }

  const bool newState = !currentOut;
  const EziIOError setErr = m_ioManager->SetOutputByName(b.outputDevice, b.outputPinName, newState);
  if (setErr != EziIOError::SUCCESS) {
    const std::string msg = "'" + b.name + "': failed to set output " +
                            b.outputDevice + ":" + b.outputPinName +
                            " -> " + (newState ? "ON" : "OFF");
    LogEvent(msg, true);
    emit watchError(QString::fromStdString(msg));
    return;
  }

  LogEvent("TOGGLE '" + b.name + "': " + b.outputDevice + ":" + b.outputPinName +
           " -> " + (newState ? "ON" : "OFF"));

  emit bindingTriggered(
    QString::fromStdString(b.name),
    QString::fromStdString(b.outputDevice),
    QString::fromStdString(b.outputPinName),
    newState
  );
}

bool IoWatchManager::ShouldTrigger(bool prevState, bool currState, TriggerEdge edge) const {
  if (prevState == currState) return false;
  switch (edge) {
    case TriggerEdge::RISING:  return !prevState && currState;
    case TriggerEdge::FALLING: return prevState && !currState;
    case TriggerEdge::BOTH:    return true;
    default:                   return false;
  }
}

std::string IoWatchManager::MakeInputKey(const std::string& deviceName, const std::string& pinName) const {
  return deviceName + ":" + pinName;
}

void IoWatchManager::LogEvent(const std::string& message, bool isError) {
  if (!m_logManager) return;
  if (isError) {
    m_logManager->Error(message, "IoWatch");
  } else {
    m_logManager->Info(message, "IoWatch");
  }
}

IoWatchManager::TriggerEdge IoWatchManager::ParseTriggerEdge(const std::string& edgeStr) {
  if (edgeStr == "falling" || edgeStr == "FALLING") return TriggerEdge::FALLING;
  if (edgeStr == "both"    || edgeStr == "BOTH")    return TriggerEdge::BOTH;
  return TriggerEdge::RISING;
}

std::string IoWatchManager::TriggerEdgeToString(TriggerEdge edge) {
  switch (edge) {
    case TriggerEdge::RISING:  return "rising";
    case TriggerEdge::FALLING: return "falling";
    case TriggerEdge::BOTH:    return "both";
    default:                   return "rising";
  }
}
