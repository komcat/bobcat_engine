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
 *
 * Usage in sequence:
 *   AddAction("auxwindow", "EnableCrosshair", {{"enabled", "true"}});
 *   AddAction("auxwindow", "EnableClickToMove", {{"enabled", "false"}});
 *   AddAction("auxwindow", "SetChartChannel", {{"channel", "SMU_Keithley_6482_Ch1"}});
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
