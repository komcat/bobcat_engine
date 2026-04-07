// SequenceBuilder.h
#pragma once

#include "ApplicationContext.h"
#include "CommandDispatcher.h"
#include "SequenceEngine.h"  // Need full definition for ConditionalBlock::ComparisonType
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>

class ProcessSequenceFactory;

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

  // Add step marker (for tracking recipe step execution)
  void AddStepMarker(int stepIndex, const std::string& stepName, const std::string& stepType);

  // Loop control
  void BeginLoop(int count);
  void EndLoop();

  // Conditional control (context-based - NEW)
  void BeginIf(const std::string& contextKey,
    ConditionalBlock::ComparisonType comparison,
    float threshold = 0.0f);

  // Conditional control (old device/method - kept for backward compatibility)
  void BeginIf(const std::string& conditionDevice,
    const std::string& conditionMethod);

  void Else();
  void EndIf();

  // Getters
  std::string GetName() const { return m_name; }
  const std::vector<std::shared_ptr<ISequenceStep>>& GetSteps() const { return m_steps; }

  // Factory access (used by sub-recipe plugin to inline steps)
  void SetFactory(ProcessSequenceFactory* factory) { m_factory = factory; }
  ProcessSequenceFactory* GetFactory() const { return m_factory; }

  // Parent recipe path (used by sub-recipe plugin for self-reference detection)
  void SetRecipeFilePath(const std::string& path) { m_recipeFilePath = path; }
  std::string GetRecipeFilePath() const { return m_recipeFilePath; }

private:
  std::string m_name;
  SequenceEngine* m_engine;
  ProcessSequenceFactory* m_factory = nullptr;
  std::string m_recipeFilePath;
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