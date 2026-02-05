// ProgramManager.h
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>

// Maximum number of dependency sets per program
static constexpr int MAX_DEPENDENCY_SETS = 5;

// Maximum number of recipe slots per program
static constexpr int MAX_RECIPE_SLOTS = 5;

/**
 * @brief GraphNodePosition - Position of a node in the dependency graph
 */
struct GraphNodePosition {
  double x;
  double y;
  bool hasPosition;  // true if user has manually positioned this node

  GraphNodePosition() : x(0), y(0), hasPosition(false) {}
  GraphNodePosition(double px, double py) : x(px), y(py), hasPosition(true) {}
};

/**
 * @brief DependencyLink - A dependency with optional condition
 *
 * Each dependency can have a condition expression that is evaluated
 * against the SequenceContext when the prerequisite recipe completes.
 * If the condition evaluates to false, the dependent recipe is skipped.
 */
struct DependencyLink {
  int predecessorIndex;      // Recipe index that must complete before this
  std::string condition;     // Condition expression (empty = always proceed)

  DependencyLink(int predIdx, const std::string& cond = "")
    : predecessorIndex(predIdx), condition(cond) {}
};

/**
 * @brief DependencySet - A named collection of dependencies
 *
 * Allows multiple dependency configurations per program.
 * One set is marked as "active" (POR - Plan of Record) for actual execution,
 * while others serve as test configurations.
 */
struct DependencySet {
  std::string name;                                      // "Default", "Test Config 1", etc.
  std::map<int, std::vector<DependencyLink>> dependencies;  // recipeIndex -> links

  DependencySet(const std::string& setName = "Default") : name(setName) {}
};

/**
 * @brief RecipeReference - Points to a recipe file
 */
struct RecipeReference {
  std::string recipeFilePath;        // Path to recipe JSON file
  std::string displayName;           // Name shown in UI (extracted from recipe or filename)
  bool enabled;                      // Can disable recipe without removing it
  bool includeInLoop;                // Include this recipe in infinite loop execution
  // NOTE: Dependencies moved to DependencySet for multi-set support

  RecipeReference(const std::string& path, const std::string& name, bool enable = true)
    : recipeFilePath(path), displayName(name), enabled(enable), includeInLoop(false) {
  }
};

/**
 * @brief RecipeSlot - A named container for recipes with its own dependencies
 *
 * Each slot has its own:
 * - Name (e.g., "Production Slot", "Helper Slot", "Initial Slot")
 * - List of recipes
 * - Dependency sets (per-slot dependencies)
 * - Graph node positions (per-slot graph layout)
 *
 * Use cases: Production, Troubleshooting, Initial Setup, Helper sequences
 */
struct RecipeSlot {
  std::string name;
  std::vector<std::unique_ptr<RecipeReference>> recipes;
  std::vector<DependencySet> dependencySets;  // Per-slot dependencies
  int activeDependencySetIndex = 0;
  std::map<int, GraphNodePosition> graphPositions;  // Per-slot graph layout

  RecipeSlot(const std::string& slotName = "Slot 1") : name(slotName), activeDependencySetIndex(0) {
    dependencySets.emplace_back("Default");
  }

  // Move constructor and assignment
  RecipeSlot(RecipeSlot&&) = default;
  RecipeSlot& operator=(RecipeSlot&&) = default;

  // Delete copy constructor and assignment (unique_ptr)
  RecipeSlot(const RecipeSlot&) = delete;
  RecipeSlot& operator=(const RecipeSlot&) = delete;
};

/**
 * @brief Program - Collection of recipe slots to execute
 *
 * A program contains up to MAX_RECIPE_SLOTS (5) slots, where each slot has its own
 * recipes, dependencies, and graph layout. Only the active slot is executed.
 */
struct Program {
  std::string name;
  std::string version;

  // Slot System - replaces flat recipes list
  std::vector<RecipeSlot> recipeSlots;
  int activeSlotIndex = 0;  // Currently selected slot for execution

  Program() : name("Untitled Program"), version("1.1"), activeSlotIndex(0) {
    // Initialize with one default slot
    recipeSlots.emplace_back("Slot 1");
  }
};

/**
 * @brief ProgramManager - Manages program data and operations
 *
 * Similar to RecipeManager but manages collections of recipes
 */
class ProgramManager
{
public:
  ProgramManager();
  ~ProgramManager();

  // Program operations
  void newProgram();
  bool loadProgram(const std::string& filename);
  bool saveProgram(const std::string& filename);

  // Program info
  std::string getProgramName() const { return m_currentProgram.name; }
  void setProgramName(const std::string& name) { m_currentProgram.name = name; }
  std::string getCurrentFilePath() const { return m_currentFilePath; }
  int getRecipeCount() const;  // Returns recipe count in active slot

  // ============================================================================
  // RECIPE SLOT MANAGEMENT
  // ============================================================================

  // Create a new slot (returns index, -1 if max reached)
  int createSlot(const std::string& name = "");

  // Delete a slot (cannot delete last slot)
  bool deleteSlot(int slotIndex);

  // Rename a slot
  bool renameSlot(int slotIndex, const std::string& name);

  // Duplicate an existing slot
  int duplicateSlot(int sourceIndex, const std::string& name = "");

  // Get the number of slots
  int getSlotCount() const;

  // Get the name of a slot
  std::string getSlotName(int slotIndex) const;

  // Set the active slot
  bool setActiveSlot(int slotIndex);

  // Get the index of the active slot
  int getActiveSlotIndex() const;

  // Get a slot by index (non-const)
  RecipeSlot* getSlot(int slotIndex);

  // Get a slot by index (const)
  const RecipeSlot* getSlot(int slotIndex) const;

  // Get all slot names (for populating dropdown)
  std::vector<std::string> getSlotNames() const;

  // Move a recipe from current slot to another slot
  bool moveRecipeToSlot(int recipeIndex, int targetSlotIndex);

  // Recipe management
  void addRecipe(const std::string& recipeFilePath, const std::string& displayName);
  void removeRecipe(int index);
  bool moveRecipeUp(int index);
  bool moveRecipeDown(int index);

  // Recipe access (operates on active slot)
  RecipeReference* getRecipe(int index);
  const RecipeReference* getRecipe(int index) const;
  const std::vector<std::unique_ptr<RecipeReference>>& getAllRecipes() const;

  // Recipe properties
  void setRecipeEnabled(int index, bool enabled);
  void setRecipeDisplayName(int index, const std::string& name);
  void setRecipeIncludeInLoop(int index, bool include);

  // Loop configuration
  std::vector<int> getLoopRecipeIndices() const;  // Get indices of recipes marked for loop
  std::vector<int> getNonLoopRecipeIndices() const;  // Get indices of recipes not in loop
  bool hasLoopRecipes() const;  // Check if any recipes are marked for loop

  // ============================================================================
  // DEPENDENCY SET MANAGEMENT
  // ============================================================================

  // Create a new dependency set (returns index, -1 if max reached)
  int createDependencySet(const std::string& name);

  // Delete a dependency set (cannot delete the active/POR set)
  bool deleteDependencySet(int setIndex);

  // Rename a dependency set
  bool renameDependencySet(int setIndex, const std::string& name);

  // Duplicate an existing dependency set
  int duplicateDependencySet(int sourceIndex, const std::string& name);

  // Get the number of dependency sets
  int getDependencySetCount() const;

  // Get the name of a dependency set
  std::string getDependencySetName(int setIndex) const;

  // Set the active (POR) dependency set
  bool setActiveDependencySet(int setIndex);

  // Get the index of the active (POR) dependency set
  int getActiveDependencySetIndex() const;

  // ============================================================================
  // DEPENDENCY MANAGEMENT (with set index)
  // ============================================================================

  // Add dependency to a specific set
  bool addRecipeDependency(int setIndex, int recipeIndex, int prerequisiteIndex,
                           const std::string& condition = "");

  // Remove dependency from a specific set
  void removeRecipeDependency(int setIndex, int recipeIndex, int prerequisiteIndex);

  // Clear all dependencies for a recipe in a specific set
  void clearRecipeDependencies(int setIndex, int recipeIndex);

  // Get dependencies for a recipe in a specific set
  std::vector<DependencyLink> getRecipeDependencies(int setIndex, int recipeIndex) const;

  // Check if dependency exists in a specific set
  bool hasRecipeDependency(int setIndex, int recipeIndex, int prerequisiteIndex) const;

  // Set condition for a dependency in a specific set
  void setDependencyCondition(int setIndex, int recipeIndex, int prerequisiteIndex,
                              const std::string& condition);

  // Get condition for a dependency in a specific set
  std::string getDependencyCondition(int setIndex, int recipeIndex, int prerequisiteIndex) const;

  // ============================================================================
  // BACKWARD-COMPATIBLE DEPENDENCY METHODS (use active set)
  // ============================================================================

  // Add dependency to active set
  bool addRecipeDependency(int recipeIndex, int prerequisiteIndex,
                           const std::string& condition = "");

  // Remove dependency from active set
  void removeRecipeDependency(int recipeIndex, int prerequisiteIndex);

  // Clear all dependencies in active set
  void clearRecipeDependencies(int recipeIndex);

  // Get dependencies from active set
  std::vector<DependencyLink> getRecipeDependencies(int recipeIndex) const;

  // Check if dependency exists in active set
  bool hasRecipeDependency(int recipeIndex, int prerequisiteIndex) const;

  // Condition management for active set
  void setDependencyCondition(int recipeIndex, int prerequisiteIndex,
                              const std::string& condition);
  std::string getDependencyCondition(int recipeIndex, int prerequisiteIndex) const;

  // Helper: get just predecessor indices (for backward compatibility)
  std::vector<int> getRecipePredecessorIndices(int recipeIndex) const;

  // Validation
  bool validateProgram(std::vector<std::string>& errors) const;
  bool validateDependencies(std::vector<std::string>& errors) const;

  // Graph node position management (for dependency graph visual layout)
  // Note: Positions are now stored per-slot in RecipeSlot::graphPositions
  void setNodePosition(int recipeIndex, double x, double y);
  GraphNodePosition getNodePosition(int recipeIndex) const;
  bool hasNodePosition(int recipeIndex) const;
  void clearNodePositions();
  const std::map<int, GraphNodePosition>& getAllNodePositions() const;

private:
  Program m_currentProgram;
  std::string m_currentFilePath;

  // JSON serialization
  bool saveToJson(const std::string& filename) const;
  bool loadFromJson(const std::string& filename);
};