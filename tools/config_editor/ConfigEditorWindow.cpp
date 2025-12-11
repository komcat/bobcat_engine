// ConfigEditorWindow.cpp
#include "ConfigEditorWindow.h"
#include "JsonFileListWidget.h"
#include "JsonEditorWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QCoreApplication>
#include <QDir>
#include <QToolBar>
#include <QLabel>
#include <QStatusBar>
#include <QApplication>

ConfigEditorWindow::ConfigEditorWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("Config Editor");
    setMinimumSize(1200, 700);
    resize(1400, 800);

    loadSettings();
    applyStyleSheet();
    setupUI();
    setupMenuBar();
    setupToolBar();

    // Restore window geometry and splitter state after UI is created
    QSettings settings("BobcatEngine", "ConfigEditor");
    if (settings.contains("geometry")) {
        restoreGeometry(settings.value("geometry").toByteArray());
    }
    if (settings.contains("splitterState") && m_splitter) {
        m_splitter->restoreState(settings.value("splitterState").toByteArray());
    }

    // Set default folder to ../config relative to executable
    if (m_currentFolder.isEmpty()) {
        QString defaultPath = QCoreApplication::applicationDirPath() + "/../config";
        QDir dir(defaultPath);
        if (dir.exists()) {
            m_currentFolder = dir.absolutePath();
        } else {
            // Fallback to source config folder
            defaultPath = QCoreApplication::applicationDirPath() + "/config";
            dir.setPath(defaultPath);
            if (dir.exists()) {
                m_currentFolder = dir.absolutePath();
            }
        }
    }

    if (!m_currentFolder.isEmpty()) {
        m_fileListWidget->setFolder(m_currentFolder);
    }
}

void ConfigEditorWindow::applyStyleSheet()
{
    QString styleSheet = QString(R"(
        * {
            font-size: %1pt;
        }

        QMainWindow, QWidget {
            background-color: #1e1e1e;
            color: #d4d4d4;
        }

        QMenuBar {
            background-color: #2d2d30;
            color: #d4d4d4;
            border-bottom: 1px solid #3f3f46;
            padding: 2px;
        }

        QMenuBar::item {
            background: transparent;
            padding: 6px 12px;
        }

        QMenuBar::item:selected {
            background-color: #3e3e42;
        }

        QMenu {
            background-color: #2d2d30;
            color: #d4d4d4;
            border: 1px solid #3f3f46;
        }

        QMenu::item {
            padding: 6px 30px;
        }

        QMenu::item:selected {
            background-color: #094771;
        }

        QMenu::separator {
            height: 1px;
            background: #3f3f46;
            margin: 4px 0;
        }

        QToolBar {
            background: #2d2d30;
            border-bottom: 1px solid #3f3f46;
            padding: 4px;
            spacing: 8px;
        }

        QPushButton {
            background-color: #0e639c;
            color: white;
            border: none;
            padding: 8px 20px;
            font-weight: bold;
            border-radius: 4px;
        }

        QPushButton:hover {
            background-color: #1177bb;
        }

        QPushButton:pressed {
            background-color: #094771;
        }

        QPushButton:disabled {
            background-color: #555555;
            color: #888888;
        }

        QPushButton#saveButton {
            background-color: #27ae60;
        }

        QPushButton#saveButton:hover {
            background-color: #2ecc71;
        }

        QPushButton#saveButton:pressed {
            background-color: #1e8449;
        }

        QLineEdit {
            background-color: #3c3c3c;
            color: #d4d4d4;
            border: 1px solid #555555;
            border-radius: 4px;
            padding: 8px;
        }

        QLineEdit:focus {
            border: 1px solid #0e639c;
        }

        QLineEdit:disabled {
            background-color: #2d2d30;
            color: #808080;
            border: 1px solid #3f3f46;
        }

        QSpinBox, QDoubleSpinBox {
            background-color: #3c3c3c;
            color: #d4d4d4;
            border: 1px solid #555555;
            border-radius: 4px;
            padding: 6px;
        }

        QSpinBox:focus, QDoubleSpinBox:focus {
            border-color: #0e639c;
        }

        QCheckBox {
            color: #d4d4d4;
            spacing: 8px;
        }

        QCheckBox::indicator {
            width: 18px;
            height: 18px;
            border-radius: 3px;
            border: 1px solid #555555;
            background-color: #3c3c3c;
        }

        QCheckBox::indicator:checked {
            background-color: #0e639c;
            border-color: #0e639c;
        }

        QListWidget {
            background-color: #1e1e1e;
            color: #d4d4d4;
            border: 1px solid #3f3f46;
            border-radius: 4px;
            outline: none;
        }

        QListWidget::item {
            padding: 8px 12px;
            border-bottom: 1px solid #3f3f46;
        }

        QListWidget::item:selected {
            background-color: #0e639c;
            color: #ffffff;
        }

        QListWidget::item:hover:!selected {
            background-color: #2d2d30;
        }

        QScrollArea {
            background-color: #1e1e1e;
            border: none;
        }

        QScrollBar:vertical {
            background: #2d2d30;
            width: 12px;
            border-radius: 6px;
        }

        QScrollBar::handle:vertical {
            background: #555555;
            border-radius: 6px;
            min-height: 30px;
        }

        QScrollBar::handle:vertical:hover {
            background: #666666;
        }

        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
        }

        QScrollBar:horizontal {
            background: #2d2d30;
            height: 12px;
            border-radius: 6px;
        }

        QScrollBar::handle:horizontal {
            background: #555555;
            border-radius: 6px;
            min-width: 30px;
        }

        QScrollBar::handle:horizontal:hover {
            background: #666666;
        }

        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
            width: 0;
        }

        QGroupBox {
            color: #4fc3f7;
            font-weight: bold;
            border: 1px solid #3f3f46;
            border-radius: 6px;
            margin-top: 12px;
            padding-top: 8px;
        }

        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            padding: 0 8px;
            background-color: #1e1e1e;
        }

        QToolButton {
            background-color: #2d2d30;
            color: #4fc3f7;
            border: 1px solid #3f3f46;
            border-radius: 4px;
            padding: 8px 12px;
            text-align: left;
            font-weight: bold;
        }

        QToolButton:hover {
            background-color: #3c3c3c;
        }

        QToolButton:checked {
            background-color: #3c3c3c;
        }

        QStatusBar {
            background-color: #007acc;
            color: white;
        }

        QSplitter::handle {
            background-color: #3f3f46;
        }

        QSplitter::handle:horizontal {
            width: 2px;
        }

        QSplitter::handle:vertical {
            height: 2px;
        }

        QLabel {
            color: #d4d4d4;
        }

        /* Changed field highlight */
        QLineEdit[changed="true"], QSpinBox[changed="true"], QDoubleSpinBox[changed="true"] {
            border: 2px solid #f39c12;
        }
    )").arg(m_fontSize);

    qApp->setStyleSheet(styleSheet);
}

void ConfigEditorWindow::setupUI()
{
    QWidget* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Create splitter for two-panel layout
    m_splitter = new QSplitter(Qt::Horizontal, this);

    // Left panel - file list
    m_fileListWidget = new JsonFileListWidget(this);
    m_fileListWidget->setMinimumWidth(250);
    m_fileListWidget->setMaximumWidth(400);

    // Right panel - JSON editor
    m_editorWidget = new JsonEditorWidget(this);

    m_splitter->addWidget(m_fileListWidget);
    m_splitter->addWidget(m_editorWidget);
    m_splitter->setStretchFactor(0, 1);  // File list gets 1 part
    m_splitter->setStretchFactor(1, 3);  // Editor gets 3 parts

    mainLayout->addWidget(m_splitter);

    // Connect signals
    connect(m_fileListWidget, &JsonFileListWidget::fileSelected,
            this, &ConfigEditorWindow::onFileSelected);
}

void ConfigEditorWindow::setupMenuBar()
{
    QMenuBar* menuBar = this->menuBar();

    // File menu
    QMenu* fileMenu = menuBar->addMenu("&File");

    QAction* setFolderAction = fileMenu->addAction("Set &Folder...");
    setFolderAction->setShortcut(QKeySequence("Ctrl+O"));
    connect(setFolderAction, &QAction::triggered, this, &ConfigEditorWindow::onSetFolderPath);

    fileMenu->addSeparator();

    QAction* saveAction = fileMenu->addAction("&Save");
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &ConfigEditorWindow::onSaveClicked);

    QAction* reloadAction = fileMenu->addAction("&Reload");
    reloadAction->setShortcut(QKeySequence("F5"));
    connect(reloadAction, &QAction::triggered, this, &ConfigEditorWindow::onReloadClicked);

    fileMenu->addSeparator();

    QAction* exitAction = fileMenu->addAction("E&xit");
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QMainWindow::close);

    // View menu
    QMenu* viewMenu = menuBar->addMenu("&View");

    QAction* increaseFontAction = viewMenu->addAction("Increase Font Size");
    increaseFontAction->setShortcut(QKeySequence("Ctrl++"));
    connect(increaseFontAction, &QAction::triggered, this, &ConfigEditorWindow::onIncreaseFontSize);

    QAction* decreaseFontAction = viewMenu->addAction("Decrease Font Size");
    decreaseFontAction->setShortcut(QKeySequence("Ctrl+-"));
    connect(decreaseFontAction, &QAction::triggered, this, &ConfigEditorWindow::onDecreaseFontSize);

    QAction* resetFontAction = viewMenu->addAction("Reset Font Size");
    resetFontAction->setShortcut(QKeySequence("Ctrl+0"));
    connect(resetFontAction, &QAction::triggered, this, &ConfigEditorWindow::onResetFontSize);

    // Path menu
    QMenu* pathMenu = menuBar->addMenu("&Path");
    QAction* setPathAction = pathMenu->addAction("Set Config &Folder...");
    connect(setPathAction, &QAction::triggered, this, &ConfigEditorWindow::onSetFolderPath);
}

void ConfigEditorWindow::setupToolBar()
{
    QToolBar* toolbar = addToolBar("Main Toolbar");
    toolbar->setMovable(false);
    toolbar->setFloatable(false);

    // Save button
    m_saveButton = new QPushButton("Save", this);
    m_saveButton->setObjectName("saveButton");
    m_saveButton->setEnabled(false);
    connect(m_saveButton, &QPushButton::clicked, this, &ConfigEditorWindow::onSaveClicked);
    toolbar->addWidget(m_saveButton);

    // Reload button
    m_reloadButton = new QPushButton("Reload", this);
    m_reloadButton->setEnabled(false);
    connect(m_reloadButton, &QPushButton::clicked, this, &ConfigEditorWindow::onReloadClicked);
    toolbar->addWidget(m_reloadButton);

    toolbar->addSeparator();

    // Current file label
    QLabel* fileLabel = new QLabel("", this);
    fileLabel->setObjectName("currentFileLabel");
    toolbar->addWidget(fileLabel);
}

void ConfigEditorWindow::loadSettings()
{
    QSettings settings("BobcatEngine", "ConfigEditor");
    m_currentFolder = settings.value("lastFolder", "").toString();
    m_fontSize = settings.value("fontSize", DEFAULT_FONT_SIZE).toInt();

    // Clamp font size to valid range
    if (m_fontSize < MIN_FONT_SIZE) m_fontSize = MIN_FONT_SIZE;
    if (m_fontSize > MAX_FONT_SIZE) m_fontSize = MAX_FONT_SIZE;

    // Note: Window geometry and splitter state are restored after setupUI() in constructor
}

void ConfigEditorWindow::saveSettings()
{
    QSettings settings("BobcatEngine", "ConfigEditor");
    settings.setValue("lastFolder", m_currentFolder);
    settings.setValue("fontSize", m_fontSize);
    settings.setValue("geometry", saveGeometry());
    if (m_splitter) {
        settings.setValue("splitterState", m_splitter->saveState());
    }
}

void ConfigEditorWindow::onIncreaseFontSize()
{
    if (m_fontSize < MAX_FONT_SIZE) {
        m_fontSize += 2;
        applyStyleSheet();
        saveSettings();
        statusBar()->showMessage(QString("Font size: %1pt").arg(m_fontSize), 2000);
    }
}

void ConfigEditorWindow::onDecreaseFontSize()
{
    if (m_fontSize > MIN_FONT_SIZE) {
        m_fontSize -= 2;
        applyStyleSheet();
        saveSettings();
        statusBar()->showMessage(QString("Font size: %1pt").arg(m_fontSize), 2000);
    }
}

void ConfigEditorWindow::onResetFontSize()
{
    m_fontSize = DEFAULT_FONT_SIZE;
    applyStyleSheet();
    saveSettings();
    statusBar()->showMessage(QString("Font size: %1pt").arg(m_fontSize), 2000);
}

void ConfigEditorWindow::onSetFolderPath()
{
    QString folder = QFileDialog::getExistingDirectory(
        this,
        "Select Config Folder",
        m_currentFolder.isEmpty() ? QCoreApplication::applicationDirPath() : m_currentFolder,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );

    if (!folder.isEmpty()) {
        m_currentFolder = folder;
        m_fileListWidget->setFolder(folder);
        saveSettings();
    }
}

void ConfigEditorWindow::onFileSelected(const QString& filePath)
{
    if (m_editorWidget->hasUnsavedChanges()) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            "Unsaved Changes",
            "You have unsaved changes. Do you want to save before switching files?",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel
        );

        if (reply == QMessageBox::Save) {
            onSaveClicked();
        } else if (reply == QMessageBox::Cancel) {
            return;
        }
    }

    m_currentFile = filePath;
    m_editorWidget->loadFile(filePath);

    // Update toolbar
    m_saveButton->setEnabled(true);
    m_reloadButton->setEnabled(true);

    // Update file label in toolbar
    QLabel* fileLabel = findChild<QLabel*>("currentFileLabel");
    if (fileLabel) {
        QFileInfo info(filePath);
        fileLabel->setText("File: " + info.fileName());
    }

    setWindowTitle("Config Editor - " + QFileInfo(filePath).fileName());
}

void ConfigEditorWindow::onSaveClicked()
{
    if (m_currentFile.isEmpty()) {
        return;
    }

    if (m_editorWidget->saveFile(m_currentFile)) {
        statusBar()->showMessage("File saved successfully", 3000);
    } else {
        QMessageBox::warning(this, "Save Error", "Failed to save the file.");
    }
}

void ConfigEditorWindow::onReloadClicked()
{
    if (m_currentFile.isEmpty()) {
        return;
    }

    if (m_editorWidget->hasUnsavedChanges()) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            "Unsaved Changes",
            "You have unsaved changes. Are you sure you want to reload?",
            QMessageBox::Yes | QMessageBox::No
        );

        if (reply != QMessageBox::Yes) {
            return;
        }
    }

    m_editorWidget->loadFile(m_currentFile);
    statusBar()->showMessage("File reloaded", 3000);
}
