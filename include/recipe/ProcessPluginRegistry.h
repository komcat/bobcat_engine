// ProcessPluginRegistry.h
#pragma once

#include "IProcessPlugin.h"
#include <string>
#include <map>
#include <vector>
#include <memory>

/**
 * @brief Central registry for all process plugins
 *
 * Singleton that manages all registered process plugins.
 * Process plugins self-register at startup using static initialization.
 *
 * Usage:
 *   auto& registry = ProcessPluginRegistry::Instance();
 *   registry.RegisterPlugin(std::make_unique<MyProcessPlugin>());
 *   IProcessPlugin* plugin = registry.GetPlugin("My Process");
 */
class ProcessPluginRegistry
{
public:
  /**
   * @brief Get the singleton instance
   */
  static ProcessPluginRegistry& Instance();

  /**
   * @brief Register a process plugin
   * @param plugin Unique pointer to plugin (registry takes ownership)
   */
  void RegisterPlugin(std::unique_ptr<IProcessPlugin> plugin);

  /**
   * @brief Get all registered process type names
   * @return Vector of process type names (e.g., ["Pick", "Place", "Wait", ...])
   */
  std::vector<std::string> GetAllProcessTypes() const;

  /**
   * @brief Get all process types in a specific category
   * @param category Category name (e.g., "Motion", "System", "I/O & Pneumatics")
   * @return Vector of process type names in that category
   */
  std::vector<std::string> GetProcessesByCategory(const std::string& category) const;

  /**
   * @brief Get all unique category names
   * @return Vector of category names
   */
  std::vector<std::string> GetAllCategories() const;

  /**
   * @brief Get the plugin for a specific process type
   * @param processType Process type name (e.g., "Manual Adjust")
   * @return Pointer to plugin, or nullptr if not found
   */
  IProcessPlugin* GetPlugin(const std::string& processType);

  /**
   * @brief Get the category for a process type
   * @param processType Process type name
   * @return Category name, or "Uncategorized" if not found
   */
  std::string GetProcessCategory(const std::string& processType) const;

  /**
   * @brief Get default parameters for a process type
   * @param processType Process type name
   * @return Map of parameter name -> default value
   */
  std::map<std::string, std::string> GetDefaultParameters(const std::string& processType) const;

  /**
   * @brief Check if a process type is registered
   * @param processType Process type name
   * @return true if registered, false otherwise
   */
  bool HasPlugin(const std::string& processType) const;

private:
  ProcessPluginRegistry() = default;
  ~ProcessPluginRegistry() = default;

  // Delete copy/move constructors
  ProcessPluginRegistry(const ProcessPluginRegistry&) = delete;
  ProcessPluginRegistry& operator=(const ProcessPluginRegistry&) = delete;
  ProcessPluginRegistry(ProcessPluginRegistry&&) = delete;
  ProcessPluginRegistry& operator=(ProcessPluginRegistry&&) = delete;

  std::map<std::string, std::unique_ptr<IProcessPlugin>> m_plugins;
};
