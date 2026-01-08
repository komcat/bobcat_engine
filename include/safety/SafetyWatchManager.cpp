// SafetyWatchManager.cpp
#include "SafetyWatchManager.h"
#include "../IO/EziIOManager.h"
#include "../logger/LogManager.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <chrono>

using json = nlohmann::json;

SafetyWatchManager::SafetyWatchManager(QObject* parent)
  : QObject(parent)
  , m_ioManager(nullptr)
  , m_logManager(nullptr)
  , m_enabled(false)
  , m_pollingIntervalMs(100)
  , m_activeAlertCritical(false)
  , m_stopRequested(false)
  , m_isRunning(false)
  , m_safetyFlagRaised(false)
  , m_criticalFlagRaised(false)
{
  std::cout << "[SafetyWatchManager] Created" << std::endl;
}

SafetyWatchManager::~SafetyWatchManager()
{
  std::cout << "[SafetyWatchManager] Destroying - stopping watching" << std::endl;
  StopWatching();
}

void SafetyWatchManager::SetIOManager(EziIOManager* ioManager)
{
  m_ioManager = ioManager;
}

void SafetyWatchManager::SetLogManager(LogManager* logManager)
{
  m_logManager = logManager;
}

bool SafetyWatchManager::LoadConfig(const std::string& configPath)
{
  std::ifstream file(configPath);
  if (!file.is_open()) {
    std::cerr << "[SafetyWatchManager] Failed to open config: " << configPath << std::endl;
    LogEvent("Failed to load safety watch config: " + configPath, true);
    return false;
  }

  try {
    json config = json::parse(file);

    m_enabled = config.value("enabled", true);
    m_pollingIntervalMs = config.value("polling_interval_ms", 100);

    m_watchedInputs.clear();

    if (config.contains("watched_inputs") && config["watched_inputs"].is_array()) {
      for (const auto& inputJson : config["watched_inputs"]) {
        WatchedInput input;
        input.deviceName = inputJson.value("device", "");
        input.pinName = inputJson.value("pin_name", "");
        input.pinNumber = inputJson.value("pin_number", 0);
        input.trigger = ParseTriggerEdge(inputJson.value("trigger", "rising"));
        input.alertTitle = inputJson.value("alert_title", "Safety Alert");
        input.alertMessage = inputJson.value("alert_message", "A safety condition was triggered.");
        input.critical = inputJson.value("critical", false);
        input.enabled = inputJson.value("enabled", true);

        if (!input.deviceName.empty() && !input.pinName.empty()) {
          m_watchedInputs.push_back(input);
          std::cout << "[SafetyWatchManager] Loaded watch: " << input.deviceName
                    << ":" << input.pinName << " (trigger=" << TriggerEdgeToString(input.trigger)
                    << ", critical=" << (input.critical ? "yes" : "no") << ")" << std::endl;
        }
      }
    }

    std::cout << "[SafetyWatchManager] Config loaded: " << m_watchedInputs.size()
              << " watched inputs, interval=" << m_pollingIntervalMs << "ms"
              << ", enabled=" << (m_enabled ? "yes" : "no") << std::endl;

    LogEvent("Safety watch config loaded: " + std::to_string(m_watchedInputs.size()) + " inputs");
    return true;

  } catch (const json::exception& e) {
    std::cerr << "[SafetyWatchManager] JSON parse error: " << e.what() << std::endl;
    LogEvent("Safety watch config parse error: " + std::string(e.what()), true);
    return false;
  }
}

bool SafetyWatchManager::SaveConfig(const std::string& configPath)
{
  try {
    json config;
    config["enabled"] = m_enabled;
    config["polling_interval_ms"] = m_pollingIntervalMs;

    json inputsArray = json::array();
    for (const auto& input : m_watchedInputs) {
      json inputJson;
      inputJson["device"] = input.deviceName;
      inputJson["pin_name"] = input.pinName;
      inputJson["pin_number"] = input.pinNumber;
      inputJson["trigger"] = TriggerEdgeToString(input.trigger);
      inputJson["alert_title"] = input.alertTitle;
      inputJson["alert_message"] = input.alertMessage;
      inputJson["critical"] = input.critical;
      inputJson["enabled"] = input.enabled;
      inputsArray.push_back(inputJson);
    }
    config["watched_inputs"] = inputsArray;

    std::ofstream file(configPath);
    if (!file.is_open()) {
      std::cerr << "[SafetyWatchManager] Failed to save config: " << configPath << std::endl;
      return false;
    }

    file << config.dump(2);
    return true;

  } catch (const std::exception& e) {
    std::cerr << "[SafetyWatchManager] Save error: " << e.what() << std::endl;
    return false;
  }
}

bool SafetyWatchManager::StartWatching(int intervalMs)
{
  if (!m_enabled) {
    std::cout << "[SafetyWatchManager] Safety watch is disabled in config" << std::endl;
    return false;
  }

  if (!m_ioManager) {
    std::cerr << "[SafetyWatchManager] No IO manager set" << std::endl;
    emit watchError("No IO manager configured");
    return false;
  }

  if (m_isRunning) {
    std::cout << "[SafetyWatchManager] Already watching" << std::endl;
    return true;
  }

  if (intervalMs > 0) {
    m_pollingIntervalMs = intervalMs;
  }

  // Initialize last known states for all inputs
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lastInputStates.clear();

    for (const auto& input : m_watchedInputs) {
      if (!input.enabled) continue;

      bool state = false;
      if (m_ioManager->GetInputByName(input.deviceName, input.pinName, state)) {
        std::string key = MakeInputKey(input.deviceName, input.pinName);
        m_lastInputStates[key] = state;
        std::cout << "[SafetyWatchManager] Initial state " << key << " = " << (state ? "ON" : "OFF") << std::endl;
      } else {
        std::cerr << "[SafetyWatchManager] Failed to read initial state for "
                  << input.deviceName << ":" << input.pinName << std::endl;
      }
    }
  }

  m_stopRequested = false;
  m_pollingThread = std::make_unique<std::thread>(&SafetyWatchManager::PollingThreadFunction, this);

  std::cout << "[SafetyWatchManager] Started watching (interval=" << m_pollingIntervalMs << "ms)" << std::endl;
  LogEvent("Safety watch started");

  emit watchingStarted();
  return true;
}

void SafetyWatchManager::StopWatching()
{
  if (!m_isRunning && !m_pollingThread) {
    return;
  }

  std::cout << "[SafetyWatchManager] Stopping watch..." << std::endl;
  m_stopRequested = true;

  if (m_pollingThread && m_pollingThread->joinable()) {
    m_pollingThread->join();
  }
  m_pollingThread.reset();

  std::cout << "[SafetyWatchManager] Watch stopped" << std::endl;
  LogEvent("Safety watch stopped");

  emit watchingStopped();
}

void SafetyWatchManager::AcknowledgeSafetyFlag()
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_safetyFlagRaised) {
    return;
  }

  std::cout << "[SafetyWatchManager] Safety flag acknowledged" << std::endl;
  LogEvent("Safety alert acknowledged: " + m_activeAlertTitle);

  m_safetyFlagRaised = false;
  m_criticalFlagRaised = false;
  m_activeAlertTitle.clear();
  m_activeAlertMessage.clear();
  m_activeAlertCritical = false;

  emit safetyCleared();
}

std::string SafetyWatchManager::GetActiveAlertTitle() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_activeAlertTitle;
}

std::string SafetyWatchManager::GetActiveAlertMessage() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_activeAlertMessage;
}

bool SafetyWatchManager::IsActiveAlertCritical() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_activeAlertCritical;
}

void SafetyWatchManager::PollingThreadFunction()
{
  m_isRunning = true;
  std::cout << "[SafetyWatchManager] Polling thread started - watching "
            << m_watchedInputs.size() << " inputs" << std::endl;

  // Log which inputs we're watching
  for (const auto& input : m_watchedInputs) {
    if (input.enabled) {
      std::cout << "[SafetyWatchManager]   - " << input.deviceName << ":" << input.pinName
                << " (trigger=" << TriggerEdgeToString(input.trigger) << ")" << std::endl;
    }
  }

  auto nextPollTime = std::chrono::steady_clock::now();

  while (!m_stopRequested) {
    // Poll each watched input
    for (const auto& input : m_watchedInputs) {
      if (!input.enabled) continue;
      if (m_stopRequested) break;

      bool currentState = false;
      if (!m_ioManager->GetInputByName(input.deviceName, input.pinName, currentState)) {
        // Failed to read - emit error but continue
        // Only log occasionally to avoid spam (every 50 polls = 5 seconds at 100ms)
        static int failCount = 0;
        if (failCount++ % 50 == 0) {
          std::cerr << "[SafetyWatchManager] Failed to read " << input.deviceName
                    << ":" << input.pinName << " (check device connection and pin name)" << std::endl;
          emit watchError(QString::fromStdString(
            "Failed to read " + input.deviceName + ":" + input.pinName));
        }
        continue;
      }

      std::string key = MakeInputKey(input.deviceName, input.pinName);
      bool prevState = false;

      {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_lastInputStates.find(key);
        if (it != m_lastInputStates.end()) {
          prevState = it->second;
        }
      }

      // Check for state change
      if (currentState != prevState) {
        std::cout << "[SafetyWatchManager] State change detected: " << key
                  << " " << (prevState ? "ON" : "OFF") << " -> " << (currentState ? "ON" : "OFF")
                  << std::endl;

        // Emit state change signal
        emit inputStateChanged(
          QString::fromStdString(input.deviceName),
          QString::fromStdString(input.pinName),
          currentState
        );

        // Update stored state
        {
          std::lock_guard<std::mutex> lock(m_mutex);
          m_lastInputStates[key] = currentState;
        }

        // Check if this triggers a safety event
        bool shouldTrigger = ShouldTrigger(prevState, currentState, input.trigger);
        std::cout << "[SafetyWatchManager] Trigger check: edge=" << TriggerEdgeToString(input.trigger)
                  << ", shouldTrigger=" << (shouldTrigger ? "YES" : "NO") << std::endl;

        if (shouldTrigger) {
          RaiseSafetyFlag(input, prevState, currentState);
        }
      }
    }

    // Sleep until next poll time
    nextPollTime += std::chrono::milliseconds(m_pollingIntervalMs);
    std::this_thread::sleep_until(nextPollTime);
  }

  m_isRunning = false;
  std::cout << "[SafetyWatchManager] Polling thread stopped" << std::endl;
}

bool SafetyWatchManager::ShouldTrigger(bool prevState, bool currState, TriggerEdge edge) const
{
  if (prevState == currState) {
    return false;
  }

  switch (edge) {
    case TriggerEdge::RISING:
      return !prevState && currState;  // OFF -> ON
    case TriggerEdge::FALLING:
      return prevState && !currState;  // ON -> OFF
    case TriggerEdge::BOTH:
      return true;  // Any change
    default:
      return false;
  }
}

void SafetyWatchManager::RaiseSafetyFlag(const WatchedInput& input, bool prevState, bool currState)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  // Don't raise another flag if one is already active
  if (m_safetyFlagRaised) {
    std::cout << "[SafetyWatchManager] Safety flag already raised, ignoring new trigger" << std::endl;
    return;
  }

  m_safetyFlagRaised = true;
  m_criticalFlagRaised = input.critical;
  m_activeAlertTitle = input.alertTitle;
  m_activeAlertMessage = input.alertMessage;
  m_activeAlertCritical = input.critical;

  std::string stateChange = (prevState ? "ON" : "OFF") + std::string(" -> ") + (currState ? "ON" : "OFF");
  std::cout << "[SafetyWatchManager] SAFETY TRIGGERED: " << input.deviceName << ":" << input.pinName
            << " (" << stateChange << ")" << std::endl;
  std::cout << "[SafetyWatchManager]   Title: " << input.alertTitle << std::endl;
  std::cout << "[SafetyWatchManager]   Critical: " << (input.critical ? "YES" : "NO") << std::endl;

  // Log to application logger
  std::string logMsg = "SAFETY: " + input.alertTitle + " [" + input.deviceName + ":" + input.pinName + " " + stateChange + "]";
  if (input.critical) {
    LogEvent(logMsg + " (CRITICAL)", true);
  } else {
    LogEvent(logMsg);
  }

  // Emit signal (must be done without lock in some cases, but signals are thread-safe)
  emit safetyTriggered(
    QString::fromStdString(input.alertTitle),
    QString::fromStdString(input.alertMessage),
    input.critical
  );
}

std::string SafetyWatchManager::MakeInputKey(const std::string& deviceName, const std::string& pinName) const
{
  return deviceName + ":" + pinName;
}

void SafetyWatchManager::LogEvent(const std::string& message, bool isError)
{
  if (m_logManager) {
    if (isError) {
      m_logManager->Error(message, "SafetyWatch");
    } else {
      m_logManager->Info(message, "SafetyWatch");
    }
  }
}

SafetyWatchManager::TriggerEdge SafetyWatchManager::ParseTriggerEdge(const std::string& edgeStr)
{
  if (edgeStr == "falling" || edgeStr == "FALLING") {
    return TriggerEdge::FALLING;
  } else if (edgeStr == "both" || edgeStr == "BOTH") {
    return TriggerEdge::BOTH;
  }
  // Default to rising
  return TriggerEdge::RISING;
}

std::string SafetyWatchManager::TriggerEdgeToString(TriggerEdge edge)
{
  switch (edge) {
    case TriggerEdge::RISING:
      return "rising";
    case TriggerEdge::FALLING:
      return "falling";
    case TriggerEdge::BOTH:
      return "both";
    default:
      return "rising";
  }
}
