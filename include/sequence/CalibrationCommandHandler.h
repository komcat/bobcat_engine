// CalibrationCommandHandler.h
#pragma once

#include "ICommandHandler.h"
#include <string>

struct ApplicationContext;

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

  // Cross-family utilities used by every save path. Public so per-family
  // helpers under include/sequence/calibration/ can reach them via
  // deps.parent->... — mirrors Motion's deps.parent->ResolveNumericValue.
  std::string GetCurrentDateTime(bool dateOnly = false) const;
  bool EnsureCalibrationDirectoryExists(std::string& errorMessage);

private:
  ApplicationContext* m_context;
};
