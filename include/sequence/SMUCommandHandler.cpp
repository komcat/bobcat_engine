// SMUCommandHandler.cpp
// Thin dispatch ladder. Per-family helpers under include/sequence/smu/ own
// the actual command logic; this file does the manager+device pre-flight,
// builds the deps struct, walks each family's TryExecute in order, and falls
// through to "unknown method".
#include "SMUCommandHandler.h"

#include "ApplicationContext.h"
#include "LogManager.h"
#include "SMUManager.h"
#include "SMU/SMUPollingManager.h"
#include "SMU/KeithleySMUAdapter.h"
#include "SMU/Keithley6482Adapter.h"

#include "smu/SmuCommandDeps.h"
#include "smu/SmuConfigurationCommands.h"
#include "smu/SmuConnectionCommands.h"
#include "smu/SmuOutputMeasureCommands.h"
#include "smu/SmuPollingCommands.h"

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

  // Manager-level commands routed through the sentinel "smu" device.
  // Handled here BEFORE the per-device lookup since they don't target a
  // specific SMU. Keep this list small — single-device commands belong in
  // their per-family helper.
  if (methodName == "OutputOffAll") {
    // SMUManager::OutputOffAll() only calls SetOutputOff() once per device,
    // which on TSP multi-channel adapters (e.g., Keithley 2602B) only kills
    // the currently-selected channel (smua or smub). SelectChannel isn't on
    // the ISMUDevice interface, so dynamic-cast to reach it on adapters that
    // actually need per-channel iteration.
    int devicesTouched = 0;
    int channelsOff = 0;
    for (const auto& name : smuManager->GetDeviceNames()) {
      auto device = smuManager->GetDevice(name);
      if (!device || !device->IsConnected()) continue;

      auto channels = device->GetEnabledChannels();

      // KeithleySMUAdapter (2400/2600): SetOutputOff() is per-selected-channel,
      // so we must SelectChannel before each call.
      if (auto smu = std::dynamic_pointer_cast<KeithleySMUAdapter>(device)) {
        if (channels.empty()) channels.push_back(1);
        for (int ch : channels) {
          smu->SelectChannel(ch);
          if (smu->SetOutputOff()) ++channelsOff;
        }
      }
      // Everything else (e.g., Keithley 6482 picoammeter): SetOutputOff() is
      // a global :OUTP OFF on the device — single call is enough.
      else {
        if (device->SetOutputOff()) {
          channelsOff += channels.empty() ? 1 : static_cast<int>(channels.size());
        }
      }
      ++devicesTouched;
    }
    if (m_logger) {
      m_logger->Info("SMU OutputOffAll: " + std::to_string(channelsOff) +
                     " channel(s) off across " + std::to_string(devicesTouched) + " device(s)",
                     "SMUHandler");
    }
    return true;
  }

  if (methodName == "StopPollingAll") {
    if (!m_context || !m_context->smuPollingManager) {
      errorMessage = "SMU Polling Manager not available";
      return false;
    }
    m_context->smuPollingManager->StopAllPolling();
    if (m_logger) {
      m_logger->Info("SMU StopPollingAll: polling halted on all SMU devices", "SMUHandler");
    }
    return true;
  }

  // Get the device
  auto device = smuManager->GetDevice(deviceName);
  if (!device) {
    errorMessage = "SMU device not found: " + deviceName;
    return false;
  }

  SmuCommandDeps deps{};
  deps.app        = m_context;
  deps.logger     = m_logger;   // protected member from ICommandHandler base
  deps.smuManager = smuManager;
  deps.device     = device;

  if (auto r = SmuConnectionCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) {
    return *r;
  }
  if (auto r = SmuOutputMeasureCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) {
    return *r;
  }
  if (auto r = SmuConfigurationCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) {
    return *r;
  }
  if (auto r = SmuPollingCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) {
    return *r;
  }

  errorMessage = "Unknown SMU method: " + methodName;
  return false;
}
