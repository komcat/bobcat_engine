// SmuConfigurationCommands.cpp — see header for description.
#include "SmuConfigurationCommands.h"

#include "Keithley6482Adapter.h"
#include "LogManager.h"
#include "SMUManager.h"
#include "SequenceContext.h"

#include <memory>
#include <string>

std::optional<bool> SmuConfigurationCommands::TryExecute(
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
  // SET VOLTAGE (for voltage source devices)
  // ============================================================================
  if (methodName == "SetVoltage") {
    auto voltageIt = params.find("voltage");
    auto channelIt = params.find("channel");

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

    int channel = 1;
    if (channelIt != params.end()) {
      try {
        channel = std::stoi(channelIt->second);
      } catch (...) {
        errorMessage = "Invalid channel parameter: " + channelIt->second;
        return false;
      }
    }

    // Select channel if device supports it
    auto keithley6482 = std::dynamic_pointer_cast<Keithley6482Adapter>(device);
    if (keithley6482) {
      keithley6482->SelectChannel(channel);
    }

    if (m_logger) {
      m_logger->Info("Setting voltage on " + deviceName + " channel " + std::to_string(channel) +
                    " to " + std::to_string(voltage) + " V", "SMUHandler");
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
  if (methodName == "SetCurrent") {
    auto currentIt = params.find("current");
    auto channelIt = params.find("channel");

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

    int channel = 1;
    if (channelIt != params.end()) {
      try {
        channel = std::stoi(channelIt->second);
      } catch (...) {
        errorMessage = "Invalid channel parameter: " + channelIt->second;
        return false;
      }
    }

    // Select channel if device supports it
    auto keithley6482 = std::dynamic_pointer_cast<Keithley6482Adapter>(device);
    if (keithley6482) {
      keithley6482->SelectChannel(channel);
    }

    if (m_logger) {
      m_logger->Info("Setting current on " + deviceName + " channel " + std::to_string(channel) +
                    " to " + std::to_string(current) + " A", "SMUHandler");
    }

    if (!device->SetCurrent(current)) {
      errorMessage = "Failed to set current: " + device->GetLastError();
      return false;
    }

    return true;
  }

  // ============================================================================
  // SET CURRENT COMPLIANCE (over-current protection for voltage source)
  // ============================================================================
  if (methodName == "SetCurrentCompliance") {
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
      m_logger->Info("Setting current compliance on " + deviceName + " to " + std::to_string(limit) + " A",
                    "SMUHandler");
    }

    if (!device->SetCompliance(limit)) {
      errorMessage = "Failed to set current compliance: " + device->GetLastError();
      return false;
    }

    return true;
  }

  // ============================================================================
  // SET VOLTAGE COMPLIANCE (over-voltage protection for current source)
  // ============================================================================
  if (methodName == "SetVoltageCompliance") {
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
      m_logger->Info("Setting voltage compliance on " + deviceName + " to " + std::to_string(limit) + " V",
                    "SMUHandler");
    }

    // NOTE: Currently ISMUDevice interface only has SetCompliance() for current compliance
    // Voltage compliance would require interface extension (e.g., SetVoltageCompliance() method)
    // For now, log a warning
    if (m_logger) {
      m_logger->Warning("Voltage compliance not yet implemented in device interface (placeholder)",
                       "SMUHandler");
    }

    // Store in context for future use
    if (context) {
      context->SetValue(deviceName + ".voltageComplianceLimit", static_cast<float>(limit),
                       deviceName, methodName);
    }

    return true;  // Success (even though not actually set on device)
  }

  // ============================================================================
  // SET COMPLIANCE (backward compatibility - defaults to current compliance)
  // ============================================================================
  if (methodName == "SetCompliance") {
    auto limitIt = params.find("limit");
    auto channelIt = params.find("channel");

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

    int channel = 1;
    if (channelIt != params.end()) {
      try {
        channel = std::stoi(channelIt->second);
      } catch (...) {
        errorMessage = "Invalid channel parameter: " + channelIt->second;
        return false;
      }
    }

    // Select channel if device supports it
    auto keithley6482 = std::dynamic_pointer_cast<Keithley6482Adapter>(device);
    if (keithley6482) {
      keithley6482->SelectChannel(channel);
    }

    if (m_logger) {
      m_logger->Info("Setting compliance (current) on " + deviceName + " channel " + std::to_string(channel) +
                    " to " + std::to_string(limit) + " A", "SMUHandler");
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
  if (methodName == "SetMode") {
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

  return std::nullopt;  // not our family
}
