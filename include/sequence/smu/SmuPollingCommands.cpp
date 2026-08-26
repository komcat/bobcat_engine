// SmuPollingCommands.cpp — see header for description.
#include "SmuPollingCommands.h"

#include "ApplicationContext.h"
#include "Keithley6482Adapter.h"
#include "LogManager.h"
#include "SMU/SMUPollingManager.h"
#include "SMUManager.h"
#include "SequenceContext.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

std::optional<bool> SmuPollingCommands::TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const SmuCommandDeps& deps)
{
  LogManager* m_logger = deps.logger;
  auto& device = deps.device;

  // ============================================================================
  // START POLLING
  // ============================================================================
  if (methodName == "StartPolling") {
    // ✅ Get polling manager from context
    SMUPollingManager* pollingManager = deps.app->smuPollingManager;
    if (!pollingManager) {
      errorMessage = "SMU Polling Manager not available";
      return false;
    }

    // Extract parameters
    auto modeIt = params.find("mode");
    auto intervalIt = params.find("intervalMs");
    auto channelIt = params.find("channel");

    if (modeIt == params.end()) {
      errorMessage = "Missing required parameter: mode (ACCURATE/FAST/ULTRA_FAST)";
      return false;
    }

    std::string mode = modeIt->second;
    // 0 (or negative) = continuous: the poll thread free-runs and the
    // instrument's NPLC + GPIB latency set the publish rate. > 0 is a rate cap.
    int intervalMs = 100;  // Default 100ms
    int channel = 1;       // Default channel 1

    if (intervalIt != params.end()) {
      try {
        intervalMs = std::stoi(intervalIt->second);
      } catch (...) {
        errorMessage = "Invalid intervalMs parameter: " + intervalIt->second;
        return false;
      }
    }

    bool pollAllChannels = false;
    if (channelIt != params.end()) {
      if (channelIt->second == "all") {
        pollAllChannels = true;
      } else {
        try {
          channel = std::stoi(channelIt->second);
        } catch (...) {
          errorMessage = "Invalid channel parameter: " + channelIt->second;
          return false;
        }
      }
    }

    // Check connection status
    if (!device->IsConnected()) {
      errorMessage = "SMU device not connected: " + deviceName;
      return false;
    }

    // Try to cast to Keithley6482Adapter for mode configuration (optional)
    auto keithley6482 = std::dynamic_pointer_cast<Keithley6482Adapter>(device);
    if (keithley6482) {
      // Configure measurement mode for Keithley 6482
      Keithley6482Adapter::MeasurementMode measMode = Keithley6482Adapter::MeasurementMode::ACCURATE;
      if (mode == "FAST") {
        measMode = Keithley6482Adapter::MeasurementMode::FAST;
      } else if (mode == "ULTRA_FAST") {
        measMode = Keithley6482Adapter::MeasurementMode::ULTRA_FAST;
      } else if (mode != "ACCURATE") {
        errorMessage = "Invalid mode: " + mode + " (must be ACCURATE, FAST, or ULTRA_FAST)";
        return false;
      }

      if (!keithley6482->ConfigureMeasurementMode(measMode)) {
        errorMessage = "Failed to configure measurement mode";
        return false;
      }

      if (!pollAllChannels) {
        keithley6482->SelectChannel(channel);
      }
    }
    // For other SMU devices (like KeithleySMUAdapter), mode is ignored
    // and polling just uses the default measurement settings

    // Determine which channels to poll
    std::vector<int> channelsToPoll;
    if (pollAllChannels) {
      channelsToPoll = device->GetEnabledChannels();
      if (channelsToPoll.empty()) {
        errorMessage = "No enabled channels on device: " + deviceName;
        return false;
      }
    } else {
      channelsToPoll.push_back(channel);
    }

    if (m_logger) {
      std::string channelStr = pollAllChannels ? "all(" + std::to_string(channelsToPoll.size()) + ")" : std::to_string(channel);
      std::string rateStr = (intervalMs > 0)
        ? std::to_string(intervalMs) + "ms rate cap"
        : std::string("continuous");
      m_logger->Info(
        "Starting polling on " + deviceName + " (mode=" + mode +
        ", " + rateStr + ", channels=" + channelStr + ")",
        "SMUHandler"
      );
    }

    // Start polling on each channel
    for (int ch : channelsToPoll) {
      if (!pollingManager->StartPolling(deviceName, device, ch, intervalMs,
                                        SMUPollingManager::MeasurementType::CURRENT)) {
        errorMessage = "Failed to start polling on channel " + std::to_string(ch);
        return false;
      }
    }

    if (m_logger) {
      m_logger->Info("Polling started on " + deviceName + " (" + std::to_string(channelsToPoll.size()) + " channels)", "SMUHandler");
    }

    return true;
  }

  // ============================================================================
  // STOP POLLING
  // ============================================================================
  if (methodName == "StopPolling") {
    // ✅ Get polling manager from context
    SMUPollingManager* pollingManager = deps.app->smuPollingManager;
    if (!pollingManager) {
      errorMessage = "SMU Polling Manager not available";
      return false;
    }

    if (m_logger) {
      m_logger->Info("Stopping polling on " + deviceName, "SMUHandler");
    }

    // ✅ Get total sample count BEFORE stopping (sum of all channels)
    uint64_t totalSamples = 0;
    if (context) {
      auto pollingChannels = pollingManager->GetPollingChannels(deviceName);
      for (int ch : pollingChannels) {
        totalSamples += pollingManager->GetSampleCount(deviceName, ch);
      }
    }

    // Stop polling on all channels for this device
    pollingManager->StopPolling(deviceName);

    // Store total sample count in context
    if (context) {
      context->SetValue(deviceName + ".sampleCount", static_cast<float>(totalSamples),
                       deviceName, methodName);
    }

    if (m_logger) {
      m_logger->Info("Stopped polling on " + deviceName + " (total samples: " + std::to_string(totalSamples) + ")", "SMUHandler");
    }

    return true;
  }

  return std::nullopt;  // not our family
}
