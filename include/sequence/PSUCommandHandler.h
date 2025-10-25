// PSUCommandHandler.h
#pragma once
#include "ICommandHandler.h"

/**
 * @brief Command handler for Power Supply Unit (PSU) devices in sequence engine
 *
 * Handles:
 * - SetOutputOn - Turn PSU output on
 * - SetOutputOff - Turn PSU output off
 * - SetVoltage - Set PSU voltage (CV mode)
 * - SetCurrent - Set PSU current (CC mode)
 */
class PSUCommandHandler : public ICommandHandler {
public:
  PSUCommandHandler() = default;
  ~PSUCommandHandler() override = default;

  /**
   * @brief Execute PSU command
   * @param manager Pointer to PowerSupplyManager (cast from void*)
   * @param deviceName Name of PSU device
   * @param methodName Method/action to execute
   * @param params Parameters map
   * @param errorMessage Output error message
   * @return true if command successful
   */
  bool ExecuteCommand(
    void* manager,
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage
  ) override;

  std::string GetManagerType() const override { return "psu"; }

private:
  /**
   * @brief Parse voltage value from parameters
   */
  bool ParseVoltage(const std::map<std::string, std::string>& params, double& outVoltage);

  /**
   * @brief Parse current value from parameters (in mA)
   */
  bool ParseCurrent(const std::map<std::string, std::string>& params, double& outCurrent);
};
