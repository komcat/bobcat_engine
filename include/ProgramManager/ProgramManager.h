// ProgramManager.h
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>

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
 * @brief RecipeReference - Points to a recipe file
 */
struct RecipeReference {
  std::string recipeFilePath;        // Path to recipe JSON file
  std::string displayName;           // Name shown in UI (extracted from recipe or filename)
  bool enabled;                      // Can disable recipe without removing it
  bool includeInLoop;                // Include this recipe in infinite loop execution
  std::vector<DependencyLink> dependencies; // Dependencies with optional conditions

  RecipeReference(const std::string& path, const std::string& name, bool enable = true)
    : recipeFilePath(path), displayName(name), enabled(enable), includeInLoop(false) {
  }
};

/**
 * @brief Program - Collection of recipes to execute sequentially
 */
struct Program {
  std::string name;
  std::string version;
  std::vector<std::unique_ptr<RecipeReference>> recipes;

  Program() : name("Untitled Program"), version("1.0") {}
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
  int getRecipeCount() const { return static_cast<int>(m_currentProgram.recipes.size()); }

  // Recipe management
  void addRecipe(const std::string& recipeFilePath, const std::string& displayName);
  void removeRecipe(int index);
  bool moveRecipeUp(int index);
  bool moveRecipeDown(int index);

  // Recipe access
  RecipeReference* getRecipe(int index);
  const RecipeReference* getRecipe(int index) const;
  const std::vector<std::unique_ptr<RecipeReference>>& getAllRecipes() const {
    return m_currentProgram.recipes;
  }

  // Recipe properties
  void setRecipeEnabled(int index, bool enabled);
  void setRecipeDisplayName(int index, const std::string& name);
  void setRecipeIncludeInLoop(int index, bool include);

  // Loop configuration
  std::vector<int> getLoopRecipeIndices() const;  // Get indices of recipes marked for loop
  std::vector<int> getNonLoopRecipeIndices() const;  // Get indices of recipes not in loop
  bool hasLoopRecipes() const;  // Check if any recipes are marked for loop

  // Dependency management
  bool addRecipeDependency(int recipeIndex, int prerequisiteIndex,
                           const std::string& condition = "");
  void removeRecipeDependency(int recipeIndex, int prerequisiteIndex);
  void clearRecipeDependencies(int recipeIndex);
  std::vector<DependencyLink> getRecipeDependencies(int recipeIndex) const;
  bool hasRecipeDependency(int recipeIndex, int prerequisiteIndex) const;

  // Condition management
  void setDependencyCondition(int recipeIndex, int prerequisiteIndex,
                              const std::string& condition);
  std::string getDependencyCondition(int recipeIndex, int prerequisiteIndex) const;

  // Helper: get just predecessor indices (for backward compatibility)
  std::vector<int> getRecipePredecessorIndices(int recipeIndex) const;

  // Validation
  bool validateProgram(std::vector<std::string>& errors) const;
  bool validateDependencies(std::vector<std::string>& errors) const;

  // Graph node position management (for dependency graph visual layout)
  void setNodePosition(int recipeIndex, double x, double y);
  GraphNodePosition getNodePosition(int recipeIndex) const;
  bool hasNodePosition(int recipeIndex) const;
  void clearNodePositions();
  const std::map<int, GraphNodePosition>& getAllNodePositions() const { return m_graphPositions; }

private:
  Program m_currentProgram;
  std::string m_currentFilePath;
  std::map<int, GraphNodePosition> m_graphPositions;  // Recipe index -> visual position

  // JSON serialization
  bool saveToJson(const std::string& filename) const;
  bool loadFromJson(const std::string& filename);
};