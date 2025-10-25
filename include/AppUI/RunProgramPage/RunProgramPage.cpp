// RunProgramPage.cpp
#include "RunProgramPage.h"
#include "ApplicationContext.h"
#include "ProgramManager.h"
#include "ProgramWorker.h"
#include "ProcessSequenceFactory.h"
#include "UserPromptWidget.h"
#include "ScanControlWidget.h"
#include "ScanExecutor.h"
#include "ILogger.h"
#include "LogManager.h"
#include "DataStore.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QTextEdit>
#include <QEventLoop>
#include <QMetaObject>
#include <iostream>
#include <cmath>

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
  , m_scanWidget(nullptr)
  , m_scanExecutor(nullptr)
  , m_dataStore(context->dataStore)  // Add this line
  , m_currentSubscribedChannel("")   // Add this line
{
  // Create ProcessSequenceFactory
  m_processFactory = new ProcessSequenceFactory();

  // Create ScanExecutor
  m_scanExecutor = new ScanExecutor(this);
  m_scanExecutor->setMotionManager(m_context->motionManager);
  m_scanExecutor->setDataStore(m_context->dataStore);
  if (m_context->logManager) {
    m_scanExecutor->setLogger(m_context->logManager);
  }

  setupUI();
}

RunProgramPage::~RunProgramPage()
{
  // ✅ ADDED: Unsubscribe from DataStore
  if (m_dataStore && !m_currentSubscribedChannel.isEmpty()) {
    m_dataStore->Unsubscribe(m_currentSubscribedChannel.toStdString());
  }

  if (m_programWorker && m_programWorker->isRunning()) {
    m_programWorker->requestStop();
    m_programWorker->wait(3000);
  }

  // ScanExecutor cleanup is automatic via QObject parent-child relationship

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

  // === SCANNING CONTROLS (Refactored) ===
  m_scanWidget = new ScanControlWidget();

  // Populate channels from DataStore
  if (m_context && m_context->dataStore) {
    QStringList channels;
    auto channelNames = m_context->dataStore->GetAllChannelNames();
    for (const auto& channel : channelNames) {
      channels << QString::fromStdString(channel);
    }
    m_scanWidget->setAvailableChannels(channels);
  }

  layout->addWidget(m_scanWidget);

  // Connect scan widget to this page
  connect(m_scanWidget, &ScanControlWidget::scanRequested,
    this, &RunProgramPage::onScanRequested);
  connect(m_scanWidget, &ScanControlWidget::stopRequested,
    this, &RunProgramPage::onStopScanRequested);
  connect(m_scanWidget, &ScanControlWidget::channelChanged,
    this, &RunProgramPage::onChannelChanged);  // ✅ ADDED

  // ✅ ADDED: Trigger subscription for initial channel
  if (!m_scanWidget->currentChannel().isEmpty()) {
    onChannelChanged(m_scanWidget->currentChannel());
  }

  // Connect executor signals to UI
  connect(m_scanExecutor, &ScanExecutor::logMessage,
    this, &RunProgramPage::onScanLogMessage);
  connect(m_scanExecutor, &ScanExecutor::scanCompleted,
    this, &RunProgramPage::onScanCompleted);
  connect(m_scanExecutor, &ScanExecutor::scanError,
    this, &RunProgramPage::onScanError);
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

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

namespace {
  // Format value with SI prefix (mA, μA, nA, etc.)
  QString formatWithSIPrefix(double value) {
    const double absValue = std::abs(value);

    if (absValue == 0.0) {
      return "0.000";
    }

    // SI prefixes and their thresholds
    if (absValue >= 1.0) {
      return QString("%1").arg(value, 0, 'f', 3);
    }
    else if (absValue >= 1e-3) {
      return QString("%1 m").arg(value * 1e3, 0, 'f', 3);
    }
    else if (absValue >= 1e-6) {
      return QString("%1 μ").arg(value * 1e6, 0, 'f', 3);
    }
    else if (absValue >= 1e-9) {
      return QString("%1 n").arg(value * 1e9, 0, 'f', 3);
    }
    else {
      return QString("%1 p").arg(value * 1e12, 0, 'f', 3);
    }
  }
}

// SCANNING INTEGRATION (Refactored)
// ============================================================================

void RunProgramPage::onScanRequested(QString deviceName, bool isCoarse, QString channel)
{
  // Validation
  if (channel.isEmpty()) {
    QMessageBox::warning(this, "Scan Error", "Please select a data channel");
    return;
  }

  if (!m_context || !m_context->motionManager || !m_context->dataStore) {
    QMessageBox::warning(this, "Scan Error", "Motion manager or data store not available");
    return;
  }

  // Disable scan buttons, enable stop button
  m_scanWidget->setScanButtonsEnabled(false);
  m_scanWidget->setStopButtonEnabled(true);

  // Execute scan - all business logic handled by ScanExecutor
  bool success = m_scanExecutor->executeScan(deviceName, isCoarse, channel);

  if (!success) {
    // Re-enable controls if scan failed to start
    m_scanWidget->setScanButtonsEnabled(true);
    m_scanWidget->setStopButtonEnabled(false);
  }
}

void RunProgramPage::onScanLogMessage(QString message)
{
  // Forward log messages to execution log
  m_logTextEdit->append(message);
}

void RunProgramPage::onScanCompleted(QString deviceName, double peakValue, bool success)
{
  // Re-enable scan controls, disable stop button
  m_scanWidget->setScanButtonsEnabled(true);
  m_scanWidget->setStopButtonEnabled(false);

  // Show completion notification
  if (success) {
    QMessageBox::information(this, "Scan Complete",
      QString("Scan completed on %1\nPeak value: %2")
      .arg(deviceName)
      .arg(peakValue, 0, 'f', 6));
  }
  else {
    QMessageBox::warning(this, "Scan Complete",
      QString("Scan completed on %1, but no peak found")
      .arg(deviceName));
  }
}

void RunProgramPage::onScanError(QString errorMessage)
{
  // Re-enable scan controls, disable stop button
  m_scanWidget->setScanButtonsEnabled(true);
  m_scanWidget->setStopButtonEnabled(false);

  // Show error dialog
  QMessageBox::critical(this, "Scan Error", errorMessage);
}

void RunProgramPage::onStopScanRequested()
{
  // Stop any active scan
  m_scanExecutor->stopScan();

  // Re-enable scan controls, disable stop button
  m_scanWidget->setScanButtonsEnabled(true);
  m_scanWidget->setStopButtonEnabled(false);

  // Log the stop action
  m_logTextEdit->append("Scan stopped by user");
}

// ✅ ADDED: Handle channel change from scan widget
void RunProgramPage::onChannelChanged(QString channel)
{
  if (!m_dataStore) return;

  // Unsubscribe from old channel
  if (!m_currentSubscribedChannel.isEmpty()) {
    m_dataStore->Unsubscribe(m_currentSubscribedChannel.toStdString());
  }

  // Store new channel
  m_currentSubscribedChannel = channel;

  if (channel.isEmpty()) {
    m_scanWidget->updateChannelValue(0.0, "--");
    return;
  }

  // Subscribe to new channel
  std::string channelStd = channel.toStdString();
  m_dataStore->Subscribe(channelStd,
    [this, channel](const std::string& channelId, float value) {
    // Update widget on UI thread with SI formatted value
    QMetaObject::invokeMethod(this, [this, value]() {
      m_scanWidget->setDisplayText(formatWithSIPrefix(static_cast<double>(value)));
    }, Qt::QueuedConnection);
  });

  // Show current value immediately
  float currentValue = m_dataStore->GetValue(channelStd);
  m_scanWidget->setDisplayText(formatWithSIPrefix(static_cast<double>(currentValue)));
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