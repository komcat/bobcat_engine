// SmuOutputMeasureCommands.cpp — see header for description.
#include "SmuOutputMeasureCommands.h"

#include "SmuChannelSelect.h"

#include "LogManager.h"
#include "SMUManager.h"
#include "SequenceContext.h"

#include <string>

std::optional<bool> SmuOutputMeasureCommands::TryExecute(
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
  // READ CURRENT (single measurement)
  // ============================================================================
  if (methodName == "ReadCurrent") {
    auto channelIt = params.find("channel");
    auto captureAsIt = params.find("captureAs");

    int channel = 1;  // Default channel 1
    if (channelIt != params.end()) {
      try {
        channel = std::stoi(channelIt->second);
      } catch (...) {
        errorMessage = "Invalid channel parameter: " + channelIt->second;
        return false;
      }
    }

    if (m_logger) {
      m_logger->Info("Reading current from " + deviceName + " channel " + std::to_string(channel),
                    "SMUHandler");
    }

    double current = device->ReadChannel(channel);
    if (current == -999.0) {
      errorMessage = "Failed to read current: " + device->GetLastError();
      return false;
    }

    // Store in context
    if (context) {
      std::string key = deviceName + ".current_ch" + std::to_string(channel);
      context->SetValue(key, static_cast<float>(current), deviceName, methodName);

      // If captureAs provided, also store with that name
      if (captureAsIt != params.end()) {
        context->SetValue(captureAsIt->second, static_cast<float>(current), deviceName, methodName);
      }
    }

    return true;
  }

  // ============================================================================
  // READ VOLTAGE (single measurement)
  // ============================================================================
  if (methodName == "ReadVoltage") {
    auto channelIt = params.find("channel");
    auto captureAsIt = params.find("captureAs");

    int channel = 1;  // Default channel 1
    if (channelIt != params.end()) {
      try {
        channel = std::stoi(channelIt->second);
      } catch (...) {
        errorMessage = "Invalid channel parameter: " + channelIt->second;
        return false;
      }
    }

    SmuHelpers::SelectChannelIfSupported(device, channel);

    if (m_logger) {
      m_logger->Info("Reading voltage from " + deviceName + " channel " + std::to_string(channel),
                    "SMUHandler");
    }

    double voltage = device->ReadVoltage();
    if (voltage == -999.0) {
      errorMessage = "Failed to read voltage: " + device->GetLastError();
      return false;
    }

    // Store in context
    if (context) {
      std::string key = deviceName + ".voltage_ch" + std::to_string(channel);
      context->SetValue(key, static_cast<float>(voltage), deviceName, methodName);

      // If captureAs provided, also store with that name
      if (captureAsIt != params.end()) {
        context->SetValue(captureAsIt->second, static_cast<float>(voltage), deviceName, methodName);
      }
    }

    return true;
  }

  // ============================================================================
  // OUTPUT ON
  // ============================================================================
  if (methodName == "OutputOn") {
    auto channelIt = params.find("channel");

    int channel = 1;
    if (channelIt != params.end()) {
      try {
        channel = std::stoi(channelIt->second);
      } catch (...) {
        errorMessage = "Invalid channel parameter: " + channelIt->second;
        return false;
      }
    }

    SmuHelpers::SelectChannelIfSupported(device, channel);

    if (m_logger) {
      m_logger->Info("Turning output ON for " + deviceName + " channel " + std::to_string(channel),
                    "SMUHandler");
    }

    if (!device->SetOutputOn()) {
      errorMessage = "Failed to turn output on: " + device->GetLastError();
      return false;
    }

    return true;
  }

  // ============================================================================
  // OUTPUT OFF
  // ============================================================================
  if (methodName == "OutputOff") {
    auto channelIt = params.find("channel");

    int channel = 1;
    if (channelIt != params.end()) {
      try {
        channel = std::stoi(channelIt->second);
      } catch (...) {
        errorMessage = "Invalid channel parameter: " + channelIt->second;
        return false;
      }
    }

    SmuHelpers::SelectChannelIfSupported(device, channel);

    if (m_logger) {
      m_logger->Info("Turning output OFF for " + deviceName + " channel " + std::to_string(channel),
                    "SMUHandler");
    }

    if (!device->SetOutputOff()) {
      errorMessage = "Failed to turn output off: " + device->GetLastError();
      return false;
    }

    return true;
  }

  return std::nullopt;  // not our family
}
