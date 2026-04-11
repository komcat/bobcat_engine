// RecipeRunWindow.h
//
// Top-level floating window that hosts the four recipe execution-time
// widgets (RecipeProgressWidget, ManualAdjustWidget, ManualAlignPointWidget,
// UserInputWidget) in a QStackedWidget.
//
// Replaces the previous behaviour of swapping these widgets into the
// RecipePage right-panel scroll area, freeing the right panel to keep
// showing Process Configuration during execution and giving operators a
// resizable, dockable, multi-monitor-friendly run UI.
//
// Lifetime: created once in RecipePage::setupUI(), reused across runs.
// While a recipe is running the window's close button is trapped so the
// operator must press STOP first.
#pragma once

#include <QWidget>

class QStackedWidget;
class QCloseEvent;
class QShowEvent;

class RecipeRunWindow : public QWidget
{
  Q_OBJECT

public:
  explicit RecipeRunWindow(QWidget* parent = nullptr);
  ~RecipeRunWindow() override;

  /**
   * @brief Show one of the registered widgets in the internal stack.
   *
   * The first call for a given widget reparents it into the stack.
   * Subsequent calls just switch the visible page.  Pass nullptr to leave
   * the stack empty (e.g., during teardown).
   */
  void setActiveWidget(QWidget* w);

  /**
   * @brief Mark the window as running, restore geometry, show, raise, and
   *        activate it.  While running, the close button is blocked.
   */
  void openForRun();

  /**
   * @brief Mark the window as not running, save geometry, and hide it.
   */
  void closeAfterRun();

  /**
   * @brief True while a recipe is executing.  Used by RecipePage to decide
   *        whether to dismiss the window from the auto-dismiss timer.
   */
  [[nodiscard]] bool isRunning() const { return m_running; }

signals:
  /**
   * @brief Emitted when the operator clicks the X button while a run is in
   *        progress.  RecipePage may use this as a hint (no required action).
   */
  void closeAttemptedWhileRunning();

protected:
  void closeEvent(QCloseEvent* event) override;
  void showEvent(QShowEvent* event) override;

private:
  void persistGeometry();
  void restorePersistedGeometry();
  void centerOnParent();

  QStackedWidget* m_stack = nullptr;
  bool m_running = false;
  bool m_geometryRestored = false;
};
