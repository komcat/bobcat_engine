// ProcessSequenceFactory.h
#pragma once

#include "SequenceEngine.h"
#include "RecipeManager.h"
#include <string>
#include <map>
#include <functional>

/**
 * @brief Factory for building sequences from process instances
 *
 * This factory pattern keeps the RecipePage clean and makes it easy
 * to add new process types without modifying existing code.
 */
class ProcessSequenceFactory
{
public:
  // Builder function type - takes sequence and step, builds the operations
  using SequenceBuilderFunc = std::function<void(
    SequenceBuilder* sequence,
    const ProcessInstance* step
    )>;

  ProcessSequenceFactory();
  ~ProcessSequenceFactory() = default;

  // Register a builder for a process type
  void RegisterProcessBuilder(const std::string& processType, SequenceBuilderFunc builder);

  // Build sequence for a specific step
  bool BuildStep(SequenceBuilder* sequence, const ProcessInstance* step, std::string& errorMsg);

  // Check if a process type is supported
  bool IsSupported(const std::string& processType) const;

  // Get all supported process types
  std::vector<std::string> GetSupportedProcessTypes() const;

private:
  std::map<std::string, SequenceBuilderFunc> m_builders;
};