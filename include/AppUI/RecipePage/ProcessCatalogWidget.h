// ProcessCatalogWidget.h
// Self-contained "Available Processes" catalog (the left panel of the
// RecipePage). Lists every registered process type, grouped by category,
// with a search box, a visibility manager (gear icon), a "selected
// process" preview label and a "+" add button.
//
// Extracted from RecipePage so the page class can stay focused on
// orchestration. Behaviour is preserved exactly:
//   * Categories come from RecipeManager::getProcessCategories() and
//     ::getProcessesByCategory(...).
//   * Hidden processes are loaded from
//     `config/hidden_processes(.local).json` via ProcessVisibilityDialog.
//   * "+" button or double-clicking a process item emits
//     `addProcessRequested(processType)`; the page handles the actual
//     recipe insertion.
//   * Quick-add mode (Ctrl+B on the page) reads `hiddenProcesses()` so
//     it skips the same items the catalog hides.
#pragma once

#include <QWidget>
#include <QString>
#include <string>
#include <unordered_set>

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QPushButton;

class ProcessCatalogWidget : public QWidget
{
  Q_OBJECT

public:
  explicit ProcessCatalogWidget(QWidget* parent = nullptr);

  // Reload the hidden-process set from disk and rebuild the tree.
  // Called externally on showEvent() to pick up edits made by other tools.
  void refresh();

  // The current hidden-process set. QuickAddController consults this so
  // it skips the same items the catalog hides.
  const std::unordered_set<std::string>& hiddenProcesses() const { return m_hiddenProcesses; }

signals:
  // Emitted when the user wants the page to insert this process type
  // into the recipe (via "+" button or double-click).
  void addProcessRequested(const QString& processType);

private slots:
  void onShowVisibilityDialog();
  void onProcessTemplateSelected(QTreeWidgetItem* item, int column);
  void onAddButtonClicked();

private:
  void setupUI();
  void loadHiddenProcessesFromDisk();
  // Re-populate the tree from RecipeManager + m_hiddenProcesses.
  // Does NOT touch m_hiddenProcesses; call loadHiddenProcessesFromDisk()
  // first if you need the on-disk set.
  void rebuildTreeFromCurrentState();

  std::unordered_set<std::string> m_hiddenProcesses;

  QTreeWidget* m_processTemplateList = nullptr;
  QLabel* m_selectedProcessLabel = nullptr;
  QPushButton* m_addButton = nullptr;
};
