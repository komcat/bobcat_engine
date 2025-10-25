// RunProgramPage.cpp
#include "RunProgramPage.h"
#include "ApplicationContext.h"
#include "ProgramManager.h"
#include "ProgramWorker.h"
#include "ProcessSequenceFactory.h"
#include "UserPromptWidget.h"
#include "AdaptiveScanController.h"
#include "ScanTypes.h"
#include "ILogger.h"
#include "LogManager.h" 

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QTextEdit>
#include <QEventLoop>
#include <iostream>

RunProgramPage::RunProgramPage(ApplicationContext* context, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_programManager(new ProgramManager())
  , m_programWorker(nullptr)
  , m_processFactory(nullptr)
  , m_isExecuting(false)
  , m_totalRecipes(0)
  , m_currentRecipeIndex(-1)
  , m_executionStack(nullptr)
  , m_promptWidget(nullptr)
  , m_promptEventLoop(nullptr)
  , m_promptResponse(false)
  , m_scanController(nullptr)
{
  // Create ProcessSequenceFactory
  m_processFactory = new ProcessSequenceFactory();

  setupUI();
}

RunProgramPage::~RunProgramPage()
{
  if (m_programWorker && m_programWorker->isRunning()) {
    m_programWorker->requestStop();
    m_programWorker->wait(3000);
  }

  cleanupScanController();

  delete m_programManager;
  delete m_processFactory;
  std::cout << "[RunProgramPage] Destroyed" << std::endl;
}

void RunProgramPage::setupUI()
{
  QHBoxLayout* mainLayout = new QHBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Left panel (20%)
  m_leftPanel = new QWidget();
  m_leftPanel->setMinimumWidth(250);
  m_leftPanel->setMaximumWidth(350);
  QVBoxLayout* leftLayout = new QVBoxLayout(m_leftPanel);
  createLeftPanel(leftLayout);

  // Right panel (80%)
  createRightPanel();

  // Add to main layout with stretch factors (20/80 split)
  mainLayout->addWidget(m_leftPanel, 2);
  mainLayout->addWidget(m_rightStack, 8);

  // Start with edit widget visible
  showEditWidget();
}

void RunProgramPage::createLeftPanel(QVBoxLayout* leftLayout)
{
  leftLayout->setSpacing(10);
  leftLayout->setContentsMargins(10, 10, 10, 10);

  // === TOP SECTION: File Operations ===
  QGroupBox* fileGroup = new QGroupBox("Program File");
  QVBoxLayout* fileLayout = new QVBoxLayout(fileGroup);

  m_programNameLabel = new QLabel("No program loaded");
  m_programNameLabel->setWordWrap(true);
  m_programNameLabel->setStyleSheet("font-weight: bold; color: #2196F3;");
  fileLayout->addWidget(m_programNameLabel);

  QHBoxLayout* fileButtonLayout = new QHBoxLayout();
  m_loadButton = new QPushButton("Load");
  m_saveButton = new QPushButton("Save");
  m_editButton = new QPushButton("Edit");

  fileButtonLayout->addWidget(m_loadButton);
  fileButtonLayout->addWidget(m_saveButton);
  fileButtonLayout->addWidget(m_editButton);
  fileLayout->addLayout(fileButtonLayout);

  leftLayout->addWidget(fileGroup);

  // === MIDDLE SECTION: Execute Controls ===
  QGroupBox* executeGroup = new QGroupBox("Execution");
  QVBoxLayout* executeLayout = new QVBoxLayout(executeGroup);

  m_executeButton = new QPushButton("▶ Execute Program");
  m_executeButton->setMinimumHeight(50);
  m_executeButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #4CAF50; "
    "  color: white; "
    "  font-size: 16px; "
    "  font-weight: bold; "
    "  border-radius: 5px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #45a049; "
    "} "
    "QPushButton:disabled { "
    "  background-color: #cccccc; "
    "}");
  executeLayout->addWidget(m_executeButton);

  m_autoRunNextCheckBox = new QCheckBox("Auto-run next recipe");
  m_autoRunNextCheckBox->setChecked(false);
  m_autoRunNextCheckBox->setStyleSheet(
    "QCheckBox { "
    "  font-size: 12px; "
    "  padding: 5px; "
    "} "
    "QCheckBox::indicator { "
    "  width: 18px; "
    "  height: 18px; "
    "}");
  executeLayout->addWidget(m_autoRunNextCheckBox);

  m_progressBar = new QProgressBar();
  m_progressBar->setRange(0, 100);
  m_progressBar->setValue(0);
  m_progressBar->setTextVisible(true);
  executeLayout->addWidget(m_progressBar);

  leftLayout->addWidget(executeGroup);

  // === BOTTOM SECTION: Recipe List ===
  QGroupBox* recipeGroup = new QGroupBox("Recipes in Program");
  QVBoxLayout* recipeLayout = new QVBoxLayout(recipeGroup);

  m_recipeListWidget = new QListWidget();
  m_recipeListWidget->setAlternatingRowColors(true);

  m_recipeListWidget->setStyleSheet(
    "QListWidget { "
    "  font-size: 14px; "
    "  padding: 2px; "
    "} "
    "QListWidget::item { "
    "  padding: 10px 8px; "
    "  border-bottom: 1px solid #e0e0e0; "
    "} "
    "QListWidget::item:selected { "
    "  background-color: #2196F3; "
    "  color: white; "
    "} "
    "QListWidget::item:hover { "
    "  background-color: #e3f2fd; "
    "} "
    "QListWidget::item:selected:hover { "
    "  background-color: #1976D2; "
    "  color: white; "
    "}");

  recipeLayout->addWidget(m_recipeListWidget);

  leftLayout->addWidget(recipeGroup, 1);

  // === BACK BUTTON ===
  m_backButton = new QPushButton("← Back to Main Menu");
  leftLayout->addWidget(m_backButton);

  // Connect signals
  connect(m_loadButton, &QPushButton::clicked, this, &RunProgramPage::onLoadProgram);
  connect(m_saveButton, &QPushButton::clicked, this, &RunProgramPage::onSaveProgram);
  connect(m_editButton, &QPushButton::clicked, this, &RunProgramPage::onEditProgram);
  connect(m_executeButton, &QPushButton::clicked, this, &RunProgramPage::onExecuteProgram);
  connect(m_backButton, &QPushButton::clicked, this, &RunProgramPage::backToMainMenu);
}

void RunProgramPage::createRightPanel()
{
  m_rightStack = new QStackedWidget();

  createEditWidget();
  createExecutionWidget();

  m_rightStack->addWidget(m_editWidget);
  m_rightStack->addWidget(m_executionWidget);
}

void RunProgramPage::createEditWidget()
{
  m_editWidget = new QWidget();
  QVBoxLayout* layout = new QVBoxLayout(m_editWidget);
  layout->setContentsMargins(20, 20, 20, 20);

  // ✅ NEW: Top bar with Close button and Title
  QHBoxLayout* topBarLayout = new QHBoxLayout();

  QPushButton* closeButton = new QPushButton("← Close");
  closeButton->setMinimumHeight(35);
  closeButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #757575; "
    "  color: white; "
    "  font-size: 13px; "
    "  font-weight: bold; "
    "  padding: 5px 15px; "
    "  border-radius: 5px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #616161; "
    "}");

  QLabel* titleLabel = new QLabel("Edit Program");
  titleLabel->setStyleSheet("font-size: 18px; font-weight: bold;");

  topBarLayout->addWidget(closeButton);
  topBarLayout->addWidget(titleLabel);
  topBarLayout->addStretch();

  layout->addLayout(topBarLayout);

  // Connect close button
  connect(closeButton, &QPushButton::clicked, this, &RunProgramPage::showExecutionWidget);

  // Add Recipe button
  m_addRecipeButton = new QPushButton("+ Add Recipe");
  m_addRecipeButton->setMinimumHeight(40);
  layout->addWidget(m_addRecipeButton);

  // Recipe list
  m_editRecipeList = new QListWidget();
  m_editRecipeList->setAlternatingRowColors(true);
  layout->addWidget(m_editRecipeList, 1);

  // Control buttons
  QHBoxLayout* controlLayout = new QHBoxLayout();
  m_removeRecipeButton = new QPushButton("Remove");
  m_moveUpButton = new QPushButton("↑ Move Up");
  m_moveDownButton = new QPushButton("↓ Move Down");

  controlLayout->addWidget(m_removeRecipeButton);
  controlLayout->addWidget(m_moveUpButton);
  controlLayout->addWidget(m_moveDownButton);
  controlLayout->addStretch();

  layout->addLayout(controlLayout);

  // Save button
  m_saveEditButton = new QPushButton("💾 Save Program");
  m_saveEditButton->setMinimumHeight(40);
  m_saveEditButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #2196F3; "
    "  color: white; "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "  border-radius: 5px; "
    "}");
  layout->addWidget(m_saveEditButton);

  // Connect signals
  connect(m_addRecipeButton, &QPushButton::clicked, this, &RunProgramPage::onAddRecipe);
  connect(m_removeRecipeButton, &QPushButton::clicked, this, &RunProgramPage::onRemoveRecipe);
  connect(m_moveUpButton, &QPushButton::clicked, this, &RunProgramPage::onMoveRecipeUp);
  connect(m_moveDownButton, &QPushButton::clicked, this, &RunProgramPage::onMoveRecipeDown);
  connect(m_saveEditButton, &QPushButton::clicked, this, &RunProgramPage::onSaveEdit);
  connect(m_editRecipeList, &QListWidget::itemSelectionChanged,
    this, &RunProgramPage::onEditRecipeSelectionChanged);
}

void RunProgramPage::createExecutionWidget()
{
  m_executionWidget = new QWidget();
  QVBoxLayout* layout = new QVBoxLayout(m_executionWidget);
  layout->setContentsMargins(20, 20, 20, 20);

  // Title
  QLabel* titleLabel = new QLabel("Program Execution");
  titleLabel->setStyleSheet("font-size: 18px; font-weight: bold;");
  layout->addWidget(titleLabel);

  // Status
  m_executionStatusLabel = new QLabel("Ready to execute");
  m_executionStatusLabel->setStyleSheet("font-size: 14px; color: #666;");
  layout->addWidget(m_executionStatusLabel);

  // Current recipe
  m_currentRecipeLabel = new QLabel("Current Recipe: None");
  m_currentRecipeLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #2196F3;");
  layout->addWidget(m_currentRecipeLabel);

  layout->addSpacing(20);

  // Create stacked widget to switch between log and prompt
  m_executionStack = new QStackedWidget();

  // === LOG VIEW (Index 0) ===
  QWidget* logWidget = new QWidget();
  QVBoxLayout* logLayout = new QVBoxLayout(logWidget);
  logLayout->setContentsMargins(0, 0, 0, 0);

  QLabel* logLabel = new QLabel("Execution Log:");
  logLabel->setStyleSheet("font-weight: bold;");
  logLayout->addWidget(logLabel);

  m_logTextEdit = new QTextEdit();
  m_logTextEdit->setReadOnly(true);
  m_logTextEdit->setStyleSheet("background-color: #f5f5f5; font-family: monospace;");
  logLayout->addWidget(m_logTextEdit, 1);

  m_executionLogLabel = logLabel;

  // === PROMPT VIEW (Index 1) ===
  m_promptWidget = new UserPromptWidget();
  m_promptWidget->setStyleSheet("background-color: #f5f5f5;");

  // Connect prompt response
  connect(m_promptWidget, &UserPromptWidget::responseReceived,
    this, &RunProgramPage::onPromptResponse);

  // Add both views to stack
  m_executionStack->addWidget(logWidget);       // Index 0: Log
  m_executionStack->addWidget(m_promptWidget);  // Index 1: Prompt
  m_executionStack->setCurrentIndex(0);         // Start with log

  layout->addWidget(m_executionStack, 1);

  // ✅ NEW: Scanning control panel (fixed height at bottom)
  createScanControlPanel();
  layout->addWidget(m_scanControlPanel);
}

void RunProgramPage::createScanControlPanel()
{
  m_scanControlPanel = new QWidget();
  m_scanControlPanel->setFixedHeight(150);

  QVBoxLayout* mainLayout = new QVBoxLayout(m_scanControlPanel);
  mainLayout->setContentsMargins(5, 5, 5, 5);

  // Title
  QLabel* title = new QLabel("Hexapod Scanning");
  title->setStyleSheet("font-weight: bold; font-size: 11pt;");
  mainLayout->addWidget(title);

  // Data Channel selector
  QHBoxLayout* controlsRow = new QHBoxLayout();

  QLabel* channelLabel = new QLabel("Data Channel:");
  controlsRow->addWidget(channelLabel);

  m_scanChannelCombo = new QComboBox();
  // Populate from DataStore
  if (m_context && m_context->dataStore) {
    auto channels = m_context->dataStore->GetAllChannelNames();
    for (const auto& channel : channels) {
      m_scanChannelCombo->addItem(QString::fromStdString(channel));
    }
  }
  controlsRow->addWidget(m_scanChannelCombo);
  controlsRow->addStretch();

  mainLayout->addLayout(controlsRow);

  // Info label
  QLabel* infoLabel = new QLabel("Scans all axes: Z → Y → X");
  infoLabel->setStyleSheet("color: #666; font-size: 10pt; font-style: italic;");
  mainLayout->addWidget(infoLabel);

  // Two columns for scan buttons
  QHBoxLayout* columnsLayout = new QHBoxLayout();

  // Left column
  QGroupBox* leftGroup = new QGroupBox("Left Scanning");
  QVBoxLayout* leftLayout = new QVBoxLayout(leftGroup);
  leftLayout->setSpacing(5);

  m_leftCoarseButton = new QPushButton("Left Coarse");
  m_leftCoarseButton->setToolTip("Step sizes: 10, 5, 2, 1 µm");
  leftLayout->addWidget(m_leftCoarseButton);

  m_leftFineButton = new QPushButton("Left Fine");
  m_leftFineButton->setToolTip("Step sizes: 2, 1, 0.5 µm");
  leftLayout->addWidget(m_leftFineButton);

  columnsLayout->addWidget(leftGroup);

  // Right column
  QGroupBox* rightGroup = new QGroupBox("Right Scanning");
  QVBoxLayout* rightLayout = new QVBoxLayout(rightGroup);
  rightLayout->setSpacing(5);

  m_rightCoarseButton = new QPushButton("Right Coarse");
  m_rightCoarseButton->setToolTip("Step sizes: 10, 5, 2, 1 µm");
  rightLayout->addWidget(m_rightCoarseButton);

  m_rightFineButton = new QPushButton("Right Fine");
  m_rightFineButton->setToolTip("Step sizes: 2, 1, 0.5 µm");
  rightLayout->addWidget(m_rightFineButton);

  columnsLayout->addWidget(rightGroup);

  mainLayout->addLayout(columnsLayout);

  // Connect signals
  connect(m_leftCoarseButton, &QPushButton::clicked, this, &RunProgramPage::onLeftCoarseScan);
  connect(m_leftFineButton, &QPushButton::clicked, this, &RunProgramPage::onLeftFineScan);
  connect(m_rightCoarseButton, &QPushButton::clicked, this, &RunProgramPage::onRightCoarseScan);
  connect(m_rightFineButton, &QPushButton::clicked, this, &RunProgramPage::onRightFineScan);
}

void RunProgramPage::showEditWidget()
{
  m_rightStack->setCurrentWidget(m_editWidget);
  updateRecipeList();
}

void RunProgramPage::showExecutionWidget()
{
  m_rightStack->setCurrentWidget(m_executionWidget);
}

// ============================================================================
// FILE OPERATIONS
// ============================================================================

void RunProgramPage::onLoadProgram()
{
  QString filename = QFileDialog::getOpenFileName(
    this,
    "Load Program",
    "programs",
    "Program Files (*.json)"
  );

  if (filename.isEmpty()) return;

  if (!m_programManager->loadProgram(filename.toStdString())) {
    QMessageBox::critical(this, "Load Error", "Failed to load program file");
    return;
  }

  m_currentProgramPath = filename;
  updateProgramInfo();
  updateRecipeList();

  QMessageBox::information(this, "Success",
    QString("Loaded program: %1")
    .arg(QString::fromStdString(m_programManager->getProgramName())));
}

void RunProgramPage::onSaveProgram()
{
  if (m_currentProgramPath.isEmpty()) {
    QString filename = QFileDialog::getSaveFileName(
      this,
      "Save Program",
      "programs",
      "Program Files (*.json)"
    );

    if (filename.isEmpty()) return;
    m_currentProgramPath = filename;
  }

  if (!m_programManager->saveProgram(m_currentProgramPath.toStdString())) {
    QMessageBox::critical(this, "Save Error", "Failed to save program");
    return;
  }

  QMessageBox::information(this, "Success", "Program saved successfully");
}

void RunProgramPage::onEditProgram()
{
  showEditWidget();
}

// ============================================================================
// EXECUTION
// ============================================================================

void RunProgramPage::onExecuteProgram()
{
  if (m_isExecuting) {
    onStopProgram();
    return;
  }

  bool autoRunMode = m_autoRunNextCheckBox->isChecked();
  int selectedRecipeIndex = m_recipeListWidget->currentRow();

  // Validate based on mode
  if (!autoRunMode) {
    if (selectedRecipeIndex < 0) {
      QMessageBox::warning(this, "No Selection",
        "Please select a recipe to execute.\n\n"
        "Tip: Check 'Auto-run next recipe' to run all recipes.");
      return;
    }
  }

  // Validate program
  std::vector<std::string> errors;
  if (!m_programManager->validateProgram(errors)) {
    QString errorMsg = "Program validation failed:\n\n";
    for (const auto& err : errors) {
      errorMsg += "• " + QString::fromStdString(err) + "\n";
    }
    QMessageBox::critical(this, "Validation Error", errorMsg);
    return;
  }

  // Confirm execution
  QString programName = QString::fromStdString(m_programManager->getProgramName());
  QString confirmMsg;

  if (autoRunMode) {
    int enabledCount = 0;
    for (const auto& recipe : m_programManager->getAllRecipes()) {
      if (recipe->enabled) enabledCount++;
    }
    confirmMsg = QString("Execute all %1 enabled recipe(s) in program '%2'?")
      .arg(enabledCount)
      .arg(programName);
  }
  else {
    const RecipeReference* recipe = m_programManager->getRecipe(selectedRecipeIndex);
    if (!recipe) {
      QMessageBox::critical(this, "Error", "Invalid recipe selection");
      return;
    }

    if (!recipe->enabled) {
      QMessageBox::warning(this, "Recipe Disabled",
        QString("Recipe '%1' is disabled. Please enable it first or select a different recipe.")
        .arg(QString::fromStdString(recipe->displayName)));
      return;
    }

    confirmMsg = QString("Execute recipe '%1' only?\n\n"
      "Tip: Check 'Auto-run next recipe' to run all recipes.")
      .arg(QString::fromStdString(recipe->displayName));
  }

  QMessageBox::StandardButton reply = QMessageBox::question(
    this, "Execute Program",
    confirmMsg,
    QMessageBox::Yes | QMessageBox::No
  );

  if (reply != QMessageBox::Yes) return;

  // Switch to execution view
  showExecutionWidget();

  // Update UI
  m_isExecuting = true;
  m_executeButton->setText("⏹ Stop Program");
  m_executeButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #f44336; "
    "  color: white; "
    "  font-size: 16px; "
    "  font-weight: bold; "
    "}");

  m_autoRunNextCheckBox->setEnabled(false);

  // Create and start worker
  m_programWorker = new ProgramWorker(
    m_context->sequenceEngine,
    m_programManager,
    m_processFactory,
    m_context->productManager,
    m_context->motionManager,
    this
  );

  m_programWorker->setAutoRunMode(autoRunMode);
  if (!autoRunMode) {
    m_programWorker->setStartRecipeIndex(selectedRecipeIndex);
  }

  // Set UserPrompt callback
  connect(this, &RunProgramPage::showUserPrompt,
    this, &RunProgramPage::onShowUserPrompt);

  m_programWorker->setUserPromptCallback(
    [this](const std::string& title, const std::string& message) -> bool {
    emit showUserPrompt(QString::fromStdString(title), QString::fromStdString(message));

    QEventLoop loop;
    m_promptEventLoop = &loop;
    loop.exec();
    m_promptEventLoop = nullptr;

    return m_promptResponse;
  });

  // Connect signals
  connect(m_programWorker, &ProgramWorker::programExecutionStarted,
    this, &RunProgramPage::onProgramExecutionStarted);
  connect(m_programWorker, &ProgramWorker::recipeStarted,
    this, &RunProgramPage::onRecipeStarted);
  connect(m_programWorker, &ProgramWorker::recipeCompleted,
    this, &RunProgramPage::onRecipeCompleted);
  connect(m_programWorker, &ProgramWorker::programExecutionCompleted,
    this, &RunProgramPage::onProgramExecutionCompleted);
  connect(m_programWorker, &ProgramWorker::programStopped,
    this, &RunProgramPage::onProgramStopped);
  connect(m_programWorker, &ProgramWorker::logMessage,
    this, &RunProgramPage::onWorkerLogMessage);
  connect(m_programWorker, &ProgramWorker::finished,
    m_programWorker, &QObject::deleteLater);

  // Start execution
  m_programWorker->start();
}

void RunProgramPage::onStopProgram()
{
  if (m_programWorker && m_programWorker->isRunning()) {
    m_programWorker->requestStop();
    m_executionStatusLabel->setText("Stopping program...");
  }
}

void RunProgramPage::onProgramExecutionStarted(int totalRecipes)
{
  m_totalRecipes = totalRecipes;
  m_currentRecipeIndex = 0;
  m_progressBar->setRange(0, totalRecipes);
  m_progressBar->setValue(0);
  m_executionStatusLabel->setText(QString("Executing program with %1 recipes").arg(totalRecipes));
}

void RunProgramPage::onRecipeStarted(int recipeIndex, QString recipeName)
{
  m_currentRecipeIndex = recipeIndex;
  m_currentRecipeLabel->setText(QString("Current Recipe: %1").arg(recipeName));
  m_executionStatusLabel->setText(QString("Recipe %1 of %2: %3")
    .arg(recipeIndex + 1).arg(m_totalRecipes).arg(recipeName));

  m_progressBar->setValue(recipeIndex);
}

void RunProgramPage::onRecipeCompleted(int recipeIndex, bool success, QString message)
{
  m_progressBar->setValue(recipeIndex + 1);

  if (!success) {
    m_executionStatusLabel->setText(QString("Recipe failed: %1").arg(message));
  }
}

void RunProgramPage::onProgramExecutionCompleted(bool success, QString message)
{
  m_isExecuting = false;

  // Re-enable execute button
  m_executeButton->setText("▶ Execute Program");
  m_executeButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #4CAF50; "
    "  color: white; "
    "  font-size: 16px; "
    "  font-weight: bold; "
    "  border-radius: 5px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #45a049; "
    "} "
    "QPushButton:disabled { "
    "  background-color: #cccccc; "
    "}");

  m_autoRunNextCheckBox->setEnabled(true);

  // Show result
  if (success) {
    QMessageBox::information(this, "Success", message);
  }
  else {
    QMessageBox::critical(this, "Execution Failed", message);
  }
}

void RunProgramPage::onProgramStopped()
{
  m_isExecuting = false;

  // Re-enable execute button
  m_executeButton->setText("▶ Execute Program");
  m_executeButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #4CAF50; "
    "  color: white; "
    "  font-size: 16px; "
    "  font-weight: bold; "
    "  border-radius: 5px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #45a049; "
    "} "
    "QPushButton:disabled { "
    "  background-color: #cccccc; "
    "}");

  m_autoRunNextCheckBox->setEnabled(true);

  QMessageBox::information(this, "Stopped", "Program execution stopped by user.");
}

void RunProgramPage::onWorkerLogMessage(QString message)
{
  std::cout << "[RunProgramPage] " << message.toStdString() << std::endl;
}

// ============================================================================
// USER PROMPT HANDLING
// ============================================================================

void RunProgramPage::onShowUserPrompt(QString title, QString message)
{
  // Switch from log view to prompt view
  m_executionStack->setCurrentIndex(1);

  // Show the prompt widget
  m_promptWidget->showPrompt(title, message);
}

void RunProgramPage::onPromptResponse(bool accepted)
{
  // Store the response
  m_promptResponse = accepted;

  // Switch back to log view
  m_executionStack->setCurrentIndex(0);

  // Exit the event loop to unblock the callback
  if (m_promptEventLoop) {
    m_promptEventLoop->quit();
  }
}

// ============================================================================
// SCANNING OPERATIONS
// ============================================================================

void RunProgramPage::onLeftCoarseScan()
{
  startScan("hex-left", true);
}

void RunProgramPage::onLeftFineScan()
{
  startScan("hex-left", false);
}

void RunProgramPage::onRightCoarseScan()
{
  startScan("hex-right", true);
}

void RunProgramPage::onRightFineScan()
{
  startScan("hex-right", false);
}

void RunProgramPage::startScan(const QString& deviceName, bool isCoarse)
{
  // Clean up any existing scan controller
  cleanupScanController();

  // Check if scan already active
  if (m_scanController && m_scanController->IsScanActive()) {
    QMessageBox::warning(this, "Scan Active", "A scan is already in progress");
    return;
  }

  // Get selected data channel
  QString channel = m_scanChannelCombo->currentText();

  if (channel.isEmpty()) {
    QMessageBox::warning(this, "Scan Error", "Please select a data channel");
    return;
  }

  // Validate required components
  if (!m_context || !m_context->motionManager || !m_context->dataStore) {
    QMessageBox::warning(this, "Scan Error", "Motion manager or data store not available");
    return;
  }

  // ✅ CREATE SCAN CONTROLLER ON-DEMAND
  m_scanController = new AdaptiveScanController(
    *m_context->motionManager,
    *m_context->dataStore,
    deviceName.toStdString(),
    channel.toStdString()
  );

  // Set logger if available
  if (m_context->logManager) {
    m_scanController->SetLogger(m_context->logManager);
  }

  // Create scan parameters
  ScanParameters params;

  // ✅ HEXAPOD SCANNING: Scan all 3 axes (Z, Y, X) consecutively
  params.axesToScan = { "Z", "Y", "X" };

  // Set step sizes (convert µm to mm)
  if (isCoarse) {
    // Coarse: 10, 5, 2, 1 µm = 0.010, 0.005, 0.002, 0.001 mm
    params.stepSizes = { 0.010, 0.005, 0.002, 0.001 };
  }
  else {
    // Fine: 2, 1, 0.5 µm = 0.002, 0.001, 0.0005 mm
    params.stepSizes = { 0.002, 0.001, 0.0005 };
  }

  // Motion parameters
  params.motionSettleTimeMs = 400;
  params.velocityPercent = 50.0;

  // Termination criteria (±100µm = 0.2mm total range)
  params.consecutiveDecreasesLimit = 3;
  params.significantDecreaseThreshold = 0.05;  // 5%
  params.maxTotalDistance = 0.2;  // 200µm = 0.2mm per direction

  // Position optimization
  params.improvementThreshold = 0.01;  // 1%

  // Measurement parameters
  params.averagingCount = 3;
  params.measurementTimeout = std::chrono::seconds(5);
  params.minValidValue = -1000.0;  // Adjust based on your sensor
  params.maxValidValue = 1000.0;

  // Safety limits
  params.maxStepSize = 0.5;  // 500µm max
  params.scanTimeout = std::chrono::minutes(10);

  // Log to execution window
  m_logTextEdit->append(QString("Starting %1 scan on %2")
    .arg(isCoarse ? "COARSE" : "FINE")
    .arg(deviceName));
  m_logTextEdit->append(QString("  Scanning axes: Z, Y, X with steps: %1")
    .arg(isCoarse ? "10→5→2→1 µm" : "2→1→0.5 µm"));
  m_logTextEdit->append(QString("  Data channel: %1").arg(channel));

  // Set up callbacks
  m_scanController->SetProgressCallback(
    [this](const ScanProgressEvent& event) {
    QMetaObject::invokeMethod(this, [this, event]() {
      m_logTextEdit->append(QString("Scan: %1% - %2")
        .arg(event.progress * 100, 0, 'f', 1)
        .arg(QString::fromStdString(event.message)));
    }, Qt::QueuedConnection);
  });

  m_scanController->SetCompletionCallback(
    [this, deviceName](const ScanResults& results) {
    QMetaObject::invokeMethod(this, [this, deviceName, results]() {
      if (results.globalPeak.valid) {
        m_logTextEdit->append(QString("✓ Scan complete on %1 - Peak value: %2")
          .arg(deviceName)
          .arg(results.globalPeak.value, 0, 'f', 6));
      }
      else {
        m_logTextEdit->append(QString("✗ Scan complete on %1 - No peak found")
          .arg(deviceName));
      }

      // ✅ CLEANUP AFTER SCAN COMPLETES
      cleanupScanController();
    }, Qt::QueuedConnection);
  });

  m_scanController->SetErrorCallback(
    [this](const ScanError& error) {
    QMetaObject::invokeMethod(this, [this, error]() {
      m_logTextEdit->append(QString("ERROR: %1")
        .arg(QString::fromStdString(error.message)));
      QMessageBox::critical(this, "Scan Error",
        QString::fromStdString(error.message));

      // ✅ CLEANUP AFTER ERROR
      cleanupScanController();
    }, Qt::QueuedConnection);
  });

  // Start the scan (uses default CoarseToFine strategy)
  bool success = m_scanController->StartScan(params);

  if (!success) {
    m_logTextEdit->append("Failed to start scan");
    QMessageBox::critical(this, "Scan Error", "Failed to start scan");
    cleanupScanController();
  }
}

// ============================================================================
// EDIT OPERATIONS
// ============================================================================

void RunProgramPage::onAddRecipe()
{
  QString filename = QFileDialog::getOpenFileName(
    this,
    "Add Recipe",
    "recipes",
    "Recipe Files (*.json)"
  );

  if (filename.isEmpty()) return;

  QFileInfo fileInfo(filename);
  QString displayName = fileInfo.baseName();

  m_programManager->addRecipe(filename.toStdString(), displayName.toStdString());
  updateRecipeList();
}

void RunProgramPage::onRemoveRecipe()
{
  int selectedRow = m_editRecipeList->currentRow();
  if (selectedRow < 0) {
    QMessageBox::warning(this, "No Selection", "Please select a recipe to remove");
    return;
  }

  m_programManager->removeRecipe(selectedRow);
  updateRecipeList();
}

void RunProgramPage::onMoveRecipeUp()
{
  int selectedRow = m_editRecipeList->currentRow();
  if (selectedRow <= 0) return;

  if (m_programManager->moveRecipeUp(selectedRow)) {
    updateRecipeList();
    m_editRecipeList->setCurrentRow(selectedRow - 1);
  }
}

void RunProgramPage::onMoveRecipeDown()
{
  int selectedRow = m_editRecipeList->currentRow();
  if (selectedRow < 0 || selectedRow >= m_programManager->getRecipeCount() - 1) return;

  if (m_programManager->moveRecipeDown(selectedRow)) {
    updateRecipeList();
    m_editRecipeList->setCurrentRow(selectedRow + 1);
  }
}

void RunProgramPage::onSaveEdit()
{
  onSaveProgram();
}

void RunProgramPage::onEditRecipeSelectionChanged()
{
  bool hasSelection = m_editRecipeList->currentRow() >= 0;
  m_removeRecipeButton->setEnabled(hasSelection);
  m_moveUpButton->setEnabled(hasSelection && m_editRecipeList->currentRow() > 0);
  m_moveDownButton->setEnabled(hasSelection &&
    m_editRecipeList->currentRow() < m_programManager->getRecipeCount() - 1);
}

// ============================================================================
// UI UPDATES
// ============================================================================

void RunProgramPage::updateProgramInfo()
{
  QString programName = QString::fromStdString(m_programManager->getProgramName());
  m_programNameLabel->setText(programName);
}

void RunProgramPage::updateRecipeList()
{
  m_recipeListWidget->clear();
  m_editRecipeList->clear();

  int recipeNumber = 1;
  for (const auto& recipe : m_programManager->getAllRecipes()) {
    QString displayText;

    if (recipe->enabled) {
      displayText = QString("%1    %2")
        .arg(recipeNumber, 2, 10, QChar('0'))
        .arg(QString::fromStdString(recipe->displayName));
    }
    else {
      displayText = QString("%1    %2 (disabled)")
        .arg(recipeNumber, 2, 10, QChar('0'))
        .arg(QString::fromStdString(recipe->displayName));

      QListWidgetItem* item = new QListWidgetItem(displayText);
      item->setForeground(QColor(150, 150, 150));
      m_recipeListWidget->addItem(item);
      m_editRecipeList->addItem(QString::fromStdString(recipe->displayName));
      recipeNumber++;
      continue;
    }

    m_recipeListWidget->addItem(displayText);
    m_editRecipeList->addItem(QString::fromStdString(recipe->displayName));
    recipeNumber++;
  }

  updateExecuteButton();
}

void RunProgramPage::updateExecuteButton()
{
  bool hasRecipes = m_programManager->getRecipeCount() > 0;
  m_executeButton->setEnabled(hasRecipes && !m_isExecuting);
}

// ============================================================================
// SCANNING CLEANUP
// ============================================================================

void RunProgramPage::cleanupScanController()
{
  if (m_scanController) {
    // Stop any active scan
    if (m_scanController->IsScanActive()) {
      m_scanController->HaltScan();
    }

    delete m_scanController;
    m_scanController = nullptr;

    std::cout << "[RunProgramPage] Scan controller cleaned up" << std::endl;
  }
}