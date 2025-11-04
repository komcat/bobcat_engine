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
#include <atomic>

// Forward declarations
class SequenceBuilder;
struct ISequenceStep;
class SystemCommandHandler;
class LogManager;
class SequenceContext;

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
  enum class ComparisonType {
    GREATER_THAN,      // >
    LESS_THAN,         // <
    EQUAL,             // ==
    NOT_EQUAL,         // !=
    GREATER_OR_EQUAL,  // >=
    LESS_OR_EQUAL,     // <=
    IS_TRUE,           // Boolean true
    IS_FALSE,          // Boolean false
    EXISTS,            // Key exists in context
    NOT_EXISTS         // Key does not exist in context
  };

  std::string contextKey;        // Key in SequenceContext to evaluate
  ComparisonType comparison;
  float threshold;

  std::vector<std::shared_ptr<ISequenceStep>> thenActions;
  std::vector<std::shared_ptr<ISequenceStep>> elseActions;

  // New constructor for context-based conditionals
  ConditionalBlock(const std::string& key, ComparisonType comp, float thresh = 0.0f)
    : contextKey(key), comparison(comp), threshold(thresh) {
  }

  // Old constructor for backward compatibility (deprecated)
  ConditionalBlock(const std::string& device, const std::string& method)
    : contextKey(device + "." + method), comparison(ComparisonType::IS_TRUE), threshold(0.0f) {
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
  LogManager* GetLogger() const { return m_logger; }
  void BuildDeviceRegistry();
  void SetUserPromptCallback(UserPromptCallback callback);
  void SetUserNotificationCallback(UserNotificationCallback callback);
  void SetManualAdjustCallback(ManualAdjustCallback callback);
  void SetUserInputCallback(UserInputCallback callback);

  // ✅ NEW: Stop control
  void RequestStop();        // Request stop at next checkpoint
  void ClearStopFlag();      // Reset stop flag before execution
  bool IsStopRequested() const { return m_stopRequested.load(); }

  // Sequence creation and execution
  SequenceBuilder* CreateSequence(const std::string& name);

  // Execute with context
  bool ExecuteSequence(SequenceBuilder* sequence, SequenceContext* context);

  // Validate sequence
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

  // ✅ NEW: Stop flag for graceful cancellation
  std::atomic<bool> m_stopRequested;

  bool ExecuteStep(ISequenceStep* step, SequenceContext* context);
  bool ValidateStep(ISequenceStep* step, std::vector<std::string>& errors);
  bool EvaluateCondition(const ConditionalBlock* cond, SequenceContext* context);
  std::string ComparisonToString(ConditionalBlock::ComparisonType comp) const;

  void LogInfo(const std::string& message);
  void LogWarning(const std::string& message);
  void LogError(const std::string& message);
};