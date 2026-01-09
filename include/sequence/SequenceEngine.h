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
#include <mutex>
#include <condition_variable>

// Forward declarations
class SequenceBuilder;
struct ISequenceStep;
class SystemCommandHandler;
class LogManager;
class SequenceContext;

// Callback type for user prompts - returns true if user confirmed, false if cancelled
using UserPromptCallback = std::function<bool(const std::string& title, const std::string& message)>;

// Callback types for step execution events (pub-sub pattern)
using StepStartedCallback = std::function<void(int stepIndex, const std::string& stepName, const std::string& stepType)>;
using StepCompletedCallback = std::function<void(int stepIndex, bool success)>;

// Action info for command tracker
struct ActionInfo {
  std::string deviceName;
  std::string methodName;
  bool isValid = false;

  ActionInfo() = default;
  ActionInfo(const std::string& dev, const std::string& method)
    : deviceName(dev), methodName(method), isValid(true) {}

  std::string toString() const {
    return isValid ? (deviceName + "." + methodName) : "";
  }
};

// Callback for individual action execution with look-ahead info
// Parameters: current action, next action, after-next action
using ActionStartedCallback = std::function<void(const ActionInfo& current, const ActionInfo& next, const ActionInfo& afterNext)>;

//=============================================================================
// SEQUENCE STEP INTERFACE
//=============================================================================
struct ISequenceStep
{
  enum class Type {
    ACTION,
    LOOP,
    CONDITIONAL,
    STEP_MARKER  // Marker for recipe step boundaries
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
// STEP MARKER - Marks the beginning of a recipe step for tracking
//=============================================================================
struct StepMarker : public ISequenceStep
{
  int stepIndex;           // Recipe step index (0-based)
  std::string stepName;    // User-friendly name (e.g., "Pick 1", "Light: All Off 2")
  std::string stepType;    // Process type (e.g., "Pick", "Light: All Off")

  StepMarker(int index, const std::string& name, const std::string& type)
    : stepIndex(index), stepName(name), stepType(type) {}

  Type GetType() const override { return Type::STEP_MARKER; }
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
  void SetManualAlignPointCallback(ManualAlignPointCallback callback);

  // Step execution event callbacks (pub-sub)
  void SetStepStartedCallback(StepStartedCallback callback);
  void SetStepCompletedCallback(StepCompletedCallback callback);
  void SetActionStartedCallback(ActionStartedCallback callback);

  // Breakpoint callback - called when execution hits a breakpoint (for UI notification)
  using BreakpointCallback = std::function<void(int stepIndex, const std::string& stepName)>;
  void SetBreakpointCallback(BreakpointCallback callback);

  // Set breakpoints for steps (step index -> has breakpoint)
  void SetBreakpoints(const std::vector<bool>& breakpoints);

  // ✅ NEW: Stop control
  void RequestStop();        // Request stop at next checkpoint
  void ClearStopFlag();      // Reset stop flag before execution
  bool IsStopRequested() const { return m_stopRequested.load(); }

  // Step-by-step execution control
  void SetStepByStepMode(bool enabled);  // Enable/disable step-by-step mode
  bool IsStepByStepMode() const { return m_stepByStepMode.load(); }
  void RequestContinue();                 // Continue to next step (or breakpoint)
  void RequestRunToEnd();                 // Continue running without pausing

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

  // Step execution event callbacks
  StepStartedCallback m_stepStartedCallback;
  StepCompletedCallback m_stepCompletedCallback;
  ActionStartedCallback m_actionStartedCallback;
  int m_currentStepIndex;  // Track current step for completion callback

  // Action list for look-ahead (built before execution)
  std::vector<ActionInfo> m_actionList;
  int m_currentActionIndex;
  void BuildActionList(SequenceBuilder* sequence);
  void FlattenSteps(const std::vector<std::shared_ptr<ISequenceStep>>& steps);

  // Breakpoint support
  BreakpointCallback m_breakpointCallback;
  std::vector<bool> m_breakpoints;  // Breakpoint flag for each enabled step

  // Step-by-step execution control
  std::atomic<bool> m_stepByStepMode;      // True = pause at each step
  std::atomic<bool> m_continueRequested;   // True = continue to next step
  std::condition_variable m_pauseCondition;
  std::mutex m_pauseMutex;

  bool ExecuteStep(ISequenceStep* step, SequenceContext* context);
  bool ValidateStep(ISequenceStep* step, std::vector<std::string>& errors);
  bool EvaluateCondition(const ConditionalBlock* cond, SequenceContext* context);
  std::string ComparisonToString(ConditionalBlock::ComparisonType comp) const;

  void LogInfo(const std::string& message);
  void LogWarning(const std::string& message);
  void LogError(const std::string& message);
};