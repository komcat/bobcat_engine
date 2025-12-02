// AuxWindowCommandHandler.h
#pragma once

#include "ICommandHandler.h"

class AuxWindowManager;

/**
 * @brief Command handler for Auxiliary Window controls
 *
 * Provides sequence commands to control the auxiliary display window:
 * - EnableCrosshair: Enable/disable live camera crosshair overlay
 * - EnableClickToMove: Enable/disable click-to-move feature
 * - SetChartChannel: Set which DataStore channel to display in live chart
 * - SetTarget: Set target value for current or specified channel
 * - ClearTarget: Clear target value for current or specified channel
 *
 * Usage in sequence:
 *   AddAction("auxwindow", "EnableCrosshair", {{"enabled", "true"}});
 *   AddAction("auxwindow", "EnableClickToMove", {{"enabled", "false"}});
 *   AddAction("auxwindow", "SetChartChannel", {{"channel", "SMU_Keithley_6482_Ch1"}});
 *   AddAction("auxwindow", "SetTarget", {{"value", "1.5e-6"}}); // Set target for current channel
 *   AddAction("auxwindow", "SetTarget", {{"channel", "SMU_Ch1"}, {"value", "1.5e-6"}}); // Set target for specific channel
 *   AddAction("auxwindow", "ClearTarget", {}); // Clear target for current channel
 *   AddAction("auxwindow", "ClearTarget", {{"channel", "SMU_Ch1"}}); // Clear target for specific channel
 */
class AuxWindowCommandHandler : public ICommandHandler
{
public:
  explicit AuxWindowCommandHandler(AuxWindowManager* auxWindowManager);

  bool ExecuteCommand(
    void* manager,
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context
  ) override;

  std::string GetManagerType() const override { return "auxwindow"; }

private:
  AuxWindowManager* m_auxWindowManager;
};
