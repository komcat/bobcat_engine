// RunProgramPage.cpp
#include "RunProgramPage.h"
#include "ApplicationContext.h"
#include "ProgramManager.h"
#include "ProgramWorker.h"
#include "ProcessSequenceFactory.h"
#include "UserPromptWidget.h"
#include "ManualAdjustWidget.h"
#include "ScanControlWidget.h"
#include "ScanExecutor.h"
#include "ILogger.h"
#include "LogManager.h"
#include "DataStore.h"
#include "RecipeManager.h"
#include "product_position_manager.h"  // ✅ For product folder path
#include "FileSelectorDialog.h"  // ✅ Custom file selector

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QTextEdit>
#include <QEventLoop>
#include <QMetaObject>
#include <QHeaderView>
#include <QTableWidgetItem>
#include <QSettings>
#include <QFile>
#include <QDir>
#include <iostream>
#include <cmath>

RunProgramPage::RunProgramPage(ApplicationContext* context, TransformationMatrixLoader* matrixLoader, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_matrixLoader(matrixLoader)
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

  // Update product display
  if (m_context->productPositionManager && m_context->productPositionManager->HasLoadedProduct()) {
    std::string productName = m_context->productPositionManager->GetCurrentProductName();
    m_currentProductLabel->setText(QString("Product: %1").arg(QString::fromStdString(productName)));
  }

  // ✅ Auto-load last program from settings
  loadLastProgramFromSettings();
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
  // Set dark theme for entire page
  setStyleSheet(
    "RunProgramPage { "
    "  background-color: #2d2d30; "
    "  color: #d4d4d4; "
    "} "
    "QWidget { "
    "  color: #d4d4d4; "
    "} "
    "QLabel { "
    "  color: #d4d4d4; "
    "} "
    "QLineEdit { "
    "  background-color: #3c3c3c; "
    "  color: #d4d4d4; "
    "  border: 1px solid #555555; "
    "  border-radius: 3px; "
    "  padding: 5px; "
    "} "
    "QLineEdit:focus { "
    "  border: 1px solid #0e639c; "
    "}"
  );

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

  // === BACK BUTTON (at the very top) ===
  m_backButton = new QPushButton("← Back to Main Menu");
  m_backButton->setMinimumHeight(35);
  m_backButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #3d5a8c; "
    "  color: white; "
    "  font-size: 12px; "
    "  font-weight: bold; "
    "  padding: 5px 15px; "
    "  border-radius: 5px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #5a8fd6; "
    "}");
  leftLayout->addWidget(m_backButton);

  // === Product Display ===
  m_currentProductLabel = new QLabel("Product: None");
  m_currentProductLabel->setStyleSheet(
    "QLabel {"
    "  color: #cccccc;"
    "  font-size: 12px;"
    "  font-weight: bold;"
    "  padding: 5px;"
    "}"
  );
  m_currentProductLabel->setWordWrap(true);
  leftLayout->addWidget(m_currentProductLabel);

  // === TOP SECTION: File Operations ===
  QGroupBox* fileGroup = new QGroupBox("Program File");
  fileGroup->setStyleSheet(
    "QGroupBox { "
    "  background-color: #1e1e1e; "
    "  border: 1px solid #3f3f46; "
    "  border-radius: 4px; "
    "  margin-top: 10px; "
    "  padding-top: 15px; "
    "  color: #d4d4d4; "
    "  font-weight: bold; "
    "} "
    "QGroupBox::title { "
    "  subcontrol-origin: margin; "
    "  subcontrol-position: top left; "
    "  padding: 2px 5px; "
    "  color: #ffffff; "
    "}"
  );
  QVBoxLayout* fileLayout = new QVBoxLayout(fileGroup);

  m_programNameLabel = new QLabel("No program loaded");
  m_programNameLabel->setWordWrap(true);
  m_programNameLabel->setStyleSheet("font-weight: bold; color: #2196F3;");
  fileLayout->addWidget(m_programNameLabel);

  QHBoxLayout* fileButtonLayout = new QHBoxLayout();
  m_loadButton = new QPushButton("Load");
  m_loadButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #3c3c3c; "
    "  color: #d4d4d4; "
    "  border: 1px solid #555555; "
    "  border-radius: 3px; "
    "  padding: 5px 10px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { "
    "  background-color: #4a4a4a; "
    "  border-color: #0e639c; "
    "}"
  );
  m_saveButton = new QPushButton("Save");
  m_saveButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #3c3c3c; "
    "  color: #d4d4d4; "
    "  border: 1px solid #555555; "
    "  border-radius: 3px; "
    "  padding: 5px 10px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { "
    "  background-color: #4a4a4a; "
    "  border-color: #0e639c; "
    "}"
  );
  m_editButton = new QPushButton("Edit");
  m_editButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #3c3c3c; "
    "  color: #d4d4d4; "
    "  border: 1px solid #555555; "
    "  border-radius: 3px; "
    "  padding: 5px 10px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { "
    "  background-color: #4a4a4a; "
    "  border-color: #0e639c; "
    "}"
  );

  fileButtonLayout->addWidget(m_loadButton);
  fileButtonLayout->addWidget(m_saveButton);
  fileButtonLayout->addWidget(m_editButton);
  fileLayout->addLayout(fileButtonLayout);

  leftLayout->addWidget(fileGroup);

  // === MIDDLE SECTION: Execute Controls ===
  QGroupBox* executeGroup = new QGroupBox("Execution");
  executeGroup->setStyleSheet(
    "QGroupBox { "
    "  background-color: #1e1e1e; "
    "  border: 1px solid #3f3f46; "
    "  border-radius: 4px; "
    "  margin-top: 10px; "
    "  padding-top: 15px; "
    "  color: #d4d4d4; "
    "  font-weight: bold; "
    "} "
    "QGroupBox::title { "
    "  subcontrol-origin: margin; "
    "  subcontrol-position: top left; "
    "  padding: 2px 5px; "
    "  color: #ffffff; "
    "}"
  );
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
    "  spacing: 8px; "
    "  font-size: 12px; "
    "  padding: 5px; "
    "  color: #d4d4d4; "
    "} "
    "QCheckBox::indicator { "
    "  width: 20px; "
    "  height: 20px; "
    "  border: 2px solid #555555; "
    "  border-radius: 3px; "
    "  background-color: #2d2d30; "
    "} "
    "QCheckBox::indicator:hover { "
    "  border-color: #0e639c; "
    "} "
    "QCheckBox::indicator:checked { "
    "  background-color: #27ae60; "
    "  border-color: #27ae60; "
    "} "
    "QCheckBox::indicator:checked:hover { "
    "  background-color: #2ecc71; "
    "  border-color: #2ecc71; "
    "} "
    "QCheckBox::indicator:unchecked { "
    "  background-color: #2d2d30; "
    "  border-color: #555555; "
    "}"
  );
  executeLayout->addWidget(m_autoRunNextCheckBox);

  m_progressBar = new QProgressBar();
  m_progressBar->setRange(0, 100);
  m_progressBar->setValue(0);
  m_progressBar->setTextVisible(true);
  m_progressBar->setStyleSheet(
    "QProgressBar { "
    "  border: 1px solid #555555; "
    "  border-radius: 3px; "
    "  background-color: #2d2d30; "
    "  text-align: center; "
    "  color: #d4d4d4; "
    "} "
    "QProgressBar::chunk { "
    "  background-color: #4CAF50; "
    "  border-radius: 2px; "
    "}"
  );
  executeLayout->addWidget(m_progressBar);

  leftLayout->addWidget(executeGroup);

  // === BOTTOM SECTION: Recipe List ===
  QGroupBox* recipeGroup = new QGroupBox("Recipes in Program");
  recipeGroup->setStyleSheet(
    "QGroupBox { "
    "  background-color: #1e1e1e; "
    "  border: 1px solid #3f3f46; "
    "  border-radius: 4px; "
    "  margin-top: 10px; "
    "  padding-top: 15px; "
    "  color: #d4d4d4; "
    "  font-weight: bold; "
    "} "
    "QGroupBox::title { "
    "  subcontrol-origin: margin; "
    "  subcontrol-position: top left; "
    "  padding: 2px 5px; "
    "  color: #ffffff; "
    "}"
  );
  QVBoxLayout* recipeLayout = new QVBoxLayout(recipeGroup);

  m_recipeListWidget = new QListWidget();
  m_recipeListWidget->setAlternatingRowColors(true);

  m_recipeListWidget->setStyleSheet(
    "QListWidget { "
    "  background-color: #1e1e1e; "
    "  border: 1px solid #3f3f46; "
    "  border-radius: 4px; "
    "  font-size: 14px; "
    "  padding: 2px; "
    "  color: #d4d4d4; "
    "  gridline-color: #3f3f46; "
    "} "
    "QListWidget::item { "
    "  padding: 10px 8px; "
    "  border-bottom: 1px solid #3f3f46; "
    "} "
    "QListWidget::item:selected { "
    "  background-color: #0e639c; "
    "  color: white; "
    "} "
    "QListWidget::item:hover { "
    "  background-color: #2a2d2e; "
    "} "
    "QListWidget::item:selected:hover { "
    "  background-color: #0e639c; "
    "  color: white; "
    "}"
  );

  recipeLayout->addWidget(m_recipeListWidget);

  leftLayout->addWidget(recipeGroup, 1);

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

  // === PROGRAM NAME FIELD WITH SAVE BUTTON ===
  QGroupBox* programNameGroup = new QGroupBox("Program Name");
  programNameGroup->setStyleSheet(
    "QGroupBox { "
    "  background-color: #1e1e1e; "
    "  border: 1px solid #3f3f46; "
    "  border-radius: 4px; "
    "  margin-top: 10px; "
    "  padding-top: 15px; "
    "  color: #d4d4d4; "
    "  font-weight: bold; "
    "} "
    "QGroupBox::title { "
    "  subcontrol-origin: margin; "
    "  subcontrol-position: top left; "
    "  padding: 2px 5px; "
    "  color: #ffffff; "
    "}"
  );
  QVBoxLayout* programNameLayout = new QVBoxLayout(programNameGroup);

  // Horizontal layout for name field and save button
  QHBoxLayout* nameAndSaveLayout = new QHBoxLayout();

  m_programNameEdit = new QLineEdit();
  m_programNameEdit->setPlaceholderText("Enter program name...");
  m_programNameEdit->setMinimumHeight(35);
  m_programNameEdit->setStyleSheet("font-size: 14px; padding: 5px;");
  nameAndSaveLayout->addWidget(m_programNameEdit, 1);  // Stretch factor 1

  // Save button next to name field
  m_saveEditButton = new QPushButton("💾 Save");
  m_saveEditButton->setMinimumHeight(35);
  m_saveEditButton->setMinimumWidth(100);
  m_saveEditButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #2196F3; "
    "  color: white; "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "  border-radius: 5px; "
    "  padding: 5px 15px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #1976D2; "
    "}");
  nameAndSaveLayout->addWidget(m_saveEditButton);

  programNameLayout->addLayout(nameAndSaveLayout);
  layout->addWidget(programNameGroup);

  // Add Recipe button
  m_addRecipeButton = new QPushButton("+ Add Recipe");
  m_addRecipeButton->setMinimumHeight(40);
  m_addRecipeButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #27ae60; "
    "  color: white; "
    "  font-weight: bold; "
    "  border-radius: 5px; "
    "  font-size: 14px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #2ecc71; "
    "}"
  );
  layout->addWidget(m_addRecipeButton);

  // === RECIPE TABLE AND PREVIEW (side by side) ===
  QSplitter* recipeSplitter = new QSplitter(Qt::Horizontal);

  // Left: Recipe Table
  QWidget* tableContainer = new QWidget();
  QVBoxLayout* tableLayout = new QVBoxLayout(tableContainer);
  tableLayout->setContentsMargins(0, 0, 0, 0);

  QLabel* tableLabel = new QLabel("Recipes in Program");
  tableLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #ffffff;");
  tableLayout->addWidget(tableLabel);

  m_editRecipeTable = new QTableWidget();
  m_editRecipeTable->setColumnCount(3);
  m_editRecipeTable->setHorizontalHeaderLabels({"#", "Recipe Name", "Status"});
  m_editRecipeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_editRecipeTable->setSelectionMode(QAbstractItemView::SingleSelection);
  m_editRecipeTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_editRecipeTable->setAlternatingRowColors(true);
  m_editRecipeTable->verticalHeader()->setVisible(false);
  m_editRecipeTable->horizontalHeader()->setStretchLastSection(false);
  m_editRecipeTable->setColumnWidth(0, 40);   // # column
  m_editRecipeTable->setColumnWidth(1, 250);  // Recipe Name
  m_editRecipeTable->setColumnWidth(2, 80);   // Status
  m_editRecipeTable->setStyleSheet(
    "QTableWidget { "
    "  background-color: #1e1e1e; "
    "  border: 1px solid #3f3f46; "
    "  border-radius: 4px; "
    "  gridline-color: #3f3f46; "
    "  font-size: 13px; "
    "  color: #d4d4d4; "
    "  show-decoration-selected: 1; "
    "} "
    "QTableWidget::item { "
    "  border-bottom: 1px solid #3f3f46; "
    "  padding: 2px; "
    "} "
    "QTableWidget::item:hover { "
    "  background-color: #2a2d2e; "
    "} "
    "QTableWidget::item:selected { "
    "  background-color: #0e639c; "
    "  color: white; "
    "} "
    "QHeaderView::section { "
    "  background-color: #2d2d30; "
    "  color: #d4d4d4; "
    "  padding: 5px; "
    "  border: 1px solid #3f3f46; "
    "  font-weight: bold; "
    "}"
  );
  tableLayout->addWidget(m_editRecipeTable);

  recipeSplitter->addWidget(tableContainer);

  // Right: Recipe Preview
  QWidget* previewContainer = new QWidget();
  QVBoxLayout* previewLayout = new QVBoxLayout(previewContainer);
  previewLayout->setContentsMargins(0, 0, 0, 0);

  QLabel* previewLabel = new QLabel("Recipe Preview");
  previewLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #ffffff;");
  previewLayout->addWidget(previewLabel);

  m_recipePreviewText = new QTextEdit();
  m_recipePreviewText->setReadOnly(true);
  m_recipePreviewText->setPlaceholderText("Select a recipe to see its steps...");
  m_recipePreviewText->setStyleSheet(
    "QTextEdit { "
    "  font-family: 'Consolas', 'Courier New', monospace; "
    "  font-size: 12px; "
    "  background-color: #1e1e1e; "
    "  color: #d4d4d4; "
    "  border: 1px solid #3f3f46; "
    "  border-radius: 4px; "
    "}"
  );
  previewLayout->addWidget(m_recipePreviewText);

  recipeSplitter->addWidget(previewContainer);

  // Set splitter sizes (60% table, 40% preview)
  recipeSplitter->setStretchFactor(0, 6);
  recipeSplitter->setStretchFactor(1, 4);

  layout->addWidget(recipeSplitter, 1);

  // Control buttons
  QHBoxLayout* controlLayout = new QHBoxLayout();
  m_removeRecipeButton = new QPushButton("Remove");
  m_removeRecipeButton->setStyleSheet(
    "QPushButton { background-color: #e74c3c; color: white; font-weight: bold; border-radius: 3px; padding: 5px 15px; }"
    "QPushButton:hover { background-color: #c0392b; }"
    "QPushButton:disabled { background-color: #bdc3c7; }"
  );

  m_moveUpButton = new QPushButton("↑ Move Up");
  m_moveUpButton->setStyleSheet(
    "QPushButton { background-color: #95a5a6; color: white; font-weight: bold; border-radius: 3px; padding: 5px 15px; }"
    "QPushButton:hover { background-color: #7f8c8d; }"
    "QPushButton:disabled { background-color: #bdc3c7; }"
  );

  m_moveDownButton = new QPushButton("↓ Move Down");
  m_moveDownButton->setStyleSheet(
    "QPushButton { background-color: #95a5a6; color: white; font-weight: bold; border-radius: 3px; padding: 5px 15px; }"
    "QPushButton:hover { background-color: #7f8c8d; }"
    "QPushButton:disabled { background-color: #bdc3c7; }"
  );

  // ✅ NEW: Toggle Enable/Disable button
  m_toggleEnableButton = new QPushButton("⇄ Toggle Enable/Disable");
  m_toggleEnableButton->setStyleSheet(
    "QPushButton { background-color: #f39c12; color: white; font-weight: bold; border-radius: 3px; padding: 5px 15px; }"
    "QPushButton:hover { background-color: #e67e22; }"
    "QPushButton:disabled { background-color: #bdc3c7; }"
  );
  m_toggleEnableButton->setEnabled(false);

  controlLayout->addWidget(m_removeRecipeButton);
  controlLayout->addWidget(m_moveUpButton);
  controlLayout->addWidget(m_moveDownButton);
  controlLayout->addSpacing(10);
  controlLayout->addWidget(m_toggleEnableButton);
  controlLayout->addStretch();

  layout->addLayout(controlLayout);

  // Connect signals
  connect(m_addRecipeButton, &QPushButton::clicked, this, &RunProgramPage::onAddRecipe);
  connect(m_removeRecipeButton, &QPushButton::clicked, this, &RunProgramPage::onRemoveRecipe);
  connect(m_moveUpButton, &QPushButton::clicked, this, &RunProgramPage::onMoveRecipeUp);
  connect(m_moveDownButton, &QPushButton::clicked, this, &RunProgramPage::onMoveRecipeDown);
  connect(m_toggleEnableButton, &QPushButton::clicked, this, &RunProgramPage::onToggleRecipeEnabled);  // ✅ NEW
  connect(m_saveEditButton, &QPushButton::clicked, this, &RunProgramPage::onSaveEdit);
  connect(m_editRecipeTable, &QTableWidget::itemSelectionChanged,
    this, &RunProgramPage::onEditRecipeSelectionChanged);
}

void RunProgramPage::createExecutionWidget()
{
  m_executionWidget = new QWidget();
  QVBoxLayout* layout = new QVBoxLayout(m_executionWidget);
  layout->setContentsMargins(20, 20, 20, 20);

  // Title
  QLabel* titleLabel = new QLabel("Program Execution");
  titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #ffffff;");
  layout->addWidget(titleLabel);

  // Status
  m_executionStatusLabel = new QLabel("Ready to execute");
  m_executionStatusLabel->setStyleSheet("font-size: 14px; color: #cccccc;");
  layout->addWidget(m_executionStatusLabel);

  // Current recipe
  m_currentRecipeLabel = new QLabel("Current Recipe: None");
  m_currentRecipeLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #0e639c;");
  layout->addWidget(m_currentRecipeLabel);

  layout->addSpacing(20);

  // Create stacked widget to switch between log and prompt
  m_executionStack = new QStackedWidget();

  // === LOG VIEW (Index 0) ===
  QWidget* logWidget = new QWidget();
  QVBoxLayout* logLayout = new QVBoxLayout(logWidget);
  logLayout->setContentsMargins(0, 0, 0, 0);

  QLabel* logLabel = new QLabel("Execution Log:");
  logLabel->setStyleSheet("font-weight: bold; color: #ffffff;");
  logLayout->addWidget(logLabel);

  m_logTextEdit = new QTextEdit();
  m_logTextEdit->setReadOnly(true);
  m_logTextEdit->setStyleSheet(
    "QTextEdit { "
    "  background-color: #1e1e1e; "
    "  color: #d4d4d4; "
    "  border: 1px solid #3f3f46; "
    "  border-radius: 4px; "
    "  font-family: 'Consolas', 'Courier New', monospace; "
    "  font-size: 12px; "
    "}"
  );
  logLayout->addWidget(m_logTextEdit, 1);

  m_executionLogLabel = logLabel;

  // === PROMPT VIEW (Index 1) ===
  m_promptWidget = new UserPromptWidget();
  m_promptWidget->setStyleSheet(
    "UserPromptWidget { "
    "  background-color: #1e1e1e; "
    "}"
  );

  // Connect prompt response
  connect(m_promptWidget, &UserPromptWidget::responseReceived,
    this, &RunProgramPage::onPromptResponse);

  // === MANUAL ADJUST VIEW (Index 2) ===
  m_manualAdjustWidget = new ManualAdjustWidget(
    m_context->motionManager,
    m_matrixLoader);
  m_manualAdjustWidget->setStyleSheet(
    "ManualAdjustWidget { "
    "  background-color: #1e1e1e; "
    "}"
  );

  // Connect manual adjust response
  connect(m_manualAdjustWidget, &ManualAdjustWidget::responseReceived,
    this, &RunProgramPage::onManualAdjustResponse);

  // Add all views to stack
  m_executionStack->addWidget(logWidget);              // Index 0: Log
  m_executionStack->addWidget(m_promptWidget);         // Index 1: Prompt
  m_executionStack->addWidget(m_manualAdjustWidget);  // Index 2: Manual Adjust
  m_executionStack->setCurrentIndex(0);                // Start with log

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

  // Update program name field
  m_programNameEdit->setText(QString::fromStdString(m_programManager->getProgramName()));
}

void RunProgramPage::showExecutionWidget()
{
  m_rightStack->setCurrentWidget(m_executionWidget);
}

// ============================================================================
// FILE OPERATIONS
// ============================================================================

void RunProgramPage::onProductChanged()
{
  // Update product display
  if (m_context->productPositionManager && m_context->productPositionManager->HasLoadedProduct()) {
    std::string productName = m_context->productPositionManager->GetCurrentProductName();
    m_currentProductLabel->setText(QString("Product: %1").arg(QString::fromStdString(productName)));

    std::cout << "[RunProgramPage] Product changed to: " << productName << std::endl;

    // ✅ Auto-load last program when product is loaded
    loadLastProgramFromSettings();
  } else {
    m_currentProductLabel->setText("Product: None");
    std::cout << "[RunProgramPage] No product loaded" << std::endl;

    // Clear current program when no product loaded
    if (m_programManager->getRecipeCount() > 0) {
      m_programManager->newProgram();
      m_currentProgramPath.clear();
      updateRecipeList();
      updateProgramInfo();

      std::cout << "[RunProgramPage] No product - program cleared" << std::endl;
    }
  }
}

void RunProgramPage::onLoadProgram()
{
  // ✅ Check if product is loaded
  if (!m_context->productPositionManager || !m_context->productPositionManager->HasLoadedProduct()) {
    QMessageBox::warning(this, "No Product Loaded",
      "Please load a product first before loading programs.\n\n"
      "Use the 'Load Product' button in the main menu.");
    return;
  }

  // ✅ Get product name and build path to product's programs folder
  QString productName = QString::fromStdString(m_context->productPositionManager->GetCurrentProductName());
  QString programsPath = QString("config/%1/programs").arg(productName);

  // ✅ Use FileSelectorDialog to restrict to product's programs folder
  QString filename = FileSelectorDialog::getOpenFileName(
    this,
    programsPath,  // Folder path: config/[product]/programs/
    "*.json",  // File pattern
    "Load Program",  // Dialog title
    "program files"  // File type description
  );

  if (filename.isEmpty()) return;

  if (!m_programManager->loadProgram(filename.toStdString())) {
    QMessageBox::critical(this, "Load Error", "Failed to load program file");
    return;
  }

  m_currentProgramPath = filename;

  // ✅ Save to settings for persistence
  saveLastProgramToSettings(filename);
  saveRecentPrograms(filename);

  updateProgramInfo();
  updateRecipeList();

  // ✅ Update the program name edit field (for when user switches to edit mode)
  m_programNameEdit->setText(QString::fromStdString(m_programManager->getProgramName()));

  QMessageBox::information(this, "Success",
    QString("Loaded program: %1")
    .arg(QString::fromStdString(m_programManager->getProgramName())));
}

void RunProgramPage::onSaveProgram()
{
  // ✅ Check if product is loaded
  if (!m_context->productPositionManager || !m_context->productPositionManager->HasLoadedProduct()) {
    QMessageBox::warning(this, "No Product Loaded",
      "Please load a product first before saving programs.\n\n"
      "Use the 'Load Product' button in the main menu.");
    return;
  }

  if (m_currentProgramPath.isEmpty()) {
    // ✅ Get product name and build path to product's programs folder
    QString productName = QString::fromStdString(m_context->productPositionManager->GetCurrentProductName());
    QString programsPath = QString("config/%1/programs").arg(productName);

    // ✅ Ensure programs folder exists
    QDir().mkpath(programsPath);

    QString filename = QFileDialog::getSaveFileName(
      this,
      "Save Program",
      programsPath,  // Start in product's programs folder
      "Program Files (*.json)"
    );

    if (filename.isEmpty()) return;
    m_currentProgramPath = filename;
  }

  if (!m_programManager->saveProgram(m_currentProgramPath.toStdString())) {
    QMessageBox::critical(this, "Save Error", "Failed to save program");
    return;
  }

  QMessageBox::information(this, "Success",
    QString("Program saved to:\n%1").arg(m_currentProgramPath));
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
    m_context->productPositionManager,
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

  // Set ManualAdjust callback
  connect(this, &RunProgramPage::showManualAdjust,
    this, &RunProgramPage::onShowManualAdjust);

  m_programWorker->setManualAdjustCallback(
    [this](const std::string& message, const std::string& deviceName,
           double stepSizeUm, bool enableX, bool enableY, bool enableZ) -> bool {
    emit showManualAdjust(QString::fromStdString(message), QString::fromStdString(deviceName),
                          stepSizeUm, enableX, enableY, enableZ);

    QEventLoop loop;
    m_promptEventLoop = &loop;
    loop.exec();
    m_promptEventLoop = nullptr;

    return m_manualAdjustResponse;
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
  // Display in execution log
  m_logTextEdit->append(message);

  // Also print to console for debugging
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
// MANUAL ADJUST HANDLING
// ============================================================================

void RunProgramPage::onShowManualAdjust(QString message, QString deviceName, double stepSizeUm,
                                         bool enableX, bool enableY, bool enableZ)
{
  // Switch from log view to manual adjust view
  m_executionStack->setCurrentIndex(2);

  // Show the manual adjust widget
  m_manualAdjustWidget->showAdjustment(message, deviceName, stepSizeUm, enableX, enableY, enableZ);
}

void RunProgramPage::onManualAdjustResponse(bool completed)
{
  // Store the response
  m_manualAdjustResponse = completed;

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
  // ✅ Check if product is loaded
  if (!m_context->productPositionManager || !m_context->productPositionManager->HasLoadedProduct()) {
    QMessageBox::warning(this, "No Product Loaded",
      "Please load a product first before adding recipes.\n\n"
      "Use the 'Load Product' button in the main menu.");
    return;
  }

  // ✅ Get product name and build path to product's recipes folder
  QString productName = QString::fromStdString(m_context->productPositionManager->GetCurrentProductName());
  QString recipesPath = QString("config/%1/recipes").arg(productName);

  // ✅ Use FileSelectorDialog to restrict to product's recipes folder
  QString filename = FileSelectorDialog::getOpenFileName(
    this,
    recipesPath,  // Folder path: config/[product]/recipes/
    "*.json",  // File pattern
    "Add Recipe",  // Dialog title
    "recipe files"  // File type description
  );

  if (filename.isEmpty()) return;

  QFileInfo fileInfo(filename);
  QString displayName = fileInfo.baseName();

  m_programManager->addRecipe(filename.toStdString(), displayName.toStdString());
  updateRecipeList();
}

void RunProgramPage::onRemoveRecipe()
{
  int selectedRow = m_editRecipeTable->currentRow();
  if (selectedRow < 0) {
    QMessageBox::warning(this, "No Selection", "Please select a recipe to remove");
    return;
  }

  m_programManager->removeRecipe(selectedRow);
  updateRecipeList();
}

void RunProgramPage::onMoveRecipeUp()
{
  int selectedRow = m_editRecipeTable->currentRow();
  if (selectedRow <= 0) return;

  if (m_programManager->moveRecipeUp(selectedRow)) {
    updateRecipeList();
    m_editRecipeTable->selectRow(selectedRow - 1);
  }
}

void RunProgramPage::onMoveRecipeDown()
{
  int selectedRow = m_editRecipeTable->currentRow();
  if (selectedRow < 0 || selectedRow >= m_programManager->getRecipeCount() - 1) return;

  if (m_programManager->moveRecipeDown(selectedRow)) {
    updateRecipeList();
    m_editRecipeTable->selectRow(selectedRow + 1);
  }
}

void RunProgramPage::onToggleRecipeEnabled()
{
  int selectedRow = m_editRecipeTable->currentRow();
  if (selectedRow < 0) return;

  // Get current recipe
  const RecipeReference* recipe = m_programManager->getRecipe(selectedRow);
  if (!recipe) return;

  // Toggle the enabled state
  bool newState = !recipe->enabled;
  m_programManager->setRecipeEnabled(selectedRow, newState);

  // Update the display
  updateRecipeList();

  // Restore selection
  m_editRecipeTable->selectRow(selectedRow);
}

void RunProgramPage::onSaveEdit()
{
  // Save program name from edit field
  QString programName = m_programNameEdit->text().trimmed();
  if (!programName.isEmpty()) {
    m_programManager->setProgramName(programName.toStdString());
  }

  // Update the program name label
  updateProgramInfo();

  // Save the program
  onSaveProgram();
}

void RunProgramPage::onEditRecipeSelectionChanged()
{
  int currentRow = m_editRecipeTable->currentRow();
  bool hasSelection = currentRow >= 0;

  m_removeRecipeButton->setEnabled(hasSelection);
  m_moveUpButton->setEnabled(hasSelection && currentRow > 0);
  m_moveDownButton->setEnabled(hasSelection && currentRow < m_programManager->getRecipeCount() - 1);
  m_toggleEnableButton->setEnabled(hasSelection);  // ✅ NEW: Enable toggle button when recipe is selected

  // Load and display recipe preview
  if (hasSelection) {
    const RecipeReference* recipe = m_programManager->getRecipe(currentRow);
    if (recipe) {
      loadRecipePreview(recipe->recipeFilePath);
    }
  } else {
    m_recipePreviewText->clear();
    m_recipePreviewText->setPlaceholderText("Select a recipe to see its steps...");
  }
}

void RunProgramPage::loadRecipePreview(const std::string& recipeFilePath)
{
  RecipeManager tempRecipeManager;

  if (!tempRecipeManager.loadRecipe(recipeFilePath)) {
    m_recipePreviewText->setPlainText("Error: Failed to load recipe file");
    return;
  }

  // Build preview text
  QString previewText;
  previewText += QString("Recipe: %1\n").arg(QString::fromStdString(tempRecipeManager.getRecipeName()));
  previewText += QString("Steps: %1\n").arg(tempRecipeManager.getStepCount());
  previewText += QString("\n");
  previewText += QString("═══════════════════════════════════════\n\n");

  int stepNumber = 1;
  for (const auto& step : tempRecipeManager.getAllSteps()) {
    if (step->enabled) {
      previewText += QString("%1. %2\n")
        .arg(stepNumber, 2, 10, QChar('0'))
        .arg(QString::fromStdString(step->userNickname));
      previewText += QString("   Type: %1\n")
        .arg(QString::fromStdString(step->processType));
    } else {
      previewText += QString("%1. %2 (DISABLED)\n")
        .arg(stepNumber, 2, 10, QChar('0'))
        .arg(QString::fromStdString(step->userNickname));
    }
    previewText += QString("\n");
    stepNumber++;
  }

  m_recipePreviewText->setPlainText(previewText);
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
  // Update left panel list widget
  m_recipeListWidget->clear();

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
      recipeNumber++;
      continue;
    }

    m_recipeListWidget->addItem(displayText);
    recipeNumber++;
  }

  // Update edit widget table
  m_editRecipeTable->setRowCount(0);  // Clear table

  int rowIndex = 0;
  for (const auto& recipe : m_programManager->getAllRecipes()) {
    m_editRecipeTable->insertRow(rowIndex);

    // Column 0: Recipe Number
    QTableWidgetItem* numItem = new QTableWidgetItem(QString::number(rowIndex + 1));
    numItem->setTextAlignment(Qt::AlignCenter);
    m_editRecipeTable->setItem(rowIndex, 0, numItem);

    // Column 1: Recipe Name
    QTableWidgetItem* nameItem = new QTableWidgetItem(QString::fromStdString(recipe->displayName));
    m_editRecipeTable->setItem(rowIndex, 1, nameItem);

    // Column 2: Status
    QString status = recipe->enabled ? "Enabled" : "Disabled";
    QTableWidgetItem* statusItem = new QTableWidgetItem(status);
    statusItem->setTextAlignment(Qt::AlignCenter);
    if (!recipe->enabled) {
      statusItem->setForeground(QColor(150, 150, 150));
      numItem->setForeground(QColor(150, 150, 150));
      nameItem->setForeground(QColor(150, 150, 150));
    }
    m_editRecipeTable->setItem(rowIndex, 2, statusItem);

    rowIndex++;
  }

  updateExecuteButton();
}

void RunProgramPage::updateExecuteButton()
{
  bool hasRecipes = m_programManager->getRecipeCount() > 0;
  m_executeButton->setEnabled(hasRecipes && !m_isExecuting);
}

// ============================================================================
// SETTINGS PERSISTENCE
// ============================================================================

void RunProgramPage::saveLastProgramToSettings(const QString& programPath)
{
  QSettings settings("Fabrinet", "BobcatEngine");
  settings.setValue("RunProgramPage/lastProgram", programPath);

  std::cout << "[RunProgramPage] Saved last program to settings: "
            << programPath.toStdString() << std::endl;
}

void RunProgramPage::saveRecentPrograms(const QString& programPath)
{
  QSettings settings("Fabrinet", "BobcatEngine");

  // Get existing recent programs list
  QStringList recentPrograms = settings.value("RunProgramPage/recentPrograms").toStringList();

  // Remove path if it already exists (to avoid duplicates)
  recentPrograms.removeAll(programPath);

  // Add to front of list
  recentPrograms.prepend(programPath);

  // Keep only last 10 programs
  if (recentPrograms.size() > 10) {
    recentPrograms = recentPrograms.mid(0, 10);
  }

  // Save back to settings
  settings.setValue("RunProgramPage/recentPrograms", recentPrograms);

  std::cout << "[RunProgramPage] Updated recent programs list (size: "
            << recentPrograms.size() << ")" << std::endl;
}

void RunProgramPage::loadLastProgramFromSettings()
{
  // ✅ Check if product is loaded first
  if (!m_context->productPositionManager || !m_context->productPositionManager->HasLoadedProduct()) {
    std::cout << "[RunProgramPage] No product loaded - skipping auto-load of last program" << std::endl;
    return;
  }

  QSettings settings("Fabrinet", "BobcatEngine");
  QString lastProgramPath = settings.value("RunProgramPage/lastProgram", "").toString();

  if (lastProgramPath.isEmpty()) {
    std::cout << "[RunProgramPage] No last program found in settings" << std::endl;
    return;
  }

  // ✅ Verify the program belongs to the current product
  QString productName = QString::fromStdString(m_context->productPositionManager->GetCurrentProductName());
  QString expectedPathPrefix = QString("config/%1/programs/").arg(productName);

  if (!lastProgramPath.startsWith(expectedPathPrefix)) {
    std::cout << "[RunProgramPage] Last program is from different product - skipping auto-load" << std::endl;
    std::cout << "  Last program: " << lastProgramPath.toStdString() << std::endl;
    std::cout << "  Current product: " << productName.toStdString() << std::endl;
    return;
  }

  // Check if file still exists
  if (!QFile::exists(lastProgramPath)) {
    std::cout << "[RunProgramPage] Last program file no longer exists: "
              << lastProgramPath.toStdString() << std::endl;
    return;
  }

  std::cout << "[RunProgramPage] Auto-loading last program: "
            << lastProgramPath.toStdString() << std::endl;

  // Load the program
  if (!m_programManager->loadProgram(lastProgramPath.toStdString())) {
    std::cout << "[RunProgramPage] Failed to auto-load last program" << std::endl;
    return;
  }

  // Update UI
  m_currentProgramPath = lastProgramPath;
  updateProgramInfo();
  updateRecipeList();

  // ✅ IMPORTANT: Also update the program name edit field in the edit widget
  m_programNameEdit->setText(QString::fromStdString(m_programManager->getProgramName()));

  std::cout << "[RunProgramPage] Successfully auto-loaded: "
            << m_programManager->getProgramName() << std::endl;
}