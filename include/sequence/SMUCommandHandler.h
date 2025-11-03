// SMUCommandHandler.h
#pragma once

#include "ICommandHandler.h"

// Forward declaration
struct ApplicationContext;

/**
 * @brief Command handler for SMU (Source Measure Unit) devices
 *
 * Supports commands:
 * - OutputOn: Turn output ON
 * - OutputOff: Turn output OFF
 * - SetVoltage: Set source voltage (for voltage source devices)
 * - SetCurrent: Set source current (for current source devices)
 * - ReadVoltage: Single voltage reading
 * - ReadCurrent: Single current reading from specific channel
 * - SetCurrentCompliance: Set current compliance limit (over-current protection)
 * - SetVoltageCompliance: Set voltage compliance limit (over-voltage protection)
 * - StartPolling: Begin continuous measurement with polling manager
 * - StopPolling: Stop continuous measurement
 * - SetMode: Configure measurement mode (ACCURATE/FAST/ULTRA_FAST)
 */
class SMUCommandHandler : public ICommandHandler
{
public:
  bool ExecuteCommand(
    void* manager,
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context
  ) override;

  std::string GetManagerType() const override { return "smu"; }

  // ✅ ADD: Need context to access polling manager
  void SetContext(ApplicationContext* context) { m_context = context; }

private:
  ApplicationContext* m_context = nullptr;  // ✅ ADD THIS
};
