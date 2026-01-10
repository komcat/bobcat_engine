// ProgramManager.cpp
#include "ProgramManager.h"
#include <iostream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>

ProgramManager::ProgramManager()
  : m_currentFilePath("")
{
  std::cout << "[ProgramManager] Created" << std::endl;
}

ProgramManager::~ProgramManager()
{
  std::cout << "[ProgramManager] Destroyed" << std::endl;
}

void ProgramManager::newProgram()
{
  m_currentProgram = Program();
  m_currentFilePath.clear();
  std::cout << "[ProgramManager] New program created" << std::endl;
}

void ProgramManager::addRecipe(const std::string& recipeFilePath, const std::string& displayName)
{
  auto recipeRef = std::make_unique<RecipeReference>(recipeFilePath, displayName);
  m_currentProgram.recipes.push_back(std::move(recipeRef));

  std::cout << "[ProgramManager] Added recipe: " << displayName
    << " (" << recipeFilePath << ")" << std::endl;
}

void ProgramManager::removeRecipe(int index)
{
  if (index < 0 || index >= static_cast<int>(m_currentProgram.recipes.size())) {
    std::cerr << "[ProgramManager] Invalid index for removeRecipe: " << index << std::endl;
    return;
  }

  std::string removedName = m_currentProgram.recipes[index]->displayName;
  m_currentProgram.recipes.erase(m_currentProgram.recipes.begin() + index);

  std::cout << "[ProgramManager] Removed recipe at index " << index
    << " (" << removedName << ")" << std::endl;
}

bool ProgramManager::moveRecipeUp(int index)
{
  if (index <= 0 || index >= static_cast<int>(m_currentProgram.recipes.size())) {
    return false;
  }

  std::swap(m_currentProgram.recipes[index], m_currentProgram.recipes[index - 1]);
  std::cout << "[ProgramManager] Moved recipe " << index << " up" << std::endl;
  return true;
}

bool ProgramManager::moveRecipeDown(int index)
{
  if (index < 0 || index >= static_cast<int>(m_currentProgram.recipes.size()) - 1) {
    return false;
  }

  std::swap(m_currentProgram.recipes[index], m_currentProgram.recipes[index + 1]);
  std::cout << "[ProgramManager] Moved recipe " << index << " down" << std::endl;
  return true;
}

RecipeReference* ProgramManager::getRecipe(int index)
{
  if (index < 0 || index >= static_cast<int>(m_currentProgram.recipes.size())) {
    return nullptr;
  }
  return m_currentProgram.recipes[index].get();
}

const RecipeReference* ProgramManager::getRecipe(int index) const
{
  if (index < 0 || index >= static_cast<int>(m_currentProgram.recipes.size())) {
    return nullptr;
  }
  return m_currentProgram.recipes[index].get();
}

void ProgramManager::setRecipeEnabled(int index, bool enabled)
{
  RecipeReference* recipe = getRecipe(index);
  if (recipe) {
    recipe->enabled = enabled;
  }
}

void ProgramManager::setRecipeDisplayName(int index, const std::string& name)
{
  RecipeReference* recipe = getRecipe(index);
  if (recipe) {
    recipe->displayName = name;
  }
}

// ============================================================================
// DEPENDENCY MANAGEMENT
// ============================================================================

bool ProgramManager::addRecipeDependency(int recipeIndex, int prerequisiteIndex,
                                          const std::string& condition)
{
  if (recipeIndex < 0 || recipeIndex >= getRecipeCount() ||
      prerequisiteIndex < 0 || prerequisiteIndex >= getRecipeCount()) {
    std::cerr << "[ProgramManager] Invalid indices for dependency" << std::endl;
    return false;
  }

  if (recipeIndex == prerequisiteIndex) {
    std::cerr << "[ProgramManager] Cannot add self-dependency" << std::endl;
    return false;
  }

  RecipeReference* recipe = getRecipe(recipeIndex);
  if (!recipe) return false;

  // Check if dependency already exists
  auto& deps = recipe->dependencies;
  auto it = std::find_if(deps.begin(), deps.end(),
    [prerequisiteIndex](const DependencyLink& d) {
      return d.predecessorIndex == prerequisiteIndex;
    });

  if (it != deps.end()) {
    // Update condition on existing dependency
    it->condition = condition;
    std::cout << "[ProgramManager] Updated dependency condition: Recipe " << recipeIndex
              << " depends on Recipe " << prerequisiteIndex
              << " with condition: " << (condition.empty() ? "(none)" : condition) << std::endl;
    return true;
  }

  // Add new dependency
  deps.emplace_back(prerequisiteIndex, condition);

  std::cout << "[ProgramManager] Added dependency: Recipe " << recipeIndex
            << " depends on Recipe " << prerequisiteIndex
            << " with condition: " << (condition.empty() ? "(none)" : condition) << std::endl;
  return true;
}

void ProgramManager::removeRecipeDependency(int recipeIndex, int prerequisiteIndex)
{
  RecipeReference* recipe = getRecipe(recipeIndex);
  if (!recipe) return;

  auto& deps = recipe->dependencies;
  deps.erase(
    std::remove_if(deps.begin(), deps.end(),
      [prerequisiteIndex](const DependencyLink& d) {
        return d.predecessorIndex == prerequisiteIndex;
      }),
    deps.end());

  std::cout << "[ProgramManager] Removed dependency: Recipe " << recipeIndex
            << " no longer depends on Recipe " << prerequisiteIndex << std::endl;
}

void ProgramManager::clearRecipeDependencies(int recipeIndex)
{
  RecipeReference* recipe = getRecipe(recipeIndex);
  if (!recipe) return;

  recipe->dependencies.clear();
  std::cout << "[ProgramManager] Cleared all dependencies for Recipe " << recipeIndex << std::endl;
}

std::vector<DependencyLink> ProgramManager::getRecipeDependencies(int recipeIndex) const
{
  const RecipeReference* recipe = getRecipe(recipeIndex);
  if (!recipe) return {};
  return recipe->dependencies;
}

bool ProgramManager::hasRecipeDependency(int recipeIndex, int prerequisiteIndex) const
{
  const RecipeReference* recipe = getRecipe(recipeIndex);
  if (!recipe) return false;

  const auto& deps = recipe->dependencies;
  return std::find_if(deps.begin(), deps.end(),
    [prerequisiteIndex](const DependencyLink& d) {
      return d.predecessorIndex == prerequisiteIndex;
    }) != deps.end();
}

void ProgramManager::setDependencyCondition(int recipeIndex, int prerequisiteIndex,
                                             const std::string& condition)
{
  RecipeReference* recipe = getRecipe(recipeIndex);
  if (!recipe) return;

  auto& deps = recipe->dependencies;
  auto it = std::find_if(deps.begin(), deps.end(),
    [prerequisiteIndex](const DependencyLink& d) {
      return d.predecessorIndex == prerequisiteIndex;
    });

  if (it != deps.end()) {
    it->condition = condition;
    std::cout << "[ProgramManager] Set condition for dependency: Recipe " << recipeIndex
              << " -> Recipe " << prerequisiteIndex
              << " condition: " << (condition.empty() ? "(none)" : condition) << std::endl;
  }
}

std::string ProgramManager::getDependencyCondition(int recipeIndex, int prerequisiteIndex) const
{
  const RecipeReference* recipe = getRecipe(recipeIndex);
  if (!recipe) return "";

  const auto& deps = recipe->dependencies;
  auto it = std::find_if(deps.begin(), deps.end(),
    [prerequisiteIndex](const DependencyLink& d) {
      return d.predecessorIndex == prerequisiteIndex;
    });

  if (it != deps.end()) {
    return it->condition;
  }
  return "";
}

std::vector<int> ProgramManager::getRecipePredecessorIndices(int recipeIndex) const
{
  const RecipeReference* recipe = getRecipe(recipeIndex);
  if (!recipe) return {};

  std::vector<int> indices;
  for (const auto& dep : recipe->dependencies) {
    indices.push_back(dep.predecessorIndex);
  }
  return indices;
}

bool ProgramManager::validateDependencies(std::vector<std::string>& errors) const
{
  errors.clear();

  // Check that all dependency indices are valid
  for (size_t i = 0; i < m_currentProgram.recipes.size(); i++) {
    const auto& recipe = m_currentProgram.recipes[i];

    for (const auto& dep : recipe->dependencies) {
      if (dep.predecessorIndex < 0 ||
          dep.predecessorIndex >= static_cast<int>(m_currentProgram.recipes.size())) {
        errors.push_back("Recipe " + std::to_string(i) +
                         " has invalid dependency index: " + std::to_string(dep.predecessorIndex));
      }
    }
  }

  // TODO: Add cycle detection using RecipeExecutionStateMachine

  return errors.empty();
}

bool ProgramManager::validateProgram(std::vector<std::string>& errors) const
{
  errors.clear();

  if (m_currentProgram.recipes.empty()) {
    errors.push_back("Program has no recipes");
  }

  // Check if recipe files exist
  for (size_t i = 0; i < m_currentProgram.recipes.size(); i++) {
    const auto& recipe = m_currentProgram.recipes[i];
    QFileInfo fileInfo(QString::fromStdString(recipe->recipeFilePath));

    if (!fileInfo.exists()) {
      errors.push_back("Recipe file not found: " + recipe->recipeFilePath);
    }
  }

  // Validate dependencies
  std::vector<std::string> depErrors;
  if (!validateDependencies(depErrors)) {
    errors.insert(errors.end(), depErrors.begin(), depErrors.end());
  }

  return errors.empty();
}

bool ProgramManager::loadProgram(const std::string& filename)
{
  return loadFromJson(filename);
}

bool ProgramManager::saveProgram(const std::string& filename)
{
  if (saveToJson(filename)) {
    m_currentFilePath = filename;
    return true;
  }
  return false;
}

bool ProgramManager::saveToJson(const std::string& filename) const
{
  QJsonObject root;
  root["version"] = QString::fromStdString(m_currentProgram.version);
  root["program_name"] = QString::fromStdString(m_currentProgram.name);

  QJsonArray recipesArray;
  for (const auto& recipe : m_currentProgram.recipes) {
    QJsonObject recipeObj;
    recipeObj["recipe_file"] = QString::fromStdString(recipe->recipeFilePath);
    recipeObj["display_name"] = QString::fromStdString(recipe->displayName);
    recipeObj["enabled"] = recipe->enabled;

    // Save dependencies with conditions (new format)
    if (!recipe->dependencies.empty()) {
      QJsonArray dependenciesArray;
      for (const auto& dep : recipe->dependencies) {
        QJsonObject depObj;
        depObj["predecessor_index"] = dep.predecessorIndex;
        if (!dep.condition.empty()) {
          depObj["condition"] = QString::fromStdString(dep.condition);
        }
        dependenciesArray.append(depObj);
      }
      recipeObj["dependencies"] = dependenciesArray;
    }

    recipesArray.append(recipeObj);
  }

  root["recipes"] = recipesArray;

  QJsonDocument doc(root);

  QFile file(QString::fromStdString(filename));
  if (!file.open(QIODevice::WriteOnly)) {
    std::cerr << "[ProgramManager] Failed to write file: " << filename << std::endl;
    return false;
  }

  file.write(doc.toJson(QJsonDocument::Indented));
  file.close();

  std::cout << "[ProgramManager] Saved program '" << m_currentProgram.name
    << "' with " << m_currentProgram.recipes.size() << " recipes to "
    << filename << std::endl;

  return true;
}

bool ProgramManager::loadFromJson(const std::string& filename)
{
  QFile file(QString::fromStdString(filename));
  if (!file.open(QIODevice::ReadOnly)) {
    std::cerr << "[ProgramManager] Failed to open file: " << filename << std::endl;
    return false;
  }

  QByteArray data = file.readAll();
  file.close();

  QJsonDocument doc = QJsonDocument::fromJson(data);
  if (!doc.isObject()) {
    std::cerr << "[ProgramManager] Invalid JSON format" << std::endl;
    return false;
  }

  QJsonObject root = doc.object();

  // Create new program
  Program newProgram;
  newProgram.version = root["version"].toString("1.0").toStdString();
  newProgram.name = root["program_name"].toString("Untitled Program").toStdString();

  QJsonArray recipesArray = root["recipes"].toArray();

  for (const QJsonValue& val : recipesArray) {
    QJsonObject recipeObj = val.toObject();

    std::string filePath = recipeObj["recipe_file"].toString().toStdString();
    std::string displayName = recipeObj["display_name"].toString().toStdString();
    bool enabled = recipeObj["enabled"].toBool(true);

    auto recipeRef = std::make_unique<RecipeReference>(filePath, displayName, enabled);

    // Load dependencies (new format with conditions)
    if (recipeObj.contains("dependencies")) {
      QJsonArray depsArray = recipeObj["dependencies"].toArray();
      for (const QJsonValue& depVal : depsArray) {
        if (depVal.isObject()) {
          QJsonObject depObj = depVal.toObject();
          int predIndex = depObj["predecessor_index"].toInt();
          std::string condition = depObj.value("condition").toString("").toStdString();
          recipeRef->dependencies.emplace_back(predIndex, condition);
        }
      }
    }
    // Legacy format: "predecessors" array of ints (backward compatibility)
    else if (recipeObj.contains("predecessors")) {
      QJsonArray predecessorsArray = recipeObj["predecessors"].toArray();
      for (const QJsonValue& predVal : predecessorsArray) {
        recipeRef->dependencies.emplace_back(predVal.toInt(), "");  // No condition
      }
    }

    newProgram.recipes.push_back(std::move(recipeRef));
  }

  // Replace current program
  m_currentProgram = std::move(newProgram);
  m_currentFilePath = filename;

  std::cout << "[ProgramManager] Loaded program '" << m_currentProgram.name
    << "' with " << m_currentProgram.recipes.size() << " recipes from "
    << filename << std::endl;

  return true;
}