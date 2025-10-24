// RunProgramPage.cpp
#include "RunProgramPage.h"
#include "ApplicationContext.h"
#include "ProgramManager.h"
#include "ProgramWorker.h"
#include "ProcessSequenceFactory.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QTextEdit>
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

  // Execution log
  QLabel* logLabel = new QLabel("Execution Log:");
  logLabel->setStyleSheet("font-weight: bold;");
  layout->addWidget(logLabel);

  QTextEdit* logTextEdit = new QTextEdit();
  logTextEdit->setReadOnly(true);
  logTextEdit->setStyleSheet("background-color: #f5f5f5; font-family: monospace;");
  layout->addWidget(logTextEdit, 1);

  m_executionLogLabel = logLabel; // Store reference for updates
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
  int recipeCount = m_programManager->getRecipeCount();

  QMessageBox::StandardButton reply = QMessageBox::question(
    this, "Execute Program",
    QString("Execute program '%1' with %2 recipe(s)?").arg(programName).arg(recipeCount),
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

  // Create and start worker
  m_programWorker = new ProgramWorker(
    m_context->sequenceEngine,
    m_programManager,
    m_processFactory,
    m_context->productManager,
    m_context->motionManager,
    this
  );

  // ✅ NEW: Set UserPrompt callback to show dialog
  m_programWorker->setUserPromptCallback(
    [this](const std::string& title, const std::string& message) -> bool {
    bool result = false;
    // Must use BlockingQueuedConnection to wait for UI response
    QMetaObject::invokeMethod(this, [this, title, message, &result]() {
      QMessageBox msgBox(this);
      msgBox.setWindowTitle(QString::fromStdString(title));
      msgBox.setText(QString::fromStdString(message));
      msgBox.setIcon(QMessageBox::Question);
      msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
      msgBox.setDefaultButton(QMessageBox::Yes);
      result = (msgBox.exec() == QMessageBox::Yes);
    }, Qt::BlockingQueuedConnection);
    return result;
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

void RunProgramPage::onProgramExecutionCompleted(bool success, QString message)
{
  m_isExecuting = false;

  m_executeButton->setText("▶ Execute Program");
  m_executeButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #4CAF50; "
    "  color: white; "
    "  font-size: 16px; "
    "  font-weight: bold; "
    "}");

  m_executionStatusLabel->setText(success ? "✓ Program completed successfully" : "✗ Program failed");
  m_currentRecipeLabel->setText("Program finished");

  QMessageBox msgBox;
  msgBox.setWindowTitle(success ? "Success" : "Failed");
  msgBox.setText(message);
  msgBox.setIcon(success ? QMessageBox::Information : QMessageBox::Critical);
  msgBox.exec();
}

void RunProgramPage::onProgramStopped()
{
  m_isExecuting = false;
  m_executeButton->setText("▶ Execute Program");
  m_executionStatusLabel->setText("Program stopped by user");
}

void RunProgramPage::onWorkerLogMessage(QString message)
{
  std::cout << "[RunProgramPage] " << message.toStdString() << std::endl;
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

  for (const auto& recipe : m_programManager->getAllRecipes()) {
    QString displayText = QString::fromStdString(recipe->displayName);

    if (!recipe->enabled) {
      displayText = "🔘 " + displayText + " (disabled)";
    }
    else {
      displayText = "✓ " + displayText;
    }

    m_recipeListWidget->addItem(displayText);
    m_editRecipeList->addItem(QString::fromStdString(recipe->displayName));
  }

  updateExecuteButton();
}

void RunProgramPage::updateExecuteButton()
{
  bool hasRecipes = m_programManager->getRecipeCount() > 0;
  m_executeButton->setEnabled(hasRecipes && !m_isExecuting);
}