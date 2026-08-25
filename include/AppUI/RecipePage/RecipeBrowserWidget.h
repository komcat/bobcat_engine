// RecipeBrowserWidget.h
// Self-contained "Recipes" tab for the RecipePage middle panel: lists all
// recipe JSON files in the active product's `config/{product}/recipes/`
// folder, supports drag-and-drop folder organisation, plus rename / copy /
// delete / move operations and pre-delete usage checking against programs
// and sub-recipe references.
//
// Extracted from RecipePage so the page class can stay focused on
// orchestration. Behaviour is preserved exactly:
//   * Recipe list is read from `config/{product}/recipes/` recursively
//     (one folder level deep — folders shown as expandable nodes).
//   * Drag-and-drop moves the on-disk file to the target folder after
//     a confirmation prompt.
//   * Right-click context menu offers Load / Rename / Delete / Move To...
//   * Delete checks for usage in programs and sub-recipe references and
//     warns the user before removing the file.
//
// The widget owns no recipe state; loading is delegated back to the page
// via the `loadRecipeRequested` signal.
#pragma once

#include <QWidget>
#include <QString>
#include <QStringList>

class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;
class QLabel;
class QPushButton;
class QEvent;
class QFileInfo;
class QSettings;
struct ApplicationContext;
class RecipeManager;

class RecipeBrowserWidget : public QWidget
{
  Q_OBJECT

public:
  RecipeBrowserWidget(ApplicationContext* context,
                      RecipeManager* recipeManager,
                      QWidget* parent = nullptr);

  // Repopulate the recipe list from disk. Call after a save, product
  // change, or any time on-disk state may have changed.
  void refresh();

  // Re-applies all locally-owned stylesheets using current Theme colours.
  void applyTheme();

signals:
  // Emitted when the user double-clicks a recipe or selects "Load Recipe"
  // from the context menu. The page handles the actual recipe load.
  void loadRecipeRequested(const QString& filePath);

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
  void onRefreshClicked();
  void onItemDoubleClicked(QTreeWidgetItem* item, int column);
  void onContextMenu(const QPoint& pos);
  void onCreateFolder();
  void onRenameFolder(QTreeWidgetItem* folderItem);
  void onDeleteFolder(QTreeWidgetItem* folderItem);
  void onCopyRecipe();
  void onDeleteSelectedRecipe();
  void onRenameSelectedRecipe();
  void onLoadSelectedRecipe();

private:
  void setupUI();
  QString getRecipeFolderPath() const;
  void moveRecipeToFolder(const QString& srcPath, const QString& targetFolderName);
  bool confirmRecipeMove(const QString& recipeName, const QString& targetFolder);
  QStringList findRecipeUsageInPrograms(const QString& recipeFilePath) const;
  QStringList findRecipeUsageAsSubRecipe(const QString& recipeFilePath) const;

  // Display name for a recipe file, i.e. its "recipe_name" field, falling
  // back to the file's base name.
  //
  // refresh() needs one short string per recipe, and used to get it by
  // opening every file, reading it whole and running QJsonDocument::fromJson
  // over it. With 59 recipes that measured 258ms on a release build with a
  // cold file cache (~800ms fully cold) - two thirds of the entire Recipe
  // page build - to obtain 59 strings.
  //
  // This caches the answer against the file's modification time and size, so
  // an unchanged recipe is never opened at all on later runs. The cache is
  // persisted in QSettings, because the expensive case is the first refresh
  // after the application starts.
  // `cache` must already be positioned in the RecipeNameCache group; refresh()
  // opens one for the whole listing rather than one per file.
  QString recipeDisplayName(const QFileInfo& fileInfo, QSettings& cache) const;

  // Reads "recipe_name" out of the first kHeadBytes of the file. Recipe JSON
  // is written with sorted keys, so "recipe_name" always precedes the large
  // "steps" array - in practice it lands around byte 56. Returns an empty
  // string if it is not in the head, in which case the caller parses fully.
  static QString readRecipeNameFromHead(const QString& absoluteFilePath);

  ApplicationContext* m_context;
  RecipeManager* m_recipeManager;

  QTreeWidget* m_recipeListWidget = nullptr;
  QLineEdit* m_recipeFilterEdit = nullptr;
  QLabel* m_recipeCountLabel = nullptr;
  QPushButton* m_refreshRecipeListButton = nullptr;
  QPushButton* m_createFolderButton = nullptr;
  QPushButton* m_removeFolderButton = nullptr;
  QPushButton* m_copyRecipeButton = nullptr;
  QPushButton* m_deleteRecipeButton = nullptr;
};
