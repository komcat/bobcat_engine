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
  // Program() constructor initializes with one default slot containing one "Default" dependency set
  m_currentFilePath.clear();
  std::cout << "[ProgramManager] New program created with 1 default slot" << std::endl;
}

// ============================================================================
// RECIPE SLOT MANAGEMENT
// ============================================================================

int ProgramManager::createSlot(const std::string& name)
{
  if (static_cast<int>(m_currentProgram.recipeSlots.size()) >= MAX_RECIPE_SLOTS) {
    std::cerr << "[ProgramManager] Cannot create slot: max " << MAX_RECIPE_SLOTS << " reached" << std::endl;
    return -1;
  }

  std::string slotName = name.empty()
    ? "Slot " + std::to_string(m_currentProgram.recipeSlots.size() + 1)
    : name;

  m_currentProgram.recipeSlots.emplace_back(slotName);
  int newIndex = static_cast<int>(m_currentProgram.recipeSlots.size()) - 1;

  std::cout << "[ProgramManager] Created slot '" << slotName << "' at index " << newIndex << std::endl;
  return newIndex;
}

bool ProgramManager::deleteSlot(int slotIndex)
{
  if (slotIndex < 0 || slotIndex >= static_cast<int>(m_currentProgram.recipeSlots.size())) {
    std::cerr << "[ProgramManager] Invalid slot index: " << slotIndex << std::endl;
    return false;
  }

  if (m_currentProgram.recipeSlots.size() == 1) {
    std::cerr << "[ProgramManager] Cannot delete last slot" << std::endl;
    return false;
  }

  std::string deletedName = m_currentProgram.recipeSlots[slotIndex].name;
  m_currentProgram.recipeSlots.erase(m_currentProgram.recipeSlots.begin() + slotIndex);

  // Adjust active index if needed
  if (m_currentProgram.activeSlotIndex >= static_cast<int>(m_currentProgram.recipeSlots.size())) {
    m_currentProgram.activeSlotIndex = static_cast<int>(m_currentProgram.recipeSlots.size()) - 1;
  } else if (m_currentProgram.activeSlotIndex > slotIndex) {
    m_currentProgram.activeSlotIndex--;
  }

  std::cout << "[ProgramManager] Deleted slot '" << deletedName << "'" << std::endl;
  return true;
}

bool ProgramManager::renameSlot(int slotIndex, const std::string& name)
{
  if (slotIndex < 0 || slotIndex >= static_cast<int>(m_currentProgram.recipeSlots.size())) {
    std::cerr << "[ProgramManager] Invalid slot index: " << slotIndex << std::endl;
    return false;
  }

  std::string oldName = m_currentProgram.recipeSlots[slotIndex].name;
  m_currentProgram.recipeSlots[slotIndex].name = name;

  std::cout << "[ProgramManager] Renamed slot from '" << oldName << "' to '" << name << "'" << std::endl;
  return true;
}

int ProgramManager::duplicateSlot(int sourceIndex, const std::string& name)
{
  if (sourceIndex < 0 || sourceIndex >= static_cast<int>(m_currentProgram.recipeSlots.size())) {
    std::cerr << "[ProgramManager] Invalid source slot index: " << sourceIndex << std::endl;
    return -1;
  }

  if (static_cast<int>(m_currentProgram.recipeSlots.size()) >= MAX_RECIPE_SLOTS) {
    std::cerr << "[ProgramManager] Cannot duplicate: max " << MAX_RECIPE_SLOTS << " reached" << std::endl;
    return -1;
  }

  const RecipeSlot& source = m_currentProgram.recipeSlots[sourceIndex];
  std::string slotName = name.empty() ? source.name + " (Copy)" : name;

  // Create new slot
  m_currentProgram.recipeSlots.emplace_back(slotName);
  RecipeSlot& newSlot = m_currentProgram.recipeSlots.back();

  // Copy recipes
  for (const auto& recipe : source.recipes) {
    auto newRecipe = std::make_unique<RecipeReference>(
      recipe->recipeFilePath, recipe->displayName, recipe->enabled);
    newRecipe->includeInLoop = recipe->includeInLoop;
    newSlot.recipes.push_back(std::move(newRecipe));
  }

  // Copy dependency sets
  newSlot.dependencySets = source.dependencySets;
  newSlot.activeDependencySetIndex = source.activeDependencySetIndex;

  // Copy graph positions
  newSlot.graphPositions = source.graphPositions;

  int newIndex = static_cast<int>(m_currentProgram.recipeSlots.size()) - 1;
  std::cout << "[ProgramManager] Duplicated slot '" << source.name
            << "' to '" << slotName << "' at index " << newIndex << std::endl;
  return newIndex;
}

int ProgramManager::getSlotCount() const
{
  return static_cast<int>(m_currentProgram.recipeSlots.size());
}

std::string ProgramManager::getSlotName(int slotIndex) const
{
  if (slotIndex < 0 || slotIndex >= static_cast<int>(m_currentProgram.recipeSlots.size())) {
    return "";
  }
  return m_currentProgram.recipeSlots[slotIndex].name;
}

bool ProgramManager::setActiveSlot(int slotIndex)
{
  if (slotIndex < 0 || slotIndex >= static_cast<int>(m_currentProgram.recipeSlots.size())) {
    std::cerr << "[ProgramManager] Invalid slot index: " << slotIndex << std::endl;
    return false;
  }

  m_currentProgram.activeSlotIndex = slotIndex;
  std::cout << "[ProgramManager] Set active slot to index " << slotIndex
            << " ('" << m_currentProgram.recipeSlots[slotIndex].name << "')" << std::endl;
  return true;
}

int ProgramManager::getActiveSlotIndex() const
{
  return m_currentProgram.activeSlotIndex;
}

RecipeSlot* ProgramManager::getSlot(int slotIndex)
{
  if (slotIndex < 0 || slotIndex >= static_cast<int>(m_currentProgram.recipeSlots.size())) {
    return nullptr;
  }
  return &m_currentProgram.recipeSlots[slotIndex];
}

const RecipeSlot* ProgramManager::getSlot(int slotIndex) const
{
  if (slotIndex < 0 || slotIndex >= static_cast<int>(m_currentProgram.recipeSlots.size())) {
    return nullptr;
  }
  return &m_currentProgram.recipeSlots[slotIndex];
}

std::vector<std::string> ProgramManager::getSlotNames() const
{
  std::vector<std::string> names;
  for (const auto& slot : m_currentProgram.recipeSlots) {
    names.push_back(slot.name);
  }
  return names;
}

bool ProgramManager::moveRecipeToSlot(int recipeIndex, int targetSlotIndex)
{
  // Validate source (active slot)
  if (m_currentProgram.activeSlotIndex < 0 ||
      m_currentProgram.activeSlotIndex >= static_cast<int>(m_currentProgram.recipeSlots.size())) {
    std::cerr << "[ProgramManager] Invalid active slot index" << std::endl;
    return false;
  }

  // Validate target slot
  if (targetSlotIndex < 0 ||
      targetSlotIndex >= static_cast<int>(m_currentProgram.recipeSlots.size())) {
    std::cerr << "[ProgramManager] Invalid target slot index: " << targetSlotIndex << std::endl;
    return false;
  }

  // Cannot move to same slot
  if (targetSlotIndex == m_currentProgram.activeSlotIndex) {
    std::cerr << "[ProgramManager] Cannot move recipe to the same slot" << std::endl;
    return false;
  }

  auto& sourceSlot = m_currentProgram.recipeSlots[m_currentProgram.activeSlotIndex];
  auto& targetSlot = m_currentProgram.recipeSlots[targetSlotIndex];

  // Validate recipe index
  if (recipeIndex < 0 || recipeIndex >= static_cast<int>(sourceSlot.recipes.size())) {
    std::cerr << "[ProgramManager] Invalid recipe index: " << recipeIndex << std::endl;
    return false;
  }

  // Get recipe info before moving
  std::string recipeName = sourceSlot.recipes[recipeIndex]->displayName;

  // Move the recipe (transfer ownership)
  targetSlot.recipes.push_back(std::move(sourceSlot.recipes[recipeIndex]));
  sourceSlot.recipes.erase(sourceSlot.recipes.begin() + recipeIndex);

  // Clean up dependencies in source slot that reference this recipe
  for (auto& depSet : sourceSlot.dependencySets) {
    // Remove dependencies where this recipe was a dependent
    depSet.dependencies.erase(recipeIndex);

    // Update indices for remaining recipes and remove references to moved recipe
    std::map<int, std::vector<DependencyLink>> updatedDeps;
    for (auto& [depRecipeIdx, links] : depSet.dependencies) {
      int newIdx = (depRecipeIdx > recipeIndex) ? depRecipeIdx - 1 : depRecipeIdx;

      // Filter out links to the moved recipe and update remaining indices
      std::vector<DependencyLink> updatedLinks;
      for (auto& link : links) {
        if (link.predecessorIndex != recipeIndex) {
          int newPredIdx = (link.predecessorIndex > recipeIndex) ? link.predecessorIndex - 1 : link.predecessorIndex;
          updatedLinks.emplace_back(newPredIdx, link.condition);
        }
      }

      if (!updatedLinks.empty()) {
        updatedDeps[newIdx] = std::move(updatedLinks);
      }
    }
    depSet.dependencies = std::move(updatedDeps);
  }

  // Update graph positions in source slot
  std::map<int, GraphNodePosition> updatedPositions;
  for (auto& [posIdx, pos] : sourceSlot.graphPositions) {
    if (posIdx != recipeIndex) {
      int newIdx = (posIdx > recipeIndex) ? posIdx - 1 : posIdx;
      updatedPositions[newIdx] = pos;
    }
  }
  sourceSlot.graphPositions = std::move(updatedPositions);

  std::cout << "[ProgramManager] Moved recipe '" << recipeName
            << "' from slot '" << sourceSlot.name
            << "' to slot '" << targetSlot.name << "'" << std::endl;

  return true;
}

// ============================================================================
// RECIPE COUNT (active slot)
// ============================================================================

int ProgramManager::getRecipeCount() const
{
  if (m_currentProgram.activeSlotIndex < 0 ||
      m_currentProgram.activeSlotIndex >= static_cast<int>(m_currentProgram.recipeSlots.size())) {
    return 0;
  }
  return static_cast<int>(m_currentProgram.recipeSlots[m_currentProgram.activeSlotIndex].recipes.size());
}

const std::vector<std::unique_ptr<RecipeReference>>& ProgramManager::getAllRecipes() const
{
  static std::vector<std::unique_ptr<RecipeReference>> emptyVec;
  if (m_currentProgram.activeSlotIndex < 0 ||
      m_currentProgram.activeSlotIndex >= static_cast<int>(m_currentProgram.recipeSlots.size())) {
    return emptyVec;
  }
  return m_currentProgram.recipeSlots[m_currentProgram.activeSlotIndex].recipes;
}

void ProgramManager::addRecipe(const std::string& recipeFilePath, const std::string& displayName)
{
  if (m_currentProgram.activeSlotIndex < 0 ||
      m_currentProgram.activeSlotIndex >= static_cast<int>(m_currentProgram.recipeSlots.size())) {
    std::cerr << "[ProgramManager] Invalid active slot index" << std::endl;
    return;
  }

  auto& activeSlot = m_currentProgram.recipeSlots[m_currentProgram.activeSlotIndex];
  auto recipeRef = std::make_unique<RecipeReference>(recipeFilePath, displayName);
  activeSlot.recipes.push_back(std::move(recipeRef));

  std::cout << "[ProgramManager] Added recipe to slot '" << activeSlot.name << "': " << displayName
    << " (" << recipeFilePath << ")" << std::endl;
}

void ProgramManager::addScript(const std::string& scriptFilePath, const std::string& displayName)
{
  if (m_currentProgram.activeSlotIndex < 0 ||
      m_currentProgram.activeSlotIndex >= static_cast<int>(m_currentProgram.recipeSlots.size())) {
    std::cerr << "[ProgramManager] Invalid active slot index" << std::endl;
    return;
  }

  auto& activeSlot = m_currentProgram.recipeSlots[m_currentProgram.activeSlotIndex];
  auto scriptRef = std::make_unique<RecipeReference>(scriptFilePath, displayName, true, RecipeReference::SCRIPT);
  activeSlot.recipes.push_back(std::move(scriptRef));

  std::cout << "[ProgramManager] Added script to slot '" << activeSlot.name << "': " << displayName
    << " (" << scriptFilePath << ")" << std::endl;
}

void ProgramManager::removeRecipe(int index)
{
  if (m_currentProgram.activeSlotIndex < 0 ||
      m_currentProgram.activeSlotIndex >= static_cast<int>(m_currentProgram.recipeSlots.size())) {
    std::cerr << "[ProgramManager] Invalid active slot index" << std::endl;
    return;
  }

  auto& activeSlot = m_currentProgram.recipeSlots[m_currentProgram.activeSlotIndex];
  if (index < 0 || index >= static_cast<int>(activeSlot.recipes.size())) {
    std::cerr << "[ProgramManager] Invalid index for removeRecipe: " << index << std::endl;
    return;
  }

  std::string removedName = activeSlot.recipes[index]->displayName;
  activeSlot.recipes.erase(activeSlot.recipes.begin() + index);

  std::cout << "[ProgramManager] Removed recipe at index " << index
    << " (" << removedName << ") from slot '" << activeSlot.name << "'" << std::endl;
}

bool ProgramManager::moveRecipeUp(int index)
{
  if (m_currentProgram.activeSlotIndex < 0 ||
      m_currentProgram.activeSlotIndex >= static_cast<int>(m_currentProgram.recipeSlots.size())) {
    return false;
  }

  auto& activeSlot = m_currentProgram.recipeSlots[m_currentProgram.activeSlotIndex];
  if (index <= 0 || index >= static_cast<int>(activeSlot.recipes.size())) {
    return false;
  }

  std::swap(activeSlot.recipes[index], activeSlot.recipes[index - 1]);
  std::cout << "[ProgramManager] Moved recipe " << index << " up in slot '" << activeSlot.name << "'" << std::endl;
  return true;
}

bool ProgramManager::moveRecipeDown(int index)
{
  if (m_currentProgram.activeSlotIndex < 0 ||
      m_currentProgram.activeSlotIndex >= static_cast<int>(m_currentProgram.recipeSlots.size())) {
    return false;
  }

  auto& activeSlot = m_currentProgram.recipeSlots[m_currentProgram.activeSlotIndex];
  if (index < 0 || index >= static_cast<int>(activeSlot.recipes.size()) - 1) {
    return false;
  }

  std::swap(activeSlot.recipes[index], activeSlot.recipes[index + 1]);
  std::cout << "[ProgramManager] Moved recipe " << index << " down in slot '" << activeSlot.name << "'" << std::endl;
  return true;
}

RecipeReference* ProgramManager::getRecipe(int index)
{
  if (m_currentProgram.activeSlotIndex < 0 ||
      m_currentProgram.activeSlotIndex >= static_cast<int>(m_currentProgram.recipeSlots.size())) {
    return nullptr;
  }

  auto& activeSlot = m_currentProgram.recipeSlots[m_currentProgram.activeSlotIndex];
  if (index < 0 || index >= static_cast<int>(activeSlot.recipes.size())) {
    return nullptr;
  }
  return activeSlot.recipes[index].get();
}

const RecipeReference* ProgramManager::getRecipe(int index) const
{
  if (m_currentProgram.activeSlotIndex < 0 ||
      m_currentProgram.activeSlotIndex >= static_cast<int>(m_currentProgram.recipeSlots.size())) {
    return nullptr;
  }

  const auto& activeSlot = m_currentProgram.recipeSlots[m_currentProgram.activeSlotIndex];
  if (index < 0 || index >= static_cast<int>(activeSlot.recipes.size())) {
    return nullptr;
  }
  return activeSlot.recipes[index].get();
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
  if (m_currentProgram.activeSlotIndex < 0 ||
      m_currentProgram.activeSlotIndex >= static_cast<int>(m_currentProgram.recipeSlots.size())) {
    return indices;
  }

  const auto& activeSlot = m_currentProgram.recipeSlots[m_currentProgram.activeSlotIndex];
  for (size_t i = 0; i < activeSlot.recipes.size(); i++) {
    if (activeSlot.recipes[i]->includeInLoop && activeSlot.recipes[i]->enabled) {
      indices.push_back(static_cast<int>(i));
    }
  }
  return indices;
}

std::vector<int> ProgramManager::getNonLoopRecipeIndices() const
{
  std::vector<int> indices;
  if (m_currentProgram.activeSlotIndex < 0 ||
      m_currentProgram.activeSlotIndex >= static_cast<int>(m_currentProgram.recipeSlots.size())) {
    return indices;
  }

  const auto& activeSlot = m_currentProgram.recipeSlots[m_currentProgram.activeSlotIndex];
  for (size_t i = 0; i < activeSlot.recipes.size(); i++) {
    if (!activeSlot.recipes[i]->includeInLoop && activeSlot.recipes[i]->enabled) {
      indices.push_back(static_cast<int>(i));
    }
  }
  return indices;
}

bool ProgramManager::hasLoopRecipes() const
{
  if (m_currentProgram.activeSlotIndex < 0 ||
      m_currentProgram.activeSlotIndex >= static_cast<int>(m_currentProgram.recipeSlots.size())) {
    return false;
  }

  const auto& activeSlot = m_currentProgram.recipeSlots[m_currentProgram.activeSlotIndex];
  for (const auto& recipe : activeSlot.recipes) {
    if (recipe->includeInLoop && recipe->enabled) {
      return true;
    }
  }
  return false;
}

// ============================================================================
// DEPENDENCY SET MANAGEMENT
// ============================================================================

// Helper to get active slot (internal use)
static RecipeSlot* getActiveSlotMutable(Program& program)
{
  if (program.activeSlotIndex < 0 ||
      program.activeSlotIndex >= static_cast<int>(program.recipeSlots.size())) {
    return nullptr;
  }
  return &program.recipeSlots[program.activeSlotIndex];
}

static const RecipeSlot* getActiveSlotConst(const Program& program)
{
  if (program.activeSlotIndex < 0 ||
      program.activeSlotIndex >= static_cast<int>(program.recipeSlots.size())) {
    return nullptr;
  }
  return &program.recipeSlots[program.activeSlotIndex];
}

int ProgramManager::createDependencySet(const std::string& name)
{
  RecipeSlot* activeSlot = getActiveSlotMutable(m_currentProgram);
  if (!activeSlot) {
    std::cerr << "[ProgramManager] No active slot" << std::endl;
    return -1;
  }

  if (static_cast<int>(activeSlot->dependencySets.size()) >= MAX_DEPENDENCY_SETS) {
    std::cerr << "[ProgramManager] Cannot create dependency set: max " << MAX_DEPENDENCY_SETS << " reached" << std::endl;
    return -1;
  }

  activeSlot->dependencySets.emplace_back(name);
  int newIndex = static_cast<int>(activeSlot->dependencySets.size()) - 1;

  std::cout << "[ProgramManager] Created dependency set '" << name << "' at index " << newIndex
            << " in slot '" << activeSlot->name << "'" << std::endl;
  return newIndex;
}

bool ProgramManager::deleteDependencySet(int setIndex)
{
  RecipeSlot* activeSlot = getActiveSlotMutable(m_currentProgram);
  if (!activeSlot) {
    std::cerr << "[ProgramManager] No active slot" << std::endl;
    return false;
  }

  if (setIndex < 0 || setIndex >= static_cast<int>(activeSlot->dependencySets.size())) {
    std::cerr << "[ProgramManager] Invalid set index: " << setIndex << std::endl;
    return false;
  }

  if (activeSlot->dependencySets.size() == 1) {
    std::cerr << "[ProgramManager] Cannot delete last dependency set" << std::endl;
    return false;
  }

  if (setIndex == activeSlot->activeDependencySetIndex) {
    std::cerr << "[ProgramManager] Cannot delete active (POR) dependency set" << std::endl;
    return false;
  }

  std::string deletedName = activeSlot->dependencySets[setIndex].name;
  activeSlot->dependencySets.erase(activeSlot->dependencySets.begin() + setIndex);

  // Adjust active index if needed
  if (activeSlot->activeDependencySetIndex > setIndex) {
    activeSlot->activeDependencySetIndex--;
  }

  std::cout << "[ProgramManager] Deleted dependency set '" << deletedName << "'" << std::endl;
  return true;
}

bool ProgramManager::renameDependencySet(int setIndex, const std::string& name)
{
  RecipeSlot* activeSlot = getActiveSlotMutable(m_currentProgram);
  if (!activeSlot) {
    std::cerr << "[ProgramManager] No active slot" << std::endl;
    return false;
  }

  if (setIndex < 0 || setIndex >= static_cast<int>(activeSlot->dependencySets.size())) {
    std::cerr << "[ProgramManager] Invalid set index: " << setIndex << std::endl;
    return false;
  }

  std::string oldName = activeSlot->dependencySets[setIndex].name;
  activeSlot->dependencySets[setIndex].name = name;

  std::cout << "[ProgramManager] Renamed dependency set from '" << oldName << "' to '" << name << "'" << std::endl;
  return true;
}

int ProgramManager::duplicateDependencySet(int sourceIndex, const std::string& name)
{
  RecipeSlot* activeSlot = getActiveSlotMutable(m_currentProgram);
  if (!activeSlot) {
    std::cerr << "[ProgramManager] No active slot" << std::endl;
    return -1;
  }

  if (sourceIndex < 0 || sourceIndex >= static_cast<int>(activeSlot->dependencySets.size())) {
    std::cerr << "[ProgramManager] Invalid source set index: " << sourceIndex << std::endl;
    return -1;
  }

  if (static_cast<int>(activeSlot->dependencySets.size()) >= MAX_DEPENDENCY_SETS) {
    std::cerr << "[ProgramManager] Cannot duplicate: max " << MAX_DEPENDENCY_SETS << " reached" << std::endl;
    return -1;
  }

  // Create new set with copied dependencies
  DependencySet newSet(name);
  newSet.dependencies = activeSlot->dependencySets[sourceIndex].dependencies;
  activeSlot->dependencySets.push_back(newSet);

  int newIndex = static_cast<int>(activeSlot->dependencySets.size()) - 1;
  std::cout << "[ProgramManager] Duplicated dependency set '"
            << activeSlot->dependencySets[sourceIndex].name
            << "' to '" << name << "' at index " << newIndex << std::endl;
  return newIndex;
}

int ProgramManager::getDependencySetCount() const
{
  const RecipeSlot* activeSlot = getActiveSlotConst(m_currentProgram);
  if (!activeSlot) return 0;
  return static_cast<int>(activeSlot->dependencySets.size());
}

std::string ProgramManager::getDependencySetName(int setIndex) const
{
  const RecipeSlot* activeSlot = getActiveSlotConst(m_currentProgram);
  if (!activeSlot) return "";

  if (setIndex < 0 || setIndex >= static_cast<int>(activeSlot->dependencySets.size())) {
    return "";
  }
  return activeSlot->dependencySets[setIndex].name;
}

bool ProgramManager::setActiveDependencySet(int setIndex)
{
  RecipeSlot* activeSlot = getActiveSlotMutable(m_currentProgram);
  if (!activeSlot) {
    std::cerr << "[ProgramManager] No active slot" << std::endl;
    return false;
  }

  if (setIndex < 0 || setIndex >= static_cast<int>(activeSlot->dependencySets.size())) {
    std::cerr << "[ProgramManager] Invalid set index: " << setIndex << std::endl;
    return false;
  }

  activeSlot->activeDependencySetIndex = setIndex;
  std::cout << "[ProgramManager] Set active dependency set to index " << setIndex
            << " ('" << activeSlot->dependencySets[setIndex].name << "')" << std::endl;
  return true;
}

int ProgramManager::getActiveDependencySetIndex() const
{
  const RecipeSlot* activeSlot = getActiveSlotConst(m_currentProgram);
  if (!activeSlot) return 0;
  return activeSlot->activeDependencySetIndex;
}

// ============================================================================
// DEPENDENCY MANAGEMENT (with set index)
// ============================================================================

bool ProgramManager::addRecipeDependency(int setIndex, int recipeIndex, int prerequisiteIndex,
                                          const std::string& condition)
{
  RecipeSlot* activeSlot = getActiveSlotMutable(m_currentProgram);
  if (!activeSlot) {
    std::cerr << "[ProgramManager] No active slot" << std::endl;
    return false;
  }

  if (setIndex < 0 || setIndex >= static_cast<int>(activeSlot->dependencySets.size())) {
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

  auto& deps = activeSlot->dependencySets[setIndex].dependencies[recipeIndex];

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
  RecipeSlot* activeSlot = getActiveSlotMutable(m_currentProgram);
  if (!activeSlot) return;

  if (setIndex < 0 || setIndex >= static_cast<int>(activeSlot->dependencySets.size())) {
    return;
  }

  auto& depsMap = activeSlot->dependencySets[setIndex].dependencies;
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
  RecipeSlot* activeSlot = getActiveSlotMutable(m_currentProgram);
  if (!activeSlot) return;

  if (setIndex < 0 || setIndex >= static_cast<int>(activeSlot->dependencySets.size())) {
    return;
  }

  activeSlot->dependencySets[setIndex].dependencies.erase(recipeIndex);
  std::cout << "[ProgramManager] Set " << setIndex << ": Cleared all dependencies for Recipe " << recipeIndex << std::endl;
}

std::vector<DependencyLink> ProgramManager::getRecipeDependencies(int setIndex, int recipeIndex) const
{
  const RecipeSlot* activeSlot = getActiveSlotConst(m_currentProgram);
  if (!activeSlot) return {};

  if (setIndex < 0 || setIndex >= static_cast<int>(activeSlot->dependencySets.size())) {
    return {};
  }

  const auto& depsMap = activeSlot->dependencySets[setIndex].dependencies;
  auto it = depsMap.find(recipeIndex);
  if (it != depsMap.end()) {
    return it->second;
  }
  return {};
}

bool ProgramManager::hasRecipeDependency(int setIndex, int recipeIndex, int prerequisiteIndex) const
{
  const RecipeSlot* activeSlot = getActiveSlotConst(m_currentProgram);
  if (!activeSlot) return false;

  if (setIndex < 0 || setIndex >= static_cast<int>(activeSlot->dependencySets.size())) {
    return false;
  }

  const auto& depsMap = activeSlot->dependencySets[setIndex].dependencies;
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
  RecipeSlot* activeSlot = getActiveSlotMutable(m_currentProgram);
  if (!activeSlot) return;

  if (setIndex < 0 || setIndex >= static_cast<int>(activeSlot->dependencySets.size())) {
    return;
  }

  auto& depsMap = activeSlot->dependencySets[setIndex].dependencies;
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
  const RecipeSlot* activeSlot = getActiveSlotConst(m_currentProgram);
  if (!activeSlot) return "";

  if (setIndex < 0 || setIndex >= static_cast<int>(activeSlot->dependencySets.size())) {
    return "";
  }

  const auto& depsMap = activeSlot->dependencySets[setIndex].dependencies;
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
  return addRecipeDependency(getActiveDependencySetIndex(),
                             recipeIndex, prerequisiteIndex, condition);
}

void ProgramManager::removeRecipeDependency(int recipeIndex, int prerequisiteIndex)
{
  removeRecipeDependency(getActiveDependencySetIndex(),
                         recipeIndex, prerequisiteIndex);
}

void ProgramManager::clearRecipeDependencies(int recipeIndex)
{
  clearRecipeDependencies(getActiveDependencySetIndex(), recipeIndex);
}

std::vector<DependencyLink> ProgramManager::getRecipeDependencies(int recipeIndex) const
{
  return getRecipeDependencies(getActiveDependencySetIndex(), recipeIndex);
}

bool ProgramManager::hasRecipeDependency(int recipeIndex, int prerequisiteIndex) const
{
  return hasRecipeDependency(getActiveDependencySetIndex(),
                             recipeIndex, prerequisiteIndex);
}

void ProgramManager::setDependencyCondition(int recipeIndex, int prerequisiteIndex,
                                             const std::string& condition)
{
  setDependencyCondition(getActiveDependencySetIndex(),
                         recipeIndex, prerequisiteIndex, condition);
}

std::string ProgramManager::getDependencyCondition(int recipeIndex, int prerequisiteIndex) const
{
  return getDependencyCondition(getActiveDependencySetIndex(),
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

  const RecipeSlot* activeSlot = getActiveSlotConst(m_currentProgram);
  if (!activeSlot) {
    errors.push_back("No active slot");
    return false;
  }

  // Validate all dependency sets in active slot
  for (size_t setIdx = 0; setIdx < activeSlot->dependencySets.size(); setIdx++) {
    const auto& depSet = activeSlot->dependencySets[setIdx];

    for (const auto& [recipeIndex, deps] : depSet.dependencies) {
      // Check recipe index is valid
      if (recipeIndex < 0 || recipeIndex >= static_cast<int>(activeSlot->recipes.size())) {
        errors.push_back("Set '" + depSet.name + "': Invalid recipe index " +
                         std::to_string(recipeIndex));
        continue;
      }

      // Check each dependency
      for (const auto& dep : deps) {
        if (dep.predecessorIndex < 0 ||
            dep.predecessorIndex >= static_cast<int>(activeSlot->recipes.size())) {
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

  const RecipeSlot* activeSlot = getActiveSlotConst(m_currentProgram);
  if (!activeSlot) {
    errors.push_back("No active slot");
    return false;
  }

  if (activeSlot->recipes.empty()) {
    errors.push_back("Active slot has no recipes");
  }

  // Check if recipe files exist
  for (size_t i = 0; i < activeSlot->recipes.size(); i++) {
    const auto& recipe = activeSlot->recipes[i];
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
// GRAPH NODE POSITION MANAGEMENT (now per-slot)
// ============================================================================

void ProgramManager::setNodePosition(int recipeIndex, double x, double y)
{
  RecipeSlot* activeSlot = getActiveSlotMutable(m_currentProgram);
  if (!activeSlot) return;

  if (recipeIndex < 0 || recipeIndex >= static_cast<int>(activeSlot->recipes.size())) {
    return;
  }
  activeSlot->graphPositions[recipeIndex] = GraphNodePosition(x, y);
  std::cout << "[ProgramManager] Set node position for recipe " << recipeIndex
            << " to (" << x << ", " << y << ") in slot '" << activeSlot->name << "'" << std::endl;
}

GraphNodePosition ProgramManager::getNodePosition(int recipeIndex) const
{
  const RecipeSlot* activeSlot = getActiveSlotConst(m_currentProgram);
  if (!activeSlot) return GraphNodePosition();

  auto it = activeSlot->graphPositions.find(recipeIndex);
  if (it != activeSlot->graphPositions.end()) {
    return it->second;
  }
  return GraphNodePosition();  // Return default (hasPosition = false)
}

bool ProgramManager::hasNodePosition(int recipeIndex) const
{
  const RecipeSlot* activeSlot = getActiveSlotConst(m_currentProgram);
  if (!activeSlot) return false;

  auto it = activeSlot->graphPositions.find(recipeIndex);
  return (it != activeSlot->graphPositions.end() && it->second.hasPosition);
}

void ProgramManager::clearNodePositions()
{
  RecipeSlot* activeSlot = getActiveSlotMutable(m_currentProgram);
  if (!activeSlot) return;

  activeSlot->graphPositions.clear();
  std::cout << "[ProgramManager] Cleared all node positions in slot '" << activeSlot->name << "'" << std::endl;
}

const std::map<int, GraphNodePosition>& ProgramManager::getAllNodePositions() const
{
  static std::map<int, GraphNodePosition> emptyMap;
  const RecipeSlot* activeSlot = getActiveSlotConst(m_currentProgram);
  if (!activeSlot) return emptyMap;

  return activeSlot->graphPositions;
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
  root["active_slot_index"] = m_currentProgram.activeSlotIndex;

  // Save recipe slots (new format with slot system)
  QJsonArray recipeSlotsArray;
  for (const auto& slot : m_currentProgram.recipeSlots) {
    QJsonObject slotObj;
    slotObj["name"] = QString::fromStdString(slot.name);
    slotObj["active_dependency_set_index"] = slot.activeDependencySetIndex;

    // Save recipes in this slot
    QJsonArray recipesArray;
    for (const auto& recipe : slot.recipes) {
      QJsonObject recipeObj;
      recipeObj["recipe_file"] = QString::fromStdString(recipe->recipeFilePath);
      recipeObj["display_name"] = QString::fromStdString(recipe->displayName);
      recipeObj["enabled"] = recipe->enabled;
      recipeObj["include_in_loop"] = recipe->includeInLoop;
      if (recipe->type == RecipeReference::SCRIPT) {
        recipeObj["type"] = "script";
      }
      recipesArray.append(recipeObj);
    }
    slotObj["recipes"] = recipesArray;

    // Save dependency sets for this slot
    QJsonArray dependencySetsArray;
    for (const auto& depSet : slot.dependencySets) {
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
    slotObj["dependency_sets"] = dependencySetsArray;

    // Save graph node positions for this slot
    if (!slot.graphPositions.empty()) {
      QJsonObject positionsObj;
      for (const auto& [index, pos] : slot.graphPositions) {
        if (pos.hasPosition) {
          QJsonObject posObj;
          posObj["x"] = pos.x;
          posObj["y"] = pos.y;
          positionsObj[QString::number(index)] = posObj;
        }
      }
      if (!positionsObj.isEmpty()) {
        slotObj["graph_positions"] = positionsObj;
      }
    }

    recipeSlotsArray.append(slotObj);
  }
  root["recipe_slots"] = recipeSlotsArray;

  QJsonDocument doc(root);

  QFile file(QString::fromStdString(filename));
  if (!file.open(QIODevice::WriteOnly)) {
    std::cerr << "[ProgramManager] Failed to write file: " << filename << std::endl;
    return false;
  }

  file.write(doc.toJson(QJsonDocument::Indented));
  file.close();

  int totalRecipes = 0;
  for (const auto& slot : m_currentProgram.recipeSlots) {
    totalRecipes += static_cast<int>(slot.recipes.size());
  }

  std::cout << "[ProgramManager] Saved program '" << m_currentProgram.name
    << "' with " << m_currentProgram.recipeSlots.size() << " slot(s) and "
    << totalRecipes << " total recipes to " << filename << std::endl;

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

  // Check format: new format has "recipe_slots", old format has "recipes" at root level
  bool hasSlotFormat = root.contains("recipe_slots");

  if (hasSlotFormat) {
    // =========================================================================
    // NEW FORMAT: Load recipe_slots array
    // =========================================================================
    newProgram.activeSlotIndex = root["active_slot_index"].toInt(0);
    newProgram.recipeSlots.clear();

    QJsonArray slotsArray = root["recipe_slots"].toArray();
    for (const QJsonValue& slotVal : slotsArray) {
      QJsonObject slotObj = slotVal.toObject();

      std::string slotName = slotObj["name"].toString("Slot 1").toStdString();
      RecipeSlot newSlot(slotName);
      newSlot.activeDependencySetIndex = slotObj["active_dependency_set_index"].toInt(0);

      // Load recipes for this slot
      QJsonArray recipesArray = slotObj["recipes"].toArray();
      for (const QJsonValue& recipeVal : recipesArray) {
        QJsonObject recipeObj = recipeVal.toObject();

        std::string filePath = recipeObj["recipe_file"].toString().toStdString();
        std::string displayName = recipeObj["display_name"].toString().toStdString();
        bool enabled = recipeObj["enabled"].toBool(true);
        bool includeInLoop = recipeObj["include_in_loop"].toBool(false);
        RecipeReference::EntryType entryType = RecipeReference::RECIPE;
        if (recipeObj.contains("type") && recipeObj["type"].toString() == "script") {
          entryType = RecipeReference::SCRIPT;
        }

        auto recipeRef = std::make_unique<RecipeReference>(filePath, displayName, enabled, entryType);
        recipeRef->includeInLoop = includeInLoop;
        newSlot.recipes.push_back(std::move(recipeRef));
      }

      // Load dependency sets for this slot
      newSlot.dependencySets.clear();
      QJsonArray dependencySetsArray = slotObj["dependency_sets"].toArray();
      for (const QJsonValue& setVal : dependencySetsArray) {
        QJsonObject setObj = setVal.toObject();
        std::string setName = setObj["name"].toString("Default").toStdString();

        DependencySet depSet(setName);

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

        newSlot.dependencySets.push_back(depSet);
      }

      // Ensure at least one dependency set exists
      if (newSlot.dependencySets.empty()) {
        newSlot.dependencySets.emplace_back("Default");
      }

      // Validate active dependency set index
      if (newSlot.activeDependencySetIndex < 0 ||
          newSlot.activeDependencySetIndex >= static_cast<int>(newSlot.dependencySets.size())) {
        newSlot.activeDependencySetIndex = 0;
      }

      // Load graph node positions for this slot
      if (slotObj.contains("graph_positions")) {
        QJsonObject positionsObj = slotObj["graph_positions"].toObject();
        for (auto it = positionsObj.begin(); it != positionsObj.end(); ++it) {
          bool ok;
          int index = it.key().toInt(&ok);
          if (ok && it.value().isObject()) {
            QJsonObject posObj = it.value().toObject();
            double x = posObj["x"].toDouble();
            double y = posObj["y"].toDouble();
            newSlot.graphPositions[index] = GraphNodePosition(x, y);
          }
        }
      }

      newProgram.recipeSlots.push_back(std::move(newSlot));
    }

    std::cout << "[ProgramManager] Loaded " << newProgram.recipeSlots.size()
              << " slot(s) from new format" << std::endl;
  }
  else {
    // =========================================================================
    // LEGACY FORMAT: Migrate to single "Slot 1" with recipes at root
    // =========================================================================
    newProgram.recipeSlots.clear();
    RecipeSlot defaultSlot("Slot 1");

    // Check if old format has dependency_sets at root (intermediate format)
    bool hasRootDependencySets = root.contains("dependency_sets");

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

      // If very old format, collect dependencies from recipe objects
      if (!hasRootDependencySets) {
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

      defaultSlot.recipes.push_back(std::move(recipeRef));
      recipeIndex++;
    }

    // Load dependency sets from root (intermediate format) or migrate
    defaultSlot.dependencySets.clear();

    if (hasRootDependencySets) {
      // Intermediate format: dependency_sets at root level
      QJsonArray dependencySetsArray = root["dependency_sets"].toArray();

      for (const QJsonValue& setVal : dependencySetsArray) {
        QJsonObject setObj = setVal.toObject();
        std::string setName = setObj["name"].toString("Default").toStdString();

        DependencySet depSet(setName);

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

        defaultSlot.dependencySets.push_back(depSet);
      }

      defaultSlot.activeDependencySetIndex = root["active_dependency_set_index"].toInt(0);
      std::cout << "[ProgramManager] Migrated intermediate format with "
                << defaultSlot.dependencySets.size() << " dependency set(s)" << std::endl;
    }
    else {
      // Very old format: migrate to single "Default" dependency set
      DependencySet defaultSet("Default");
      defaultSet.dependencies = legacyDependencies;
      defaultSlot.dependencySets.push_back(defaultSet);
      defaultSlot.activeDependencySetIndex = 0;

      std::cout << "[ProgramManager] Migrated legacy dependencies to 'Default' set" << std::endl;
    }

    // Ensure at least one dependency set exists
    if (defaultSlot.dependencySets.empty()) {
      defaultSlot.dependencySets.emplace_back("Default");
    }

    // Validate active dependency set index
    if (defaultSlot.activeDependencySetIndex < 0 ||
        defaultSlot.activeDependencySetIndex >= static_cast<int>(defaultSlot.dependencySets.size())) {
      defaultSlot.activeDependencySetIndex = 0;
    }

    // Load graph node positions from root (legacy location)
    if (root.contains("graph_positions")) {
      QJsonObject positionsObj = root["graph_positions"].toObject();
      for (auto it = positionsObj.begin(); it != positionsObj.end(); ++it) {
        bool ok;
        int index = it.key().toInt(&ok);
        if (ok && it.value().isObject()) {
          QJsonObject posObj = it.value().toObject();
          double x = posObj["x"].toDouble();
          double y = posObj["y"].toDouble();
          defaultSlot.graphPositions[index] = GraphNodePosition(x, y);
        }
      }
      std::cout << "[ProgramManager] Loaded " << defaultSlot.graphPositions.size()
                << " graph node positions from legacy format" << std::endl;
    }

    newProgram.recipeSlots.push_back(std::move(defaultSlot));
    newProgram.activeSlotIndex = 0;

    std::cout << "[ProgramManager] Migrated legacy format to single 'Slot 1'" << std::endl;
  }

  // Ensure at least one slot exists
  if (newProgram.recipeSlots.empty()) {
    newProgram.recipeSlots.emplace_back("Slot 1");
  }

  // Validate active slot index
  if (newProgram.activeSlotIndex < 0 ||
      newProgram.activeSlotIndex >= static_cast<int>(newProgram.recipeSlots.size())) {
    newProgram.activeSlotIndex = 0;
  }

  // Replace current program
  m_currentProgram = std::move(newProgram);
  m_currentFilePath = filename;

  int totalRecipes = 0;
  for (const auto& slot : m_currentProgram.recipeSlots) {
    totalRecipes += static_cast<int>(slot.recipes.size());
  }

  std::cout << "[ProgramManager] Loaded program '" << m_currentProgram.name
    << "' with " << m_currentProgram.recipeSlots.size() << " slot(s) and "
    << totalRecipes << " total recipes from " << filename << std::endl;

  return true;
}