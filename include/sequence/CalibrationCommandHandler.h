// CalibrationCommandHandler.h
#pragma once

#include "ICommandHandler.h"
#include <string>

class ApplicationContext;

class CalibrationCommandHandler : public ICommandHandler
{
public:
  CalibrationCommandHandler(ApplicationContext* context);
  ~CalibrationCommandHandler() override = default;

  bool ExecuteCommand(
    void* manager,
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context
  ) override;

  std::string GetManagerType() const override { return "calibration"; }

private:
  ApplicationContext* m_context;

  // Helper methods
  bool SaveNeedleZCalibration(
    const std::string& deviceName,
    const std::string& pneumaticSlide,
    double camX, double camY, double camZ,
    double offsetX, double offsetY, double offsetZ,
    double needleX, double needleY, double needleZ,
    std::string& errorMessage,
    std::string& deltaMessage
  );

  bool LoadExistingCalibration(
    double& oldNeedleHeight,
    double& camX, double& camY, double& camZ,
    double& offsetX, double& offsetY, double& offsetZ
  );

  std::string GetCalibrationFilePath() const;
  std::string GetCurrentDateTime(bool dateOnly = false) const;
  bool EnsureCalibrationDirectoryExists(std::string& errorMessage);
};
