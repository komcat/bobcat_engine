// ScriptRunnerPage.h - Script editor and runner UI page
#pragma once

#include <QWidget>
#include <QTreeView>
#include <QFileSystemModel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QSplitter>

struct ApplicationContext;
class ScriptWorker;
class ScriptEditorWidget;
class LuaSyntaxHighlighter;

class ScriptRunnerPage : public QWidget
{
    Q_OBJECT

public:
    explicit ScriptRunnerPage(ApplicationContext* context, QWidget* parent = nullptr);
    ~ScriptRunnerPage();

public slots:
    void onProductChanged();

signals:
    void backToMainMenu();

private slots:
    void onRunClicked();
    void onStopClicked();
    void onClearClicked();
    void onNewClicked();
    void onSaveClicked();
    void onSaveAsClicked();
    void onFileClicked(const QModelIndex& index);
    void onFileDoubleClicked(const QModelIndex& index);
    void onEditorModified();
    void onExecutionStarted();
    void onExecutionCompleted(bool success, QString message);
    void onScriptOutput(QString message);

private:
    void setupUI();
    void refreshFileRoot();
    QString getScriptsDirectory() const;
    void appendOutput(const QString& text, const QString& color = "");
    void loadFile(const QString& path);
    bool saveFile(const QString& path);
    bool promptSaveIfModified();
    void updateFilenameLabel();

    ApplicationContext* m_context;

    // Left panel
    QTreeView* m_fileTree = nullptr;
    QFileSystemModel* m_fileModel = nullptr;
    QPushButton* m_runButton = nullptr;
    QPushButton* m_stopButton = nullptr;

    // Right panel - editor
    ScriptEditorWidget* m_editor = nullptr;
    LuaSyntaxHighlighter* m_highlighter = nullptr;
    QLabel* m_filenameLabel = nullptr;
    QPushButton* m_newButton = nullptr;
    QPushButton* m_saveButton = nullptr;
    QPushButton* m_saveAsButton = nullptr;

    // Right panel - console
    QPlainTextEdit* m_outputConsole = nullptr;
    QPushButton* m_clearButton = nullptr;

    // Top bar
    QPushButton* m_backButton = nullptr;
    QLabel* m_statusLabel = nullptr;

    // State
    ScriptWorker* m_worker = nullptr;
    QString m_currentFilePath;
    bool m_modified = false;
};
