// TECCommandHandler.h
#pragma once
#include "ICommandHandler.h"

/**
 * @brief Command handler for TEC devices in sequence engine
 *
 * Follows same pattern as MotionCommandHandler, IOCommandHandler, etc.
 */
class TECCommandHandler : public ICommandHandler {
public:
  TECCommandHandler() = default;
  ~TECCommandHandler() override = default;

  /**
   * @brief Execute TEC command
   * @param manager Pointer to TECManager (cast from void*)
   * @param deviceName Name of TEC device
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

  std::string GetManagerType() const override { return "tec"; }

private:
  /**
   * @brief Parse temperature value from parameters
   */
  bool ParseTemperature(const std::map<std::string, std::string>& params, double& outTemp);

  /**
   * @brief Parse current value from parameters
   */
  bool ParseCurrent(const std::map<std::string, std::string>& params, double& outCurrent);
};