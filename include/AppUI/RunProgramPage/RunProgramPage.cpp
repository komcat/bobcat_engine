// RunProgramPage.cpp
#include "RunProgramPage.h"
#include "ApplicationContext.h"
#include "ProgramManager.h"
#include "ProgramWorker.h"
#include "ProcessSequenceFactory.h"
#include "UserPromptWidget.h"
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


  // ✅ ADD THIS NEW CHECKBOX HERE:
  m_autoRunNextCheckBox = new QCheckBox("Auto-run next recipe");
  m_autoRunNextCheckBox->setChecked(false);  // Default: unchecked (single recipe mode)
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

  // ✅ Larger font and better spacing
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
    "  background-color: #1976D2; "  // Darker blue on hover
    "  color: white; "                // Keep white text
    "}");


  recipeLayout->addWidget(m_recipeListWidget);

  leftLayout->addWidget(recipeGroup, 1); // Give it stretch factor

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

  // Title
  QLabel* titleLabel = new QLabel("Edit Program");
  titleLabel->setStyleSheet("font-size: 18px; font-weight: bold;");
  layout->addWidget(titleLabel);

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

  // ✅ NEW: Create stacked widget to switch between log and prompt
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

  m_executionLogLabel = logLabel; // Store reference for updates

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
}

void RunProgramPage::showEditWidget()
{
  m_rightStack->setCurrentWidget(m_editWidget);
  updateRecipeList(); // Update edit list
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

  // ✅ CHECK EXECUTION MODE
  bool autoRunMode = m_autoRunNextCheckBox->isChecked();
  int selectedRecipeIndex = m_recipeListWidget->currentRow();

  // Validate based on mode
  if (!autoRunMode) {
    // Single recipe mode - need a selection
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
    // Auto-run mode - all enabled recipes
    int enabledCount = 0;
    for (const auto& recipe : m_programManager->getAllRecipes()) {
      if (recipe->enabled) enabledCount++;
    }
    confirmMsg = QString("Execute all %1 enabled recipe(s) in program '%2'?")
      .arg(enabledCount)
      .arg(programName);
  }
  else {
    // Single recipe mode
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

  // Disable checkbox during execution
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

  // ✅ CONFIGURE EXECUTION MODE
  m_programWorker->setAutoRunMode(autoRunMode);
  if (!autoRunMode) {
    m_programWorker->setStartRecipeIndex(selectedRecipeIndex);
  }

  // ✅ Set UserPrompt callback to show inline widget
  connect(this, &RunProgramPage::showUserPrompt,
    this, &RunProgramPage::onShowUserPrompt);

  m_programWorker->setUserPromptCallback(
    [this](const std::string& title, const std::string& message) -> bool {
    // Emit signal to show prompt in UI thread
    emit showUserPrompt(QString::fromStdString(title), QString::fromStdString(message));

    // Create event loop to wait for response (makes callback blocking)
    QEventLoop loop;
    m_promptEventLoop = &loop;
    loop.exec();  // Wait until user responds
    m_promptEventLoop = nullptr;

    return m_promptResponse;
  }
  );

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

  // Update progress
  m_progressBar->setValue(recipeIndex);
}

void RunProgramPage::onRecipeCompleted(int recipeIndex, bool success, QString message)
{
  m_progressBar->setValue(recipeIndex + 1);

  if (!success) {
    m_executionStatusLabel->setText(QString("Recipe failed: %1").arg(message));
  }
}

// In onProgramExecutionCompleted() method, add this to re-enable the checkbox:

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

  // ✅ RE-ENABLE CHECKBOX
  m_autoRunNextCheckBox->setEnabled(true);

  // Show result
  if (success) {
    QMessageBox::information(this, "Success", message);
  }
  else {
    QMessageBox::critical(this, "Execution Failed", message);
  }

  // Update UI...
  // (rest of your existing code)
}

// Also add to onProgramStopped():
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

  // ✅ RE-ENABLE CHECKBOX
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

  // Extract display name from filename
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
  // Update left panel list (read-only display)
  m_recipeListWidget->clear();

  // Update edit list
  m_editRecipeList->clear();

  int recipeNumber = 1;
  for (const auto& recipe : m_programManager->getAllRecipes()) {
    QString displayText;

    // ✅ Format: "1    Recipe Name" or "1    Recipe Name (disabled)"
    if (recipe->enabled) {
      displayText = QString("%1    %2")
        .arg(recipeNumber, 2, 10, QChar('0'))  // Zero-padded number (01, 02, etc.)
        .arg(QString::fromStdString(recipe->displayName));
    }
    else {
      displayText = QString("%1    %2 (disabled)")
        .arg(recipeNumber, 2, 10, QChar('0'))
        .arg(QString::fromStdString(recipe->displayName));

      // Create item with gray color for disabled recipes
      QListWidgetItem* item = new QListWidgetItem(displayText);
      item->setForeground(QColor(150, 150, 150));  // Gray text
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