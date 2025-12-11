// JsonFileListWidget.h
#pragma once

#include <QWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QLabel>
#include <QString>
#include <QDir>

/**
 * @brief Left panel widget showing list of JSON files with filter
 *
 * Features:
 * - Text filter bar for filename filtering
 * - List of .json files from selected folder
 * - Click to select file
 */
class JsonFileListWidget : public QWidget
{
    Q_OBJECT

public:
    explicit JsonFileListWidget(QWidget* parent = nullptr);
    ~JsonFileListWidget() = default;

    void setFolder(const QString& folderPath);
    QString currentFolder() const { return m_folderPath; }

signals:
    void fileSelected(const QString& filePath);

private slots:
    void onFilterChanged(const QString& text);
    void onFileClicked(QListWidgetItem* item);
    void onFileDoubleClicked(QListWidgetItem* item);

private:
    void setupUI();
    void refreshFileList();
    void applyFilter(const QString& filter);

    // UI Components
    QLabel* m_folderLabel;
    QLineEdit* m_filterEdit;
    QListWidget* m_fileList;

    // State
    QString m_folderPath;
    QStringList m_allFiles;  // All JSON files in folder
};
