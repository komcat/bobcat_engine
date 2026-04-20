// ScriptRunnerPage.cpp - Script editor, runner, and debugger UI
#include "ScriptRunnerPage.h"
#include "ScriptEditorWidget.h"
#include "LuaSyntaxHighlighter.h"
#include "ApplicationContext.h"
#include "ScriptEngine.h"
#include "ScriptWorker.h"
#include "LogManager.h"
#include "SequenceEngine.h"
#include "SequencePromptDialog.h"
#include "product_position_manager.h"

#include <QApplication>
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
#include <QThread>
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
    m_statusLabel->setFixedWidth(160);
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

    // === LEFT PANEL (fixed 280px) ===
    auto* leftPanel = new QVBoxLayout();
    leftPanel->setSpacing(2);

    auto* folderBar = new QHBoxLayout();
    folderBar->setSpacing(4);

    auto* fileLabel = new QLabel("Scripts");
    fileLabel->setStyleSheet("font-weight: bold;");
    fileLabel->setFixedHeight(20);
    folderBar->addWidget(fileLabel);

    m_folderToggle = new QPushButton("Product");
    m_folderToggle->setFixedSize(70, 20);
    m_folderToggle->setCheckable(true);
    m_folderToggle->setStyleSheet(
        "QPushButton { background-color: #444; color: #aaa; font-size: 11px; border: 1px solid #555; border-radius: 3px; }"
        "QPushButton:checked { background-color: #2563eb; color: white; border: 1px solid #3b82f6; }");
    connect(m_folderToggle, &QPushButton::clicked, this, &ScriptRunnerPage::onFolderToggle);
    folderBar->addWidget(m_folderToggle);
    folderBar->addStretch();

    leftPanel->addLayout(folderBar);

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

    // Run/Debug/Stop buttons
    auto* runBar = new QHBoxLayout();
    runBar->setSpacing(4);

    m_runButton = new QPushButton("Run");
    m_runButton->setFixedSize(70, 28);
    m_runButton->setStyleSheet("background-color: #22c55e; color: white; font-weight: bold;");
    connect(m_runButton, &QPushButton::clicked, this, &ScriptRunnerPage::onRunClicked);

    m_debugButton = new QPushButton("Debug");
    m_debugButton->setFixedSize(70, 28);
    m_debugButton->setStyleSheet("background-color: #f59e0b; color: white; font-weight: bold;");
    connect(m_debugButton, &QPushButton::clicked, this, [this]() { startExecution(true); });

    m_stopButton = new QPushButton("Stop");
    m_stopButton->setFixedSize(70, 28);
    m_stopButton->setStyleSheet("background-color: #ef4444; color: white; font-weight: bold;");
    m_stopButton->setEnabled(false);
    connect(m_stopButton, &QPushButton::clicked, this, &ScriptRunnerPage::onStopClicked);

    runBar->addWidget(m_runButton);
    runBar->addWidget(m_debugButton);
    runBar->addWidget(m_stopButton);
    runBar->addStretch();
    leftPanel->addLayout(runBar);

    // Step/Continue buttons
    auto* debugBar = new QHBoxLayout();
    debugBar->setSpacing(4);

    m_stepButton = new QPushButton("Step");
    m_stepButton->setFixedSize(70, 28);
    m_stepButton->setStyleSheet("background-color: #3b82f6; color: white; font-weight: bold;");
    m_stepButton->setEnabled(false);
    connect(m_stepButton, &QPushButton::clicked, this, &ScriptRunnerPage::onStepClicked);

    m_continueButton = new QPushButton("Continue");
    m_continueButton->setFixedSize(90, 28);
    m_continueButton->setStyleSheet("background-color: #8b5cf6; color: white; font-weight: bold;");
    m_continueButton->setEnabled(false);
    connect(m_continueButton, &QPushButton::clicked, this, &ScriptRunnerPage::onContinueClicked);

    debugBar->addWidget(m_stepButton);
    debugBar->addWidget(m_continueButton);
    debugBar->addStretch();
    leftPanel->addLayout(debugBar);

    // Variable watch table
    auto* varLabel = new QLabel("Variables");
    varLabel->setStyleSheet("font-weight: bold;");
    varLabel->setFixedHeight(20);
    leftPanel->addWidget(varLabel);

    m_variablesTable = new QTableWidget(0, 2);
    m_variablesTable->setFixedWidth(280);
    m_variablesTable->setHorizontalHeaderLabels({"Name", "Value"});
    m_variablesTable->horizontalHeader()->setStretchLastSection(true);
    m_variablesTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_variablesTable->verticalHeader()->hide();
    m_variablesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_variablesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_variablesTable->setFont(QFont("Consolas", 9));
    leftPanel->addWidget(m_variablesTable, 1);

    contentLayout->addLayout(leftPanel);

    // === RIGHT PANEL: editor (top) + console (bottom) ===
    auto* rightSplitter = new QSplitter(Qt::Vertical);

    // Editor panel
    auto* editorContainer = new QWidget();
    auto* editorLayout = new QVBoxLayout(editorContainer);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    editorLayout->setSpacing(2);

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
    connect(m_editor, &ScriptEditorWidget::breakpointToggled, this, &ScriptRunnerPage::onBreakpointToggled);
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
    // Auto-switch to product scripts if the folder exists
    if (m_context && m_context->productPositionManager) {
        auto* ppm = m_context->productPositionManager;
        if (ppm->HasLoadedProduct()) {
            QString productPath = QString::fromStdString(ppm->GetCurrentProductPath());
            bool productFolderExists = QDir(productPath + "/scripts").exists();
            m_useProductScripts = productFolderExists;
            m_folderToggle->setChecked(productFolderExists);
        } else {
            m_useProductScripts = false;
            m_folderToggle->setChecked(false);
        }
    }
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
    if (m_useProductScripts && m_context && m_context->productPositionManager) {
        auto* ppm = m_context->productPositionManager;
        if (ppm->HasLoadedProduct()) {
            QString productPath = QString::fromStdString(ppm->GetCurrentProductPath());
            return productPath + "/scripts";
        }
    }
    return "scripts";
}

void ScriptRunnerPage::onFolderToggle()
{
    if (m_folderToggle->isChecked()) {
        if (!m_context || !m_context->productPositionManager ||
            !m_context->productPositionManager->HasLoadedProduct()) {
            appendOutput("No product loaded - cannot switch to product scripts", "#f59e0b");
            m_folderToggle->setChecked(false);
            return;
        }
        m_useProductScripts = true;
    } else {
        m_useProductScripts = false;
    }
    refreshFileRoot();
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

void ScriptRunnerPage::onBreakpointToggled(int line)
{
    if (m_context && m_context->scriptEngine) {
        m_context->scriptEngine->ToggleBreakpoint(line);
    }
}

// ============================================================================
// Run / Debug / Stop / Step / Continue
// ============================================================================

void ScriptRunnerPage::onRunClicked()
{
    startExecution(false);
}

void ScriptRunnerPage::startExecution(bool debugMode)
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

    if (m_modified && !m_currentFilePath.isEmpty()) {
        saveFile(m_currentFilePath);
    }

    m_outputConsole->clear();
    m_editor->clearDebugLine();
    m_debugging = debugMode;

    m_context->scriptEngine->SetDebugMode(debugMode);

    // Sync breakpoints from editor to engine
    m_context->scriptEngine->ClearAllBreakpoints();
    for (int bp : m_editor->getBreakpoints()) {
        m_context->scriptEngine->SetBreakpoint(bp);
    }

    // Wire up user prompt callback so bobcat.prompt() works from scripts
    if (m_context->sequenceEngine) {
        m_context->sequenceEngine->SetUserPromptCallback(
            [this](const std::string& title, const std::string& message) -> bool {
                bool result = false;
                if (QThread::currentThread() == QApplication::instance()->thread()) {
                    result = SequencePromptDialog::ShowPrompt(
                        QString::fromStdString(title),
                        QString::fromStdString(message),
                        this);
                } else {
                    QMetaObject::invokeMethod(this, [this, title, message, &result]() {
                        result = SequencePromptDialog::ShowPrompt(
                            QString::fromStdString(title),
                            QString::fromStdString(message),
                            this);
                    }, Qt::BlockingQueuedConnection);
                }
                return result;
            });
    }

    // Set debug pause callback
    m_context->scriptEngine->SetDebugPauseCallback(
        [this](int line, const std::string& source,
               const std::vector<ScriptEngine::LocalVar>& locals) {
            QStringList names, values;
            for (const auto& lv : locals) {
                names.append(QString::fromStdString(lv.first));
                values.append(QString::fromStdString(lv.second));
            }
            QMetaObject::invokeMethod(this, "onDebugPaused",
                Qt::QueuedConnection,
                Q_ARG(int, line),
                Q_ARG(QString, QString::fromStdString(source)),
                Q_ARG(QStringList, names),
                Q_ARG(QStringList, values));
        });

    if (!m_currentFilePath.isEmpty()) {
        appendOutput((debugMode ? "Debugging: " : "Running: ") +
                     QFileInfo(m_currentFilePath).fileName(), "#60a5fa");
        m_worker = new ScriptWorker(
            m_context->scriptEngine,
            m_currentFilePath.toStdString(),
            ScriptWorker::Mode::FILE, this);
    } else {
        appendOutput(debugMode ? "Debugging inline script..." : "Running inline script...", "#60a5fa");
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
    if (m_context && m_context->scriptEngine) {
        m_context->scriptEngine->RequestStop();
    }
    appendOutput("Stop requested...", "#f59e0b");
}

void ScriptRunnerPage::onStepClicked()
{
    if (m_context && m_context->scriptEngine) {
        m_context->scriptEngine->RequestDebugContinue();
    }
}

void ScriptRunnerPage::onContinueClicked()
{
    if (m_context && m_context->scriptEngine) {
        m_context->scriptEngine->RequestDebugRunToEnd();
        setDebugButtonsEnabled(false);
        m_editor->clearDebugLine();
        m_statusLabel->setText("Running...");
        m_statusLabel->setStyleSheet("color: #22c55e; font-weight: bold;");
    }
}

void ScriptRunnerPage::onClearClicked()
{
    m_outputConsole->clear();
}

void ScriptRunnerPage::setDebugButtonsEnabled(bool enabled)
{
    m_stepButton->setEnabled(enabled);
    m_continueButton->setEnabled(enabled);
}

// ============================================================================
// Execution callbacks
// ============================================================================

void ScriptRunnerPage::onExecutionStarted()
{
    m_runButton->setEnabled(false);
    m_debugButton->setEnabled(false);
    m_stopButton->setEnabled(true);
    if (m_debugging) {
        m_statusLabel->setText("Debugging...");
        m_statusLabel->setStyleSheet("color: #f59e0b; font-weight: bold;");
    } else {
        m_statusLabel->setText("Running...");
        m_statusLabel->setStyleSheet("color: #22c55e; font-weight: bold;");
    }
}

void ScriptRunnerPage::onExecutionCompleted(bool success, QString message)
{
    m_runButton->setEnabled(true);
    m_debugButton->setEnabled(true);
    m_stopButton->setEnabled(false);
    setDebugButtonsEnabled(false);
    m_editor->clearDebugLine();
    m_worker = nullptr;
    m_debugging = false;

    if (m_context && m_context->scriptEngine) {
        m_context->scriptEngine->SetDebugPauseCallback(nullptr);
        m_context->scriptEngine->SetDebugMode(false);
    }
    if (m_context && m_context->sequenceEngine) {
        m_context->sequenceEngine->SetUserPromptCallback(nullptr);
    }

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

void ScriptRunnerPage::onDebugPaused(int line, QString /*source*/,
                                      QStringList varNames, QStringList varValues)
{
    m_editor->setDebugLine(line);
    setDebugButtonsEnabled(true);

    m_statusLabel->setText(QString("Paused: line %1").arg(line));
    m_statusLabel->setStyleSheet("color: #f59e0b; font-weight: bold;");

    // Update variables table
    m_variablesTable->setRowCount(varNames.size());
    for (int i = 0; i < varNames.size(); ++i) {
        m_variablesTable->setItem(i, 0, new QTableWidgetItem(varNames[i]));
        m_variablesTable->setItem(i, 1, new QTableWidgetItem(varValues[i]));
    }
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
