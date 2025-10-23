// RecipeManager.cpp
#include "RecipeManager.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <random>
#include <iomanip>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>

RecipeManager::RecipeManager()
  : m_currentFilePath("")
{
  std::cout << "[RecipeManager] Created" << std::endl;
}

RecipeManager::~RecipeManager()
{
  std::cout << "[RecipeManager] Destroyed" << std::endl;
}

void RecipeManager::newRecipe()
{
  m_currentRecipe = Recipe();
  m_currentFilePath.clear();
  std::cout << "[RecipeManager] New recipe created" << std::endl;
}

void RecipeManager::addStep(const std::string& processType)
{
  std::string uniqueId = generateUniqueId(processType);
  std::string nickname = processType + " " + std::to_string(m_currentRecipe.steps.size() + 1);

  auto instance = std::make_unique<ProcessInstance>(processType, nickname, uniqueId);
  instance->parameters = getDefaultParameters(processType);

  m_currentRecipe.steps.push_back(std::move(instance));

  std::cout << "[RecipeManager] Added step: " << processType
    << " (ID: " << uniqueId << ")" << std::endl;
}

void RecipeManager::removeStep(int index)
{
  if (index < 0 || index >= static_cast<int>(m_currentRecipe.steps.size())) {
    std::cerr << "[RecipeManager] Invalid index for removeStep: " << index << std::endl;
    return;
  }

  std::string removedId = m_currentRecipe.steps[index]->uniqueId;
  m_currentRecipe.steps.erase(m_currentRecipe.steps.begin() + index);

  std::cout << "[RecipeManager] Removed step at index " << index
    << " (ID: " << removedId << ")" << std::endl;
}

bool RecipeManager::moveStepUp(int index)
{
  if (index <= 0 || index >= static_cast<int>(m_currentRecipe.steps.size())) {
    return false;
  }

  std::swap(m_currentRecipe.steps[index], m_currentRecipe.steps[index - 1]);
  std::cout << "[RecipeManager] Moved step " << index << " up" << std::endl;
  return true;
}

bool RecipeManager::moveStepDown(int index)
{
  if (index < 0 || index >= static_cast<int>(m_currentRecipe.steps.size()) - 1) {
    return false;
  }

  std::swap(m_currentRecipe.steps[index], m_currentRecipe.steps[index + 1]);
  std::cout << "[RecipeManager] Moved step " << index << " down" << std::endl;
  return true;
}

ProcessInstance* RecipeManager::getStep(int index)
{
  if (index < 0 || index >= static_cast<int>(m_currentRecipe.steps.size())) {
    return nullptr;
  }
  return m_currentRecipe.steps[index].get();
}

const ProcessInstance* RecipeManager::getStep(int index) const
{
  if (index < 0 || index >= static_cast<int>(m_currentRecipe.steps.size())) {
    return nullptr;
  }
  return m_currentRecipe.steps[index].get();
}

void RecipeManager::setStepNickname(int index, const std::string& nickname)
{
  ProcessInstance* step = getStep(index);
  if (step) {
    step->userNickname = nickname;
  }
}

void RecipeManager::setStepEnabled(int index, bool enabled)
{
  ProcessInstance* step = getStep(index);
  if (step) {
    step->enabled = enabled;
  }
}

void RecipeManager::setStepParameter(int index, const std::string& key, const std::string& value)
{
  ProcessInstance* step = getStep(index);
  if (step) {
    step->parameters[key] = value;
  }
}

std::string RecipeManager::generateUniqueId(const std::string& processType)
{
  // Generate random alphanumeric ID: 3 letters + 3 digits + 1 letter
  // Example: XKJ515D, ABC123Z, etc.

  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<> letter_dist(0, 25);  // A-Z
  static std::uniform_int_distribution<> digit_dist(0, 9);    // 0-9

  std::stringstream ss;

  // 3 random uppercase letters
  for (int i = 0; i < 3; i++) {
    ss << static_cast<char>('A' + letter_dist(gen));
  }

  // 3 random digits
  for (int i = 0; i < 3; i++) {
    ss << digit_dist(gen);
  }

  // 1 random uppercase letter
  ss << static_cast<char>('A' + letter_dist(gen));

  std::string id = ss.str();

  // Optional: Check if ID already exists (very unlikely with this format)
  for (const auto& step : m_currentRecipe.steps) {
    if (step->uniqueId == id) {
      // Regenerate if duplicate (recursive call)
      return generateUniqueId(processType);
    }
  }

  return id;
}

std::vector<std::string> RecipeManager::getAvailableProcessTypes()
{
  return {
    "Pick",
    "Place",
    "Pick and Place",
    "Move to Position",
    "Move Relative",
    "Set Output",
    "Wait",
    "Ask User",
    "Move Relative Multi-Axis",
    "Move to Position with Offset",
    "Slide Extend",
    "Slide Retract",
    "Set TEC Temp",
    "Set Laser Current",
    "Read DataStore",
    "Read Temperature",      // ← ADD
    "Read Laser Current"     // ← ADD
  };
}

std::map<std::string, std::string> RecipeManager::getDefaultParameters(const std::string& processType)
{
  std::map<std::string, std::string> params;

  if (processType == "Pick") {
    params["gripper_device"] = "hex-left";
    params["camera_device"] = "gantry-main";
    params["io_device"] = "IOBottom";
    params["gripper_pin"] = "0";
    params["pick_node"] = "node_5647";
    params["view_node"] = "node_4186";
    params["release_wait_ms"] = "1500";
    params["regrip_wait_ms"] = "500";
    params["lens_name"] = "lens";
  }
  else if (processType == "Place") {
    params["gripper_device"] = "hex-left";
    params["camera_device"] = "gantry-main";
    params["io_device"] = "IOBottom";
    params["gripper_pin"] = "0";
    params["place_node"] = "node_5662";
    params["view_node"] = "node_4137";
    params["lens_name"] = "lens";
  }
  else if (processType == "Pick and Place") {
    params["gripper_device"] = "hex-left";
    params["camera_device"] = "gantry-main";
    params["io_device"] = "IOBottom";
    params["gripper_pin"] = "0";
    params["pick_node"] = "node_5647";
    params["pick_view_node"] = "node_4186";
    params["place_node"] = "node_5662";
    params["place_view_node"] = "node_4137";
    params["release_wait_ms"] = "1500";
    params["regrip_wait_ms"] = "500";
    params["lens_name"] = "lens";
  }
  else if (processType == "Move to Position") {
    params["device"] = "gantry-main";
    params["target_position"] = "safe";
  }
  else if (processType == "Move Relative") {
    params["device"] = "gantry-main";
    params["axis"] = "Z";
    params["distance"] = "10.0";
  }
  else if (processType == "Set Output") {
    params["io_device"] = "IOBottom";
    params["pin"] = "0";
    params["state"] = "true";  // "true" for ON, "false" for OFF
  }
  else if (processType == "Wait") {
    params["duration_ms"] = "1000";  // Default 1 second
  }
  else if (processType == "Ask User") {
    params["title"] = "Confirmation Required";
    params["message"] = "Please confirm to continue";
    params["blocking"] = "true";  // "true" = wait for response, "false" = just show notification
  }
  else if (processType == "Move Relative Multi-Axis") {
    params["device"] = "gantry-main";
    params["distance_x"] = "0";
    params["distance_y"] = "0";
    params["distance_z"] = "0";
  }
  else if (processType == "Move to Position with Offset") {
    params["device"] = "gantry-main";
    params["base_position"] = "safe";
    params["offset_x"] = "0";
    params["offset_y"] = "0";
    params["offset_z"] = "0";
  }
  else if (processType == "Slide Extend") {
    //params["device"] = "pneumatic";  // ✅ Make sure this line exists
    params["slide_name"] = "UV_Head";
    params["inbounce_delay_ms"] = "300";
    params["timeout_ms"] = "10000";
    params["debounce_delay_ms"] = "300";
  }
  else if (processType == "Slide Retract") {
    //params["device"] = "pneumatic";  // ✅ Make sure this line exists
    params["slide_name"] = "UV_Head";
    params["inbounce_delay_ms"] = "300";
    params["timeout_ms"] = "10000";
    params["debounce_delay_ms"] = "300";
  }
  else if (processType == "Set TEC Temp") {
    params["device"] = "tec";        // Device name
    params["temperature"] = "25.0";  // Default 25°C
    params["power_state"] = "on";    // "on" or "off"
  }
  else if (processType == "Set Laser Current") {
    params["device"] = "laser";      // Device name
    params["current_ma"] = "150";    // Default 100 mA
    params["power_state"] = "on";    // "on" or "off"
  }
  else if (processType == "Read DataStore") {  // ← ADD THIS BLOCK
    params["channel_name"] = "GPIB-Current";
    params["log_level"] = "Info";
  }
  else if (processType == "Read Temperature") {
    params["tec_device"] = "TEC1";
    params["log_level"] = "Info";
  }
  else if (processType == "Read Laser Current") {
    params["tec_device"] = "TEC1";
    params["log_level"] = "Info";
  }
  return params;
}

bool RecipeManager::validateRecipe(std::vector<std::string>& errors) const
{
  errors.clear();

  if (m_currentRecipe.steps.empty()) {
    errors.push_back("Recipe has no steps");
  }

  // Check for duplicate IDs
  std::map<std::string, int> idCount;
  for (const auto& step : m_currentRecipe.steps) {
    idCount[step->uniqueId]++;
  }

  for (const auto& [id, count] : idCount) {
    if (count > 1) {
      errors.push_back("Duplicate ID found: " + id);
    }
  }

  return errors.empty();
}

bool RecipeManager::loadRecipe(const std::string& filename)
{
  return loadFromJson(filename);
}

bool RecipeManager::saveRecipe(const std::string& filename)
{
  if (saveToJson(filename)) {
    m_currentFilePath = filename;
    return true;
  }
  return false;
}

bool RecipeManager::saveToJson(const std::string& filename) const
{
  QJsonObject root;
  root["version"] = QString::fromStdString(m_currentRecipe.version);
  root["recipe_name"] = QString::fromStdString(m_currentRecipe.name);
  root["next_id_counter"] = m_currentRecipe.nextIdCounter;
  root["product_file"] = QString::fromStdString(m_currentRecipe.productFile);

  // ✅ Debug output
  std::cout << "[RecipeManager] Saving product_file: '" << m_currentRecipe.productFile << "'" << std::endl;

  QJsonArray stepsArray;
  for (const auto& step : m_currentRecipe.steps) {
    QJsonObject stepObj;
    stepObj["id"] = QString::fromStdString(step->uniqueId);
    stepObj["type"] = QString::fromStdString(step->processType);
    stepObj["nickname"] = QString::fromStdString(step->userNickname);
    stepObj["enabled"] = step->enabled;

    QJsonObject paramsObj;
    for (const auto& [key, value] : step->parameters) {
      paramsObj[QString::fromStdString(key)] = QString::fromStdString(value);
    }
    stepObj["parameters"] = paramsObj;

    stepsArray.append(stepObj);
  }

  root["steps"] = stepsArray;

  QJsonDocument doc(root);

  QFile file(QString::fromStdString(filename));
  if (!file.open(QIODevice::WriteOnly)) {
    std::cerr << "[RecipeManager] Failed to write file: " << filename << std::endl;
    return false;
  }

  file.write(doc.toJson(QJsonDocument::Indented));
  file.close();

  std::cout << "[RecipeManager] Saved recipe '" << m_currentRecipe.name
    << "' with " << m_currentRecipe.steps.size() << " steps to "
    << filename << std::endl;

  return true;
}



//```
//
//The key additions are :
//1. Line 5 : `root["product_file"] = QString::fromStdString(m_currentRecipe.productFile); `
//2. Lines 7 - 8: Debug output to show what's being saved
//
//Now when you save, check the console.It should print something like :
//```
//[RecipeManager] Saving product_file : 'config/lens1/positions.json'




bool RecipeManager::loadFromJson(const std::string& filename)
{
  QFile file(QString::fromStdString(filename));
  if (!file.open(QIODevice::ReadOnly)) {
    std::cerr << "[RecipeManager] Failed to open file: " << filename << std::endl;
    return false;
  }

  QByteArray data = file.readAll();
  file.close();

  QJsonDocument doc = QJsonDocument::fromJson(data);
  if (!doc.isObject()) {
    std::cerr << "[RecipeManager] Invalid JSON format" << std::endl;
    return false;
  }

  QJsonObject root = doc.object();

  // Create new recipe
  Recipe newRecipe;
  newRecipe.version = root["version"].toString("1.0").toStdString();
  newRecipe.name = root["recipe_name"].toString("Untitled Recipe").toStdString();
  newRecipe.nextIdCounter = root["next_id_counter"].toInt(1);
  newRecipe.productFile = root["product_file"].toString("").toStdString();  // ✅ Added

  QJsonArray stepsArray = root["steps"].toArray();

  for (const QJsonValue& val : stepsArray) {
    QJsonObject stepObj = val.toObject();

    std::string id = stepObj["id"].toString().toStdString();
    std::string type = stepObj["type"].toString().toStdString();
    std::string nickname = stepObj["nickname"].toString().toStdString();

    auto instance = std::make_unique<ProcessInstance>(type, nickname, id);
    instance->enabled = stepObj["enabled"].toBool(true);

    QJsonObject paramsObj = stepObj["parameters"].toObject();
    for (const QString& key : paramsObj.keys()) {
      instance->parameters[key.toStdString()] =
        paramsObj[key].toString().toStdString();
    }

    newRecipe.steps.push_back(std::move(instance));
  }

  // Replace current recipe
  m_currentRecipe = std::move(newRecipe);
  m_currentFilePath = filename;

  std::cout << "[RecipeManager] Loaded recipe '" << m_currentRecipe.name
    << "' with " << m_currentRecipe.steps.size() << " steps from "
    << filename << std::endl;

  return true;
}