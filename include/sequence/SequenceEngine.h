// SequenceEngine.h
#pragma once

#include "ApplicationContext.h"
#include "CommandDispatcher.h"
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
  void SetUserPromptCallback(UserPromptCallback callback);

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

//=============================================================================
// SEQUENCE BUILDER
//=============================================================================
class SequenceBuilder
{
public:
  SequenceBuilder(const std::string& name, SequenceEngine* engine);
  ~SequenceBuilder();

  // Add simple action
  void AddAction(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params = {}
  );

  // Loop control
  void BeginLoop(int count);
  void EndLoop();

  // Conditional control
  void BeginIf(const std::string& conditionDevice,
    const std::string& conditionMethod);
  void Else();
  void EndIf();

  // Getters
  std::string GetName() const { return m_name; }
  const std::vector<std::shared_ptr<ISequenceStep>>& GetSteps() const { return m_steps; }

private:
  std::string m_name;
  SequenceEngine* m_engine;
  std::vector<std::shared_ptr<ISequenceStep>> m_steps;

  // Stack to track nested blocks (for loops/conditionals)
  enum class BlockType { LOOP, CONDITIONAL_THEN, CONDITIONAL_ELSE };
  struct BlockContext {
    BlockType type;
    std::shared_ptr<ISequenceStep> block;
  };
  std::vector<BlockContext> m_blockStack;

  // Helper to get current active step list
  std::vector<std::shared_ptr<ISequenceStep>>* GetActiveStepList();
};