// QuickAddController.h
// Two-keystroke "quick add" UX for the recipe editor: Ctrl+B activates
// an overlay that lists process categories (1-9). The first number key
// picks a category; the second picks a process within that category;
// the chosen process type is emitted as a signal so the page can insert
// it into the recipe.
//
// Extracted from RecipePage so the page class can stay focused on
// orchestration. Behaviour is preserved exactly:
//   * Ctrl+B toggles the overlay on/off.
//   * Esc cancels.
//   * Hidden processes (managed by ProcessCatalogWidget) are filtered
//     out so quick-add indexing matches what the user sees.
//   * Only the first 9 categories / processes are addressable by key.
#pragma once

#include <QObject>
#include <QString>

class QWidget;
class QLabel;
class QKeyEvent;
class RecipeManager;
class ProcessCatalogWidget;

class QuickAddController : public QObject
{
  Q_OBJECT

public:
  // `pageWidget` is the parent for the floating overlay (its size drives
  // the overlay's centering). `recipeManager` provides the category /
  // process registry. `processCatalog` provides the hidden-process set
  // so quick-add indexing matches the visible catalog.
  QuickAddController(QWidget* pageWidget,
                     RecipeManager* recipeManager,
                     ProcessCatalogWidget* processCatalog,
                     QObject* parent = nullptr);

  bool IsActive() const { return m_active; }

  void Activate();
  void Deactivate();

  // Route a key event from the page's keyPressEvent. Returns true if the
  // event was consumed (page should accept and stop propagation), false
  // if not relevant (page should fall through to base class).
  bool HandleKeyPress(QKeyEvent* event);

signals:
  // Emitted when the user has picked a process type. The page handles
  // the actual recipe insertion + UI refresh.
  void processSelected(const QString& processType);

private:
  void handleCategoryKey(int key);
  void handleProcessKey(int key);
  void updateOverlay();
  void ensureOverlay();

  QWidget* m_pageWidget;          // For overlay parent + centering.
  RecipeManager* m_recipeManager;
  ProcessCatalogWidget* m_processCatalog;

  bool m_active = false;
  int m_selectedCategory = -1;    // -1 = no category selected yet.
  QLabel* m_overlay = nullptr;    // Lazy-created on first Activate().
};
