// ScriptRunnerPage.cpp - Script editor and runner UI implementation
#include "ScriptRunnerPage.h"
#include "ScriptEditorWidget.h"
#include "LuaSyntaxHighlighter.h"
#include "ApplicationContext.h"
#include "ScriptEngine.h"
#include "ScriptWorker.h"
#include "LogManager.h"
#include "product_position_manager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QDateTime>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QHeaderView>
#include <iostream>

ScriptRunnerPage::ScriptRunnerPage(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
{
    setupUI();
}

ScriptRunnerPage::~ScriptRunnerPage() = default;

void ScriptRunnerPage::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 4, 8, 4);
    mainLayout->setSpacing(4);

    // --- Top bar ---
    auto* topBar = new QHBoxLayout();
    topBar->setSpacing(8);

    m_backButton = new QPushButton("Back");
    m_backButton->setFixedSize(70, 28);
    connect(m_backButton, &QPushButton::clicked, this, &ScriptRunnerPage::backToMainMenu);

    auto* titleLabel = new QLabel("Script Runner");
    titleLabel->setStyleSheet("font-size: 14px; font-weight: bold;");
    titleLabel->setFixedHeight(28);

    m_statusLabel = new QLabel("Idle");
    m_statusLabel->setFixedHeight(28);
    m_statusLabel->setFixedWidth(120);
    m_statusLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_statusLabel->setStyleSheet("color: #888;");

    topBar->addWidget(m_backButton);
    topBar->addWidget(titleLabel);
    topBar->addStretch();
    topBar->addWidget(m_statusLabel);
    mainLayout->addLayout(topBar);

    // --- Content: left panel + right panel ---
    auto* contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(6);

    // === LEFT PANEL: file browser + controls (fixed 280px) ===
    auto* leftPanel = new QVBoxLayout();
    leftPanel->setSpacing(2);

    auto* fileLabel = new QLabel("Scripts");
    fileLabel->setStyleSheet("font-weight: bold;");
    fileLabel->setFixedHeight(20);
    leftPanel->addWidget(fileLabel);

    m_fileModel = new QFileSystemModel(this);
    m_fileModel->setNameFilters({"*.lua"});
    m_fileModel->setNameFilterDisables(false);

    m_fileTree = new QTreeView();
    m_fileTree->setFixedWidth(280);
    m_fileTree->setModel(m_fileModel);
    m_fileTree->setColumnHidden(1, true);
    m_fileTree->setColumnHidden(2, true);
    m_fileTree->setColumnHidden(3, true);
    m_fileTree->header()->hide();
    m_fileTree->setRootIsDecorated(true);
    connect(m_fileTree, &QTreeView::clicked, this, &ScriptRunnerPage::onFileClicked);
    connect(m_fileTree, &QTreeView::doubleClicked, this, &ScriptRunnerPage::onFileDoubleClicked);
    leftPanel->addWidget(m_fileTree, 1);

    auto* buttonBar = new QHBoxLayout();
    buttonBar->setSpacing(4);

    m_runButton = new QPushButton("Run");
    m_runButton->setFixedSize(80, 28);
    m_runButton->setStyleSheet("background-color: #22c55e; color: white; font-weight: bold;");
    connect(m_runButton, &QPushButton::clicked, this, &ScriptRunnerPage::onRunClicked);

    m_stopButton = new QPushButton("Stop");
    m_stopButton->setFixedSize(80, 28);
    m_stopButton->setStyleSheet("background-color: #ef4444; color: white; font-weight: bold;");
    m_stopButton->setEnabled(false);
    connect(m_stopButton, &QPushButton::clicked, this, &ScriptRunnerPage::onStopClicked);

    buttonBar->addWidget(m_runButton);
    buttonBar->addWidget(m_stopButton);
    buttonBar->addStretch();
    leftPanel->addLayout(buttonBar);

    contentLayout->addLayout(leftPanel);

    // === RIGHT PANEL: editor (top) + console (bottom) ===
    auto* rightSplitter = new QSplitter(Qt::Vertical);

    // Editor panel
    auto* editorContainer = new QWidget();
    auto* editorLayout = new QVBoxLayout(editorContainer);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    editorLayout->setSpacing(2);

    // Editor toolbar: filename + buttons
    auto* editorToolbar = new QHBoxLayout();
    editorToolbar->setSpacing(4);

    m_filenameLabel = new QLabel("untitled.lua");
    m_filenameLabel->setStyleSheet("font-weight: bold; color: #ccc;");
    m_filenameLabel->setFixedHeight(22);

    m_newButton = new QPushButton("New");
    m_newButton->setFixedSize(60, 22);
    connect(m_newButton, &QPushButton::clicked, this, &ScriptRunnerPage::onNewClicked);

    m_saveButton = new QPushButton("Save");
    m_saveButton->setFixedSize(60, 22);
    connect(m_saveButton, &QPushButton::clicked, this, &ScriptRunnerPage::onSaveClicked);

    m_saveAsButton = new QPushButton("Save As");
    m_saveAsButton->setFixedSize(70, 22);
    connect(m_saveAsButton, &QPushButton::clicked, this, &ScriptRunnerPage::onSaveAsClicked);

    editorToolbar->addWidget(m_filenameLabel);
    editorToolbar->addStretch();
    editorToolbar->addWidget(m_newButton);
    editorToolbar->addWidget(m_saveButton);
    editorToolbar->addWidget(m_saveAsButton);
    editorLayout->addLayout(editorToolbar);

    m_editor = new ScriptEditorWidget();
    m_highlighter = new LuaSyntaxHighlighter(m_editor->document());
    connect(m_editor, &QPlainTextEdit::textChanged, this, &ScriptRunnerPage::onEditorModified);
    editorLayout->addWidget(m_editor, 1);

    rightSplitter->addWidget(editorContainer);

    // Console panel
    auto* consoleContainer = new QWidget();
    auto* consoleLayout = new QVBoxLayout(consoleContainer);
    consoleLayout->setContentsMargins(0, 0, 0, 0);
    consoleLayout->setSpacing(2);

    auto* consoleToolbar = new QHBoxLayout();
    auto* consoleLabel = new QLabel("Output");
    consoleLabel->setStyleSheet("font-weight: bold;");
    consoleLabel->setFixedHeight(20);

    m_clearButton = new QPushButton("Clear");
    m_clearButton->setFixedSize(60, 20);
    connect(m_clearButton, &QPushButton::clicked, this, &ScriptRunnerPage::onClearClicked);

    consoleToolbar->addWidget(consoleLabel);
    consoleToolbar->addStretch();
    consoleToolbar->addWidget(m_clearButton);
    consoleLayout->addLayout(consoleToolbar);

    m_outputConsole = new QPlainTextEdit();
    m_outputConsole->setReadOnly(true);
    m_outputConsole->setFont(QFont("Consolas", 9));
    m_outputConsole->setMaximumBlockCount(10000);
    consoleLayout->addWidget(m_outputConsole, 1);

    rightSplitter->addWidget(consoleContainer);
    rightSplitter->setStretchFactor(0, 7);
    rightSplitter->setStretchFactor(1, 3);

    contentLayout->addWidget(rightSplitter, 1);
    mainLayout->addLayout(contentLayout, 1);

    refreshFileRoot();
}

// ============================================================================
// Product change
// ============================================================================

void ScriptRunnerPage::onProductChanged()
{
    refreshFileRoot();
}

void ScriptRunnerPage::refreshFileRoot()
{
    QString scriptsDir = getScriptsDirectory();
    QDir dir(scriptsDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    m_fileModel->setRootPath(scriptsDir);
    m_fileTree->setRootIndex(m_fileModel->index(scriptsDir));
}

QString ScriptRunnerPage::getScriptsDirectory() const
{
    if (m_context && m_context->productPositionManager) {
        auto* ppm = m_context->productPositionManager;
        if (ppm->HasLoadedProduct()) {
            QString productPath = QString::fromStdString(ppm->GetCurrentProductPath());
            QString productScripts = productPath + "/scripts";
            if (QDir(productScripts).exists()) {
                return productScripts;
            }
        }
    }
    return "scripts";
}

// ============================================================================
// File browser
// ============================================================================

void ScriptRunnerPage::onFileClicked(const QModelIndex& index)
{
    QString path = m_fileModel->filePath(index);
    QFileInfo info(path);
    if (info.isFile() && info.suffix() == "lua") {
        m_currentFilePath = path;
        updateFilenameLabel();
    }
}

void ScriptRunnerPage::onFileDoubleClicked(const QModelIndex& index)
{
    QString path = m_fileModel->filePath(index);
    QFileInfo info(path);
    if (info.isFile() && info.suffix() == "lua") {
        if (!promptSaveIfModified()) return;
        loadFile(path);
    }
}

// ============================================================================
// File I/O
// ============================================================================

void ScriptRunnerPage::loadFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        appendOutput("Failed to open: " + path, "#ef4444");
        return;
    }

    m_editor->blockSignals(true);
    m_editor->setPlainText(QString::fromUtf8(file.readAll()));
    m_editor->blockSignals(false);

    m_currentFilePath = path;
    m_modified = false;
    updateFilenameLabel();
}

bool ScriptRunnerPage::saveFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        appendOutput("Failed to save: " + path, "#ef4444");
        return false;
    }

    file.write(m_editor->toPlainText().toUtf8());
    file.close();

    m_currentFilePath = path;
    m_modified = false;
    updateFilenameLabel();

    if (m_context && m_context->logManager) {
        m_context->logManager->Info("[Script] Saved: " + path.toStdString(), "ScriptRunner");
    }

    return true;
}

bool ScriptRunnerPage::promptSaveIfModified()
{
    if (!m_modified) return true;

    auto result = QMessageBox::question(this, "Save Changes",
        "Script has unsaved changes. Save before continuing?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

    if (result == QMessageBox::Cancel) return false;
    if (result == QMessageBox::Discard) return true;

    if (m_currentFilePath.isEmpty()) {
        onSaveAsClicked();
        return !m_modified;
    }

    return saveFile(m_currentFilePath);
}

void ScriptRunnerPage::updateFilenameLabel()
{
    QString name = m_currentFilePath.isEmpty()
        ? "untitled.lua"
        : QFileInfo(m_currentFilePath).fileName();

    if (m_modified) name += " [*]";
    m_filenameLabel->setText(name);
    m_filenameLabel->setStyleSheet(m_modified
        ? "font-weight: bold; color: #f59e0b;"
        : "font-weight: bold; color: #ccc;");
}

// ============================================================================
// Editor actions
// ============================================================================

void ScriptRunnerPage::onNewClicked()
{
    if (!promptSaveIfModified()) return;

    m_editor->blockSignals(true);
    m_editor->clear();
    m_editor->setPlainText("-- New script\n\nlog.info(\"Hello from Lua\")\n");
    m_editor->blockSignals(false);

    m_currentFilePath.clear();
    m_modified = false;
    updateFilenameLabel();
}

void ScriptRunnerPage::onSaveClicked()
{
    if (m_currentFilePath.isEmpty()) {
        onSaveAsClicked();
        return;
    }
    saveFile(m_currentFilePath);
}

void ScriptRunnerPage::onSaveAsClicked()
{
    QString dir = getScriptsDirectory();
    QString path = QFileDialog::getSaveFileName(this, "Save Script", dir, "Lua Scripts (*.lua)");
    if (path.isEmpty()) return;

    if (!path.endsWith(".lua")) path += ".lua";
    saveFile(path);
}

void ScriptRunnerPage::onEditorModified()
{
    if (!m_modified) {
        m_modified = true;
        updateFilenameLabel();
    }
}

// ============================================================================
// Run / Stop
// ============================================================================

void ScriptRunnerPage::onRunClicked()
{
    if (!m_context || !m_context->scriptEngine) {
        appendOutput("Script engine not available", "#ef4444");
        return;
    }
    if (m_context->scriptEngine->IsRunning()) {
        appendOutput("Script engine is busy", "#ef4444");
        return;
    }

    QString code = m_editor->toPlainText().trimmed();
    if (code.isEmpty()) {
        appendOutput("Editor is empty", "#f59e0b");
        return;
    }

    // Save if modified and has a file path
    if (m_modified && !m_currentFilePath.isEmpty()) {
        saveFile(m_currentFilePath);
    }

    m_outputConsole->clear();

    if (!m_currentFilePath.isEmpty()) {
        appendOutput("Running: " + QFileInfo(m_currentFilePath).fileName(), "#60a5fa");
        m_worker = new ScriptWorker(
            m_context->scriptEngine,
            m_currentFilePath.toStdString(),
            ScriptWorker::Mode::FILE, this);
    } else {
        appendOutput("Running inline script...", "#60a5fa");
        m_worker = new ScriptWorker(
            m_context->scriptEngine,
            code.toStdString(),
            ScriptWorker::Mode::STRING, this);
    }

    connect(m_worker, &ScriptWorker::executionStarted,
            this, &ScriptRunnerPage::onExecutionStarted, Qt::QueuedConnection);
    connect(m_worker, &ScriptWorker::executionCompleted,
            this, &ScriptRunnerPage::onExecutionCompleted, Qt::QueuedConnection);
    connect(m_worker, &ScriptWorker::scriptOutput,
            this, &ScriptRunnerPage::onScriptOutput, Qt::QueuedConnection);

    m_worker->start();
}

void ScriptRunnerPage::onStopClicked()
{
    if (m_worker) {
        m_worker->requestStop();
        appendOutput("Stop requested...", "#f59e0b");
    }
}

void ScriptRunnerPage::onClearClicked()
{
    m_outputConsole->clear();
}

// ============================================================================
// Execution callbacks
// ============================================================================

void ScriptRunnerPage::onExecutionStarted()
{
    m_runButton->setEnabled(false);
    m_stopButton->setEnabled(true);
    m_statusLabel->setText("Running...");
    m_statusLabel->setStyleSheet("color: #22c55e; font-weight: bold;");
}

void ScriptRunnerPage::onExecutionCompleted(bool success, QString message)
{
    m_runButton->setEnabled(true);
    m_stopButton->setEnabled(false);
    m_worker = nullptr;

    if (success) {
        m_statusLabel->setText("Completed");
        m_statusLabel->setStyleSheet("color: #22c55e;");
        appendOutput("Script completed successfully", "#22c55e");
    } else {
        m_statusLabel->setText("Failed");
        m_statusLabel->setStyleSheet("color: #ef4444;");
        appendOutput("Error: " + message, "#ef4444");
    }
}

void ScriptRunnerPage::onScriptOutput(QString message)
{
    appendOutput(message);
}

void ScriptRunnerPage::appendOutput(const QString& text, const QString& color)
{
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");

    if (color.isEmpty()) {
        m_outputConsole->appendPlainText("[" + timestamp + "] " + text);
    } else {
        m_outputConsole->appendHtml(
            "<span style='color:#888;'>[" + timestamp + "]</span> "
            "<span style='color:" + color + ";'>" + text.toHtmlEscaped() + "</span>");
    }

    auto cursor = m_outputConsole->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_outputConsole->setTextCursor(cursor);
}
