// SMUCommandHandler.h
#pragma once

#include "ICommandHandler.h"

// Forward declaration
struct ApplicationContext;

/**
 * @brief Command handler for SMU (Source Measure Unit) devices
 *
 * Supports commands:
 * - StartPolling: Begin continuous current measurement with polling manager
 * - StopPolling: Stop continuous measurement
 * - ReadCurrent: Single current reading from specific channel
 * - ReadVoltage: Single voltage reading
 * - SetVoltage: Set source voltage (for voltage source devices)
 * - SetCurrent: Set source current (for current source devices)
 * - SetCompliance: Set compliance limit
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
