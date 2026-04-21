// ScriptRunnerPage.cpp - Script editor, runner, and debugger UI
#include "ScriptRunnerPage.h"
#include "ScriptEditorWidget.h"
#include "LuaSyntaxHighlighter.h"
#include "ApplicationContext.h"
#include "ScriptEngine.h"
#include "ScriptWorker.h"
#include "LogManager.h"
#include "SequenceEngine.h"
#include "SequenceContext.h"
#include "SequencePromptDialog.h"
#include "product_position_manager.h"

#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QDateTime>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileSystemWatcher>
#include <QInputDialog>
#include <QMessageBox>
#include <QHeaderView>
#include <QShortcut>
#include <QSettings>
#include <QTextStream>
#include <QThread>
#include <QTimer>
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

    m_variableFilter = new QLineEdit();
    m_variableFilter->setFixedWidth(280);
    m_variableFilter->setPlaceholderText("Filter... (e.g. A1B1 or P.Y)");
    m_variableFilter->setClearButtonEnabled(true);
    leftPanel->addWidget(m_variableFilter);

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

    // Text filter: hide variable rows whose Name doesn't contain the substring.
    // Case-insensitive match. Empty filter shows everything.
    connect(m_variableFilter, &QLineEdit::textChanged, this,
        [this](const QString& text) {
            QString needle = text.trimmed();
            for (int row = 0; row < m_variablesTable->rowCount(); ++row) {
                if (needle.isEmpty()) {
                    m_variablesTable->setRowHidden(row, false);
                    continue;
                }
                auto* item = m_variablesTable->item(row, 0);
                bool match = item &&
                    item->text().contains(needle, Qt::CaseInsensitive);
                m_variablesTable->setRowHidden(row, !match);
            }
        });

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

    // Plain ASCII labels — RobotoMono doesn't include U+2212; Unicode minus
    // rendered as a blank glyph on the button.
    m_fontMinusButton = new QPushButton("A-");
    m_fontMinusButton->setFixedSize(40, 22);
    m_fontMinusButton->setToolTip("Decrease font size (Ctrl+-)");
    connect(m_fontMinusButton, &QPushButton::clicked, this, &ScriptRunnerPage::onFontDecrease);

    m_fontPlusButton = new QPushButton("A+");
    m_fontPlusButton->setFixedSize(40, 22);
    m_fontPlusButton->setToolTip("Increase font size (Ctrl+=)");
    connect(m_fontPlusButton, &QPushButton::clicked, this, &ScriptRunnerPage::onFontIncrease);

    m_newButton = new QPushButton("New");
    m_newButton->setFixedSize(60, 22);
    connect(m_newButton, &QPushButton::clicked, this, &ScriptRunnerPage::onNewClicked);

    m_saveButton = new QPushButton("Save");
    m_saveButton->setFixedSize(60, 22);
    connect(m_saveButton, &QPushButton::clicked, this, &ScriptRunnerPage::onSaveClicked);

    m_saveAsButton = new QPushButton("Save As");
    m_saveAsButton->setFixedSize(70, 22);
    connect(m_saveAsButton, &QPushButton::clicked, this, &ScriptRunnerPage::onSaveAsClicked);

    // Hidden by default; shown when file changes on disk (external edit detected).
    m_reloadButton = new QPushButton("Reload");
    m_reloadButton->setFixedSize(70, 22);
    m_reloadButton->setStyleSheet("background-color: #f59e0b; color: white; font-weight: bold;");
    m_reloadButton->setToolTip("File changed on disk. Click to reload.");
    m_reloadButton->setVisible(false);
    connect(m_reloadButton, &QPushButton::clicked, this, &ScriptRunnerPage::onReloadClicked);

    editorToolbar->addWidget(m_filenameLabel);
    editorToolbar->addStretch();
    editorToolbar->addWidget(m_fontMinusButton);
    editorToolbar->addWidget(m_fontPlusButton);
    editorToolbar->addWidget(m_reloadButton);
    editorToolbar->addWidget(m_newButton);
    editorToolbar->addWidget(m_saveButton);
    editorToolbar->addWidget(m_saveAsButton);
    editorLayout->addLayout(editorToolbar);

    // File watcher for external changes to the currently-loaded script
    m_fileWatcher = new QFileSystemWatcher(this);
    connect(m_fileWatcher, &QFileSystemWatcher::fileChanged,
            this, &ScriptRunnerPage::onFileChangedOnDisk);

    m_editor = new ScriptEditorWidget();
    m_highlighter = new LuaSyntaxHighlighter(m_editor->document());
    connect(m_editor, &QPlainTextEdit::textChanged, this, &ScriptRunnerPage::onEditorModified);
    connect(m_editor, &ScriptEditorWidget::breakpointToggled, this, &ScriptRunnerPage::onBreakpointToggled);
    editorLayout->addWidget(m_editor, 1);

    // Restore persisted editor font size and wire keyboard shortcuts.
    {
        QSettings settings("Fabrinet", "BobcatEngine");
        int persisted = settings.value("ScriptEditor/FontPt", 10).toInt();
        applyEditorFontSize(persisted);
    }
    auto* scZoomIn1  = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal), m_editor);
    auto* scZoomIn2  = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Plus),  m_editor);
    auto* scZoomOut  = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus), m_editor);
    auto* scZoomRst  = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_0),     m_editor);
    connect(scZoomIn1, &QShortcut::activated, this, &ScriptRunnerPage::onFontIncrease);
    connect(scZoomIn2, &QShortcut::activated, this, &ScriptRunnerPage::onFontIncrease);
    connect(scZoomOut, &QShortcut::activated, this, &ScriptRunnerPage::onFontDecrease);
    connect(scZoomRst, &QShortcut::activated, this, &ScriptRunnerPage::onFontReset);

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
            QString productName = QString::fromStdString(ppm->GetCurrentProductName());
            bool productFolderExists = QDir("config/" + productName + "/scripts").exists();
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
    m_fileTree->expandToDepth(0);  // Expand first level so sub-folders are visible
}

QString ScriptRunnerPage::getScriptsDirectory() const
{
    if (m_useProductScripts && m_context && m_context->productPositionManager) {
        auto* ppm = m_context->productPositionManager;
        if (ppm->HasLoadedProduct()) {
            // Product directory is `config/{productName}/` — scripts live under
            // `config/{productName}/scripts/`.  GetCurrentProductPath() returns the
            // full JSON file path (positions.{productName}.json), not a directory.
            QString productName = QString::fromStdString(ppm->GetCurrentProductName());
            return "config/" + productName + "/scripts";
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
    if (info.isDir()) {
        // Let default Qt behavior toggle expand/collapse
        m_fileTree->setExpanded(index, !m_fileTree->isExpanded(index));
        return;
    }
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
    m_externalChangePending = false;
    m_reloadButton->setVisible(false);

    // Re-target the file watcher to this path only
    if (m_fileWatcher) {
        auto watched = m_fileWatcher->files();
        if (!watched.isEmpty()) m_fileWatcher->removePaths(watched);
        if (!path.isEmpty()) m_fileWatcher->addPath(path);
    }

    updateFilenameLabel();
}

bool ScriptRunnerPage::saveFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        appendOutput("Failed to save: " + path, "#ef4444");
        return false;
    }

    // Suppress the self-triggered QFileSystemWatcher event.
    m_ignoreNextFileChange = true;

    file.write(m_editor->toPlainText().toUtf8());
    file.close();

    m_currentFilePath = path;
    m_modified = false;
    m_externalChangePending = false;
    m_reloadButton->setVisible(false);

    // Ensure watcher tracks this path (add is a no-op if already watched;
    // also re-adds it in case the OS dropped it after a rewrite).
    if (m_fileWatcher && !path.isEmpty()) {
        if (!m_fileWatcher->files().contains(path)) {
            m_fileWatcher->addPath(path);
        }
    }

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

    if (m_modified)              name += " [*]";
    if (m_externalChangePending) name += " \u21BB"; // ↻
    m_filenameLabel->setText(name);

    QString color = "#ccc";
    if (m_externalChangePending) color = "#f59e0b"; // amber: external change wins visually
    else if (m_modified)         color = "#f59e0b"; // amber
    m_filenameLabel->setStyleSheet(QString("font-weight: bold; color: %1;").arg(color));
}

// ============================================================================
// File-watcher handlers
// ============================================================================

void ScriptRunnerPage::onFileChangedOnDisk(const QString& path)
{
    // Some editors (including our own Save) rewrite the file atomically which
    // can make the watcher drop the path.  Re-arm it if so.
    if (m_fileWatcher && !m_fileWatcher->files().contains(path)) {
        // Brief delay lets atomic saves (tmp -> rename) settle before re-adding
        QTimer::singleShot(150, this, [this, path]() {
            if (QFile::exists(path) && !m_fileWatcher->files().contains(path)) {
                m_fileWatcher->addPath(path);
            }
        });
    }

    if (m_ignoreNextFileChange) {
        m_ignoreNextFileChange = false;
        return;
    }

    if (path != m_currentFilePath) return; // stale signal

    m_externalChangePending = true;
    m_reloadButton->setVisible(true);
    updateFilenameLabel();
}

void ScriptRunnerPage::onReloadClicked()
{
    if (m_currentFilePath.isEmpty()) return;

    if (m_modified) {
        auto result = QMessageBox::question(this, "Reload File",
            "You have unsaved changes. Reload from disk and discard them?",
            QMessageBox::Yes | QMessageBox::No);
        if (result != QMessageBox::Yes) return;
    }

    loadFile(m_currentFilePath); // resets flags and hides reload button
    appendOutput("Reloaded: " + m_currentFilePath, "#60a5fa");
}

// ============================================================================
// Editor font-size controls (Ctrl+= / Ctrl+- / Ctrl+0 + toolbar buttons)
// ============================================================================

void ScriptRunnerPage::applyEditorFontSize(int pt)
{
    // Clamp to sane range
    if (pt < 6)  pt = 6;
    if (pt > 30) pt = 30;

    m_editorFontPt = pt;

    // Editor — both widget font AND document default font must be set, because
    // QPlainTextEdit renders text using the document's defaultFont, which does
    // NOT automatically track setFont().
    QFont ef = m_editor->font();
    ef.setPointSize(pt);
    m_editor->setFont(ef);
    m_editor->document()->setDefaultFont(ef);
    m_editor->setTabStopDistance(m_editor->fontMetrics().horizontalAdvance(' ') * 4);
    m_editor->viewport()->update();

    // Output console — same trick, set both.
    // (May be null during setupUI() when we restore the persisted size before
    // the console widget is constructed — that's fine, we just skip it.)
    if (m_outputConsole) {
        QFont cf = m_outputConsole->font();
        cf.setPointSize(pt);
        m_outputConsole->setFont(cf);
        m_outputConsole->document()->setDefaultFont(cf);
        m_outputConsole->viewport()->update();

        // Visible feedback in the Script Runner output panel (skip during init).
        appendOutput(QString("Font size: %1 pt").arg(pt), "#60a5fa");
    }

    QSettings settings("Fabrinet", "BobcatEngine");
    settings.setValue("ScriptEditor/FontPt", pt);
}

void ScriptRunnerPage::onFontIncrease() { applyEditorFontSize(m_editorFontPt + 1); }
void ScriptRunnerPage::onFontDecrease() { applyEditorFontSize(m_editorFontPt - 1); }
void ScriptRunnerPage::onFontReset()    { applyEditorFontSize(10); }

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

    // Per-run log: reset buffer, capture timestamp + script basename now so
    // the file can be flushed on completion with a sensible name.
    m_runLogBuffer.clear();
    m_runLogStartTs = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    m_runLogScriptBaseName = m_currentFilePath.isEmpty()
        ? QStringLiteral("untitled")
        : QFileInfo(m_currentFilePath).completeBaseName();

    m_context->scriptEngine->SetDebugMode(debugMode);

    // Clear the live Variables table for this run.
    m_variablesTable->setRowCount(0);

    // Wire bobcat.watch(key, value) -> live updates to the Variables table.
    // Script thread calls the callback; marshal to the UI thread safely.
    m_context->scriptEngine->SetWatchCallback(
        [this](const std::string& key, const std::string& value) {
            QString k = QString::fromStdString(key);
            QString v = QString::fromStdString(value);
            auto updater = [this, k, v]() {
                // Find existing row with this key; update in place if found.
                for (int row = 0; row < m_variablesTable->rowCount(); ++row) {
                    auto* item = m_variablesTable->item(row, 0);
                    if (item && item->text() == k) {
                        m_variablesTable->setItem(row, 1, new QTableWidgetItem(v));
                        return;
                    }
                }
                // Otherwise append a new row.
                int row = m_variablesTable->rowCount();
                m_variablesTable->insertRow(row);
                m_variablesTable->setItem(row, 0, new QTableWidgetItem(k));
                m_variablesTable->setItem(row, 1, new QTableWidgetItem(v));
                // Apply the active filter to the new row.
                QString needle = m_variableFilter ? m_variableFilter->text().trimmed() : QString();
                if (!needle.isEmpty() &&
                    !k.contains(needle, Qt::CaseInsensitive)) {
                    m_variablesTable->setRowHidden(row, true);
                }
            };
            if (QThread::currentThread() == QApplication::instance()->thread()) {
                updater();
            } else {
                QMetaObject::invokeMethod(this, updater, Qt::QueuedConnection);
            }
        });

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

        // Wire up user input callback so bobcat.askInput() works from scripts.
        // Supports an embedded marker "{presets:A,B,C}" in the prompt to show
        // quick-pick buttons alongside OK/Cancel.
        m_context->sequenceEngine->SetUserInputCallback(
            [this](const std::string& title, const std::string& prompt,
                   const std::string& defaultValue) -> std::pair<bool, std::string> {
                // Extract optional presets marker from the prompt.
                QString promptQ = QString::fromStdString(prompt);
                QStringList presets;
                QRegularExpression re(R"(\{presets:([^}]*)\})");
                auto m = re.match(promptQ);
                if (m.hasMatch()) {
                    presets = m.captured(1).split(',', Qt::SkipEmptyParts);
                    promptQ.remove(m.capturedStart(), m.capturedLength());
                    promptQ = promptQ.trimmed();
                }

                auto show = [this, &title, &promptQ, &defaultValue, &presets]() {
                    if (presets.isEmpty()) {
                        bool ok = false;
                        QString text = QInputDialog::getText(
                            this,
                            QString::fromStdString(title),
                            promptQ,
                            QLineEdit::Normal,
                            QString::fromStdString(defaultValue),
                            &ok);
                        return std::make_pair(ok, text.toStdString());
                    }

                    // Custom dialog with preset buttons.
                    QDialog dlg(this);
                    dlg.setWindowTitle(QString::fromStdString(title));

                    auto* layout = new QVBoxLayout(&dlg);
                    auto* promptLabel = new QLabel(promptQ, &dlg);
                    promptLabel->setWordWrap(true);
                    layout->addWidget(promptLabel);

                    auto* edit = new QLineEdit(QString::fromStdString(defaultValue), &dlg);
                    layout->addWidget(edit);

                    // One button per preset; click fills the edit and accepts.
                    auto* presetRow = new QHBoxLayout();
                    presetRow->addWidget(new QLabel("Quick:", &dlg));
                    for (const QString& p : presets) {
                        auto* btn = new QPushButton(p, &dlg);
                        btn->setStyleSheet(
                            "QPushButton { background-color: #3b82f6; color: white; "
                            "padding: 4px 10px; }");
                        QObject::connect(btn, &QPushButton::clicked, &dlg, [&edit, p, &dlg]() {
                            edit->setText(p);
                            dlg.accept();
                        });
                        presetRow->addWidget(btn);
                    }
                    presetRow->addStretch();
                    layout->addLayout(presetRow);

                    auto* buttonRow = new QHBoxLayout();
                    buttonRow->addStretch();
                    auto* okBtn = new QPushButton("OK", &dlg);
                    auto* cancelBtn = new QPushButton("Cancel", &dlg);
                    okBtn->setDefault(true);
                    QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
                    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
                    buttonRow->addWidget(okBtn);
                    buttonRow->addWidget(cancelBtn);
                    layout->addLayout(buttonRow);

                    edit->setFocus();
                    edit->selectAll();

                    bool ok = (dlg.exec() == QDialog::Accepted);
                    return std::make_pair(ok, edit->text().toStdString());
                };

                std::pair<bool, std::string> result;
                if (QThread::currentThread() == QApplication::instance()->thread()) {
                    result = show();
                } else {
                    QMetaObject::invokeMethod(this, [&result, show]() {
                        result = show();
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

    // Freeze the editor — no typing while a script is running.
    m_editor->setReadOnly(true);
    // File-mutating controls off (avoid saving/overwriting mid-run).
    m_newButton->setEnabled(false);
    m_saveButton->setEnabled(false);
    m_saveAsButton->setEnabled(false);
    m_reloadButton->setEnabled(false);

    if (m_debugging) {
        m_statusLabel->setText("Debugging...");
        m_statusLabel->setStyleSheet("color: #f59e0b; font-weight: bold;");
        // Amber frame while debug-paused / running
        m_editor->setStyleSheet("QPlainTextEdit { border: 2px solid #f59e0b; }");
        m_filenameLabel->setText(m_filenameLabel->text() + "   \u25CF DEBUGGING");
    } else {
        m_statusLabel->setText("Running...");
        m_statusLabel->setStyleSheet("color: #22c55e; font-weight: bold;");
        // Green frame around the editor so it's obvious at a glance
        m_editor->setStyleSheet("QPlainTextEdit { border: 2px solid #22c55e; }");
        m_filenameLabel->setText(m_filenameLabel->text() + "   \u25CF RUNNING");
    }
    m_filenameLabel->setStyleSheet("font-weight: bold; color: #22c55e;");

    emit scriptRunningChanged(true);
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

    // Unfreeze the editor and restore its default frame + filename label
    m_editor->setReadOnly(false);
    m_editor->setStyleSheet(QString());
    m_newButton->setEnabled(true);
    m_saveButton->setEnabled(true);
    m_saveAsButton->setEnabled(true);
    m_reloadButton->setEnabled(true);
    updateFilenameLabel();

    if (m_context && m_context->scriptEngine) {
        m_context->scriptEngine->SetDebugPauseCallback(nullptr);
        m_context->scriptEngine->SetDebugMode(false);
        m_context->scriptEngine->SetWatchCallback(nullptr);
    }
    if (m_context && m_context->sequenceEngine) {
        m_context->sequenceEngine->SetUserPromptCallback(nullptr);
        m_context->sequenceEngine->SetUserInputCallback(nullptr);
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

    // Flush per-run log.  If the Lua script set ctx._log_path (matching its
    // own CSV base name), we use that exact path so the log and CSV share a
    // filename.  Otherwise fall back to a default name with the DUT SN and
    // NO timestamp, so repeat runs with the same SN accumulate into one file.
    // The file is opened in APPEND mode; each run is delimited by a header.
    if (m_context && m_context->productPositionManager &&
        m_context->productPositionManager->HasLoadedProduct() &&
        !m_runLogBuffer.isEmpty())
    {
        QString productName = QString::fromStdString(
            m_context->productPositionManager->GetCurrentProductName());

        QString sn = "noSN";
        QString scriptLogPath;  // optional script-supplied path
        if (m_context->scriptEngine && m_context->scriptEngine->GetContext()) {
            auto* ctx = m_context->scriptEngine->GetContext();
            if (ctx->HasValue("_sn")) {
                std::string snStr = ctx->GetString("_sn", "");
                if (!snStr.empty()) sn = QString::fromStdString(snStr);
            }
            if (ctx->HasValue("_log_path")) {
                std::string p = ctx->GetString("_log_path", "");
                if (!p.empty()) scriptLogPath = QString::fromStdString(p);
            }
        }

        QString logPath = !scriptLogPath.isEmpty()
            ? scriptLogPath
            : QString("data/%1/ScriptLog_%2_%3.log")
                  .arg(productName, m_runLogScriptBaseName, sn);

        // Ensure parent directory exists.
        QFileInfo logInfo(logPath);
        QDir dir(logInfo.absolutePath());
        if (!dir.exists()) dir.mkpath(".");

        QFile logFile(logPath);
        if (logFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
            QTextStream ts(&logFile);
            ts << "# ================================================="   << "\n";
            ts << "# Script: "    << m_runLogScriptBaseName   << "\n";
            ts << "# Run start:  " << m_runLogStartTs          << "\n";
            ts << "# Product:    " << productName              << "\n";
            ts << "# DUT SN:     " << sn                       << "\n";
            ts << "# Result:     " << (success ? "SUCCESS" : "FAILED") << "\n";
            if (!success) ts << "# Error:      " << message << "\n";
            ts << "# -------------------------------------------------" << "\n";
            ts << m_runLogBuffer;
            ts << "\n";  // separator newline before next run's header
            logFile.close();
            appendOutput("Log saved: " + logPath, "#60a5fa");
        } else {
            appendOutput("Failed to write log file: " + logPath, "#ef4444");
        }
    }

    emit scriptRunningChanged(false);
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

    // Tee into the per-run log buffer as plain text (safe even if the
    // console widget isn't constructed yet).
    m_runLogBuffer += "[" + timestamp + "] " + text + "\n";

    if (!m_outputConsole) return;  // Defensive: setupUI ordering

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
