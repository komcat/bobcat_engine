// FileSelectorDialog.h
#pragma once

#include <QDialog>
#include <QString>
#include <QTableWidget>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>

/**
 * @brief Custom file selector dialog that restricts navigation to a specific folder
 *
 * This dialog prevents users from navigating away from the designated folder,
 * ensuring they load the correct type of file for the intended purpose.
 *
 * Features:
 * - Shows files from specified folder only
 * - No navigation to parent/other folders
 * - Displays file metadata (name, date, size)
 * - File type validation by extension
 * - Search/filter capabilities
 */
class FileSelectorDialog : public QDialog
{
  Q_OBJECT

public:
  /**
   * @brief Construct a file selector dialog
   * @param folderPath The folder to display files from (e.g., "recipes/", "programs/", "config/")
   * @param filePattern File pattern filter (e.g., "*.json")
   * @param dialogTitle Title for the dialog window
   * @param fileTypeDescription Description of file type (e.g., "Recipe Files", "Program Files")
   * @param recursive Search subdirectories recursively
   * @param parent Parent widget
   */
  explicit FileSelectorDialog(
    const QString& folderPath,
    const QString& filePattern,
    const QString& dialogTitle,
    const QString& fileTypeDescription,
    bool recursive = false,
    QWidget* parent = nullptr
  );

  /**
   * @brief Get the selected file path (absolute path)
   * @return Full path to selected file, or empty string if cancelled
   */
  QString getSelectedFilePath() const { return m_selectedFilePath; }

  /**
   * @brief Static convenience method - show dialog and return selected file
   * @param recursive Search subdirectories recursively
   * @return Full path to selected file, or empty string if cancelled
   */
  static QString getOpenFileName(
    QWidget* parent,
    const QString& folderPath,
    const QString& filePattern,
    const QString& dialogTitle,
    const QString& fileTypeDescription,
    bool recursive = false
  );

private slots:
  void onFileDoubleClicked(int row, int column);
  void onFileSelected();
  void onCancel();
  void onRefresh();
  void onSearchTextChanged(const QString& searchText);
  void onHeaderClicked(int logicalIndex);

private:
  void setupUI();
  void loadFiles();
  void populateFileTable();
  void applySearchFilter();
  void sortFiles();

  QString m_folderPath;
  QString m_filePattern;
  QString m_dialogTitle;
  QString m_fileTypeDescription;
  QString m_selectedFilePath;
  bool m_recursive;

  QTableWidget* m_fileTable;
  QLabel* m_folderLabel;
  QLabel* m_fileCountLabel;
  QPushButton* m_selectButton;
  QPushButton* m_cancelButton;
  QPushButton* m_refreshButton;
  QLineEdit* m_searchEdit;

  struct FileInfo {
    QString fileName;
    QString fullPath;
    QString relativePath;  // For displaying subdirectory structure
    QString dateModified;
    qint64 fileSize;
  };

  QList<FileInfo> m_allFiles;
  QList<FileInfo> m_filteredFiles;

  // Sorting state
  int m_sortColumn = -1;           // -1 = no sorting, otherwise column index
  Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
};
