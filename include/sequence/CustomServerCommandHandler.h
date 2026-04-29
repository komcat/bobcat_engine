// CustomServerCommandHandler.h
// Thin orchestrator for beam-server / beam-detection commands. Per-family
// implementations live in include/sequence/customserver/Custom*.{h,cpp}.
#pragma once

#include "ICommandHandler.h"

struct ApplicationContext;

class CustomServerCommandHandler : public ICommandHandler {
public:
  CustomServerCommandHandler() = default;
  ~CustomServerCommandHandler() override = default;

  bool ExecuteCommand(
    void* manager,
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context
  ) override;

  std::string GetManagerType() const override { return "customserver"; }

  void SetContext(ApplicationContext* ctx) { m_context = ctx; }

private:
  // (All Handle* implementations moved to customserver/Custom*.{h,cpp}.)
  ApplicationContext* m_context = nullptr;
};
