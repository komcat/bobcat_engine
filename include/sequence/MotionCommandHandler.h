// MotionCommandHandler.h
#pragma once

#include "ICommandHandler.h"

class MotionCommandHandler : public ICommandHandler
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

  std::string GetManagerType() const override { return "motion"; }

private:
  // Helper to resolve context: references or parse direct values
  double ResolveNumericValue(const std::string& valueStr, SequenceContext* context);

  // Target position for verification (optional)
  struct TargetPosition {
    bool hasX = false;
    bool hasY = false;
    bool hasZ = false;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
  };

  // Helper to wait for motion complete with safety stop checking and position verification
  // Returns false if stop requested, timeout, or position verification failed
  // If targetPos is nullptr, position verification is skipped
  bool WaitForMotionWithStopCheck(
    void* motionManager,
    const std::string& deviceName,
    SequenceContext* context,
    int timeoutMs = 30000,
    const TargetPosition* targetPos = nullptr);
};