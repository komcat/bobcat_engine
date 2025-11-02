// SMUCommandHandler.cpp
#include "SMUCommandHandler.h"
#include "SMUManager.h"
#include "Keithley6482Adapter.h"
#include "SMU/SMUPollingManager.h"  // ✅ ADD: Use centralized polling manager
#include "ApplicationContext.h"     // ✅ ADD: For ApplicationContext definition
#include "LogManager.h"
#include "SequenceContext.h"
#include <QMetaObject>  // For invokeMethod (thread-safe Qt calls)
#include <iostream>
#include <thread>       // For std::this_thread::sleep_for
#include <chrono>       // For std::chrono::milliseconds

bool SMUCommandHandler::ExecuteCommand(
  void* manager,
  const std::string& deviceName,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context)
{
  SMUManager* smuManager = static_cast<SMUManager*>(manager);
  if (!smuManager) {
    errorMessage = "SMUManager is null";
    return false;
  }

  // Get the device
  auto device = smuManager->GetDevice(deviceName);
  if (!device) {
    errorMessage = "SMU device not found: " + deviceName;
    return false;
  }

  // ============================================================================
  // START POLLING
  // ============================================================================
  if (methodName == "StartPolling") {
    // ✅ Get polling manager from context
    SMUPollingManager* pollingManager = m_context->smuPollingManager;
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

    if (channelIt != params.end()) {
      try {
        channel = std::stoi(channelIt->second);
      } catch (...) {
        errorMessage = "Invalid channel parameter: " + channelIt->second;
        return false;
      }
    }

    // Try to cast to Keithley6482Adapter for mode configuration
    auto keithley = std::dynamic_pointer_cast<Keithley6482Adapter>(device);
    if (!keithley) {
      errorMessage = "Device does not support polling: " + deviceName;
      return false;
    }

    // Check connection status
    if (!device->IsConnected()) {
      errorMessage = "SMU device not connected: " + deviceName;
      return false;
    }

    // Configure measurement mode
    Keithley6482Adapter::MeasurementMode measMode = Keithley6482Adapter::MeasurementMode::ACCURATE;
    if (mode == "FAST") {
      measMode = Keithley6482Adapter::MeasurementMode::FAST;
    } else if (mode == "ULTRA_FAST") {
      measMode = Keithley6482Adapter::MeasurementMode::ULTRA_FAST;
    } else if (mode != "ACCURATE") {
      errorMessage = "Invalid mode: " + mode + " (must be ACCURATE, FAST, or ULTRA_FAST)";
      return false;
    }

    if (!keithley->ConfigureMeasurementMode(measMode)) {
      errorMessage = "Failed to configure measurement mode";
      return false;
    }

    // Select channel
    keithley->SelectChannel(channel);

    if (m_logger) {
      m_logger->Info(
        "Starting polling on " + deviceName + " (mode=" + mode +
        ", interval=" + std::to_string(intervalMs) + "ms, channel=" + std::to_string(channel) + ")",
        "SMUHandler"
      );
    }

    // ✅ NEW: Use polling manager (thread-safe!)
    // Default to CURRENT measurement for backward compatibility
    // TODO: Add measurement type parameter to recipe if voltage polling is needed
    if (!pollingManager->StartPolling(deviceName, device, channel, intervalMs,
                                      SMUPollingManager::MeasurementType::CURRENT)) {
      errorMessage = "Failed to start polling";
      return false;
    }

    if (m_logger) {
      m_logger->Info("Polling started on " + deviceName, "SMUHandler");
    }

    return true;
  }

  // ============================================================================
  // STOP POLLING
  // ============================================================================
  else if (methodName == "StopPolling") {
    // ✅ Get polling manager from context
    SMUPollingManager* pollingManager = m_context->smuPollingManager;
    if (!pollingManager) {
      errorMessage = "SMU Polling Manager not available";
      return false;
    }

    if (m_logger) {
      m_logger->Info("Stopping polling on " + deviceName, "SMUHandler");
    }

    // ✅ NEW: Use polling manager
    pollingManager->StopPolling(deviceName);

    // Store sample count in context
    if (context) {
      uint64_t samples = pollingManager->GetSampleCount(deviceName);
      context->SetValue(deviceName + ".sampleCount", static_cast<float>(samples),
                       deviceName, methodName);
    }

    return true;
  }

  // ============================================================================
  // READ CURRENT (single measurement)
  // ============================================================================
  else if (methodName == "ReadCurrent") {
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
  else if (methodName == "ReadVoltage") {
    auto captureAsIt = params.find("captureAs");

    if (m_logger) {
      m_logger->Info("Reading voltage from " + deviceName, "SMUHandler");
    }

    double voltage = device->ReadVoltage();
    if (voltage == -999.0) {
      errorMessage = "Failed to read voltage: " + device->GetLastError();
      return false;
    }

    // Store in context
    if (context) {
      std::string key = deviceName + ".voltage";
      context->SetValue(key, static_cast<float>(voltage), deviceName, methodName);

      // If captureAs provided, also store with that name
      if (captureAsIt != params.end()) {
        context->SetValue(captureAsIt->second, static_cast<float>(voltage), deviceName, methodName);
      }
    }

    return true;
  }

  // ============================================================================
  // SET VOLTAGE (for voltage source devices)
  // ============================================================================
  else if (methodName == "SetVoltage") {
    auto voltageIt = params.find("voltage");

    if (voltageIt == params.end()) {
      errorMessage = "Missing required parameter: voltage";
      return false;
    }

    double voltage = 0.0;
    try {
      voltage = std::stod(voltageIt->second);
    } catch (...) {
      errorMessage = "Invalid voltage parameter: " + voltageIt->second;
      return false;
    }

    if (m_logger) {
      m_logger->Info("Setting voltage on " + deviceName + " to " + std::to_string(voltage) + " V",
                    "SMUHandler");
    }

    if (!device->SetVoltage(voltage)) {
      errorMessage = "Failed to set voltage: " + device->GetLastError();
      return false;
    }

    return true;
  }

  // ============================================================================
  // SET CURRENT (for current source devices)
  // ============================================================================
  else if (methodName == "SetCurrent") {
    auto currentIt = params.find("current");

    if (currentIt == params.end()) {
      errorMessage = "Missing required parameter: current";
      return false;
    }

    double current = 0.0;
    try {
      current = std::stod(currentIt->second);
    } catch (...) {
      errorMessage = "Invalid current parameter: " + currentIt->second;
      return false;
    }

    if (m_logger) {
      m_logger->Info("Setting current on " + deviceName + " to " + std::to_string(current) + " A",
                    "SMUHandler");
    }

    if (!device->SetCurrent(current)) {
      errorMessage = "Failed to set current: " + device->GetLastError();
      return false;
    }

    return true;
  }

  // ============================================================================
  // SET COMPLIANCE (compliance limit)
  // ============================================================================
  else if (methodName == "SetCompliance") {
    auto limitIt = params.find("limit");

    if (limitIt == params.end()) {
      errorMessage = "Missing required parameter: limit";
      return false;
    }

    double limit = 0.0;
    try {
      limit = std::stod(limitIt->second);
    } catch (...) {
      errorMessage = "Invalid limit parameter: " + limitIt->second;
      return false;
    }

    if (m_logger) {
      m_logger->Info("Setting compliance on " + deviceName + " to " + std::to_string(limit),
                    "SMUHandler");
    }

    if (!device->SetCompliance(limit)) {
      errorMessage = "Failed to set compliance: " + device->GetLastError();
      return false;
    }

    return true;
  }

  // ============================================================================
  // SET MODE (configure measurement mode without starting polling)
  // ============================================================================
  else if (methodName == "SetMode") {
    auto modeIt = params.find("mode");

    if (modeIt == params.end()) {
      errorMessage = "Missing required parameter: mode (ACCURATE/FAST/ULTRA_FAST)";
      return false;
    }

    std::string mode = modeIt->second;

    // Try to cast to Keithley6482Adapter
    auto keithley = std::dynamic_pointer_cast<Keithley6482Adapter>(device);
    if (!keithley) {
      errorMessage = "Device does not support mode configuration: " + deviceName;
      return false;
    }

    Keithley6482Adapter::MeasurementMode measMode = Keithley6482Adapter::MeasurementMode::ACCURATE;
    if (mode == "FAST") {
      measMode = Keithley6482Adapter::MeasurementMode::FAST;
    } else if (mode == "ULTRA_FAST") {
      measMode = Keithley6482Adapter::MeasurementMode::ULTRA_FAST;
    } else if (mode != "ACCURATE") {
      errorMessage = "Invalid mode: " + mode + " (must be ACCURATE, FAST, or ULTRA_FAST)";
      return false;
    }

    if (m_logger) {
      m_logger->Info("Setting " + deviceName + " mode to " + mode, "SMUHandler");
    }

    if (!keithley->ConfigureMeasurementMode(measMode)) {
      errorMessage = "Failed to configure measurement mode: " + device->GetLastError();
      return false;
    }

    return true;
  }

  // ============================================================================
  // OUTPUT ON
  // ============================================================================
  else if (methodName == "OutputOn") {
    if (m_logger) {
      m_logger->Info("Turning output ON for " + deviceName, "SMUHandler");
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
  else if (methodName == "OutputOff") {
    if (m_logger) {
      m_logger->Info("Turning output OFF for " + deviceName, "SMUHandler");
    }

    if (!device->SetOutputOff()) {
      errorMessage = "Failed to turn output off: " + device->GetLastError();
      return false;
    }

    return true;
  }

  // ============================================================================
  // UNKNOWN METHOD
  // ============================================================================
  else {
    errorMessage = "Unknown SMU method: " + methodName;
    return false;
  }
}
