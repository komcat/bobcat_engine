// ProcessPluginRegistry.cpp
#include "ProcessPluginRegistry.h"
#include <algorithm>
#include <iostream>

ProcessPluginRegistry& ProcessPluginRegistry::Instance()
{
  static ProcessPluginRegistry instance;
  return instance;
}

void ProcessPluginRegistry::RegisterPlugin(std::unique_ptr<IProcessPlugin> plugin)
{
  if (!plugin) {
    std::cerr << "[ProcessPluginRegistry] Attempted to register null plugin" << std::endl;
    return;
  }

  std::string name = plugin->GetName();

  if (m_plugins.find(name) != m_plugins.end()) {
    std::cerr << "[ProcessPluginRegistry] Plugin '" << name << "' is already registered. Skipping." << std::endl;
    return;
  }

  std::cout << "[ProcessPluginRegistry] Registered plugin: " << name
            << " (Category: " << plugin->GetCategory() << ")" << std::endl;

  m_plugins[name] = std::move(plugin);
}

std::vector<std::string> ProcessPluginRegistry::GetAllProcessTypes() const
{
  std::vector<std::string> types;
  types.reserve(m_plugins.size());

  for (const auto& pair : m_plugins) {
    types.push_back(pair.first);
  }

  return types;
}

std::vector<std::string> ProcessPluginRegistry::GetProcessesByCategory(const std::string& category) const
{
  std::vector<std::string> types;

  for (const auto& pair : m_plugins) {
    if (pair.second->GetCategory() == category) {
      types.push_back(pair.first);
    }
  }

  return types;
}

std::vector<std::string> ProcessPluginRegistry::GetAllCategories() const
{
  std::vector<std::string> categories;

  for (const auto& pair : m_plugins) {
    std::string category = pair.second->GetCategory();

    // Only add if not already in the list
    if (std::find(categories.begin(), categories.end(), category) == categories.end()) {
      categories.push_back(category);
    }
  }

  return categories;
}

IProcessPlugin* ProcessPluginRegistry::GetPlugin(const std::string& processType)
{
  auto it = m_plugins.find(processType);
  if (it != m_plugins.end()) {
    return it->second.get();
  }
  return nullptr;
}

std::string ProcessPluginRegistry::GetProcessCategory(const std::string& processType) const
{
  auto it = m_plugins.find(processType);
  if (it != m_plugins.end()) {
    return it->second->GetCategory();
  }
  return "Uncategorized";
}

std::map<std::string, std::string> ProcessPluginRegistry::GetDefaultParameters(const std::string& processType) const
{
  auto it = m_plugins.find(processType);
  if (it != m_plugins.end()) {
    return it->second->GetDefaultParameters();
  }
  return {};
}

bool ProcessPluginRegistry::HasPlugin(const std::string& processType) const
{
  return m_plugins.find(processType) != m_plugins.end();
}
