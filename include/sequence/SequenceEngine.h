// SequenceEngine.h
#pragma once

#include "ApplicationContext.h"
#include "CommandDispatcher.h"
#include "SystemCommandHandler.h"  // For callback types
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>

// Forward declarations
class SequenceBuilder;
struct ISequenceStep;
class SystemCommandHandler;
class LogManager;

// Callback type for user prompts - returns true if user confirmed, false if cancelled
using UserPromptCallback = std::function<bool(const std::string& title, const std::string& message)>;

//=============================================================================
// SEQUENCE STEP INTERFACE
//=============================================================================
struct ISequenceStep
{
  enum class Type {
    ACTION,
    LOOP,
    CONDITIONAL
  };

  virtual ~ISequenceStep() = default;
  virtual Type GetType() const = 0;
};

//=============================================================================
// ACTION STEP
//=============================================================================
struct Action : public ISequenceStep
{
  std::string deviceName;
  std::string methodName;
  std::map<std::string, std::string> params;

  Action(const std::string& device, const std::string& method,
    const std::map<std::string, std::string>& p)
    : deviceName(device), methodName(method), params(p) {
  }

  Type GetType() const override { return Type::ACTION; }
};

//=============================================================================
// LOOP BLOCK
//=============================================================================
struct LoopBlock : public ISequenceStep
{
  int count;
  std::vector<std::shared_ptr<ISequenceStep>> actions;

  explicit LoopBlock(int c) : count(c) {}

  Type GetType() const override { return Type::LOOP; }
};

//=============================================================================
// CONDITIONAL BLOCK
//=============================================================================
struct ConditionalBlock : public ISequenceStep
{
  std::string conditionDevice;
  std::string conditionMethod;
  std::vector<std::shared_ptr<ISequenceStep>> thenActions;
  std::vector<std::shared_ptr<ISequenceStep>> elseActions;

  ConditionalBlock(const std::string& device, const std::string& method)
    : conditionDevice(device), conditionMethod(method) {
  }

  Type GetType() const override { return Type::CONDITIONAL; }
};

//=============================================================================
// SEQUENCE ENGINE
//=============================================================================
class SequenceEngine
{
public:
  explicit SequenceEngine(ApplicationContext* context);
  ~SequenceEngine();

  // Configuration
  void SetLogger(LogManager* logger);
  void BuildDeviceRegistry();
  // ← ADD THESE TWO LINES:
  void SetUserPromptCallback(UserPromptCallback callback);
  void SetUserNotificationCallback(UserNotificationCallback callback);

  // Sequence creation and execution
  SequenceBuilder* CreateSequence(const std::string& name);
  bool ExecuteSequence(SequenceBuilder* sequence);
  bool ValidateSequence(SequenceBuilder* sequence, std::vector<std::string>& errors);

  // Logging
  const std::vector<std::string>& GetExecutionLog() const { return m_executionLog; }
  void ClearLog() { m_executionLog.clear(); }

  // Device resolution (used by validator)
  void* ResolveManager(const std::string& deviceName);
  std::string ResolveManagerType(const std::string& deviceName);




private:
  ApplicationContext* m_context;
  LogManager* m_logger;
  std::vector<std::string> m_executionLog;

  CommandDispatcher m_dispatcher;
  SystemCommandHandler* m_systemHandler;  // Non-owning pointer

  std::map<std::string, void*> m_deviceToManagerMap;
  std::map<std::string, std::string> m_deviceToTypeMap;

  bool ExecuteStep(ISequenceStep* step);
  bool ValidateStep(ISequenceStep* step, std::vector<std::string>& errors);

  void LogInfo(const std::string& message);
  void LogWarning(const std::string& message);
  void LogError(const std::string& message);
};
