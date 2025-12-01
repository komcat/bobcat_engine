// RecipeTreeItemFactory.h
// Factory for creating QTreeWidgetItems for the recipe steps list
#pragma once

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QWidget>
#include <QLabel>
#include <QCheckBox>
#include <functional>
#include <string>

// Forward declarations
class RecipeManager;
struct ProcessInstance;

/**
 * @brief Factory for creating tree widget items for recipe steps
 *
 * Encapsulates all the widget creation logic for recipe step rows,
 * including breakpoint checkbox, step number, symbol, process name, and run checkbox.
 */
class RecipeTreeItemFactory
{
public:
  // Callback types for user interactions
  using BreakpointCallback = std::function<void(int stepIndex, bool checked)>;
  using EnabledCallback = std::function<void(int stepIndex, bool enabled)>;

  /**
   * @brief Construct factory with callbacks for user interactions
   */
  RecipeTreeItemFactory(
    QTreeWidget* treeWidget,
    RecipeManager* recipeManager,
    BreakpointCallback onBreakpointChanged,
    EnabledCallback onEnabledChanged
  );

  /**
   * @brief Create a complete tree widget item for a process step
   * @param step The process instance data
   * @param stepIndex The index of the step in the recipe
   * @param parent Parent item for nested steps (nullptr for top-level)
   * @return The created tree widget item
   */
  QTreeWidgetItem* createStepItem(
    const ProcessInstance* step,
    int stepIndex,
    QTreeWidgetItem* parent = nullptr
  );

  /**
   * @brief Update an existing item's process name label (for fast nickname updates)
   * @param item The tree widget item to update
   * @param step The process instance with updated data
   */
  void updateProcessNameLabel(QTreeWidgetItem* item, const ProcessInstance* step);

  /**
   * @brief Update all widgets of an existing tree item with new step data
   * @param item The tree widget item to update
   * @param step The new process instance data
   * @param stepIndex The new step index (for display purposes)
   */
  void updateStepItem(QTreeWidgetItem* item, const ProcessInstance* step, int stepIndex);

  /**
   * @brief Get category symbol for a process type
   */
  static QString getCategorySymbol(const std::string& category);

private:
  // Widget creation helpers
  QWidget* createBreakpointWidget(const ProcessInstance* step, int stepIndex, const QString& bgColor);
  QWidget* createStepNumberWidget(int stepIndex, const QString& bgColor, bool enabled);
  QWidget* createSymbolWidget(const ProcessInstance* step, const QString& bgColor);
  QWidget* createProcessNameWidget(const ProcessInstance* step, const QString& bgColor);
  QWidget* createRunCheckboxWidget(const ProcessInstance* step, int stepIndex, const QString& bgColor);

  // Helper to determine display text for process name
  QString getDisplayText(const ProcessInstance* step) const;

  // Helper to get icon prefix based on step type
  QString getIconPrefix(const ProcessInstance* step) const;

  // Helper to get background color string
  QString getBackgroundColor(const ProcessInstance* step) const;

  QTreeWidget* m_treeWidget;
  RecipeManager* m_recipeManager;
  BreakpointCallback m_onBreakpointChanged;
  EnabledCallback m_onEnabledChanged;
};
