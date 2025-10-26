// RecipeManager.h
#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>

/**
 * @brief Process instance - represents one step in the recipe
 */
struct ProcessInstance {
  std::string uniqueId;           // Auto-generated unique ID (e.g., "pick_001")
  std::string processType;        // Template type: "Pick", "Place", "PickAndPlace"
  std::string userNickname;       // User-friendly name (e.g., "Pick Left Lens")
  bool enabled;                   // Can disable step without deleting

  // Process parameters (editable)
  std::map<std::string, std::string> parameters;

  ProcessInstance(const std::string& type, const std::string& nickname, const std::string& id)
    : uniqueId(id), processType(type), userNickname(nickname), enabled(true) {
  }
};

/**
 * @brief Recipe - collection of process steps
 */
struct Recipe {
  std::string name;
  std::string version;
  std::vector<std::unique_ptr<ProcessInstance>> steps;
  int nextIdCounter;
  std::string productFile;  // ✅ Added

  Recipe() : name("Untitled Recipe"), version("1.0"), nextIdCounter(1), productFile("") {}
};

/**
 * @brief RecipeManager - Manages recipe data and operations
 *
 * Handles:
 * - Adding/removing/reordering steps
 * - Loading/saving recipes from/to files
 * - Generating unique IDs
 * - Providing default parameters
 */
class RecipeManager
{
public:
  RecipeManager();
  ~RecipeManager();

  // Recipe operations
  void newRecipe();
  bool loadRecipe(const std::string& filename);
  bool saveRecipe(const std::string& filename);

  // Recipe info
  std::string getRecipeName() const { return m_currentRecipe.name; }
  void setRecipeName(const std::string& name) { m_currentRecipe.name = name; }
  std::string getCurrentFilePath() const { return m_currentFilePath; }
  int getStepCount() const { return static_cast<int>(m_currentRecipe.steps.size()); }

  // Step management
  void addStep(const std::string& processType);
  void removeStep(int index);
  bool moveStepUp(int index);
  bool moveStepDown(int index);

  // Step access
  ProcessInstance* getStep(int index);
  const ProcessInstance* getStep(int index) const;
  const std::vector<std::unique_ptr<ProcessInstance>>& getAllSteps() const {
    return m_currentRecipe.steps;
  }

  // Step properties
  void setStepNickname(int index, const std::string& nickname);
  void setStepEnabled(int index, bool enabled);
  void setStepParameter(int index, const std::string& key, const std::string& value);

  // Template info
  static std::vector<std::string> getAvailableProcessTypes();
  static std::map<std::string, std::string> getDefaultParameters(const std::string& processType);

  // Category info (NEW)
  static std::vector<std::string> getProcessCategories();
  static std::string getProcessCategory(const std::string& processType);
  static std::vector<std::string> getProcessesByCategory(const std::string& category);

  // Validation
  bool validateRecipe(std::vector<std::string>& errors) const;

  // Indentation/hierarchy (NEW)
  int getStepIndentLevel(int index) const;
  static bool isBlockStart(const std::string& processType);
  static bool isBlockEnd(const std::string& processType);

  // ✅ Product file management
  std::string getProductFile() const { return m_currentRecipe.productFile; }
  void setProductFile(const std::string& file) { m_currentRecipe.productFile = file; }

private:
  Recipe m_currentRecipe;
  std::string m_currentFilePath;

  std::string generateUniqueId(const std::string& processType);

  // JSON serialization
  bool saveToJson(const std::string& filename) const;
  bool loadFromJson(const std::string& filename);
};