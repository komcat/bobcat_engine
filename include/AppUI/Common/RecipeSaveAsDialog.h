// RecipeSaveAsDialog.h
#pragma once

#include <QDialog>
#include <QString>
#include <QTableWidget>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>

/**
 * @brief Custom save dialog for recipes that shows existing recipes and allows new name entry
 *
 * Features:
 * - Shows existing recipes in the recipes folder
 * - Text field to enter new recipe name
 * - Validates filename and prevents overwrites
 * - Consistent styling with FileSelectorDialog
 */
class RecipeSaveAsDialog : public QDialog
{
  Q_OBJECT

public:
  explicit RecipeSaveAsDialog(const QString& folderPath, const QString& currentRecipeName = "", QWidget* parent = nullptr);

  /**
   * @brief Get the selected save file path (absolute path)
   * @return Full path to save file, or empty string if cancelled
   */
  QString getSaveFilePath() const { return m_saveFilePath; }

  /**
   * @brief Static convenience method - show dialog and return save path
   * @param parent Parent widget
   * @param folderPath Folder to save in (e.g., "config/product1/recipes")
   * @param currentRecipeName Current recipe name (for default)
   * @return Full path to save file, or empty string if cancelled
   */
  static QString getSaveFileName(QWidget* parent, const QString& folderPath, const QString& currentRecipeName = "");

private slots:
  void onFileClicked(int row, int column);
  void onSaveClicked();
  void onCancel();
  void onRefresh();
  void onNameTextChanged(const QString& text);

private:
  void setupUI();
  void loadExistingRecipes();
  void populateRecipeTable();
  bool validateFilename(const QString& filename);

  QString m_currentRecipeName;
  QString m_saveFilePath;
  QString m_folderPath;

  QTableWidget* m_recipeTable;
  QLabel* m_folderLabel;
  QLabel* m_fileCountLabel;
  QLineEdit* m_nameEdit;
  QLabel* m_previewLabel;
  QPushButton* m_saveButton;
  QPushButton* m_cancelButton;
  QPushButton* m_refreshButton;

  struct RecipeInfo {
    QString fileName;
    QString fullPath;
    QString recipeName;
    QString dateModified;
    qint64 fileSize;
  };

  QList<RecipeInfo> m_existingRecipes;
};
