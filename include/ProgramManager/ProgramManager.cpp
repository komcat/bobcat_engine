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
  // Program() constructor initializes with one "Default" dependency set
  m_currentFilePath.clear();
  m_graphPositions.clear();
  std::cout << "[ProgramManager] New program created with 1 default dependency set" << std::endl;
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

void ProgramManager::setRecipeIncludeInLoop(int index, bool include)
{
  RecipeReference* recipe = getRecipe(index);
  if (recipe) {
    recipe->includeInLoop = include;
    std::cout << "[ProgramManager] Recipe " << index << " (" << recipe->displayName
              << ") include in loop: " << (include ? "true" : "false") << std::endl;
  }
}

std::vector<int> ProgramManager::getLoopRecipeIndices() const
{
  std::vector<int> indices;
  for (size_t i = 0; i < m_currentProgram.recipes.size(); i++) {
    if (m_currentProgram.recipes[i]->includeInLoop && m_currentProgram.recipes[i]->enabled) {
      indices.push_back(static_cast<int>(i));
    }
  }
  return indices;
}

std::vector<int> ProgramManager::getNonLoopRecipeIndices() const
{
  std::vector<int> indices;
  for (size_t i = 0; i < m_currentProgram.recipes.size(); i++) {
    if (!m_currentProgram.recipes[i]->includeInLoop && m_currentProgram.recipes[i]->enabled) {
      indices.push_back(static_cast<int>(i));
    }
  }
  return indices;
}

bool ProgramManager::hasLoopRecipes() const
{
  for (const auto& recipe : m_currentProgram.recipes) {
    if (recipe->includeInLoop && recipe->enabled) {
      return true;
    }
  }
  return false;
}

// ============================================================================
// DEPENDENCY SET MANAGEMENT
// ============================================================================

int ProgramManager::createDependencySet(const std::string& name)
{
  if (static_cast<int>(m_currentProgram.dependencySets.size()) >= MAX_DEPENDENCY_SETS) {
    std::cerr << "[ProgramManager] Cannot create dependency set: max " << MAX_DEPENDENCY_SETS << " reached" << std::endl;
    return -1;
  }

  m_currentProgram.dependencySets.emplace_back(name);
  int newIndex = static_cast<int>(m_currentProgram.dependencySets.size()) - 1;

  std::cout << "[ProgramManager] Created dependency set '" << name << "' at index " << newIndex << std::endl;
  return newIndex;
}

bool ProgramManager::deleteDependencySet(int setIndex)
{
  if (setIndex < 0 || setIndex >= static_cast<int>(m_currentProgram.dependencySets.size())) {
    std::cerr << "[ProgramManager] Invalid set index: " << setIndex << std::endl;
    return false;
  }

  if (m_currentProgram.dependencySets.size() == 1) {
    std::cerr << "[ProgramManager] Cannot delete last dependency set" << std::endl;
    return false;
  }

  if (setIndex == m_currentProgram.activeDependencySetIndex) {
    std::cerr << "[ProgramManager] Cannot delete active (POR) dependency set" << std::endl;
    return false;
  }

  std::string deletedName = m_currentProgram.dependencySets[setIndex].name;
  m_currentProgram.dependencySets.erase(m_currentProgram.dependencySets.begin() + setIndex);

  // Adjust active index if needed
  if (m_currentProgram.activeDependencySetIndex > setIndex) {
    m_currentProgram.activeDependencySetIndex--;
  }

  std::cout << "[ProgramManager] Deleted dependency set '" << deletedName << "'" << std::endl;
  return true;
}

bool ProgramManager::renameDependencySet(int setIndex, const std::string& name)
{
  if (setIndex < 0 || setIndex >= static_cast<int>(m_currentProgram.dependencySets.size())) {
    std::cerr << "[ProgramManager] Invalid set index: " << setIndex << std::endl;
    return false;
  }

  std::string oldName = m_currentProgram.dependencySets[setIndex].name;
  m_currentProgram.dependencySets[setIndex].name = name;

  std::cout << "[ProgramManager] Renamed dependency set from '" << oldName << "' to '" << name << "'" << std::endl;
  return true;
}

int ProgramManager::duplicateDependencySet(int sourceIndex, const std::string& name)
{
  if (sourceIndex < 0 || sourceIndex >= static_cast<int>(m_currentProgram.dependencySets.size())) {
    std::cerr << "[ProgramManager] Invalid source set index: " << sourceIndex << std::endl;
    return -1;
  }

  if (static_cast<int>(m_currentProgram.dependencySets.size()) >= MAX_DEPENDENCY_SETS) {
    std::cerr << "[ProgramManager] Cannot duplicate: max " << MAX_DEPENDENCY_SETS << " reached" << std::endl;
    return -1;
  }

  // Create new set with copied dependencies
  DependencySet newSet(name);
  newSet.dependencies = m_currentProgram.dependencySets[sourceIndex].dependencies;
  m_currentProgram.dependencySets.push_back(newSet);

  int newIndex = static_cast<int>(m_currentProgram.dependencySets.size()) - 1;
  std::cout << "[ProgramManager] Duplicated dependency set '"
            << m_currentProgram.dependencySets[sourceIndex].name
            << "' to '" << name << "' at index " << newIndex << std::endl;
  return newIndex;
}

int ProgramManager::getDependencySetCount() const
{
  return static_cast<int>(m_currentProgram.dependencySets.size());
}

std::string ProgramManager::getDependencySetName(int setIndex) const
{
  if (setIndex < 0 || setIndex >= static_cast<int>(m_currentProgram.dependencySets.size())) {
    return "";
  }
  return m_currentProgram.dependencySets[setIndex].name;
}

bool ProgramManager::setActiveDependencySet(int setIndex)
{
  if (setIndex < 0 || setIndex >= static_cast<int>(m_currentProgram.dependencySets.size())) {
    std::cerr << "[ProgramManager] Invalid set index: " << setIndex << std::endl;
    return false;
  }

  m_currentProgram.activeDependencySetIndex = setIndex;
  std::cout << "[ProgramManager] Set active dependency set to index " << setIndex
            << " ('" << m_currentProgram.dependencySets[setIndex].name << "')" << std::endl;
  return true;
}

int ProgramManager::getActiveDependencySetIndex() const
{
  return m_currentProgram.activeDependencySetIndex;
}

// ============================================================================
// DEPENDENCY MANAGEMENT (with set index)
// ============================================================================

bool ProgramManager::addRecipeDependency(int setIndex, int recipeIndex, int prerequisiteIndex,
                                          const std::string& condition)
{
  if (setIndex < 0 || setIndex >= static_cast<int>(m_currentProgram.dependencySets.size())) {
    std::cerr << "[ProgramManager] Invalid set index: " << setIndex << std::endl;
    return false;
  }

  if (recipeIndex < 0 || recipeIndex >= getRecipeCount() ||
      prerequisiteIndex < 0 || prerequisiteIndex >= getRecipeCount()) {
    std::cerr << "[ProgramManager] Invalid indices for dependency" << std::endl;
    return false;
  }

  if (recipeIndex == prerequisiteIndex) {
    std::cerr << "[ProgramManager] Cannot add self-dependency" << std::endl;
    return false;
  }

  auto& deps = m_currentProgram.dependencySets[setIndex].dependencies[recipeIndex];

  // Check if dependency already exists
  auto it = std::find_if(deps.begin(), deps.end(),
    [prerequisiteIndex](const DependencyLink& d) {
      return d.predecessorIndex == prerequisiteIndex;
    });

  if (it != deps.end()) {
    // Update condition on existing dependency
    it->condition = condition;
    std::cout << "[ProgramManager] Set " << setIndex << ": Updated dependency condition: Recipe " << recipeIndex
              << " depends on Recipe " << prerequisiteIndex
              << " with condition: " << (condition.empty() ? "(none)" : condition) << std::endl;
    return true;
  }

  // Add new dependency
  deps.emplace_back(prerequisiteIndex, condition);

  std::cout << "[ProgramManager] Set " << setIndex << ": Added dependency: Recipe " << recipeIndex
            << " depends on Recipe " << prerequisiteIndex
            << " with condition: " << (condition.empty() ? "(none)" : condition) << std::endl;
  return true;
}

void ProgramManager::removeRecipeDependency(int setIndex, int recipeIndex, int prerequisiteIndex)
{
  if (setIndex < 0 || setIndex >= static_cast<int>(m_currentProgram.dependencySets.size())) {
    return;
  }

  auto& depsMap = m_currentProgram.dependencySets[setIndex].dependencies;
  auto mapIt = depsMap.find(recipeIndex);
  if (mapIt == depsMap.end()) return;

  auto& deps = mapIt->second;
  deps.erase(
    std::remove_if(deps.begin(), deps.end(),
      [prerequisiteIndex](const DependencyLink& d) {
        return d.predecessorIndex == prerequisiteIndex;
      }),
    deps.end());

  // Clean up empty entries
  if (deps.empty()) {
    depsMap.erase(mapIt);
  }

  std::cout << "[ProgramManager] Set " << setIndex << ": Removed dependency: Recipe " << recipeIndex
            << " no longer depends on Recipe " << prerequisiteIndex << std::endl;
}

void ProgramManager::clearRecipeDependencies(int setIndex, int recipeIndex)
{
  if (setIndex < 0 || setIndex >= static_cast<int>(m_currentProgram.dependencySets.size())) {
    return;
  }

  m_currentProgram.dependencySets[setIndex].dependencies.erase(recipeIndex);
  std::cout << "[ProgramManager] Set " << setIndex << ": Cleared all dependencies for Recipe " << recipeIndex << std::endl;
}

std::vector<DependencyLink> ProgramManager::getRecipeDependencies(int setIndex, int recipeIndex) const
{
  if (setIndex < 0 || setIndex >= static_cast<int>(m_currentProgram.dependencySets.size())) {
    return {};
  }

  const auto& depsMap = m_currentProgram.dependencySets[setIndex].dependencies;
  auto it = depsMap.find(recipeIndex);
  if (it != depsMap.end()) {
    return it->second;
  }
  return {};
}

bool ProgramManager::hasRecipeDependency(int setIndex, int recipeIndex, int prerequisiteIndex) const
{
  if (setIndex < 0 || setIndex >= static_cast<int>(m_currentProgram.dependencySets.size())) {
    return false;
  }

  const auto& depsMap = m_currentProgram.dependencySets[setIndex].dependencies;
  auto mapIt = depsMap.find(recipeIndex);
  if (mapIt == depsMap.end()) return false;

  const auto& deps = mapIt->second;
  return std::find_if(deps.begin(), deps.end(),
    [prerequisiteIndex](const DependencyLink& d) {
      return d.predecessorIndex == prerequisiteIndex;
    }) != deps.end();
}

void ProgramManager::setDependencyCondition(int setIndex, int recipeIndex, int prerequisiteIndex,
                                             const std::string& condition)
{
  if (setIndex < 0 || setIndex >= static_cast<int>(m_currentProgram.dependencySets.size())) {
    return;
  }

  auto& depsMap = m_currentProgram.dependencySets[setIndex].dependencies;
  auto mapIt = depsMap.find(recipeIndex);
  if (mapIt == depsMap.end()) return;

  auto& deps = mapIt->second;
  auto it = std::find_if(deps.begin(), deps.end(),
    [prerequisiteIndex](const DependencyLink& d) {
      return d.predecessorIndex == prerequisiteIndex;
    });

  if (it != deps.end()) {
    it->condition = condition;
    std::cout << "[ProgramManager] Set " << setIndex << ": Set condition for dependency: Recipe " << recipeIndex
              << " -> Recipe " << prerequisiteIndex
              << " condition: " << (condition.empty() ? "(none)" : condition) << std::endl;
  }
}

std::string ProgramManager::getDependencyCondition(int setIndex, int recipeIndex, int prerequisiteIndex) const
{
  if (setIndex < 0 || setIndex >= static_cast<int>(m_currentProgram.dependencySets.size())) {
    return "";
  }

  const auto& depsMap = m_currentProgram.dependencySets[setIndex].dependencies;
  auto mapIt = depsMap.find(recipeIndex);
  if (mapIt == depsMap.end()) return "";

  const auto& deps = mapIt->second;
  auto it = std::find_if(deps.begin(), deps.end(),
    [prerequisiteIndex](const DependencyLink& d) {
      return d.predecessorIndex == prerequisiteIndex;
    });

  if (it != deps.end()) {
    return it->condition;
  }
  return "";
}

// ============================================================================
// BACKWARD-COMPATIBLE DEPENDENCY METHODS (use active set)
// ============================================================================

bool ProgramManager::addRecipeDependency(int recipeIndex, int prerequisiteIndex,
                                          const std::string& condition)
{
  return addRecipeDependency(m_currentProgram.activeDependencySetIndex,
                             recipeIndex, prerequisiteIndex, condition);
}

void ProgramManager::removeRecipeDependency(int recipeIndex, int prerequisiteIndex)
{
  removeRecipeDependency(m_currentProgram.activeDependencySetIndex,
                         recipeIndex, prerequisiteIndex);
}

void ProgramManager::clearRecipeDependencies(int recipeIndex)
{
  clearRecipeDependencies(m_currentProgram.activeDependencySetIndex, recipeIndex);
}

std::vector<DependencyLink> ProgramManager::getRecipeDependencies(int recipeIndex) const
{
  return getRecipeDependencies(m_currentProgram.activeDependencySetIndex, recipeIndex);
}

bool ProgramManager::hasRecipeDependency(int recipeIndex, int prerequisiteIndex) const
{
  return hasRecipeDependency(m_currentProgram.activeDependencySetIndex,
                             recipeIndex, prerequisiteIndex);
}

void ProgramManager::setDependencyCondition(int recipeIndex, int prerequisiteIndex,
                                             const std::string& condition)
{
  setDependencyCondition(m_currentProgram.activeDependencySetIndex,
                         recipeIndex, prerequisiteIndex, condition);
}

std::string ProgramManager::getDependencyCondition(int recipeIndex, int prerequisiteIndex) const
{
  return getDependencyCondition(m_currentProgram.activeDependencySetIndex,
                                recipeIndex, prerequisiteIndex);
}

std::vector<int> ProgramManager::getRecipePredecessorIndices(int recipeIndex) const
{
  std::vector<DependencyLink> deps = getRecipeDependencies(recipeIndex);
  std::vector<int> indices;
  for (const auto& dep : deps) {
    indices.push_back(dep.predecessorIndex);
  }
  return indices;
}

bool ProgramManager::validateDependencies(std::vector<std::string>& errors) const
{
  errors.clear();

  // Validate all dependency sets
  for (size_t setIdx = 0; setIdx < m_currentProgram.dependencySets.size(); setIdx++) {
    const auto& depSet = m_currentProgram.dependencySets[setIdx];

    for (const auto& [recipeIndex, deps] : depSet.dependencies) {
      // Check recipe index is valid
      if (recipeIndex < 0 || recipeIndex >= static_cast<int>(m_currentProgram.recipes.size())) {
        errors.push_back("Set '" + depSet.name + "': Invalid recipe index " +
                         std::to_string(recipeIndex));
        continue;
      }

      // Check each dependency
      for (const auto& dep : deps) {
        if (dep.predecessorIndex < 0 ||
            dep.predecessorIndex >= static_cast<int>(m_currentProgram.recipes.size())) {
          errors.push_back("Set '" + depSet.name + "': Recipe " + std::to_string(recipeIndex) +
                           " has invalid dependency index: " + std::to_string(dep.predecessorIndex));
        }
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

// ============================================================================
// GRAPH NODE POSITION MANAGEMENT
// ============================================================================

void ProgramManager::setNodePosition(int recipeIndex, double x, double y)
{
  if (recipeIndex < 0 || recipeIndex >= getRecipeCount()) {
    return;
  }
  m_graphPositions[recipeIndex] = GraphNodePosition(x, y);
  std::cout << "[ProgramManager] Set node position for recipe " << recipeIndex
            << " to (" << x << ", " << y << ")" << std::endl;
}

GraphNodePosition ProgramManager::getNodePosition(int recipeIndex) const
{
  auto it = m_graphPositions.find(recipeIndex);
  if (it != m_graphPositions.end()) {
    return it->second;
  }
  return GraphNodePosition();  // Return default (hasPosition = false)
}

bool ProgramManager::hasNodePosition(int recipeIndex) const
{
  auto it = m_graphPositions.find(recipeIndex);
  return (it != m_graphPositions.end() && it->second.hasPosition);
}

void ProgramManager::clearNodePositions()
{
  m_graphPositions.clear();
  std::cout << "[ProgramManager] Cleared all node positions" << std::endl;
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
  root["active_dependency_set_index"] = m_currentProgram.activeDependencySetIndex;

  // Save recipes (without dependencies - they're in dependency_sets now)
  QJsonArray recipesArray;
  for (const auto& recipe : m_currentProgram.recipes) {
    QJsonObject recipeObj;
    recipeObj["recipe_file"] = QString::fromStdString(recipe->recipeFilePath);
    recipeObj["display_name"] = QString::fromStdString(recipe->displayName);
    recipeObj["enabled"] = recipe->enabled;
    recipeObj["include_in_loop"] = recipe->includeInLoop;
    recipesArray.append(recipeObj);
  }
  root["recipes"] = recipesArray;

  // Save dependency sets (new format)
  QJsonArray dependencySetsArray;
  for (const auto& depSet : m_currentProgram.dependencySets) {
    QJsonObject setObj;
    setObj["name"] = QString::fromStdString(depSet.name);

    // Save dependencies as { "recipeIndex": [ {predecessor_index, condition}, ... ] }
    QJsonObject depsObj;
    for (const auto& [recipeIndex, deps] : depSet.dependencies) {
      if (!deps.empty()) {
        QJsonArray depsArray;
        for (const auto& dep : deps) {
          QJsonObject depObj;
          depObj["predecessor_index"] = dep.predecessorIndex;
          if (!dep.condition.empty()) {
            depObj["condition"] = QString::fromStdString(dep.condition);
          }
          depsArray.append(depObj);
        }
        depsObj[QString::number(recipeIndex)] = depsArray;
      }
    }
    setObj["dependencies"] = depsObj;

    dependencySetsArray.append(setObj);
  }
  root["dependency_sets"] = dependencySetsArray;

  // Save graph node positions
  if (!m_graphPositions.empty()) {
    QJsonObject positionsObj;
    for (const auto& [index, pos] : m_graphPositions) {
      if (pos.hasPosition) {
        QJsonObject posObj;
        posObj["x"] = pos.x;
        posObj["y"] = pos.y;
        positionsObj[QString::number(index)] = posObj;
      }
    }
    if (!positionsObj.isEmpty()) {
      root["graph_positions"] = positionsObj;
    }
  }

  QJsonDocument doc(root);

  QFile file(QString::fromStdString(filename));
  if (!file.open(QIODevice::WriteOnly)) {
    std::cerr << "[ProgramManager] Failed to write file: " << filename << std::endl;
    return false;
  }

  file.write(doc.toJson(QJsonDocument::Indented));
  file.close();

  std::cout << "[ProgramManager] Saved program '" << m_currentProgram.name
    << "' with " << m_currentProgram.recipes.size() << " recipes and "
    << m_currentProgram.dependencySets.size() << " dependency set(s) to "
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

  // Check if new format (has dependency_sets array)
  bool hasNewFormat = root.contains("dependency_sets");

  // Load active dependency set index
  newProgram.activeDependencySetIndex = root["active_dependency_set_index"].toInt(0);

  // Storage for migrating old format dependencies
  std::map<int, std::vector<DependencyLink>> legacyDependencies;

  QJsonArray recipesArray = root["recipes"].toArray();
  int recipeIndex = 0;

  for (const QJsonValue& val : recipesArray) {
    QJsonObject recipeObj = val.toObject();

    std::string filePath = recipeObj["recipe_file"].toString().toStdString();
    std::string displayName = recipeObj["display_name"].toString().toStdString();
    bool enabled = recipeObj["enabled"].toBool(true);
    bool includeInLoop = recipeObj["include_in_loop"].toBool(false);

    auto recipeRef = std::make_unique<RecipeReference>(filePath, displayName, enabled);
    recipeRef->includeInLoop = includeInLoop;

    // If old format, collect dependencies for migration
    if (!hasNewFormat) {
      if (recipeObj.contains("dependencies")) {
        QJsonArray depsArray = recipeObj["dependencies"].toArray();
        for (const QJsonValue& depVal : depsArray) {
          if (depVal.isObject()) {
            QJsonObject depObj = depVal.toObject();
            int predIndex = depObj["predecessor_index"].toInt();
            std::string condition = depObj.value("condition").toString("").toStdString();
            legacyDependencies[recipeIndex].emplace_back(predIndex, condition);
          }
        }
      }
      // Legacy format: "predecessors" array of ints
      else if (recipeObj.contains("predecessors")) {
        QJsonArray predecessorsArray = recipeObj["predecessors"].toArray();
        for (const QJsonValue& predVal : predecessorsArray) {
          legacyDependencies[recipeIndex].emplace_back(predVal.toInt(), "");
        }
      }
    }

    newProgram.recipes.push_back(std::move(recipeRef));
    recipeIndex++;
  }

  // Load dependency sets (new format) or migrate from legacy
  newProgram.dependencySets.clear();

  if (hasNewFormat) {
    // New format: load dependency_sets array
    QJsonArray dependencySetsArray = root["dependency_sets"].toArray();

    for (const QJsonValue& setVal : dependencySetsArray) {
      QJsonObject setObj = setVal.toObject();
      std::string setName = setObj["name"].toString("Default").toStdString();

      DependencySet depSet(setName);

      // Load dependencies from the "dependencies" object
      QJsonObject depsObj = setObj["dependencies"].toObject();
      for (auto it = depsObj.begin(); it != depsObj.end(); ++it) {
        bool ok;
        int recipeIdx = it.key().toInt(&ok);
        if (ok && it.value().isArray()) {
          QJsonArray depsArray = it.value().toArray();
          for (const QJsonValue& depVal : depsArray) {
            if (depVal.isObject()) {
              QJsonObject depObj = depVal.toObject();
              int predIndex = depObj["predecessor_index"].toInt();
              std::string condition = depObj.value("condition").toString("").toStdString();
              depSet.dependencies[recipeIdx].emplace_back(predIndex, condition);
            }
          }
        }
      }

      newProgram.dependencySets.push_back(depSet);
    }

    std::cout << "[ProgramManager] Loaded " << newProgram.dependencySets.size()
              << " dependency set(s)" << std::endl;
  }
  else {
    // Legacy format: migrate to a single "Default" dependency set
    DependencySet defaultSet("Default");
    defaultSet.dependencies = legacyDependencies;
    newProgram.dependencySets.push_back(defaultSet);
    newProgram.activeDependencySetIndex = 0;

    std::cout << "[ProgramManager] Migrated legacy dependencies to 'Default' set" << std::endl;
  }

  // Ensure at least one dependency set exists
  if (newProgram.dependencySets.empty()) {
    newProgram.dependencySets.emplace_back("Default");
  }

  // Validate active index
  if (newProgram.activeDependencySetIndex < 0 ||
      newProgram.activeDependencySetIndex >= static_cast<int>(newProgram.dependencySets.size())) {
    newProgram.activeDependencySetIndex = 0;
  }

  // Replace current program
  m_currentProgram = std::move(newProgram);
  m_currentFilePath = filename;

  // Load graph node positions
  m_graphPositions.clear();
  if (root.contains("graph_positions")) {
    QJsonObject positionsObj = root["graph_positions"].toObject();
    for (auto it = positionsObj.begin(); it != positionsObj.end(); ++it) {
      bool ok;
      int index = it.key().toInt(&ok);
      if (ok && it.value().isObject()) {
        QJsonObject posObj = it.value().toObject();
        double x = posObj["x"].toDouble();
        double y = posObj["y"].toDouble();
        m_graphPositions[index] = GraphNodePosition(x, y);
      }
    }
    std::cout << "[ProgramManager] Loaded " << m_graphPositions.size() << " graph node positions" << std::endl;
  }

  std::cout << "[ProgramManager] Loaded program '" << m_currentProgram.name
    << "' with " << m_currentProgram.recipes.size() << " recipes and "
    << m_currentProgram.dependencySets.size() << " dependency set(s) from "
    << filename << std::endl;

  return true;
}