// ConfigEditorWindow.h
#pragma once

#include <QMainWindow>
#include <QSplitter>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QPushButton>
#include <QToolBar>
#include <QString>

class JsonFileListWidget;
class JsonEditorWidget;

/**
 * @brief Main window for the standalone JSON config editor
 *
 * Two-panel layout:
 * - Left panel: List of JSON files with filter
 * - Right panel: Dynamic JSON field editor
 */
class ConfigEditorWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit ConfigEditorWindow(QWidget* parent = nullptr);
    ~ConfigEditorWindow() = default;

private slots:
    void onSetFolderPath();
    void onFileSelected(const QString& filePath);
    void onSaveClicked();
    void onReloadClicked();
    void onIncreaseFontSize();
    void onDecreaseFontSize();
    void onResetFontSize();

private:
    void setupUI();
    void setupMenuBar();
    void setupToolBar();
    void loadSettings();
    void saveSettings();
    void applyStyleSheet();

    // UI Components
    QSplitter* m_splitter;
    JsonFileListWidget* m_fileListWidget;
    JsonEditorWidget* m_editorWidget;

    // Toolbar buttons
    QPushButton* m_saveButton;
    QPushButton* m_reloadButton;

    // Current state
    QString m_currentFolder;
    QString m_currentFile;
    int m_fontSize = 18;  // Default font size
    static constexpr int DEFAULT_FONT_SIZE = 18;
    static constexpr int MIN_FONT_SIZE = 10;
    static constexpr int MAX_FONT_SIZE = 32;
};
