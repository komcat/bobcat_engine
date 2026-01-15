// ContextVariablePreviewWidget.h
// Displays predicted context variables from a loaded recipe
#pragma once

#include <QWidget>
#include <QGroupBox>
#include <QListWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QPushButton>
#include <QSet>
#include <QString>
#include <map>
#include <string>
#include <vector>
#include <memory>

// Forward declarations
class RecipeManager;
struct ProcessInstance;

/**
 * @brief Widget that displays context variable names that will be available
 *        after executing the recipe.
 *
 * This widget scans recipe steps for parameters that define context variables:
 * - captureAs, capture_as: Stores captured data (images, measurements)
 * - context_key: Stores/reads values
 * - key: For Set Context Value operations
 * - output_context_key: Output destination
 * - result_key: Result storage
 */
class ContextVariablePreviewWidget : public QGroupBox
{
  Q_OBJECT

public:
  explicit ContextVariablePreviewWidget(QWidget* parent = nullptr);
  ~ContextVariablePreviewWidget() = default;

  /**
   * @brief Update the display from recipe steps
   * @param steps Vector of process instances to scan for context variables
   */
  void updateFromRecipe(const std::vector<std::unique_ptr<ProcessInstance>>& steps);

  /**
   * @brief Clear the display
   */
  void clear();

  /**
   * @brief Get the list of discovered variable names
   */
  QStringList getVariableNames() const;

signals:
  /**
   * @brief Emitted when a variable name is clicked
   * @param variableName The clicked variable name
   */
  void variableClicked(const QString& variableName);

private:
  void setupUI();

  /**
   * @brief Extract context variable names from a step's parameters
   * @param params The parameter map from a ProcessInstance
   * @return Set of variable names found
   */
  QSet<QString> extractVariablesFromStep(const std::map<std::string, std::string>& params);

  QListWidget* m_variableList;
  QLabel* m_countLabel;
  QPushButton* m_collapseButton;
  bool m_isCollapsed;

  // Parameter keys that indicate context variable definitions
  static const QStringList CONTEXT_PARAM_KEYS;
};
