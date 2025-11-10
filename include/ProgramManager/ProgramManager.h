// ProgramManager.h
#pragma once

#include <string>
#include <vector>
#include <memory>

/**
 * @brief RecipeReference - Points to a recipe file
 */
struct RecipeReference {
  std::string recipeFilePath;        // Path to recipe JSON file
  std::string displayName;           // Name shown in UI (extracted from recipe or filename)
  bool enabled;                      // Can disable recipe without removing it
  std::vector<int> predecessorIndices; // Recipe indices that must complete before this (for dependency graph)

  RecipeReference(const std::string& path, const std::string& name, bool enable = true)
    : recipeFilePath(path), displayName(name), enabled(enable) {
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

  // Dependency management
  bool addRecipeDependency(int recipeIndex, int prerequisiteIndex);
  void removeRecipeDependency(int recipeIndex, int prerequisiteIndex);
  void clearRecipeDependencies(int recipeIndex);
  std::vector<int> getRecipeDependencies(int recipeIndex) const;
  bool hasRecipeDependency(int recipeIndex, int prerequisiteIndex) const;

  // Validation
  bool validateProgram(std::vector<std::string>& errors) const;
  bool validateDependencies(std::vector<std::string>& errors) const;

private:
  Program m_currentProgram;
  std::string m_currentFilePath;

  // JSON serialization
  bool saveToJson(const std::string& filename) const;
  bool loadFromJson(const std::string& filename);
};