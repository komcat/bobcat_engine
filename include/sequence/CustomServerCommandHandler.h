// CustomServerCommandHandler.h
// Command handler for beam detection server integration.
// Routes Connect/Disconnect/Detect/Fit/Reset commands to BeamServerClient.
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
  bool HandleConnect(const std::map<std::string, std::string>& params, std::string& errorMessage);
  bool HandleDisconnect(std::string& errorMessage);
  bool HandleDetect(const std::map<std::string, std::string>& params,
                    std::string& errorMessage, SequenceContext* context);
  bool HandleFit(const std::map<std::string, std::string>& params,
                 std::string& errorMessage, SequenceContext* context);
  bool HandleDetectFile(const std::map<std::string, std::string>& params,
                        std::string& errorMessage, SequenceContext* context);
  bool HandleDetectFolder(const std::map<std::string, std::string>& params,
                          std::string& errorMessage, SequenceContext* context);
  bool HandleReset(std::string& errorMessage);

  ApplicationContext* m_context = nullptr;
};
