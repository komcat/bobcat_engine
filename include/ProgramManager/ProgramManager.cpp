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