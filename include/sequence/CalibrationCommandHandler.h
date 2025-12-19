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
  bool SaveNavigationData(
    const std::string& uid,
    const std::string& deviceName,
    double camX, double camY, double camZ,
    std::string& errorMessage
  );

  bool LoadNavigationData(
    const std::string& uid,
    double& camX, double& camY, double& camZ
  );

  bool SaveNeedleZCalibrationGlobal(
    const std::string& deviceName,
    const std::string& pneumaticSlide,
    double needleX, double needleY, double needleZ,
    std::string& errorMessage,
    std::string& deltaMessage
  );

  bool LoadNeedleZCalibrationGlobal(
    double& oldNeedleHeight
  );

  bool SaveCameraToNeedleOffset(
    const std::string& deviceName,
    const std::string& pneumaticSlide,
    double needleX, double needleY, double needleZ,
    double cameraX, double cameraY, double cameraZ,
    std::string& errorMessage,
    std::string& resultMessage
  );

  bool LoadCameraToNeedleOffset(
    double& offsetX, double& offsetY, double& offsetZ
  );

  bool SaveDispenseZHeight(
    const std::string& uid,
    const std::string& deviceName,
    double dispenseZ,
    std::string& errorMessage
  );

  bool LoadDispenseZHeight(
    const std::string& uid,
    double& dispenseZ
  );

  std::string GetNavigationFilePath(const std::string& uid) const;
  std::string GetNeedleZCalibrationFilePath() const;
  std::string GetCameraToNeedleOffsetFilePath() const;
  std::string GetDispenseZFilePath(const std::string& uid) const;
  std::string GetYawCorrectionFilePath(const std::string& uid) const;
  std::string GetCurrentDateTime(bool dateOnly = false) const;
  bool EnsureCalibrationDirectoryExists(std::string& errorMessage);

  // Yaw Correction methods
  bool LoadYawCorrectionData(
    const std::string& uid,
    double& corner1X, double& corner1Y,
    double& corner2X, double& corner2Y,
    double& previousYawAngle
  );

  bool SaveYawCorrectionData(
    const std::string& uid,
    const std::string& gantryDevice,
    double corner1X, double corner1Y,
    double corner2X, double corner2Y,
    double yawAngleDegrees,
    std::string& errorMessage
  );

  double CalculateYawAngle(
    double corner1X, double corner1Y,
    double corner2X, double corner2Y
  );

  // Yaw Correction to Template methods
  std::string GetYawToTemplateFilePath(const std::string& uid) const;

  bool LoadYawToTemplateData(
    const std::string& uid,
    double& templateCorner1X, double& templateCorner1Y,
    double& templateCorner2X, double& templateCorner2Y,
    double& targetCorner3X, double& targetCorner3Y,
    double& targetCorner4X, double& targetCorner4Y,
    double& templateYaw, double& targetYaw, double& correctionAngle
  );

  bool SaveYawToTemplateData(
    const std::string& uid,
    const std::string& gantryDevice,
    double templateCorner1X, double templateCorner1Y,
    double templateCorner2X, double templateCorner2Y,
    double targetCorner3X, double targetCorner3Y,
    double targetCorner4X, double targetCorner4Y,
    double templateYaw, double targetYaw, double correctionAngle,
    std::string& errorMessage
  );
};
