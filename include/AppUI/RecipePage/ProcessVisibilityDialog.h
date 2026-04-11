// ProcessVisibilityDialog.h
//
// Modal dialog for managing which process plugins appear in the
// "Available Processes" picker on RecipePage.  Two-column shuttle UI:
// the left list shows visible processes, the right list shows hidden
// ones, and >> / << buttons move selected items between them.
//
// Each move auto-saves to config/hidden_processes.local.json (the local
// override always wins over the committed config/hidden_processes.json).
//
// Hidden processes are still registered in ProcessPluginRegistry — they
// just don't appear in the "add new step" picker.  Existing recipes that
// reference them load and execute normally.
#pragma once

#include <QDialog>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

class QListWidget;
class QLineEdit;
class QLabel;

class ProcessVisibilityDialog : public QDialog
{
  Q_OBJECT

public:
  /**
   * @brief Construct the dialog.
   * @param allProcessTypes Every process name registered with the registry.
   *                        The dialog snapshots this — it does not query
   *                        the registry itself.
   * @param hiddenSet Current hidden set, used as the working state.
   * @param categoryLookup Function returning the category string for a
   *                       given process name (used for display only).
   * @param parent Parent widget.
   */
  ProcessVisibilityDialog(
    const std::vector<std::string>& allProcessTypes,
    std::unordered_set<std::string> hiddenSet,
    std::function<std::string(const std::string&)> categoryLookup,
    QWidget* parent = nullptr);

  /** Final hidden set after the user's edits. */
  [[nodiscard]] const std::unordered_set<std::string>& hiddenProcesses() const
  {
    return m_hidden;
  }

  /**
   * @brief Static helper that loads the hidden set from disk using the
   *        local-first override pattern.  Used by both the dialog and
   *        RecipePage's startup loader so they share one code path.
   * @return The hidden set; empty if neither file exists or on parse error.
   */
  static std::unordered_set<std::string> loadHiddenFromDisk();

signals:
  /**
   * @brief Emitted after every successful save (i.e., after every >> or
   *        << click).  RecipePage connects this to its
   *        refreshProcessTemplateList() so the live tree behind the
   *        modal dialog updates immediately.
   */
  void visibilityChanged(const std::unordered_set<std::string>& hiddenSet);

private slots:
  void onMoveToHidden();
  void onMoveToVisible();
  void onSearchChanged(const QString& text);

private:
  void rebuildLists();
  void saveToJson();

  std::vector<std::string> m_allProcessTypes;
  std::unordered_set<std::string> m_hidden;
  std::function<std::string(const std::string&)> m_categoryLookup;
  QString m_currentFilter;

  QListWidget* m_visibleList = nullptr;
  QListWidget* m_hiddenList = nullptr;
  QLabel* m_visibleHeader = nullptr;
  QLabel* m_hiddenHeader = nullptr;
  QLineEdit* m_searchBox = nullptr;
};
