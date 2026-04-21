// ScriptRunnerPage.h - Script editor, runner, and debugger UI page
#pragma once

#include <QWidget>
#include <QTreeView>
#include <QFileSystemModel>
#include <QFileSystemWatcher>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QSplitter>
#include <QTableWidget>

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
    void scriptRunningChanged(bool running);

private slots:
    void onRunClicked();
    void onStopClicked();
    void onClearClicked();
    void onNewClicked();
    void onSaveClicked();
    void onSaveAsClicked();
    void onStepClicked();
    void onContinueClicked();
    void onFolderToggle();
    void onFileClicked(const QModelIndex& index);
    void onFileDoubleClicked(const QModelIndex& index);
    void onEditorModified();
    void onBreakpointToggled(int line);
    void onExecutionStarted();
    void onExecutionCompleted(bool success, QString message);
    void onScriptOutput(QString message);
    void onDebugPaused(int line, QString source, QStringList varNames, QStringList varValues);
    void onFileChangedOnDisk(const QString& path);
    void onReloadClicked();
    void onFontIncrease();
    void onFontDecrease();
    void onFontReset();

private:
    void setupUI();
    void refreshFileRoot();
    QString getScriptsDirectory() const;
    void appendOutput(const QString& text, const QString& color = "");
    void loadFile(const QString& path);
    bool saveFile(const QString& path);
    bool promptSaveIfModified();
    void updateFilenameLabel();
    void setDebugButtonsEnabled(bool enabled);
    void startExecution(bool debugMode);
    void applyEditorFontSize(int pt);  // Clamps, updates editor + tab stops + QSettings

    ApplicationContext* m_context;

    // Left panel
    QPushButton* m_folderToggle = nullptr;
    QTreeView* m_fileTree = nullptr;
    QFileSystemModel* m_fileModel = nullptr;
    QPushButton* m_runButton = nullptr;
    QPushButton* m_debugButton = nullptr;
    QPushButton* m_stopButton = nullptr;
    QPushButton* m_stepButton = nullptr;
    QPushButton* m_continueButton = nullptr;

    // Left panel - variable watch
    QTableWidget* m_variablesTable = nullptr;
    QLineEdit* m_variableFilter = nullptr;

    // Right panel - editor
    ScriptEditorWidget* m_editor = nullptr;
    LuaSyntaxHighlighter* m_highlighter = nullptr;
    QLabel* m_filenameLabel = nullptr;
    QPushButton* m_newButton = nullptr;
    QPushButton* m_saveButton = nullptr;
    QPushButton* m_saveAsButton = nullptr;
    QPushButton* m_reloadButton = nullptr;
    QPushButton* m_fontMinusButton = nullptr;
    QPushButton* m_fontPlusButton = nullptr;
    int m_editorFontPt = 10;

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
    bool m_debugging = false;
    bool m_useProductScripts = false;

    // Watch currently-loaded file for external edits (e.g. Claude writing to it)
    QFileSystemWatcher* m_fileWatcher = nullptr;
    bool m_ignoreNextFileChange = false;  // Set before our own saves
    bool m_externalChangePending = false; // True while the "Reload" button is armed

    // Per-run log: buffer output lines during execution, flush to a file
    // at data/{productName}/ScriptLog_{scriptName}_{SN}_{ts}.txt on completion.
    QString m_runLogBuffer;
    QString m_runLogStartTs;          // YYYYMMDD_HHMMSS captured at Run start
    QString m_runLogScriptBaseName;   // e.g. "poyntA1A2Y_balancing"
};
