// RunProgramPage.cpp
#include "RunProgramPage.h"
#include "ApplicationContext.h"
#include "ProgramManager.h"
#include "ProgramWorker.h"
#include "ProcessSequenceFactory.h"
#include "UserPromptWidget.h"
#include "ManualAdjustWidget.h"
#include "UserInputWidget.h"
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
#include <QScrollArea>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QTextEdit>
#include <QEventLoop>
#include <QMetaObject>
#include <QThread>
#include <QApplication>
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
  , m_manualAdjustWidget(nullptr)
  , m_userInputWidget(nullptr)
  , m_promptEventLoop(nullptr)
  , m_promptResponse(false)
  , m_manualAdjustResponse(false)
  , m_userInputAccepted(false)
  , m_userInputText("")
  , m_scanWidget(nullptr)
  , m_scanExecutor(nullptr)
  , m_dataStore(context->dataStore)
  , m_currentSubscribedChannel("")
  , m_currentRecipeTotalSteps(0)      // ✅ Initialize step tracking
  , m_currentRecipeCompletedSteps(0)  // ✅ Initialize step tracking
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
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5a8fd6, stop:1 #3d5a8c); "
    "  color: white; "
    "  font-weight: bold; "
    "  padding: 5px 15px; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #6a9fe6, stop:1 #5a8fd6); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #2d4a6c; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 7px; "
    "  padding-left: 17px; "
    "}");
  leftLayout->addWidget(m_backButton);

  // === Product Display ===
  m_currentProductLabel = new QLabel("Product: None");
  m_currentProductLabel->setStyleSheet(
    "QLabel {"
    "  color: #cccccc;"
    "  font-weight: bold;"
    "  padding: 5px;"
    "}"
  );
  m_currentProductLabel->setWordWrap(true);
  leftLayout->addWidget(m_currentProductLabel);

  // === TAB WIDGET ===
  m_tabWidget = new QTabWidget();
  m_tabWidget->setStyleSheet(
    "QTabWidget::pane { "
    "  border: 1px solid #3f3f46; "
    "  background-color: #1e1e1e; "
    "} "
    "QTabBar::tab { "
    "  background-color: #3c3c3c; "
    "  color: #d4d4d4; "
    "  padding: 8px 16px; "
    "  margin-right: 2px; "
    "  border-top-left-radius: 3px; "
    "  border-top-right-radius: 3px; "
    "} "
    "QTabBar::tab:selected { "
    "  background-color: #0e639c; "
    "  color: white; "
    "  font-weight: bold; "
    "} "
    "QTabBar::tab:hover:!selected { "
    "  background-color: #4a4a4a; "
    "}"
  );

  // === FILE TAB ===
  QWidget* fileTab = new QWidget();
  QVBoxLayout* fileTabLayout = new QVBoxLayout(fileTab);
  fileTabLayout->setContentsMargins(5, 10, 5, 5);
  fileTabLayout->setSpacing(10);

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

  // ✅ ADD: Program file path label
  m_programPathLabel = new QLabel("");
  m_programPathLabel->setWordWrap(true);
  m_programPathLabel->setStyleSheet("font-size: 9px; color: #888888; font-style: italic;");
  m_programPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);  // Allow text selection
  fileLayout->addWidget(m_programPathLabel);

  QHBoxLayout* fileButtonLayout = new QHBoxLayout();
  m_loadButton = new QPushButton("Load");
  m_loadButton->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5a5a5a, stop:1 #3c3c3c); "
    "  color: #d4d4d4; "
    "  padding: 5px 10px; "
    "  font-weight: bold; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.4); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.4); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #6a6a6a, stop:1 #4a4a4a); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #2c2c2c; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 7px; "
    "  padding-left: 12px; "
    "}"
  );
  m_saveButton = new QPushButton("Save");
  m_saveButton->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5a5a5a, stop:1 #3c3c3c); "
    "  color: #d4d4d4; "
    "  padding: 5px 10px; "
    "  font-weight: bold; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.4); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.4); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #6a6a6a, stop:1 #4a4a4a); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #2c2c2c; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 7px; "
    "  padding-left: 12px; "
    "}"
  );
  m_editButton = new QPushButton("Edit");
  m_editButton->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5a5a5a, stop:1 #3c3c3c); "
    "  color: #d4d4d4; "
    "  padding: 5px 10px; "
    "  font-weight: bold; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.4); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.4); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #6a6a6a, stop:1 #4a4a4a); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #2c2c2c; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 7px; "
    "  padding-left: 12px; "
    "}"
  );

  fileButtonLayout->addWidget(m_loadButton);
  fileButtonLayout->addWidget(m_saveButton);
  fileButtonLayout->addWidget(m_editButton);
  fileLayout->addLayout(fileButtonLayout);

  fileTabLayout->addWidget(fileGroup);
  fileTabLayout->addStretch();

  m_tabWidget->addTab(fileTab, "File");

  // === EXECUTE TAB ===
  QWidget* executeTab = new QWidget();
  QVBoxLayout* executeTabLayout = new QVBoxLayout(executeTab);
  executeTabLayout->setContentsMargins(5, 10, 5, 5);
  executeTabLayout->setSpacing(10);

  // === Execute Controls ===
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
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5cbf60, stop:1 #4CAF50); "
    "  color: white; "
    "  font-weight: bold; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #6ccf70, stop:1 #5cbf60); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #3d9041; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 2px; "
    "  padding-left: 2px; "
    "} "
    "QPushButton:disabled { "
    "  background-color: #7f8c8d; "
    "  color: #bdc3c7; "
    "  border-top: 1px solid rgba(255, 255, 255, 0.2); "
    "  border-left: 1px solid rgba(255, 255, 255, 0.2); "
    "  border-bottom: 1px solid rgba(0, 0, 0, 0.3); "
    "  border-right: 1px solid rgba(0, 0, 0, 0.3); "
    "}");
  executeLayout->addWidget(m_executeButton);

  m_autoRunNextCheckBox = new QCheckBox("Auto-run next recipe");
  m_autoRunNextCheckBox->setChecked(false);
  m_autoRunNextCheckBox->setStyleSheet(
    "QCheckBox { "
    "  spacing: 8px; "
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

  // ✅ NEW: Use dependencies checkbox
  m_useDependenciesCheckBox = new QCheckBox("Use recipe dependencies (advanced)");
  m_useDependenciesCheckBox->setChecked(false);
  m_useDependenciesCheckBox->setToolTip("Enable dependency-based execution where recipes run based on prerequisites");
  m_useDependenciesCheckBox->setStyleSheet(
    "QCheckBox { "
    "  spacing: 8px; "
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
    "  border-color: #9b59b6; "
    "} "
    "QCheckBox::indicator:checked { "
    "  background-color: #9b59b6; "
    "  border-color: #9b59b6; "
    "} "
    "QCheckBox::indicator:checked:hover { "
    "  background-color: #a06fbd; "
    "  border-color: #a06fbd; "
    "} "
    "QCheckBox::indicator:unchecked { "
    "  background-color: #2d2d30; "
    "  border-color: #555555; "
    "}"
  );
  executeLayout->addWidget(m_useDependenciesCheckBox);

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

  executeTabLayout->addWidget(executeGroup);

  // === Recipe List ===
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
  m_recipeListWidget->setAlternatingRowColors(false);  // Disabled for consistent dark theme

  m_recipeListWidget->setStyleSheet(
    "QListWidget { "
    "  background-color: #1e1e1e; "
    "  border: 1px solid #3f3f46; "
    "  border-radius: 4px; "
    "  font-size: 16px; "  // Increased from 14px to 16px for better readability
    "  padding: 2px; "
    "  color: #d4d4d4; "
    "  gridline-color: #3f3f46; "
    "} "
    "QListWidget::item { "
    "  padding: 10px 8px; "
    "  border-bottom: 1px solid #3f3f46; "
    "  background-color: #1e1e1e; "
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

  executeTabLayout->addWidget(recipeGroup, 1);

  m_tabWidget->addTab(executeTab, "Execute");

  // Set Execute tab as default
  m_tabWidget->setCurrentIndex(1);

  // Add tab widget to left layout
  leftLayout->addWidget(m_tabWidget, 1);

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
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #858585, stop:1 #757575); "
    "  color: white; "
    "  font-size: 13px; "
    "  font-weight: bold; "
    "  padding: 5px 15px; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #959595, stop:1 #858585); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #656565; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 7px; "
    "  padding-left: 17px; "
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
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #42a5f5, stop:1 #2196F3); "
    "  color: white; "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "  padding: 5px 15px; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #52b5ff, stop:1 #42a5f5); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #1976D2; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 7px; "
    "  padding-left: 17px; "
    "}");
  nameAndSaveLayout->addWidget(m_saveEditButton);

  programNameLayout->addLayout(nameAndSaveLayout);
  layout->addWidget(programNameGroup);

  // Add Recipe button
  m_addRecipeButton = new QPushButton("+ Add Recipe");
  m_addRecipeButton->setMinimumHeight(40);
  m_addRecipeButton->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #2ecc71, stop:1 #27ae60); "
    "  color: white; "
    "  font-weight: bold; "
    "  font-size: 14px; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #3edc81, stop:1 #2ecc71); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #1e8449; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 2px; "
    "  padding-left: 2px; "
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
  m_editRecipeTable->setColumnCount(4);
  m_editRecipeTable->setHorizontalHeaderLabels({"#", "Recipe Name", "Dependencies", "Status"});
  m_editRecipeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_editRecipeTable->setSelectionMode(QAbstractItemView::SingleSelection);
  m_editRecipeTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_editRecipeTable->setAlternatingRowColors(true);
  m_editRecipeTable->verticalHeader()->setVisible(false);
  m_editRecipeTable->horizontalHeader()->setStretchLastSection(false);
  m_editRecipeTable->setColumnWidth(0, 40);   // # column
  m_editRecipeTable->setColumnWidth(1, 250);  // Recipe Name
  m_editRecipeTable->setColumnWidth(2, 180);  // Dependencies
  m_editRecipeTable->setColumnWidth(3, 80);   // Status
  m_editRecipeTable->setStyleSheet(
    "QTableWidget { "
    "  background-color: #1e1e1e; "
    "  border: 1px solid #3f3f46; "
    "  border-radius: 4px; "
    "  gridline-color: #3f3f46; "
    "  font-size: 16px; "  // Increased from 13px to 16px
    "  color: #d4d4d4; "
    "  show-decoration-selected: 1; "
    "} "
    "QTableWidget::item { "
    "  border-bottom: 1px solid #3f3f46; "
    "  padding: 10px 8px; "  // Increased from 2px to 10px vertical, 8px horizontal
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
    "  padding: 8px; "  // Increased from 5px to 8px
    "  border: 1px solid #3f3f46; "
    "  font-size: 15px; "  // Added explicit font size for headers
    "  font-weight: bold; "
    "}"
  );

  // Set minimum row height for better readability
  m_editRecipeTable->verticalHeader()->setDefaultSectionSize(42);
  m_editRecipeTable->verticalHeader()->setMinimumSectionSize(38);

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
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ec7063, stop:1 #e74c3c); "
    "  color: white; "
    "  font-weight: bold; "
    "  padding: 5px 15px; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #fc8073, stop:1 #ec7063); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #c0392b; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 7px; "
    "  padding-left: 17px; "
    "} "
    "QPushButton:disabled { "
    "  background-color: #7f8c8d; "
    "  color: #bdc3c7; "
    "  border-top: 1px solid rgba(255, 255, 255, 0.2); "
    "  border-left: 1px solid rgba(255, 255, 255, 0.2); "
    "  border-bottom: 1px solid rgba(0, 0, 0, 0.3); "
    "  border-right: 1px solid rgba(0, 0, 0, 0.3); "
    "}"
  );

  m_moveUpButton = new QPushButton("↑ Move Up");
  m_moveUpButton->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #a5b5b6, stop:1 #95a5a6); "
    "  color: white; "
    "  font-weight: bold; "
    "  padding: 5px 15px; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #b5c5c6, stop:1 #a5b5b6); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #7f8c8d; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 7px; "
    "  padding-left: 17px; "
    "} "
    "QPushButton:disabled { "
    "  background-color: #7f8c8d; "
    "  color: #bdc3c7; "
    "  border-top: 1px solid rgba(255, 255, 255, 0.2); "
    "  border-left: 1px solid rgba(255, 255, 255, 0.2); "
    "  border-bottom: 1px solid rgba(0, 0, 0, 0.3); "
    "  border-right: 1px solid rgba(0, 0, 0, 0.3); "
    "}"
  );

  m_moveDownButton = new QPushButton("↓ Move Down");
  m_moveDownButton->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #a5b5b6, stop:1 #95a5a6); "
    "  color: white; "
    "  font-weight: bold; "
    "  padding: 5px 15px; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #b5c5c6, stop:1 #a5b5b6); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #7f8c8d; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 7px; "
    "  padding-left: 17px; "
    "} "
    "QPushButton:disabled { "
    "  background-color: #7f8c8d; "
    "  color: #bdc3c7; "
    "  border-top: 1px solid rgba(255, 255, 255, 0.2); "
    "  border-left: 1px solid rgba(255, 255, 255, 0.2); "
    "  border-bottom: 1px solid rgba(0, 0, 0, 0.3); "
    "  border-right: 1px solid rgba(0, 0, 0, 0.3); "
    "}"
  );

  // ✅ NEW: Toggle Enable/Disable button
  m_toggleEnableButton = new QPushButton("⇄ Toggle Enable/Disable");
  m_toggleEnableButton->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #f1c40f, stop:1 #f39c12); "
    "  color: white; "
    "  font-weight: bold; "
    "  padding: 5px 15px; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ffd41f, stop:1 #f1c40f); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #e67e22; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 7px; "
    "  padding-left: 17px; "
    "} "
    "QPushButton:disabled { "
    "  background-color: #7f8c8d; "
    "  color: #bdc3c7; "
    "  border-top: 1px solid rgba(255, 255, 255, 0.2); "
    "  border-left: 1px solid rgba(255, 255, 255, 0.2); "
    "  border-bottom: 1px solid rgba(0, 0, 0, 0.3); "
    "  border-right: 1px solid rgba(0, 0, 0, 0.3); "
    "}"
  );
  m_toggleEnableButton->setEnabled(false);

  // ✅ NEW: Manage Dependencies button
  QPushButton* manageDepsButton = new QPushButton("🔗 Manage Dependencies");
  manageDepsButton->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #8e44ad, stop:1 #9b59b6); "
    "  color: white; "
    "  font-weight: bold; "
    "  padding: 5px 15px; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #9b59b6, stop:1 #a06fbd); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #7d3c98; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 7px; "
    "  padding-left: 17px; "
    "}"
  );
  connect(manageDepsButton, &QPushButton::clicked, this, &RunProgramPage::onManageDependencies);

  controlLayout->addWidget(m_removeRecipeButton);
  controlLayout->addWidget(m_moveUpButton);
  controlLayout->addWidget(m_moveDownButton);
  controlLayout->addSpacing(10);
  controlLayout->addWidget(m_toggleEnableButton);
  controlLayout->addWidget(manageDepsButton);
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

  // === USER INPUT VIEW (Index 3) ===
  m_userInputWidget = new UserInputWidget();
  m_userInputWidget->setStyleSheet(
    "UserInputWidget { "
    "  background-color: #1e1e1e; "
    "}"
  );

  // Connect user input response
  connect(m_userInputWidget, &UserInputWidget::responseReceived,
    this, &RunProgramPage::onUserInputResponse);

  // Add all views to stack
  m_executionStack->addWidget(logWidget);              // Index 0: Log
  m_executionStack->addWidget(m_promptWidget);         // Index 1: Prompt
  m_executionStack->addWidget(m_manualAdjustWidget);  // Index 2: Manual Adjust
  m_executionStack->addWidget(m_userInputWidget);     // Index 3: User Input
  m_executionStack->setCurrentIndex(0);                // Start with log

  layout->addWidget(m_executionStack, 1);

  // === SCANNING CONTROLS (Refactored) ===
  m_scanWidget = new ScanControlWidget();

  // Populate channels from DataStore (initial load)
  refreshDataStoreChannels();

  // ✅ ADDED: Set up periodic channel refresh (every 3 seconds)
  // This ensures new channels (e.g., SMU channels added when polling starts) appear in the dropdown
  m_channelRefreshTimer = new QTimer(this);
  connect(m_channelRefreshTimer, &QTimer::timeout, this, &RunProgramPage::refreshDataStoreChannels);
  m_channelRefreshTimer->start(3000);  // Refresh every 3 seconds

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

  // ✅ Reset completion tracking when loading new program
  m_completedRecipes.clear();
  m_completedRecipes.resize(m_programManager->getRecipeCount(), false);

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

  // ✅ Initialize completion tracking if needed
  if (m_completedRecipes.empty() || m_completedRecipes.size() != static_cast<size_t>(m_programManager->getRecipeCount())) {
    m_completedRecipes.clear();
    m_completedRecipes.resize(m_programManager->getRecipeCount(), false);
  }

  // ✅ Reset completion tracking in auto-run mode (fresh start for entire program)
  if (autoRunMode) {
    std::fill(m_completedRecipes.begin(), m_completedRecipes.end(), false);
  }

  // ✅ Check prerequisites in single-recipe mode with dependencies enabled
  if (!autoRunMode && m_useDependenciesCheckBox->isChecked()) {
    std::vector<int> dependencies = m_programManager->getRecipeDependencies(selectedRecipeIndex);

    if (!dependencies.empty()) {
      QStringList missingPrereqs;
      for (int depIndex : dependencies) {
        if (depIndex >= 0 && depIndex < static_cast<int>(m_completedRecipes.size())) {
          if (!m_completedRecipes[depIndex]) {
            const RecipeReference* depRecipe = m_programManager->getRecipe(depIndex);
            if (depRecipe) {
              missingPrereqs << QString("• Recipe %1: %2")
                .arg(depIndex + 1)
                .arg(QString::fromStdString(depRecipe->displayName));
            }
          }
        }
      }

      if (!missingPrereqs.isEmpty()) {
        const RecipeReference* recipe = m_programManager->getRecipe(selectedRecipeIndex);
        QString recipeName = recipe ? QString::fromStdString(recipe->displayName) : QString("Recipe %1").arg(selectedRecipeIndex + 1);

        QMessageBox::warning(this, "Prerequisites Not Met",
          QString("Cannot run '%1'.\n\nThe following prerequisites have not been completed:\n%2\n\nPlease run the prerequisite recipe(s) first.")
          .arg(recipeName)
          .arg(missingPrereqs.join("\n")));
        return;
      }
    }
  }

  // Confirm execution - DISABLED for production (direct execution)
  // QString programName = QString::fromStdString(m_programManager->getProgramName());
  // QString confirmMsg;

  // if (autoRunMode) {
  //   int enabledCount = 0;
  //   for (const auto& recipe : m_programManager->getAllRecipes()) {
  //     if (recipe->enabled) enabledCount++;
  //   }
  //   confirmMsg = QString("Execute all %1 enabled recipe(s) in program '%2'?")
  //     .arg(enabledCount)
  //     .arg(programName);
  // }
  // else {
    const RecipeReference* recipe = m_programManager->getRecipe(selectedRecipeIndex);
    if (selectedRecipeIndex >= 0 && recipe) {
      if (!recipe->enabled) {
        QMessageBox::warning(this, "Recipe Disabled",
          QString("Recipe '%1' is disabled. Please enable it first or select a different recipe.")
          .arg(QString::fromStdString(recipe->displayName)));
        return;
      }
    }

  //   confirmMsg = QString("Execute recipe '%1' only?\n\n"
  //     "Tip: Check 'Auto-run next recipe' to run all recipes.")
  //     .arg(QString::fromStdString(recipe->displayName));
  // }

  // QMessageBox::StandardButton reply = QMessageBox::question(
  //   this, "Execute Program",
  //   confirmMsg,
  //   QMessageBox::Yes | QMessageBox::No
  // );

  // if (reply != QMessageBox::Yes) return;

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

  // ✅ Set dependency execution mode
  m_programWorker->setUseDependencyGraph(m_useDependenciesCheckBox->isChecked());

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

  // Set UserInput callback - uses integrated UserInputWidget
  m_programWorker->setUserInputCallback(
    [this](const std::string& title, const std::string& prompt, const std::string& defaultValue) -> std::pair<bool, std::string> {
    // Show the user input widget (must be called on UI thread)
    QMetaObject::invokeMethod(this, [this, title, prompt, defaultValue]() {
      // Switch to user input view (Index 3)
      m_executionStack->setCurrentIndex(3);

      // Show the user input widget
      m_userInputWidget->showInput(
        QString::fromStdString(title),
        QString::fromStdString(prompt),
        QString::fromStdString(defaultValue)
      );
    }, Qt::QueuedConnection);

    // Block until user responds (event loop pattern)
    QEventLoop loop;
    m_promptEventLoop = &loop;
    loop.exec();
    m_promptEventLoop = nullptr;

    // Return the stored result
    return std::make_pair(m_userInputAccepted, m_userInputText.toStdString());
  });

  // Connect signals
  connect(m_programWorker, &ProgramWorker::programExecutionStarted,
    this, &RunProgramPage::onProgramExecutionStarted);
  connect(m_programWorker, &ProgramWorker::recipeStarted,
    this, &RunProgramPage::onRecipeStarted);
  connect(m_programWorker, &ProgramWorker::recipeCompleted,
    this, &RunProgramPage::onRecipeCompleted);
  connect(m_programWorker, &ProgramWorker::recipeSkipped,
    this, &RunProgramPage::onRecipeSkipped);
  connect(m_programWorker, &ProgramWorker::programExecutionCompleted,
    this, &RunProgramPage::onProgramExecutionCompleted);
  connect(m_programWorker, &ProgramWorker::programStopped,
    this, &RunProgramPage::onProgramStopped);
  connect(m_programWorker, &ProgramWorker::logMessage,
    this, &RunProgramPage::onWorkerLogMessage);
  connect(m_programWorker, &ProgramWorker::finished,
    m_programWorker, &QObject::deleteLater);

  // ✅ Connect step-level signals for progress tracking
  connect(m_programWorker, &ProgramWorker::stepBuildStarted,
    this, &RunProgramPage::onStepBuildStarted);
  connect(m_programWorker, &ProgramWorker::stepBuildCompleted,
    this, &RunProgramPage::onStepBuildCompleted);
  connect(m_programWorker, &ProgramWorker::sequenceExecutionStarted,
    this, &RunProgramPage::onSequenceExecutionStarted);
  connect(m_programWorker, &ProgramWorker::waitProgress,
    this, &RunProgramPage::onWaitProgress);

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

  // ✅ Set progress bar to percentage mode (0-100) for step-level tracking
  m_progressBar->setRange(0, 100);
  m_progressBar->setValue(0);

  m_executionStatusLabel->setText(QString("Executing program with %1 recipes").arg(totalRecipes));
}

void RunProgramPage::onRecipeStarted(int recipeIndex, QString recipeName)
{
  m_currentRecipeIndex = recipeIndex;
  m_currentRecipeLabel->setText(QString("Current Recipe: %1").arg(recipeName));
  m_executionStatusLabel->setText(QString("Recipe %1 of %2: %3")
    .arg(recipeIndex + 1).arg(m_totalRecipes).arg(recipeName));

  // ✅ Reset step-level tracking for new recipe
  m_currentRecipeTotalSteps = 0;
  m_currentRecipeCompletedSteps = 0;
  m_progressBar->setValue(0);

  // Update list item to show running state
  QListWidgetItem* item = m_recipeListWidget->item(recipeIndex);
  if (item) {
    QString baseText = QString("%1    %2")
      .arg(recipeIndex + 1, 2, 10, QChar('0'))
      .arg(recipeName);

    // Show dependencies if using dependency mode
    if (m_useDependenciesCheckBox->isChecked()) {
      std::vector<int> deps = m_programManager->getRecipeDependencies(recipeIndex);
      if (!deps.empty()) {
        QString depsStr = " [Deps: ";
        for (size_t i = 0; i < deps.size(); ++i) {
          if (i > 0) depsStr += ", ";
          depsStr += QString::number(deps[i] + 1);
        }
        depsStr += "]";
        baseText += depsStr;
      }
    }

    item->setText("▶ " + baseText + "  [RUNNING]");
    item->setForeground(QColor(52, 152, 219));  // Blue for running
  }
}

void RunProgramPage::onRecipeCompleted(int recipeIndex, bool success, QString message)
{
  // ✅ Set progress to 100% when recipe completes
  m_progressBar->setValue(100);

  // Update list item to show completed/failed state
  QListWidgetItem* item = m_recipeListWidget->item(recipeIndex);
  if (item) {
    const RecipeReference* recipe = m_programManager->getRecipe(recipeIndex);
    if (recipe) {
      QString baseText = QString("%1    %2")
        .arg(recipeIndex + 1, 2, 10, QChar('0'))
        .arg(QString::fromStdString(recipe->displayName));

      // Show dependencies if using dependency mode
      if (m_useDependenciesCheckBox->isChecked()) {
        std::vector<int> deps = m_programManager->getRecipeDependencies(recipeIndex);
        if (!deps.empty()) {
          QString depsStr = " [Deps: ";
          for (size_t i = 0; i < deps.size(); ++i) {
            if (i > 0) depsStr += ", ";
            depsStr += QString::number(deps[i] + 1);
          }
          depsStr += "]";
          baseText += depsStr;
        }
      }

      if (success) {
        item->setText("✓ " + baseText + "  [COMPLETED]");
        item->setForeground(QColor(46, 204, 113));  // Green for success
      } else {
        item->setText("✗ " + baseText + "  [FAILED]");
        item->setForeground(QColor(231, 76, 60));  // Red for failure
      }
    }
  }

  // ✅ Mark recipe as completed in tracking vector (for prerequisite validation)
  if (success && recipeIndex >= 0 && recipeIndex < static_cast<int>(m_completedRecipes.size())) {
    m_completedRecipes[recipeIndex] = true;
  }

  if (!success) {
    m_executionStatusLabel->setText(QString("Recipe failed: %1").arg(message));
  }
}

void RunProgramPage::onRecipeSkipped(int recipeIndex, QString recipeName)
{
  // Update list item to show skipped state
  QListWidgetItem* item = m_recipeListWidget->item(recipeIndex);
  if (item) {
    QString baseText = QString("%1    %2")
      .arg(recipeIndex + 1, 2, 10, QChar('0'))
      .arg(recipeName);

    // Show dependencies if using dependency mode
    if (m_useDependenciesCheckBox->isChecked()) {
      std::vector<int> deps = m_programManager->getRecipeDependencies(recipeIndex);
      if (!deps.empty()) {
        QString depsStr = " [Deps: ";
        for (size_t i = 0; i < deps.size(); ++i) {
          if (i > 0) depsStr += ", ";
          depsStr += QString::number(deps[i] + 1);
        }
        depsStr += "]";
        baseText += depsStr;
      }
    }

    item->setText("⊘ " + baseText + "  [SKIPPED]");
    item->setForeground(QColor(149, 165, 166));  // Gray for skipped
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

  // No confirmation dialog needed - stop button itself is the confirmation
}

void RunProgramPage::onWorkerLogMessage(QString message)
{
  // Display in execution log
  m_logTextEdit->append(message);

  // Also print to console for debugging
  std::cout << "[RunProgramPage] " << message.toStdString() << std::endl;
}

// ============================================================================
// STEP-LEVEL PROGRESS TRACKING
// ============================================================================

void RunProgramPage::onStepBuildStarted(int recipeIndex, int stepIndex, QString stepName, QString stepType)
{
  // Update total steps if this is a higher step index than we've seen
  if (stepIndex + 1 > m_currentRecipeTotalSteps) {
    m_currentRecipeTotalSteps = stepIndex + 1;
  }

  // Update progress bar based on step completion
  // Progress = (completed steps / total steps) * 100
  if (m_currentRecipeTotalSteps > 0) {
    int progress = (m_currentRecipeCompletedSteps * 100) / m_currentRecipeTotalSteps;
    m_progressBar->setValue(progress);
  }
}

void RunProgramPage::onStepBuildCompleted(int recipeIndex, int stepIndex)
{
  // Increment completed steps
  m_currentRecipeCompletedSteps++;

  // Update progress bar
  if (m_currentRecipeTotalSteps > 0) {
    int progress = (m_currentRecipeCompletedSteps * 100) / m_currentRecipeTotalSteps;
    m_progressBar->setValue(progress);
  }
}

void RunProgramPage::onSequenceExecutionStarted(int recipeIndex)
{
  // All steps have been built, now executing
  // Set progress to 100% (or keep at current build progress)
  if (m_currentRecipeTotalSteps > 0) {
    int progress = (m_currentRecipeCompletedSteps * 100) / m_currentRecipeTotalSteps;
    m_progressBar->setValue(progress);
  }
}

void RunProgramPage::onWaitProgress(int remainingMs, int totalMs)
{
  if (!m_executionLogLabel) return;

  if (remainingMs <= 0) {
    // Wait complete - clear countdown
    m_executionLogLabel->setText("Execution Log");
    return;
  }

  // Convert milliseconds to readable format
  int totalSeconds = remainingMs / 1000;
  int minutes = totalSeconds / 60;
  int seconds = totalSeconds % 60;

  // Calculate progress percentage
  int progressPercent = 0;
  if (totalMs > 0) {
    progressPercent = ((totalMs - remainingMs) * 100) / totalMs;
  }

  // Update label with countdown
  QString countdownText = QString("⏳ Waiting: %1:%2 remaining (%3%%)")
    .arg(minutes, 2, 10, QChar('0'))
    .arg(seconds, 2, 10, QChar('0'))
    .arg(progressPercent);

  m_executionLogLabel->setText(countdownText);
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

void RunProgramPage::onUserInputResponse(bool accepted, QString inputText)
{
  // Store the response
  m_userInputAccepted = accepted;
  m_userInputText = inputText;

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
// ✅ ADDED: Refresh DataStore channels in the dropdown
void RunProgramPage::refreshDataStoreChannels()
{
  if (!m_context || !m_context->dataStore || !m_scanWidget) return;

  // Get current selection to preserve it
  QString currentSelection = m_scanWidget->currentChannel();

  // Get all available channels from DataStore
  QStringList channels;
  auto channelNames = m_context->dataStore->GetAllChannelNames();
  for (const auto& channel : channelNames) {
    channels << QString::fromStdString(channel);
  }

  // Only update if the channel list has changed
  static QStringList s_lastChannelList;
  if (channels != s_lastChannelList) {
    m_scanWidget->setAvailableChannels(channels);
    s_lastChannelList = channels;

    // ✅ Restore previous selection if it still exists
    if (!currentSelection.isEmpty() && channels.contains(currentSelection)) {
      m_scanWidget->setCurrentChannel(currentSelection);
    }

    std::cout << "[RunProgramPage] Channel list updated: " << channels.size()
              << " channels available" << std::endl;
  }
}

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

void RunProgramPage::onManageDependencies()
{
  int recipeCount = m_programManager->getRecipeCount();
  if (recipeCount == 0) {
    QMessageBox::information(this, "No Recipes", "Please add recipes first before managing dependencies");
    return;
  }

  // Create dialog
  QDialog* dialog = new QDialog(this);
  dialog->setWindowTitle("Manage Recipe Dependencies");
  dialog->setMinimumSize(800, 600);
  dialog->setStyleSheet("QDialog { background-color: #2b2b2b; }");

  QVBoxLayout* mainLayout = new QVBoxLayout(dialog);

  // Title
  QLabel* titleLabel = new QLabel("Recipe Dependency Management");
  titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: white; padding: 10px;");
  mainLayout->addWidget(titleLabel);

  // Info label
  QLabel* infoLabel = new QLabel(
    "Define which recipes must complete before others can start.\n"
    "Select a recipe below, then check the recipes it depends on.");
  infoLabel->setStyleSheet("color: #cccccc; padding: 5px;");
  infoLabel->setWordWrap(true);
  mainLayout->addWidget(infoLabel);

  // Recipe selector
  QGroupBox* recipeGroup = new QGroupBox("Select Recipe to Configure:");
  recipeGroup->setStyleSheet(
    "QGroupBox { "
    "  color: white; "
    "  font-weight: bold; "
    "  background-color: #1e1e1e; "
    "  border: 1px solid #3c3c3c; "
    "  border-radius: 4px; "
    "  margin-top: 10px; "
    "  padding-top: 15px; "
    "} "
    "QGroupBox::title { "
    "  subcontrol-origin: margin; "
    "  left: 10px; "
    "  padding: 0 5px; "
    "}");
  QVBoxLayout* recipeLayout = new QVBoxLayout(recipeGroup);

  QComboBox* recipeCombo = new QComboBox();
  recipeCombo->setStyleSheet(
    "QComboBox { "
    "  background-color: #2d2d30; "
    "  color: white; "
    "  border: 1px solid #3c3c3c; "
    "  border-radius: 4px; "
    "  padding: 5px; "
    "  font-size: 14px; "
    "} "
    "QComboBox:hover { border: 1px solid #1177bb; } "
    "QComboBox::drop-down { "
    "  border-left: 1px solid #3c3c3c; "
    "  background: #3c3c3c; "
    "} "
    "QComboBox QAbstractItemView { "
    "  background-color: #2d2d30; "
    "  color: white; "
    "  selection-background-color: #1177bb; "
    "}");

  for (int i = 0; i < recipeCount; ++i) {
    const RecipeReference* recipe = m_programManager->getRecipe(i);
    if (recipe) {
      recipeCombo->addItem(QString("Recipe %1: %2").arg(i + 1).arg(QString::fromStdString(recipe->displayName)));
    }
  }
  recipeLayout->addWidget(recipeCombo);
  mainLayout->addWidget(recipeGroup);

  // Dependencies group
  QGroupBox* depsGroup = new QGroupBox("Dependencies (Prerequisites):");
  depsGroup->setStyleSheet(
    "QGroupBox { "
    "  color: white; "
    "  font-weight: bold; "
    "  background-color: #1e1e1e; "
    "  border: 1px solid #3c3c3c; "
    "  border-radius: 4px; "
    "  margin-top: 10px; "
    "  padding-top: 15px; "
    "} "
    "QGroupBox::title { "
    "  subcontrol-origin: margin; "
    "  left: 10px; "
    "  padding: 0 5px; "
    "}");
  QVBoxLayout* depsLayout = new QVBoxLayout(depsGroup);

  // Scrollable area for checkboxes
  QScrollArea* scrollArea = new QScrollArea();
  scrollArea->setWidgetResizable(true);
  scrollArea->setStyleSheet("QScrollArea { background-color: #1e1e1e; border: none; }");

  QWidget* checkboxContainer = new QWidget();
  checkboxContainer->setStyleSheet("QWidget { background-color: #1e1e1e; }");
  QVBoxLayout* checkboxLayout = new QVBoxLayout(checkboxContainer);

  // Create checkboxes for each recipe
  std::vector<QCheckBox*> checkboxes;
  for (int i = 0; i < recipeCount; ++i) {
    const RecipeReference* recipe = m_programManager->getRecipe(i);
    if (recipe) {
      QCheckBox* cb = new QCheckBox(QString("Recipe %1: %2").arg(i + 1).arg(QString::fromStdString(recipe->displayName)));
      cb->setProperty("recipeIndex", i);
      cb->setStyleSheet(
        "QCheckBox { "
        "  color: white; "
        "  padding: 5px; "
        "} "
        "QCheckBox::indicator { "
        "  width: 18px; "
        "  height: 18px; "
        "  border: 2px solid #3c3c3c; "
        "  border-radius: 3px; "
        "  background-color: #2d2d30; "
        "} "
        "QCheckBox::indicator:checked { "
        "  background-color: #27ae60; "
        "  border-color: #27ae60; "
        "}");
      checkboxes.push_back(cb);
      checkboxLayout->addWidget(cb);
    }
  }
  checkboxLayout->addStretch();

  scrollArea->setWidget(checkboxContainer);
  depsLayout->addWidget(scrollArea);
  mainLayout->addWidget(depsGroup);

  // Temporary storage for all recipe dependencies as we edit
  std::vector<std::vector<int>> tempDependencies(recipeCount);

  // Initialize with current dependencies from ProgramManager
  for (int i = 0; i < recipeCount; ++i) {
    tempDependencies[i] = m_programManager->getRecipeDependencies(i);
  }

  // Function to save current recipe's checkboxes to temp storage
  auto saveCurrentToTemp = [recipeCombo, &checkboxes, &tempDependencies]() {
    int currentRecipe = recipeCombo->currentIndex();
    tempDependencies[currentRecipe].clear();

    for (QCheckBox* cb : checkboxes) {
      if (cb->isChecked() && cb->isEnabled()) {
        int depIndex = cb->property("recipeIndex").toInt();
        tempDependencies[currentRecipe].push_back(depIndex);
      }
    }
  };

  // Function to update checkboxes based on selected recipe
  auto updateCheckboxes = [this, recipeCombo, &checkboxes, &tempDependencies, saveCurrentToTemp]() {
    // First, save the current recipe's state before switching
    static int previousRecipe = -1;
    if (previousRecipe >= 0) {
      saveCurrentToTemp();
    }
    previousRecipe = recipeCombo->currentIndex();
    int currentRecipe = recipeCombo->currentIndex();

    // Get dependencies from temp storage
    const std::vector<int>& deps = tempDependencies[currentRecipe];

    // Update checkboxes
    for (size_t i = 0; i < checkboxes.size(); ++i) {
      int cbRecipeIndex = checkboxes[i]->property("recipeIndex").toInt();

      // Disable self-dependency
      if (cbRecipeIndex == currentRecipe) {
        checkboxes[i]->setEnabled(false);
        checkboxes[i]->setChecked(false);
        checkboxes[i]->setText(checkboxes[i]->text().split(":")[0] + ": (Current Recipe)");
      }
      else {
        checkboxes[i]->setEnabled(true);
        // Restore original text
        const RecipeReference* recipe = m_programManager->getRecipe(cbRecipeIndex);
        if (recipe) {
          checkboxes[i]->setText(QString("Recipe %1: %2").arg(cbRecipeIndex + 1)
            .arg(QString::fromStdString(recipe->displayName)));
        }

        // Check if this is a dependency
        bool isDep = std::find(deps.begin(), deps.end(), cbRecipeIndex) != deps.end();
        checkboxes[i]->setChecked(isDep);
      }
    }
  };

  // Connect recipe selector change
  connect(recipeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), updateCheckboxes);

  // Initialize
  updateCheckboxes();

  // Buttons
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->addStretch();

  QPushButton* cancelButton = new QPushButton("Cancel");
  cancelButton->setMinimumWidth(100);
  cancelButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #3c3c3c; "
    "  color: white; "
    "  padding: 8px; "
    "  border-radius: 4px; "
    "} "
    "QPushButton:hover { background-color: #4c4c4c; }");
  connect(cancelButton, &QPushButton::clicked, dialog, &QDialog::reject);
  buttonLayout->addWidget(cancelButton);

  QPushButton* saveButton = new QPushButton("Save");
  saveButton->setMinimumWidth(100);
  saveButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #27ae60; "
    "  color: white; "
    "  font-weight: bold; "
    "  padding: 8px; "
    "  border-radius: 4px; "
    "} "
    "QPushButton:hover { background-color: #2ecc71; }");
  connect(saveButton, &QPushButton::clicked, [this, recipeCombo, &checkboxes, &tempDependencies, saveCurrentToTemp, dialog]() {
    // Save current recipe's state to temp storage before saving
    saveCurrentToTemp();

    // Clear all existing dependencies
    for (int recipeIdx = 0; recipeIdx < m_programManager->getRecipeCount(); ++recipeIdx) {
      m_programManager->clearRecipeDependencies(recipeIdx);
    }

    // Apply all dependencies from temp storage
    for (int recipeIdx = 0; recipeIdx < static_cast<int>(tempDependencies.size()); ++recipeIdx) {
      for (int depIndex : tempDependencies[recipeIdx]) {
        m_programManager->addRecipeDependency(recipeIdx, depIndex);
      }
    }

    // Validate dependencies
    std::vector<std::string> errors;
    if (!m_programManager->validateDependencies(errors)) {
      QString errorMsg = "Dependency validation failed:\n\n";
      for (const auto& err : errors) {
        errorMsg += "• " + QString::fromStdString(err) + "\n";
      }
      QMessageBox::critical(dialog, "Validation Error", errorMsg);
      return;
    }

    QMessageBox::information(dialog, "Saved", "Dependencies have been saved.\n\nNote: Dependencies are saved with the program file.");
    dialog->accept();
  });
  buttonLayout->addWidget(saveButton);

  mainLayout->addLayout(buttonLayout);

  int result = dialog->exec();
  delete dialog;

  // Refresh the recipe table to show updated dependencies
  if (result == QDialog::Accepted) {
    updateRecipeList();
  }
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

  // ✅ UPDATE: Display program file path
  if (!m_currentProgramPath.isEmpty()) {
    m_programPathLabel->setText("Path: " + m_currentProgramPath);
  } else {
    m_programPathLabel->setText("");
  }
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

    // Column 2: Dependencies
    std::vector<int> deps = m_programManager->getRecipeDependencies(rowIndex);
    QString depsText;
    if (deps.empty()) {
      depsText = "—";  // No dependencies
    } else {
      for (size_t i = 0; i < deps.size(); ++i) {
        if (i > 0) depsText += ", ";
        depsText += QString::number(deps[i] + 1);  // Show 1-based index
      }
    }
    QTableWidgetItem* depsItem = new QTableWidgetItem(depsText);
    depsItem->setTextAlignment(Qt::AlignCenter);
    depsItem->setForeground(QColor(155, 135, 245));  // Purple color for dependencies
    m_editRecipeTable->setItem(rowIndex, 2, depsItem);

    // Column 3: Status
    QString status = recipe->enabled ? "Enabled" : "Disabled";
    QTableWidgetItem* statusItem = new QTableWidgetItem(status);
    statusItem->setTextAlignment(Qt::AlignCenter);
    if (!recipe->enabled) {
      statusItem->setForeground(QColor(150, 150, 150));
      numItem->setForeground(QColor(150, 150, 150));
      nameItem->setForeground(QColor(150, 150, 150));
      depsItem->setForeground(QColor(150, 150, 150));
    }
    m_editRecipeTable->setItem(rowIndex, 3, statusItem);

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
  std::cout << "[RunProgramPage] ========== AUTO-LOAD LAST PROGRAM ==========" << std::endl;

  // ✅ Check if product is loaded first
  if (!m_context->productPositionManager || !m_context->productPositionManager->HasLoadedProduct()) {
    std::cout << "[RunProgramPage] ❌ No product loaded - skipping auto-load of last program" << std::endl;
    std::cout << "[RunProgramPage] ===============================================" << std::endl;
    return;
  }

  QString productName = QString::fromStdString(m_context->productPositionManager->GetCurrentProductName());
  std::cout << "[RunProgramPage] Current product: " << productName.toStdString() << std::endl;

  QSettings settings("Fabrinet", "BobcatEngine");
  QString lastProgramPath = settings.value("RunProgramPage/lastProgram", "").toString();

  std::cout << "[RunProgramPage] Saved program path: "
            << (lastProgramPath.isEmpty() ? "(none)" : lastProgramPath.toStdString()) << std::endl;

  if (lastProgramPath.isEmpty()) {
    std::cout << "[RunProgramPage] ℹ️  No last program found in settings - load a program once to enable auto-load" << std::endl;
    std::cout << "[RunProgramPage] ===============================================" << std::endl;
    return;
  }

  // ✅ Verify the program belongs to the current product
  // Support both relative paths (config/PRODUCT/programs/) and absolute paths (.../config/PRODUCT/programs/)
  QString expectedPathPattern = QString("config/%1/programs/").arg(productName);

  if (!lastProgramPath.contains(expectedPathPattern)) {
    std::cout << "[RunProgramPage] ⚠️  Last program is from different product - skipping auto-load" << std::endl;
    std::cout << "[RunProgramPage]   Expected pattern: " << expectedPathPattern.toStdString() << std::endl;
    std::cout << "[RunProgramPage]   Actual path: " << lastProgramPath.toStdString() << std::endl;
    std::cout << "[RunProgramPage] ===============================================" << std::endl;
    return;
  }

  // Check if file still exists
  if (!QFile::exists(lastProgramPath)) {
    std::cout << "[RunProgramPage] ❌ Last program file no longer exists: "
              << lastProgramPath.toStdString() << std::endl;
    std::cout << "[RunProgramPage] ===============================================" << std::endl;
    return;
  }

  std::cout << "[RunProgramPage] ✅ Auto-loading last program: "
            << lastProgramPath.toStdString() << std::endl;

  // Load the program
  if (!m_programManager->loadProgram(lastProgramPath.toStdString())) {
    std::cout << "[RunProgramPage] ❌ Failed to auto-load last program" << std::endl;
    std::cout << "[RunProgramPage] ===============================================" << std::endl;
    return;
  }

  // Update UI
  m_currentProgramPath = lastProgramPath;
  updateProgramInfo();
  updateRecipeList();

  // ✅ IMPORTANT: Also update the program name edit field in the edit widget
  m_programNameEdit->setText(QString::fromStdString(m_programManager->getProgramName()));

  std::cout << "[RunProgramPage] ✅ Successfully auto-loaded: "
            << m_programManager->getProgramName() << std::endl;
  std::cout << "[RunProgramPage] ===============================================" << std::endl;
}