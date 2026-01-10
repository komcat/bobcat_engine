// RecipeExecutionStateMachine.h
// State machine for managing complex recipe execution dependencies
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <functional>

// Forward declarations
class SequenceContext;
class ConditionExpressionEvaluator;

/**
 * @brief Recipe execution states
 */
enum class RecipeExecutionState {
  Pending,      // Recipe is waiting for prerequisites
  Ready,        // All prerequisites met, ready to execute
  Running,      // Currently executing
  Completed,    // Successfully completed
  Failed,       // Execution failed
  Skipped,      // Skipped due to disabled or failed prerequisites
  Disabled      // Recipe is disabled by user
};

/**
 * @brief Predecessor info with optional condition
 */
struct PredecessorInfo {
  int index;               // Predecessor recipe index
  std::string condition;   // Condition expression (empty = always proceed)

  PredecessorInfo(int idx, const std::string& cond = "")
    : index(idx), condition(cond) {}
};

/**
 * @brief Recipe node in the execution graph
 */
struct RecipeNode {
  int recipeIndex;                           // Index in the program's recipe list
  std::string recipeName;                    // Display name
  RecipeExecutionState state;                // Current execution state

  std::vector<PredecessorInfo> predecessors; // Recipe indices that must complete before this (with conditions)
  std::vector<int> successors;               // Recipe indices that depend on this

  // Execution results
  bool success;                              // Execution success flag
  std::string errorMessage;                  // Error message if failed

  // Condition-based skip tracking
  bool skippedDueToCondition;                // True if skipped due to condition evaluation

  RecipeNode(int index, const std::string& name)
    : recipeIndex(index)
    , recipeName(name)
    , state(RecipeExecutionState::Pending)
    , success(false)
    , skippedDueToCondition(false)
  {}
};

/**
 * @brief State Machine for Recipe Execution with Dependencies
 *
 * Features:
 * - DAG-based execution graph (Directed Acyclic Graph)
 * - Support for multiple predecessors (Recipe C requires both A and B)
 * - Support for multiple successors (Recipe A enables both B and C)
 * - Automatic ready state detection when all prerequisites complete
 * - Cycle detection to prevent deadlocks
 * - Execution state tracking
 *
 * Example usage:
 *
 * Recipe dependency graph:
 *   Recipe 1: Initial Machine
 *   Recipe 2: Probe SLED (depends on Recipe 1)
 *   Recipe 3: Pick Left Lens (depends on Recipe 1)
 *   Recipe 4: Pick Right Lens (depends on Recipe 1)
 *   Recipe 5: Final Assembly (depends on Recipes 2, 3, 4)
 *
 * Execution flow:
 * 1. Recipe 1 executes
 * 2. Recipes 2, 3, 4 become ready and can execute in parallel
 * 3. Recipe 5 waits until ALL of 2, 3, 4 complete
 * 4. Recipe 5 executes
 */
class RecipeExecutionStateMachine
{
public:
  RecipeExecutionStateMachine();
  ~RecipeExecutionStateMachine();

  // ============================================================================
  // GRAPH CONSTRUCTION
  // ============================================================================

  /**
   * @brief Initialize the state machine with recipes
   * @param recipeCount Number of recipes in the program
   * @param recipeNames Display names for each recipe
   */
  void initialize(int recipeCount, const std::vector<std::string>& recipeNames);

  /**
   * @brief Add a dependency: recipeIndex depends on prerequisiteIndex
   * @param recipeIndex The recipe that has a dependency
   * @param prerequisiteIndex The recipe that must complete first
   * @param condition Optional condition expression (empty = always proceed)
   * @return True if added successfully, false if it would create a cycle
   */
  bool addDependency(int recipeIndex, int prerequisiteIndex,
                     const std::string& condition = "");

  /**
   * @brief Set the execution context for condition evaluation
   * @param context The SequenceContext to use for evaluating conditions
   */
  void setExecutionContext(SequenceContext* context);

  /**
   * @brief Remove a dependency
   * @param recipeIndex The recipe to remove dependency from
   * @param prerequisiteIndex The prerequisite to remove
   */
  void removeDependency(int recipeIndex, int prerequisiteIndex);

  /**
   * @brief Clear all dependencies for a specific recipe
   * @param recipeIndex Recipe to clear dependencies for
   */
  void clearDependencies(int recipeIndex);

  /**
   * @brief Clear all dependencies in the graph
   */
  void clearAllDependencies();

  /**
   * @brief Set a recipe as disabled
   * @param recipeIndex Recipe to disable
   */
  void setRecipeDisabled(int recipeIndex);

  // ============================================================================
  // VALIDATION
  // ============================================================================

  /**
   * @brief Check if the dependency graph is valid (no cycles)
   * @param errorMessages Output vector for error messages
   * @return True if valid, false if cycles detected
   */
  bool validateGraph(std::vector<std::string>& errorMessages) const;

  /**
   * @brief Check if adding a dependency would create a cycle
   * @param recipeIndex Recipe that would have the dependency
   * @param prerequisiteIndex Potential prerequisite
   * @return True if it would create a cycle
   */
  bool wouldCreateCycle(int recipeIndex, int prerequisiteIndex) const;

  /**
   * @brief Get topological sort order for execution
   * @param sortedIndices Output vector of recipe indices in execution order
   * @return True if successful, false if graph has cycles
   */
  bool getTopologicalSort(std::vector<int>& sortedIndices) const;

  // ============================================================================
  // EXECUTION STATE MANAGEMENT
  // ============================================================================

  /**
   * @brief Reset all recipes to initial state for execution
   */
  void resetExecution();

  /**
   * @brief Get all recipes that are ready to execute
   * @return Vector of recipe indices that have all prerequisites met
   */
  std::vector<int> getReadyRecipes() const;

  /**
   * @brief Mark a recipe as started
   * @param recipeIndex Recipe being executed
   */
  void markRecipeStarted(int recipeIndex);

  /**
   * @brief Mark a recipe as completed
   * @param recipeIndex Recipe that completed
   * @param success Whether execution was successful
   * @param errorMessage Error message if failed (optional)
   */
  void markRecipeCompleted(int recipeIndex, bool success, const std::string& errorMessage = "");

  /**
   * @brief Check if all recipes are in terminal state (completed/failed/skipped)
   * @return True if execution is complete
   */
  bool isExecutionComplete() const;

  /**
   * @brief Get count of recipes in each state
   * @param outPending Count of pending recipes
   * @param outReady Count of ready recipes
   * @param outRunning Count of running recipes
   * @param outCompleted Count of completed recipes
   * @param outFailed Count of failed recipes
   * @param outSkipped Count of skipped recipes
   */
  void getStateCounts(int& outPending, int& outReady, int& outRunning,
                      int& outCompleted, int& outFailed, int& outSkipped) const;

  // ============================================================================
  // QUERY FUNCTIONS
  // ============================================================================

  /**
   * @brief Get current state of a recipe
   * @param recipeIndex Recipe to query
   * @return Current execution state
   */
  RecipeExecutionState getRecipeState(int recipeIndex) const;

  /**
   * @brief Get recipe node information
   * @param recipeIndex Recipe to query
   * @return Const pointer to recipe node (nullptr if invalid index)
   */
  const RecipeNode* getRecipeNode(int recipeIndex) const;

  /**
   * @brief Get predecessors for a recipe
   * @param recipeIndex Recipe to query
   * @return Vector of predecessor indices
   */
  std::vector<int> getPredecessors(int recipeIndex) const;

  /**
   * @brief Get successors for a recipe
   * @param recipeIndex Recipe to query
   * @return Vector of successor indices
   */
  std::vector<int> getSuccessors(int recipeIndex) const;

  /**
   * @brief Check if a recipe has any dependencies
   * @param recipeIndex Recipe to check
   * @return True if recipe has prerequisites
   */
  bool hasDependencies(int recipeIndex) const;

  /**
   * @brief Check if a recipe has dependents
   * @param recipeIndex Recipe to check
   * @return True if other recipes depend on this one
   */
  bool hasDependents(int recipeIndex) const;

  /**
   * @brief Get execution summary string
   * @return String describing current execution state
   */
  std::string getExecutionSummary() const;

  // ============================================================================
  // SERIALIZATION (for saving/loading program files)
  // ============================================================================

  /**
   * @brief Export dependency graph to JSON-compatible structure
   * @return Map of recipe indices to their predecessor lists
   */
  std::unordered_map<int, std::vector<int>> exportDependencies() const;

  /**
   * @brief Import dependency graph from JSON-compatible structure
   * @param dependencies Map of recipe indices to their predecessor lists
   * @return True if import was successful
   */
  bool importDependencies(const std::unordered_map<int, std::vector<int>>& dependencies);

private:
  // Recipe nodes indexed by recipe index
  std::unordered_map<int, std::unique_ptr<RecipeNode>> m_nodes;

  // Total recipe count
  int m_recipeCount;

  // Execution context for condition evaluation
  SequenceContext* m_executionContext;

  // Helper functions
  bool detectCycleFromNode(int startNode,
                           std::unordered_set<int>& visited,
                           std::unordered_set<int>& recursionStack) const;

  void updateReadyStates();
  void propagateSkipState(int recipeIndex);
  bool allPrerequisitesComplete(int recipeIndex) const;
  bool anyPrerequisiteFailed(int recipeIndex) const;

  // Condition evaluation
  bool shouldSkipDueToCondition(int recipeIndex) const;
};

/**
 * @brief Helper function to convert RecipeExecutionState to string
 */
inline std::string recipeStateToString(RecipeExecutionState state) {
  switch (state) {
    case RecipeExecutionState::Pending: return "Pending";
    case RecipeExecutionState::Ready: return "Ready";
    case RecipeExecutionState::Running: return "Running";
    case RecipeExecutionState::Completed: return "Completed";
    case RecipeExecutionState::Failed: return "Failed";
    case RecipeExecutionState::Skipped: return "Skipped";
    case RecipeExecutionState::Disabled: return "Disabled";
    default: return "Unknown";
  }
}
