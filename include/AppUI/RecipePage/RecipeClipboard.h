// RecipeClipboard.h
// Clipboard for copy/paste operations on recipe steps
#pragma once

#include <string>
#include <map>
#include <vector>

// Forward declaration
struct ProcessInstance;

/**
 * @brief Manages clipboard operations for recipe steps
 *
 * Provides copy/paste functionality for process steps in the recipe editor.
 * Stores step data including process type, nickname, enabled state, and parameters.
 */
class RecipeClipboard
{
public:
  /**
   * @brief Data structure for a copied step
   */
  struct ClipboardStep {
    std::string processType;
    std::string userNickname;
    bool enabled;
    std::map<std::string, std::string> parameters;
  };

  RecipeClipboard() = default;
  ~RecipeClipboard() = default;

  /**
   * @brief Clear the clipboard
   */
  void clear();

  /**
   * @brief Check if clipboard has content
   */
  bool hasContent() const;

  /**
   * @brief Get number of steps in clipboard
   */
  int count() const;

  /**
   * @brief Copy a single step to clipboard (replaces existing content)
   */
  void copyStep(const ProcessInstance* step);

  /**
   * @brief Copy multiple steps to clipboard (replaces existing content)
   * @param steps Vector of step pointers to copy
   */
  void copySteps(const std::vector<const ProcessInstance*>& steps);

  /**
   * @brief Get all copied steps
   */
  const std::vector<ClipboardStep>& getSteps() const;

  /**
   * @brief Get a summary string for display (e.g., "3 steps copied")
   */
  std::string getSummary() const;

private:
  std::vector<ClipboardStep> m_steps;
};
