// RecipeExecutionStateMachine.cpp
#include "RecipeExecutionStateMachine.h"
#include "ConditionExpressionEvaluator.h"
#include "../sequence/SequenceContext.h"
#include <algorithm>
#include <sstream>
#include <queue>
#include <iostream>

RecipeExecutionStateMachine::RecipeExecutionStateMachine()
  : m_recipeCount(0)
  , m_executionContext(nullptr)
{
}

RecipeExecutionStateMachine::~RecipeExecutionStateMachine()
{
}

// ============================================================================
// GRAPH CONSTRUCTION
// ============================================================================

void RecipeExecutionStateMachine::initialize(int recipeCount, const std::vector<std::string>& recipeNames)
{
  m_recipeCount = recipeCount;
  m_nodes.clear();

  for (int i = 0; i < recipeCount; ++i) {
    std::string name = (i < static_cast<int>(recipeNames.size())) ? recipeNames[i] : "Recipe " + std::to_string(i + 1);
    m_nodes[i] = std::make_unique<RecipeNode>(i, name);
  }

  std::cout << "[RecipeStateMachine] Initialized with " << recipeCount << " recipes" << std::endl;
}

bool RecipeExecutionStateMachine::addDependency(int recipeIndex, int prerequisiteIndex,
                                                  const std::string& condition)
{
  // Validate indices
  if (recipeIndex < 0 || recipeIndex >= m_recipeCount ||
      prerequisiteIndex < 0 || prerequisiteIndex >= m_recipeCount) {
    std::cerr << "[RecipeStateMachine] Invalid indices: recipe=" << recipeIndex
              << ", prerequisite=" << prerequisiteIndex << std::endl;
    return false;
  }

  // Check for self-dependency
  if (recipeIndex == prerequisiteIndex) {
    std::cerr << "[RecipeStateMachine] Cannot add self-dependency for recipe " << recipeIndex << std::endl;
    return false;
  }

  // Check if adding this would create a cycle
  if (wouldCreateCycle(recipeIndex, prerequisiteIndex)) {
    std::cerr << "[RecipeStateMachine] Adding dependency would create a cycle" << std::endl;
    return false;
  }

  // Check if dependency already exists
  auto& predecessors = m_nodes[recipeIndex]->predecessors;
  auto it = std::find_if(predecessors.begin(), predecessors.end(),
    [prerequisiteIndex](const PredecessorInfo& p) {
      return p.index == prerequisiteIndex;
    });

  if (it != predecessors.end()) {
    // Update condition on existing dependency
    it->condition = condition;
    std::cout << "[RecipeStateMachine] Updated dependency condition: Recipe " << recipeIndex
              << " depends on Recipe " << prerequisiteIndex
              << " with condition: " << (condition.empty() ? "(none)" : condition) << std::endl;
    return true;
  }

  // Add predecessor to recipe (with condition)
  m_nodes[recipeIndex]->predecessors.emplace_back(prerequisiteIndex, condition);

  // Add successor to prerequisite
  m_nodes[prerequisiteIndex]->successors.push_back(recipeIndex);

  std::cout << "[RecipeStateMachine] Added dependency: Recipe " << recipeIndex
            << " depends on Recipe " << prerequisiteIndex
            << " with condition: " << (condition.empty() ? "(none)" : condition) << std::endl;

  return true;
}

void RecipeExecutionStateMachine::setExecutionContext(SequenceContext* context)
{
  m_executionContext = context;
  std::cout << "[RecipeStateMachine] Execution context set" << std::endl;
}

void RecipeExecutionStateMachine::removeDependency(int recipeIndex, int prerequisiteIndex)
{
  if (recipeIndex < 0 || recipeIndex >= m_recipeCount ||
      prerequisiteIndex < 0 || prerequisiteIndex >= m_recipeCount) {
    return;
  }

  // Remove predecessor from recipe
  auto& predecessors = m_nodes[recipeIndex]->predecessors;
  predecessors.erase(
    std::remove_if(predecessors.begin(), predecessors.end(),
      [prerequisiteIndex](const PredecessorInfo& p) {
        return p.index == prerequisiteIndex;
      }),
    predecessors.end());

  // Remove successor from prerequisite
  auto& successors = m_nodes[prerequisiteIndex]->successors;
  successors.erase(std::remove(successors.begin(), successors.end(), recipeIndex), successors.end());

  std::cout << "[RecipeStateMachine] Removed dependency: Recipe " << recipeIndex
            << " no longer depends on Recipe " << prerequisiteIndex << std::endl;
}

void RecipeExecutionStateMachine::clearDependencies(int recipeIndex)
{
  if (recipeIndex < 0 || recipeIndex >= m_recipeCount) {
    return;
  }

  // Remove from all predecessors' successor lists
  for (const auto& pred : m_nodes[recipeIndex]->predecessors) {
    auto& successors = m_nodes[pred.index]->successors;
    successors.erase(std::remove(successors.begin(), successors.end(), recipeIndex), successors.end());
  }

  // Clear predecessors
  m_nodes[recipeIndex]->predecessors.clear();

  std::cout << "[RecipeStateMachine] Cleared all dependencies for Recipe " << recipeIndex << std::endl;
}

void RecipeExecutionStateMachine::clearAllDependencies()
{
  for (auto& [index, node] : m_nodes) {
    node->predecessors.clear();
    node->successors.clear();
  }

  std::cout << "[RecipeStateMachine] Cleared all dependencies" << std::endl;
}

void RecipeExecutionStateMachine::setRecipeDisabled(int recipeIndex)
{
  if (recipeIndex < 0 || recipeIndex >= m_recipeCount) {
    return;
  }

  m_nodes[recipeIndex]->state = RecipeExecutionState::Disabled;
  std::cout << "[RecipeStateMachine] Recipe " << recipeIndex << " disabled" << std::endl;
}

// ============================================================================
// VALIDATION
// ============================================================================

bool RecipeExecutionStateMachine::validateGraph(std::vector<std::string>& errorMessages) const
{
  errorMessages.clear();

  // Check for cycles using DFS
  std::unordered_set<int> visited;
  std::unordered_set<int> recursionStack;

  for (const auto& [index, node] : m_nodes) {
    if (visited.find(index) == visited.end()) {
      if (detectCycleFromNode(index, visited, recursionStack)) {
        errorMessages.push_back("Cycle detected in recipe dependencies involving Recipe " + std::to_string(index + 1));
        return false;
      }
    }
  }

  // Check for recipes with no path to completion (isolated nodes are OK)
  std::cout << "[RecipeStateMachine] Graph validation passed" << std::endl;
  return true;
}

bool RecipeExecutionStateMachine::detectCycleFromNode(int startNode,
                                                       std::unordered_set<int>& visited,
                                                       std::unordered_set<int>& recursionStack) const
{
  visited.insert(startNode);
  recursionStack.insert(startNode);

  auto it = m_nodes.find(startNode);
  if (it == m_nodes.end()) {
    recursionStack.erase(startNode);
    return false;
  }

  // Visit all successors
  for (int successor : it->second->successors) {
    if (visited.find(successor) == visited.end()) {
      if (detectCycleFromNode(successor, visited, recursionStack)) {
        return true;
      }
    }
    else if (recursionStack.find(successor) != recursionStack.end()) {
      // Found a back edge - cycle detected
      return true;
    }
  }

  recursionStack.erase(startNode);
  return false;
}

bool RecipeExecutionStateMachine::wouldCreateCycle(int recipeIndex, int prerequisiteIndex) const
{
  // Adding edge: prerequisiteIndex -> recipeIndex
  // Check if there's already a path from recipeIndex to prerequisiteIndex
  // If so, adding this edge would create a cycle

  std::unordered_set<int> visited;
  std::queue<int> toVisit;
  toVisit.push(recipeIndex);
  visited.insert(recipeIndex);

  while (!toVisit.empty()) {
    int current = toVisit.front();
    toVisit.pop();

    auto it = m_nodes.find(current);
    if (it == m_nodes.end()) continue;

    for (int successor : it->second->successors) {
      if (successor == prerequisiteIndex) {
        // Found path from recipeIndex to prerequisiteIndex
        return true;
      }

      if (visited.find(successor) == visited.end()) {
        visited.insert(successor);
        toVisit.push(successor);
      }
    }
  }

  return false;
}

bool RecipeExecutionStateMachine::getTopologicalSort(std::vector<int>& sortedIndices) const
{
  sortedIndices.clear();

  // Kahn's algorithm for topological sorting
  std::unordered_map<int, int> inDegree;

  // Initialize in-degrees
  for (const auto& [index, node] : m_nodes) {
    inDegree[index] = static_cast<int>(node->predecessors.size());
  }

  // Queue of nodes with no incoming edges
  std::queue<int> zeroInDegree;
  for (const auto& [index, degree] : inDegree) {
    if (degree == 0) {
      zeroInDegree.push(index);
    }
  }

  // Process nodes
  while (!zeroInDegree.empty()) {
    int current = zeroInDegree.front();
    zeroInDegree.pop();
    sortedIndices.push_back(current);

    // Reduce in-degree of successors
    auto it = m_nodes.find(current);
    if (it != m_nodes.end()) {
      for (int successor : it->second->successors) {
        inDegree[successor]--;
        if (inDegree[successor] == 0) {
          zeroInDegree.push(successor);
        }
      }
    }
  }

  // If not all nodes are in sorted list, there's a cycle
  if (static_cast<int>(sortedIndices.size()) != m_recipeCount) {
    return false;
  }

  return true;
}

// ============================================================================
// EXECUTION STATE MANAGEMENT
// ============================================================================

void RecipeExecutionStateMachine::resetExecution()
{
  for (auto& [index, node] : m_nodes) {
    if (node->state != RecipeExecutionState::Disabled) {
      node->state = RecipeExecutionState::Pending;
    }
    node->success = false;
    node->errorMessage.clear();
  }

  updateReadyStates();

  std::cout << "[RecipeStateMachine] Execution state reset" << std::endl;
}

void RecipeExecutionStateMachine::resetRecipeState(int recipeIndex)
{
  auto it = m_nodes.find(recipeIndex);
  if (it == m_nodes.end()) return;

  auto& node = it->second;

  // Only reset if not disabled
  if (node->state != RecipeExecutionState::Disabled) {
    node->state = RecipeExecutionState::Pending;
    node->success = false;
    node->errorMessage.clear();
    node->skippedDueToCondition = false;
  }

  // Update ready states after reset
  updateReadyStates();
}

void RecipeExecutionStateMachine::updateReadyStates()
{
  for (auto& [index, node] : m_nodes) {
    if (node->state == RecipeExecutionState::Pending) {
      if (allPrerequisitesComplete(index)) {
        // Check if should skip due to condition evaluation
        if (shouldSkipDueToCondition(index)) {
          node->state = RecipeExecutionState::Skipped;
          node->skippedDueToCondition = true;
          std::cout << "[RecipeStateMachine] Recipe " << index
                    << " skipped (condition not met)" << std::endl;
          // Propagate skip to dependents
          propagateSkipState(index);
        } else {
          node->state = RecipeExecutionState::Ready;
          std::cout << "[RecipeStateMachine] Recipe " << index << " is now ready" << std::endl;
        }
      }
    }
  }
}

bool RecipeExecutionStateMachine::shouldSkipDueToCondition(int recipeIndex) const
{
  if (!m_executionContext) {
    // No context = no conditions to evaluate = proceed
    return false;
  }

  auto it = m_nodes.find(recipeIndex);
  if (it == m_nodes.end()) return false;

  ConditionExpressionEvaluator evaluator;

  for (const auto& pred : it->second->predecessors) {
    // Only evaluate conditions from completed predecessors
    auto predIt = m_nodes.find(pred.index);
    if (predIt == m_nodes.end()) continue;
    if (predIt->second->state != RecipeExecutionState::Completed) continue;

    // If condition is empty, it's always satisfied
    if (pred.condition.empty()) continue;

    // Evaluate condition
    std::string errorMsg;
    bool conditionResult = evaluator.Evaluate(pred.condition, m_executionContext, errorMsg);

    if (!errorMsg.empty()) {
      std::cerr << "[RecipeStateMachine] Condition evaluation error for recipe " << recipeIndex
                << " (predecessor " << pred.index << "): " << errorMsg << std::endl;
      // On error, skip the recipe for safety
      return true;
    }

    if (!conditionResult) {
      // Condition not met - should skip
      std::cout << "[RecipeStateMachine] Condition '" << pred.condition
                << "' not met for recipe " << recipeIndex
                << " (predecessor " << pred.index << ")" << std::endl;
      return true;
    } else {
      std::cout << "[RecipeStateMachine] Condition '" << pred.condition
                << "' met for recipe " << recipeIndex << std::endl;
    }
  }

  return false;
}

std::vector<int> RecipeExecutionStateMachine::getReadyRecipes() const
{
  std::vector<int> readyRecipes;

  for (const auto& [index, node] : m_nodes) {
    if (node->state == RecipeExecutionState::Ready) {
      readyRecipes.push_back(index);
    }
  }

  return readyRecipes;
}

void RecipeExecutionStateMachine::markRecipeStarted(int recipeIndex)
{
  if (recipeIndex < 0 || recipeIndex >= m_recipeCount) {
    return;
  }

  m_nodes[recipeIndex]->state = RecipeExecutionState::Running;
  std::cout << "[RecipeStateMachine] Recipe " << recipeIndex << " started" << std::endl;
}

void RecipeExecutionStateMachine::markRecipeCompleted(int recipeIndex, bool success, const std::string& errorMessage)
{
  if (recipeIndex < 0 || recipeIndex >= m_recipeCount) {
    return;
  }

  auto& node = m_nodes[recipeIndex];
  node->success = success;
  node->errorMessage = errorMessage;

  if (success) {
    node->state = RecipeExecutionState::Completed;
    std::cout << "[RecipeStateMachine] Recipe " << recipeIndex << " completed successfully" << std::endl;

    // Update ready states of dependent recipes
    updateReadyStates();
  }
  else {
    node->state = RecipeExecutionState::Failed;
    std::cout << "[RecipeStateMachine] Recipe " << recipeIndex << " failed: " << errorMessage << std::endl;

    // Propagate skip state to all dependent recipes
    propagateSkipState(recipeIndex);
  }
}

void RecipeExecutionStateMachine::propagateSkipState(int recipeIndex)
{
  // Mark all recipes that depend on this one as skipped
  std::queue<int> toProcess;
  toProcess.push(recipeIndex);
  std::unordered_set<int> visited;
  visited.insert(recipeIndex);

  while (!toProcess.empty()) {
    int current = toProcess.front();
    toProcess.pop();

    auto it = m_nodes.find(current);
    if (it == m_nodes.end()) continue;

    for (int successor : it->second->successors) {
      if (visited.find(successor) == visited.end()) {
        visited.insert(successor);

        auto& successorNode = m_nodes[successor];
        if (successorNode->state != RecipeExecutionState::Completed &&
            successorNode->state != RecipeExecutionState::Running) {
          successorNode->state = RecipeExecutionState::Skipped;
          std::cout << "[RecipeStateMachine] Recipe " << successor << " skipped due to failed prerequisite" << std::endl;
          toProcess.push(successor);
        }
      }
    }
  }
}

bool RecipeExecutionStateMachine::allPrerequisitesComplete(int recipeIndex) const
{
  auto it = m_nodes.find(recipeIndex);
  if (it == m_nodes.end()) return false;

  for (const auto& pred : it->second->predecessors) {
    auto predIt = m_nodes.find(pred.index);
    if (predIt == m_nodes.end()) return false;

    if (predIt->second->state != RecipeExecutionState::Completed) {
      return false;
    }

    // Also check if predecessor was successful
    if (!predIt->second->success) {
      return false;
    }
  }

  return true;
}

bool RecipeExecutionStateMachine::anyPrerequisiteFailed(int recipeIndex) const
{
  auto it = m_nodes.find(recipeIndex);
  if (it == m_nodes.end()) return false;

  for (const auto& pred : it->second->predecessors) {
    auto predIt = m_nodes.find(pred.index);
    if (predIt == m_nodes.end()) continue;

    if (predIt->second->state == RecipeExecutionState::Failed) {
      return true;
    }
  }

  return false;
}

bool RecipeExecutionStateMachine::isExecutionComplete() const
{
  for (const auto& [index, node] : m_nodes) {
    if (node->state != RecipeExecutionState::Completed &&
        node->state != RecipeExecutionState::Failed &&
        node->state != RecipeExecutionState::Skipped &&
        node->state != RecipeExecutionState::Disabled) {
      return false;
    }
  }

  return true;
}

void RecipeExecutionStateMachine::getStateCounts(int& outPending, int& outReady, int& outRunning,
                                                  int& outCompleted, int& outFailed, int& outSkipped) const
{
  outPending = 0;
  outReady = 0;
  outRunning = 0;
  outCompleted = 0;
  outFailed = 0;
  outSkipped = 0;

  for (const auto& [index, node] : m_nodes) {
    switch (node->state) {
      case RecipeExecutionState::Pending: outPending++; break;
      case RecipeExecutionState::Ready: outReady++; break;
      case RecipeExecutionState::Running: outRunning++; break;
      case RecipeExecutionState::Completed: outCompleted++; break;
      case RecipeExecutionState::Failed: outFailed++; break;
      case RecipeExecutionState::Skipped: outSkipped++; break;
      case RecipeExecutionState::Disabled: break;  // Not counted
    }
  }
}

// ============================================================================
// QUERY FUNCTIONS
// ============================================================================

RecipeExecutionState RecipeExecutionStateMachine::getRecipeState(int recipeIndex) const
{
  auto it = m_nodes.find(recipeIndex);
  if (it == m_nodes.end()) {
    return RecipeExecutionState::Pending;
  }

  return it->second->state;
}

const RecipeNode* RecipeExecutionStateMachine::getRecipeNode(int recipeIndex) const
{
  auto it = m_nodes.find(recipeIndex);
  if (it == m_nodes.end()) {
    return nullptr;
  }

  return it->second.get();
}

std::vector<int> RecipeExecutionStateMachine::getPredecessors(int recipeIndex) const
{
  auto it = m_nodes.find(recipeIndex);
  if (it == m_nodes.end()) {
    return {};
  }

  // Return just the indices (for backward compatibility)
  std::vector<int> indices;
  for (const auto& pred : it->second->predecessors) {
    indices.push_back(pred.index);
  }
  return indices;
}

std::vector<int> RecipeExecutionStateMachine::getSuccessors(int recipeIndex) const
{
  auto it = m_nodes.find(recipeIndex);
  if (it == m_nodes.end()) {
    return {};
  }

  return it->second->successors;
}

bool RecipeExecutionStateMachine::hasDependencies(int recipeIndex) const
{
  auto it = m_nodes.find(recipeIndex);
  if (it == m_nodes.end()) {
    return false;
  }

  return !it->second->predecessors.empty();
}

bool RecipeExecutionStateMachine::hasDependents(int recipeIndex) const
{
  auto it = m_nodes.find(recipeIndex);
  if (it == m_nodes.end()) {
    return false;
  }

  return !it->second->successors.empty();
}

std::string RecipeExecutionStateMachine::getExecutionSummary() const
{
  int pending, ready, running, completed, failed, skipped;
  getStateCounts(pending, ready, running, completed, failed, skipped);

  std::ostringstream oss;
  oss << "Execution State: ";
  oss << "Pending=" << pending << ", ";
  oss << "Ready=" << ready << ", ";
  oss << "Running=" << running << ", ";
  oss << "Completed=" << completed << ", ";
  oss << "Failed=" << failed << ", ";
  oss << "Skipped=" << skipped;

  return oss.str();
}

// ============================================================================
// SERIALIZATION
// ============================================================================

std::unordered_map<int, std::vector<int>> RecipeExecutionStateMachine::exportDependencies() const
{
  std::unordered_map<int, std::vector<int>> result;

  for (const auto& [index, node] : m_nodes) {
    if (!node->predecessors.empty()) {
      std::vector<int> indices;
      for (const auto& pred : node->predecessors) {
        indices.push_back(pred.index);
      }
      result[index] = indices;
    }
  }

  return result;
}

bool RecipeExecutionStateMachine::importDependencies(const std::unordered_map<int, std::vector<int>>& dependencies)
{
  // Clear existing dependencies
  clearAllDependencies();

  // Add each dependency
  for (const auto& [recipeIndex, predecessors] : dependencies) {
    if (recipeIndex < 0 || recipeIndex >= m_recipeCount) {
      std::cerr << "[RecipeStateMachine] Invalid recipe index in import: " << recipeIndex << std::endl;
      continue;
    }

    for (int predIndex : predecessors) {
      if (!addDependency(recipeIndex, predIndex)) {
        std::cerr << "[RecipeStateMachine] Failed to add dependency: "
                  << recipeIndex << " <- " << predIndex << std::endl;
        return false;
      }
    }
  }

  std::cout << "[RecipeStateMachine] Imported " << dependencies.size() << " dependency relationships" << std::endl;
  return true;
}
