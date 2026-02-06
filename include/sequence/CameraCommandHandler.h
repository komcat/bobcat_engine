// CameraCommandHandler.h
#pragma once

#include "ICommandHandler.h"

// Forward declaration
struct ApplicationContext;

class CameraCommandHandler : public ICommandHandler
{
public:
  CameraCommandHandler() : m_context(nullptr) {}
  CameraCommandHandler(ApplicationContext* context) : m_context(context) {}

  bool ExecuteCommand(
    void* manager,
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context
  ) override;

  std::string GetManagerType() const override { return "camera"; }

  void SetContext(ApplicationContext* context) { m_context = context; }

private:
  ApplicationContext* m_context;
};