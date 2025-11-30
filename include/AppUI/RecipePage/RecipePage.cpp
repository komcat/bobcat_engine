// RecipePage.cpp
#include "RecipePage.h"
#include "RecipeManager.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"  // ✅ For EmergencyStopAll
#include "SequenceEngine.h"
#include "SequenceBuilder.h"  // Full definition needed for delete
#include "SequenceContext.h"
#include "GripperSequenceHelpers.h"
#include "ProcessSequenceFactory.h"
#include "ProcessConfigWidget.h"
#include "ProcessPluginRegistry.h"  // ✅ Plugin system
#include "product_position_manager.h"
#include "RecipeWorker.h"
#include "RecipeProgressWidget.h"  // ✅ NEW
#include "ManualAdjustWidget.h"    // ✅ Manual adjust widget
#include "../RunProgramPage/ManualAlignPointWidget.h"  // ✅ Manual align point widget
#include "../RunProgramPage/UserInputWidget.h"  // ✅ User input widget
#include "SequencePromptDialog.h"
#include "LogManager.h"
#include "FileSelectorDialog.h"  // ✅ Custom file selector
#include "RecipeSaveAsDialog.h"  // ✅ Custom save dialog
#include "ContextViewerWindow.h"  // ✅ Context viewer
#include <QSplitter>
#include <QLabel>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QFrame>
#include <QMetaObject>
#include <QThread>
#include <QTimer>  // ✅ NEW
#include <QEventLoop>  // ✅ For blocking callbacks
#include <QApplication>
#include <QHeaderView>
#include <QCheckBox>
#include <QColorDialog>
#include <QMenu>
#include <QListWidget>
#include <QSet>
#include <QSettings>
#include <QFile>
#include <QFileInfo>  // ✅ For extracting file name from path
#include <iostream>
#include <chrono>

#ifdef HAS_OPENCV
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <QImage>
#include <QPixmap>
#endif

// Helper function to get emoji symbol for process category
static QString getCategorySymbol(const std::string& category)
{
  // Using simple ASCII/Unicode symbols that render reliably on Windows
  if (category == "Motion Control" || category == "Motion") {
    return QString::fromUtf8("\xE2\x9E\xA4");  // ➤ Arrow (motion/movement)
  }
  if (category == "I/O & Pneumatics") {
    return QString::fromUtf8("\xE2\x9A\xA1");  // ⚡ Lightning bolt (I/O)
  }
  if (category == "TEC & Laser") {
    return QString::fromUtf8("\xE2\x98\x80");  // ☀ Sun (thermal/laser)
  }
  if (category == "Power Supply") {
    return QString::fromUtf8("\xE2\x8F\xBB");  // ⏻ Power symbol
  }
  if (category == "Data") {
    return QString::fromUtf8("\xE2\x96\xA3");  // ▣ Data/storage
  }
  if (category == "System") {
    return QString::fromUtf8("\xE2\x9C\x8B");  // ✋ Hand (user interaction)
  }
  if (category == "Control Flow") {
    return QString::fromUtf8("\xE2\x86\xB9");  // ↹ Control flow
  }
  if (category == "Camera" || category == "Vision") {
    return QString::fromUtf8("\xE2\x97\x89");  // ◉ Eye/camera
  }
  if (category == "Dispensing") {
    return QString::fromUtf8("\xE2\x96\xBC");  // ▼ Dispense
  }
  if (category == "Aux Window") {
    return QString::fromUtf8("\xE2\x96\xA1");  // □ Window
  }
  if (category == "Comment") {
    return QString::fromUtf8("#");  // # Comment marker
  }
  // Default
  return QString::fromUtf8("\xE2\x97\x8F");  // ● Circle (generic)
}

RecipePage::RecipePage(ApplicationContext* context, TransformationMatrixLoader* matrixLoader, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_matrixLoader(matrixLoader)
  , m_recipeManager(new RecipeManager())
  , m_sequenceEngine(nullptr)
  , m_processFactory(new ProcessSequenceFactory())
  , m_selectedStepIndex(-1)
  , m_currentConfigWidget(nullptr)
  , m_recipeWorker(nullptr)
  , m_loadedProductLabel(nullptr)
  , m_progressWidget(nullptr)  // ✅ NEW
  , m_manualAdjustWidget(nullptr)  // ✅ Manual adjust widget
  , m_manualAlignPointWidget(nullptr)  // ✅ Manual align point widget
  , m_userInputWidget(nullptr)     // ✅ User input widget
  , m_promptEventLoop(nullptr)  // ✅ Event loop for blocking
  , m_manualAdjustResponse(false)  // ✅ Initialize response flag
  , m_manualAlignPointResponse(false)  // ✅ Initialize align point response flag
  , m_userInputAccepted(false)     // ✅ User input accepted
  , m_userInputText("")            // ✅ User input text
  , m_contextViewer(nullptr)       // ✅ Context viewer window
  , m_currentContext(nullptr)      // ✅ Current execution context
  , m_contextViewerButton(nullptr) // ✅ Context viewer button
  , m_visionResultGroup(nullptr)   // ✅ Vision result group
  , m_visionResultImageLabel(nullptr) // ✅ Vision result image
  , m_copyButton(nullptr)          // ✅ Copy button
  , m_pasteButton(nullptr)         // ✅ Paste button
  , m_selectedProcessLabel(nullptr) // ✅ Selected process display
{
  setupUI();
  setupSequenceEngine();

  // Create context viewer window (but don't show it yet)
  m_contextViewer = new ContextViewerWindow(this);
  connect(m_contextViewer, &ContextViewerWindow::refreshRequested,
          this, &RecipePage::onContextRefreshRequested);

  // ✅ Auto-load last recipe from settings
  loadLastRecipeFromSettings();

  // Check if product already loaded
  if (m_context->productPositionManager &&
    m_context->productPositionManager->HasLoadedProduct()) {
    std::string productName = m_context->productPositionManager->GetCurrentProductName();
    m_productStatusLabel->setText(QString("Product: %1")
      .arg(QString::fromStdString(productName)));
    m_productStatusLabel->setStyleSheet("font-size: 11px; color: #4ec9b0; font-weight: bold;");
  }

  std::cout << "[RecipePage] Initialized" << std::endl;
}

RecipePage::~RecipePage()
{
  // Clean up worker thread
  if (m_recipeWorker) {
    m_recipeWorker->quit();
    m_recipeWorker->wait();
    delete m_recipeWorker;
  }

  // Clean up context viewer
  if (m_contextViewer) {
    m_contextViewer->close();
    delete m_contextViewer;
  }

  // Clean up execution context
  delete m_currentContext;

  delete m_recipeManager;
  delete m_sequenceEngine;
  delete m_processFactory;
  std::cout << "[RecipePage] Destroyed" << std::endl;
}

void RecipePage::setupUI()
{
  // Set dark theme background for entire page
  setStyleSheet(
    "RecipePage { "
    "  background-color: #2d2d30; "
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
    "} "
    "QScrollArea { "
    "  background-color: #2d2d30; "
    "  border: none; "
    "}"
  );

  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(5, 0, 5, 5);
  mainLayout->setSpacing(0);

  QHBoxLayout* titleLayout = new QHBoxLayout();
  titleLayout->setSpacing(5);
  titleLayout->setContentsMargins(5, 5, 5, 5);

  m_backButton = new QPushButton("← Back to Main Menu");
  m_backButton->setFixedHeight(28);
  m_backButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #3d5a8c; "
    "  color: white; "
    "  border-radius: 3px; "
    "  font-size: 11px; "
    "  font-weight: bold; "
    "  padding: 4px 12px; "
    "} "
    "QPushButton:hover { background-color: #5a8fd6; }"
  );
  connect(m_backButton, &QPushButton::clicked, this, &RecipePage::backToMainMenu);
  titleLayout->addWidget(m_backButton);

  QLabel* titleLabel = new QLabel("Recipe Editor");
  titleLabel->setStyleSheet(
    "font-size: 15px; "
    "font-weight: bold; "
    "color: #ffffff; "
    "padding: 0px; "
    "margin: 0px;"
  );
  titleLabel->setAlignment(Qt::AlignCenter);
  titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  titleLayout->addWidget(titleLabel);

  // ✅ NEW: Recipe file name label
  m_recipeFileLabel = new QLabel("No recipe loaded");
  m_recipeFileLabel->setStyleSheet(
    "font-size: 11px; "
    "color: #d4d4d4; "
    "padding: 4px; "
    "background-color: #1e1e1e; "
    "border: 1px solid #3f3f46; "
    "border-radius: 3px;"
  );
  titleLayout->addWidget(m_recipeFileLabel);

  m_productStatusLabel = new QLabel("No product loaded");
  m_productStatusLabel->setStyleSheet(
    "font-size: 11px; "
    "color: #cccccc; "
    "padding: 4px;"
  );
  titleLayout->addWidget(m_productStatusLabel);

  // ❌ COMMENTED OUT: Load Product button (not needed - product loaded in MainWindow)
  // m_loadProductButton = new QPushButton("📁 Load Product's Position File");
  // m_loadProductButton->setFixedHeight(28);
  // m_loadProductButton->setStyleSheet(
  //   "QPushButton { "
  //   "  background-color: #27ae60; "
  //   "  color: white; "
  //   "  border-radius: 3px; "
  //   "  font-size: 11px; "
  //   "  font-weight: bold; "
  //   "  padding: 4px 12px; "
  //   "} "
  //   "QPushButton:hover { background-color: #2ecc71; }"
  // );
  // connect(m_loadProductButton, &QPushButton::clicked, this, &RecipePage::onLoadProductClicked);
  // titleLayout->addWidget(m_loadProductButton);

  // ✅ Context Viewer button (small button at far right)
  m_contextViewerButton = new QPushButton("CV");
  m_contextViewerButton->setToolTip("Context Viewer - View execution context variables");
  m_contextViewerButton->setFixedSize(40, 28);
  m_contextViewerButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #4a90a4; "
    "  color: white; "
    "  border-radius: 3px; "
    "  font-size: 11px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { background-color: #5aa0b4; }"
  );
  connect(m_contextViewerButton, &QPushButton::clicked, this, &RecipePage::onShowContextViewer);
  titleLayout->addWidget(m_contextViewerButton);

  // ✅ STOP button (emergency stop for motors and sequence)
  m_stopButton = new QPushButton("⚠ STOP");
  m_stopButton->setObjectName("stopButton");  // For dark red styling
  m_stopButton->setToolTip("Emergency stop - halt all motion and sequence execution");
  m_stopButton->setFixedSize(70, 28);
  m_stopButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #c0392b; "
    "  color: white; "
    "  border-radius: 3px; "
    "  font-size: 11px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { background-color: #e74c3c; }"
  );
  connect(m_stopButton, &QPushButton::clicked, this, &RecipePage::onStopRecipeRequested);
  titleLayout->addWidget(m_stopButton);

  titleLayout->addSpacing(m_backButton->width());

  mainLayout->addLayout(titleLayout);

  QSplitter* splitter = new QSplitter(Qt::Horizontal);

  QWidget* leftPanel = new QWidget();
  createLeftPanel(leftPanel);
  splitter->addWidget(leftPanel);

  QWidget* middlePanel = new QWidget();
  createMiddlePanel(middlePanel);
  splitter->addWidget(middlePanel);

  QWidget* rightPanel = new QWidget();
  createRightPanel(rightPanel);
  splitter->addWidget(rightPanel);

  splitter->setStretchFactor(0, 2);
  splitter->setStretchFactor(1, 3);
  splitter->setStretchFactor(2, 4);

  mainLayout->addWidget(splitter, 1);
}

void RecipePage::createLeftPanel(QWidget* leftPanel)
{
  QVBoxLayout* mainLayout = new QVBoxLayout(leftPanel);
  mainLayout->setContentsMargins(5, 5, 5, 5);
  mainLayout->setSpacing(5);

  QLabel* title = new QLabel("Available Processes");
  title->setStyleSheet("font-weight: bold; color: #ffffff;");
  mainLayout->addWidget(title);

  // Search box for filtering processes
  QLineEdit* searchBox = new QLineEdit();
  searchBox->setPlaceholderText("Search processes...");
  searchBox->setStyleSheet(
    "padding: 5px; "
    "border: 1px solid #555555; "
    "border-radius: 3px; "
    "background-color: #3c3c3c; "
    "color: #d4d4d4;"
  );
  mainLayout->addWidget(searchBox);

  // ✅ Selected process label
  m_selectedProcessLabel = new QLabel("Selected: (none)");
  m_selectedProcessLabel->setFixedHeight(50);
  m_selectedProcessLabel->setStyleSheet(
    "padding: 5px; "
    "border: 1px solid #555555; "
    "border-radius: 3px; "
    "background-color: #2d2d30; "
    "color: #4ec9b0; "
    "font-style: italic; "
    "font-size: 13px;"
  );
  m_selectedProcessLabel->setWordWrap(true);
  m_selectedProcessLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  mainLayout->addWidget(m_selectedProcessLabel);

  // ========== HORIZONTAL SPLIT: List on left, Button on right ==========
  QHBoxLayout* contentLayout = new QHBoxLayout();
  contentLayout->setSpacing(5);

  // LEFT: Process tree (categorized)
  m_processTemplateList = new QTreeWidget();
  m_processTemplateList->setHeaderHidden(true);
  m_processTemplateList->setStyleSheet(
    "QTreeWidget { "
    "  background-color: #1e1e1e; "
    "  border: 1px solid #3f3f46; "
    "  border-radius: 4px; "
    "  color: #d4d4d4; "
    "  gridline-color: #3f3f46; "
    "  font-family: 'RobotoMono-Regular', 'Roboto Mono', 'Consolas', monospace; "
    "  font-size: 13px; "
    "} "
    "QTreeWidget::item { "
    "  padding: 5px; "
    "  border-bottom: 1px solid #3f3f46; "
    "} "
    "QTreeWidget::item:hover { "
    "  background-color: #2a2d2e; "
    "} "
    "QTreeWidget::item:selected { "
    "  background-color: #0e639c; "
    "  color: white; "
    "} "
    "QTreeWidget::branch:has-children:closed { "
    "  border-image: none; "
    "} "
    "QTreeWidget::branch:has-children:open { "
    "  border-image: none; "
    "}"
  );
  m_processTemplateList->setRootIsDecorated(true);  // Show expand/collapse arrows
  m_processTemplateList->setIndentation(20);        // Indentation for child items

  // Populate tree with categories
  for (const auto& category : RecipeManager::getProcessCategories()) {
    QTreeWidgetItem* categoryItem = new QTreeWidgetItem(m_processTemplateList);
    categoryItem->setText(0, QString::fromStdString(category));
    categoryItem->setFlags(categoryItem->flags() & ~Qt::ItemIsSelectable);

    QFont font = categoryItem->font(0);
    font.setBold(true);
    categoryItem->setFont(0, font);
    categoryItem->setForeground(0, QColor("#dcdcaa"));  // Yellow/gold for category headers
    categoryItem->setBackground(0, QColor("#2d2d30"));  // Dark background for categories

    // Add processes for this category
    for (const auto& processType : RecipeManager::getProcessesByCategory(category)) {
      QTreeWidgetItem* processItem = new QTreeWidgetItem(categoryItem);
      processItem->setText(0, QString::fromStdString(processType));
      processItem->setData(0, Qt::UserRole, QString::fromStdString(processType));
    }

    categoryItem->setExpanded(false);  // Start collapsed so user sees the arrows
  }

  connect(m_processTemplateList, &QTreeWidget::itemClicked,
    this, &RecipePage::onProcessTemplateSelected);
  connect(m_processTemplateList, &QTreeWidget::itemDoubleClicked,
    [this](QTreeWidgetItem* item, int) {
      if (item && item->parent()) { // Only process items (not category headers)
        onAddProcess();
      }
    });

  contentLayout->addWidget(m_processTemplateList);

  // RIGHT: Thin vertical button with >>
  m_addButton = new QPushButton("»");
  m_addButton->setFixedWidth(35);
  m_addButton->setToolTip("Add selected process to recipe (or double-click item)");
  m_addButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #4CAF50; "
    "  color: white; "
    "  border-radius: 4px; "
    "  font-weight: bold; "
    "  padding: 5px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #45a049; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #3d8b40; "
    "}"
  );
  connect(m_addButton, &QPushButton::clicked, this, &RecipePage::onAddProcess);

  contentLayout->addWidget(m_addButton);

  mainLayout->addLayout(contentLayout);

  // Connect search box to filter tree
  connect(searchBox, &QLineEdit::textChanged, [this](const QString& searchText) {
    QString lowerSearch = searchText.toLower();

    for (int i = 0; i < m_processTemplateList->topLevelItemCount(); ++i) {
      QTreeWidgetItem* categoryItem = m_processTemplateList->topLevelItem(i);
      bool categoryHasVisibleChildren = false;

      for (int j = 0; j < categoryItem->childCount(); ++j) {
        QTreeWidgetItem* processItem = categoryItem->child(j);
        QString processName = processItem->text(0).toLower();

        bool matches = lowerSearch.isEmpty() || processName.contains(lowerSearch);
        processItem->setHidden(!matches);

        if (matches) {
          categoryHasVisibleChildren = true;
        }
      }

      // Hide category if no children are visible
      categoryItem->setHidden(!categoryHasVisibleChildren);
    }
  });
}

void RecipePage::createMiddlePanel(QWidget* middlePanel)
{
  QVBoxLayout* layout = new QVBoxLayout(middlePanel);
  layout->setContentsMargins(5, 5, 5, 5);
  layout->setSpacing(5);

  QLabel* title = new QLabel("Recipe Steps");
  title->setStyleSheet(
    "font-weight: bold; "
    "color: #ffffff; "
    "font-size: 18px; "
    "padding: 4px 0px;"
  );
  layout->addWidget(title);

  QHBoxLayout* nameLayout = new QHBoxLayout();
  QLabel* nameLabel = new QLabel("Name:");
  nameLabel->setStyleSheet(
    "font-weight: bold; "
    "color: #ffffff; "
    "font-size: 14px;"
  );
  m_recipeNameEdit = new QLineEdit(QString::fromStdString(m_recipeManager->getRecipeName()));
  m_recipeNameEdit->setStyleSheet(
    "QLineEdit { "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "  color: #ffffff; "
    "  background-color: #2d2d30; "
    "  border: 1px solid #3f3f46; "
    "  border-radius: 4px; "
    "  padding: 6px 8px; "
    "}"
    "QLineEdit:focus { "
    "  border: 1px solid #007acc; "
    "}"
  );
  connect(m_recipeNameEdit, &QLineEdit::textChanged, this, &RecipePage::onRecipeNameChanged);
  nameLayout->addWidget(nameLabel);
  nameLayout->addWidget(m_recipeNameEdit);
  layout->addLayout(nameLayout);

  // ========== SINGLE COLUMN LAYOUT: Button Rows + List ==========

  // Common button style - compact for horizontal rows
  QString buttonStyle =
    "QPushButton { "
    "  color: white; "
    "  font-weight: bold; "
    "  padding: 6px 10px; "
    "  min-height: 28px; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.4); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.4); "
    "} "
    "QPushButton:hover { "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:pressed { "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 8px; "
    "  padding-left: 12px; "
    "}";

  // ========== ROW 1: Execute Buttons ==========
  QHBoxLayout* executeRow = new QHBoxLayout();
  executeRow->setSpacing(3);

  m_runRecipeButton = new QPushButton("Run Recipe");
  m_runRecipeButton->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ffa500, stop:1 #ff8c00); "
    "  color: white; "
    "  font-weight: bold; "
    "  padding: 8px 16px; "
    "  min-height: 36px; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ffb52e, stop:1 #ff9d2e); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.7); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.7); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #ff7700; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "}"
  );
  connect(m_runRecipeButton, &QPushButton::clicked, this, &RecipePage::onRunRecipe);
  executeRow->addWidget(m_runRecipeButton, 1);

  m_runSelectedStepButton = new QPushButton("Run Selected");
  m_runSelectedStepButton->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5a9ff2, stop:1 #4a90e2); "
    "  color: white; "
    "  font-weight: bold; "
    "  padding: 8px 16px; "
    "  min-height: 36px; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #6aaff8, stop:1 #5a9ff2); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.7); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.7); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #3a80d2; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "} "
    "QPushButton:disabled { "
    "  background-color: #cccccc; "
    "  color: #666666; "
    "}"
  );
  connect(m_runSelectedStepButton, &QPushButton::clicked, this, &RecipePage::onRunSelectedStep);
  executeRow->addWidget(m_runSelectedStepButton, 1);

  // Step button (cyan/teal color for debug mode)
  m_stepButton = new QPushButton("Step");
  m_stepButton->setToolTip("Run recipe step-by-step (pause at each step)");
  m_stepButton->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #17a2b8, stop:1 #138496); "
    "  color: white; "
    "  font-weight: bold; "
    "  padding: 8px 12px; "
    "  min-height: 36px; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #1fc8e3, stop:1 #17a2b8); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #117a8b; "
    "} "
    "QPushButton:disabled { "
    "  background-color: #6c757d; "
    "  color: #adb5bd; "
    "}"
  );
  connect(m_stepButton, &QPushButton::clicked, this, &RecipePage::onStepButtonClicked);
  executeRow->addWidget(m_stepButton);

  // Continue button (green - only enabled when paused)
  m_continueButton = new QPushButton("Continue");
  m_continueButton->setToolTip("Continue execution (or run to end)");
  m_continueButton->setEnabled(false);  // Disabled until paused
  m_continueButton->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #28a745, stop:1 #1e7e34); "
    "  color: white; "
    "  font-weight: bold; "
    "  padding: 8px 12px; "
    "  min-height: 36px; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #34ce57, stop:1 #28a745); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #1c7430; "
    "} "
    "QPushButton:disabled { "
    "  background-color: #6c757d; "
    "  color: #adb5bd; "
    "}"
  );
  connect(m_continueButton, &QPushButton::clicked, this, &RecipePage::onContinueButtonClicked);
  executeRow->addWidget(m_continueButton);

  layout->addLayout(executeRow);

  // ========== DROPDOWN MENUS ROW ==========
  QHBoxLayout* menuRow = new QHBoxLayout();
  menuRow->setSpacing(3);

  // Common dropdown button style
  QString menuButtonStyle =
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #4a4a4a, stop:1 #3a3a3a); "
    "  color: white; "
    "  font-weight: bold; "
    "  padding: 6px 12px; "
    "  min-height: 28px; "
    "  border: 1px solid #555555; "
    "  border-radius: 3px; "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5a5a5a, stop:1 #4a4a4a); "
    "} "
    "QPushButton:pressed, QPushButton:menu-open { "
    "  background-color: #3a3a3a; "
    "} "
    "QPushButton::menu-indicator { "
    "  subcontrol-origin: padding; "
    "  subcontrol-position: center right; "
    "  right: 5px; "
    "}";

  QString menuStyle =
    "QMenu { "
    "  background-color: #2d2d30; "
    "  border: 1px solid #555555; "
    "  padding: 4px; "
    "} "
    "QMenu::item { "
    "  background-color: transparent; "
    "  padding: 6px 20px; "
    "  color: #d4d4d4; "
    "} "
    "QMenu::item:selected { "
    "  background-color: #0e639c; "
    "  color: white; "
    "} "
    "QMenu::separator { "
    "  height: 1px; "
    "  background: #555555; "
    "  margin: 4px 10px; "
    "}";

  // ===== FILE MENU =====
  QPushButton* fileMenuBtn = new QPushButton("File \u25BC");
  fileMenuBtn->setStyleSheet(menuButtonStyle);
  QMenu* fileMenu = new QMenu(fileMenuBtn);
  fileMenu->setStyleSheet(menuStyle);

  // Create hidden buttons for member pointer compatibility
  m_newButton = new QPushButton();
  m_newButton->setVisible(false);
  m_loadButton = new QPushButton();
  m_loadButton->setVisible(false);
  m_saveButton = new QPushButton();
  m_saveButton->setVisible(false);
  m_saveAsButton = new QPushButton();
  m_saveAsButton->setVisible(false);
  m_updateProductFileButton = new QPushButton();
  m_updateProductFileButton->setVisible(false);

  fileMenu->addAction("New", this, &RecipePage::onNewRecipe);
  fileMenu->addAction("Load...", this, &RecipePage::onLoadRecipe);
  fileMenu->addSeparator();
  fileMenu->addAction("Save", this, &RecipePage::onSaveRecipe);
  fileMenu->addAction("Save As...", this, &RecipePage::onSaveRecipeAs);
  fileMenu->addSeparator();
  fileMenu->addAction("Set Product File...", this, &RecipePage::onUpdateProductFile);

  fileMenuBtn->setMenu(fileMenu);
  menuRow->addWidget(fileMenuBtn);

  // ===== EDIT MENU =====
  QPushButton* editMenuBtn = new QPushButton("Edit \u25BC");
  editMenuBtn->setStyleSheet(menuButtonStyle);
  QMenu* editMenu = new QMenu(editMenuBtn);
  editMenu->setStyleSheet(menuStyle);

  // Create hidden buttons for member pointer compatibility
  m_copyButton = new QPushButton();
  m_copyButton->setVisible(false);
  m_pasteButton = new QPushButton();
  m_pasteButton->setVisible(false);
  m_moveUpButton = new QPushButton();
  m_moveUpButton->setVisible(false);
  m_moveDownButton = new QPushButton();
  m_moveDownButton->setVisible(false);
  m_removeButton = new QPushButton();
  m_removeButton->setVisible(false);

  editMenu->addAction("Copy", this, &RecipePage::onCopySelectedSteps, QKeySequence::Copy);
  editMenu->addAction("Paste", this, &RecipePage::onPasteSteps, QKeySequence::Paste);
  editMenu->addSeparator();
  editMenu->addAction("Move Up", this, &RecipePage::onMoveStepUp, QKeySequence(Qt::CTRL | Qt::Key_Up));
  editMenu->addAction("Move Down", this, &RecipePage::onMoveStepDown, QKeySequence(Qt::CTRL | Qt::Key_Down));
  editMenu->addSeparator();
  editMenu->addAction("Remove", this, &RecipePage::onRemoveStep, QKeySequence::Delete);

  editMenuBtn->setMenu(editMenu);
  menuRow->addWidget(editMenuBtn);

  // ===== SELECT MENU =====
  QPushButton* selectMenuBtn = new QPushButton("Select \u25BC");
  selectMenuBtn->setStyleSheet(menuButtonStyle);
  QMenu* selectMenu = new QMenu(selectMenuBtn);
  selectMenu->setStyleSheet(menuStyle);

  // Create hidden buttons for member pointer compatibility
  m_selectAllButton = new QPushButton();
  m_selectAllButton->setVisible(false);
  m_deselectAllButton = new QPushButton();
  m_deselectAllButton->setVisible(false);
  m_toggleSelectedButton = new QPushButton();
  m_toggleSelectedButton->setVisible(false);

  selectMenu->addAction("Select All", this, &RecipePage::onSelectAllProcesses, QKeySequence::SelectAll);
  selectMenu->addAction("Deselect All", this, &RecipePage::onDeselectAllProcesses);
  selectMenu->addSeparator();
  selectMenu->addAction("Toggle Selected", this, &RecipePage::onToggleSelectedProcesses);

  selectMenuBtn->setMenu(selectMenu);
  menuRow->addWidget(selectMenuBtn);

  menuRow->addStretch();  // Push menus to the left

  layout->addLayout(menuRow);

  // ========== PROCESS INSTANCE LIST ==========
  m_recipeTree = new QTreeWidget();
  m_recipeTree->setColumnCount(5);
  m_recipeTree->setHeaderLabels({ "", "#", "", "Process Instance", "Run" });  // Column 0 = breakpoint, 2 = symbol
  m_recipeTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_recipeTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_recipeTree->setRootIsDecorated(false);  // Hide expand arrows for clean look
  m_recipeTree->setIndentation(24);          // Indent for nesting clarity

  m_recipeTree->header()->setStretchLastSection(false);
  m_recipeTree->header()->setSectionResizeMode(0, QHeaderView::Interactive);       // Breakpoint column - manual drag
  m_recipeTree->setColumnWidth(0, 30);                                             // Default width for breakpoint
  m_recipeTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);  // Step number auto-fit
  m_recipeTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);  // Symbol column auto-fit
  m_recipeTree->header()->setSectionResizeMode(3, QHeaderView::Stretch);           // Process Instance stretches
  m_recipeTree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);  // Run checkbox auto-fit

  m_recipeTree->setStyleSheet(
    "QTreeWidget { "
    "  background-color: #1e1e1e; "
    "  border: 1px solid #3f3f46; "
    "  color: #d4d4d4; "
    "  font-family: 'RobotoMono-Regular', 'Roboto Mono', 'Consolas', monospace; "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "} "
    "QTreeWidget::branch { border-image: none; image: none; } "
    "QTreeWidget::item { "
    "  padding: 6px 4px; "
    "  min-height: 26px; "
    "} "
    "QTreeWidget::item:hover { "
    "  background-color: #2a2d2e; "
    "} "
    "QTreeWidget::item:selected { "
    "  background-color: #2d4f7c; "
    "  color: white; "
    "} "
    "QHeaderView::section { "
    "  background-color: #383838; "
    "  padding: 6px; "
    "  border: 1px solid #3f3f46; "
    "  font-family: 'RobotoMono-Regular', 'Roboto Mono', 'Consolas', monospace; "
    "  font-weight: bold; "
    "  font-size: 14px; "
    "  color: #d4d4d4; "
    "} "
    "QCheckBox { "
    "  spacing: 5px; "
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

  connect(m_recipeTree, &QTreeWidget::itemSelectionChanged,
    this, &RecipePage::onRecipeStepSelected);

  layout->addWidget(m_recipeTree, 1);
}

void RecipePage::createRightPanel(QWidget* rightPanel)
{
  QVBoxLayout* layout = new QVBoxLayout(rightPanel);
  layout->setContentsMargins(5, 5, 5, 5);

  QLabel* title = new QLabel("Process Configuration");
  title->setStyleSheet("font-weight: bold; color: #ffffff;");
  layout->addWidget(title);

  m_configScrollArea = new QScrollArea();
  m_configScrollArea->setWidgetResizable(true);
  m_configScrollArea->setStyleSheet(
    "QScrollArea { "
    "  background-color: #1e1e1e; "
    "  border: 1px solid #3f3f46; "
    "  border-radius: 4px; "
    "} "
    "QWidget { "
    "  background-color: #1e1e1e; "
    "  color: #d4d4d4; "
    "}"
  );

  layout->addWidget(m_configScrollArea);

  // Create manual adjust widget (will be shown when needed)
  m_manualAdjustWidget = new ManualAdjustWidget(
    m_context->motionManager,
    m_matrixLoader
  );
  m_manualAdjustWidget->setStyleSheet("background-color: #2d2d30;");

  // Connect manual adjust response
  connect(m_manualAdjustWidget, &ManualAdjustWidget::responseReceived,
    this, &RecipePage::onManualAdjustResponse);

  // Create manual align point widget (will be shown when needed)
  m_manualAlignPointWidget = new ManualAlignPointWidget(
    m_context->motionManager,
    m_context
  );
  m_manualAlignPointWidget->setStyleSheet("background-color: #2d2d30;");

  // Connect manual align point response
  connect(m_manualAlignPointWidget, &ManualAlignPointWidget::responseReceived,
    this, &RecipePage::onManualAlignPointResponse);

  // Create user input widget (will be shown when needed)
  m_userInputWidget = new UserInputWidget();
  m_userInputWidget->setStyleSheet("background-color: #2d2d30;");

  // Connect user input response
  connect(m_userInputWidget, &UserInputWidget::responseReceived,
    this, &RecipePage::onUserInputResponse);

  // ✅ Vision result image display
  m_visionResultGroup = new QGroupBox("Vision Result");
  m_visionResultGroup->setStyleSheet(
    "QGroupBox { "
    "  font-weight: bold; "
    "  color: #FFA500; "
    "  border: 1px solid #3f3f46; "
    "  border-radius: 4px; "
    "  margin-top: 10px; "
    "  padding-top: 10px; "
    "} "
    "QGroupBox::title { "
    "  subcontrol-origin: margin; "
    "  left: 10px; "
    "  padding: 0 5px; "
    "}"
  );
  QVBoxLayout* visionLayout = new QVBoxLayout(m_visionResultGroup);
  visionLayout->setContentsMargins(5, 15, 5, 5);

  m_visionResultImageLabel = new QLabel("No vision result");
  m_visionResultImageLabel->setAlignment(Qt::AlignCenter);
  m_visionResultImageLabel->setMinimumHeight(200);
  m_visionResultImageLabel->setStyleSheet(
    "QLabel { "
    "  background-color: #0f0f0f; "
    "  border: 1px solid #3f3f46; "
    "  color: #808080; "
    "  font-style: italic; "
    "}"
  );
  visionLayout->addWidget(m_visionResultImageLabel);

  layout->addWidget(m_visionResultGroup);

  clearConfigurationForm();
}

void RecipePage::onNewRecipe()
{
  if (m_recipeManager->getStepCount() > 0) {
    QMessageBox::StandardButton reply = QMessageBox::question(
      this, "New Recipe",
      "Current recipe will be cleared. Continue?",
      QMessageBox::Yes | QMessageBox::No
    );
    if (reply != QMessageBox::Yes) return;
  }

  m_recipeManager->newRecipe();
  m_recipeNameEdit->setText(QString::fromStdString(m_recipeManager->getRecipeName()));
  refreshRecipeList();
  clearConfigurationForm();
  updateRecipeFileNameDisplay();  // ✅ NEW: Update recipe file name label (will show "No recipe file")
}

void RecipePage::onProductChanged()
{
  // Product has changed - update product info and clear current recipe
  if (!m_context || !m_context->productPositionManager) {
    return;
  }

  // Get the newly loaded product information
  std::string productName = m_context->productPositionManager->GetCurrentProductName();
  std::string productPath = m_context->productPositionManager->GetCurrentProductPath();

  if (!productName.empty() && !productPath.empty()) {
    // Update RecipeManager's product file
    m_recipeManager->setProductFile(productPath);

    // Update product status labels
    m_productStatusLabel->setText(QString("Product: %1")
      .arg(QString::fromStdString(productName)));
    m_productStatusLabel->setStyleSheet("font-size: 11px; color: #4ec9b0; font-weight: bold;");

    if (m_loadedProductLabel) {
      m_loadedProductLabel->setText(QString("Product: %1\nFile: %2")
        .arg(QString::fromStdString(productName))
        .arg(QString::fromStdString(productPath)));
      m_loadedProductLabel->setStyleSheet(
      "font-size: 11px; "
      "color: #4ec9b0; "
      "font-weight: bold; "
      "padding: 5px; "
      "border: 1px solid #4ec9b0; "
      "border-radius: 3px; "
      "background-color: #1e3a32;"
    );
    }

    std::cout << "[RecipePage] Product changed to: " << productName << std::endl;
  }

  // Clear current recipe if any (user should load recipe from new product)
  if (m_recipeManager->getStepCount() > 0) {
    m_recipeManager->newRecipe();
    m_recipeNameEdit->setText(QString::fromStdString(m_recipeManager->getRecipeName()));
    refreshRecipeList();
    clearConfigurationForm();
  }
}

void RecipePage::onLoadRecipe()
{
  // ✅ Check if product is loaded
  if (!m_context->productPositionManager || !m_context->productPositionManager->HasLoadedProduct()) {
    QMessageBox::warning(this, "No Product Loaded",
      "Please load a product first before loading recipes.\n\n"
      "Use the 'Load Product' button in the main menu.");
    return;
  }

  // ✅ Get product name and build path to product's recipes folder
  QString productName = QString::fromStdString(m_context->productPositionManager->GetCurrentProductName());
  QString recipesPath = QString("config/%1/recipes").arg(productName);

  // ✅ Use FileSelectorDialog to show only recipes from current product's recipes folder
  QString filename = FileSelectorDialog::getOpenFileName(
    this,
    recipesPath,  // Folder path: config/[product]/recipes/
    "*.json",     // File pattern
    "Load Recipe", // Dialog title
    "recipe files" // File type description
  );

  if (filename.isEmpty()) return;

  if (m_recipeManager->loadRecipe(filename.toStdString())) {
    m_recipeNameEdit->setText(QString::fromStdString(m_recipeManager->getRecipeName()));
    refreshRecipeList();
    clearConfigurationForm();
    updateRecipeFileNameDisplay();  // ✅ NEW: Update recipe file name label

    // ✅ Save to settings for persistence
    saveLastRecipeToSettings(filename);

    // Auto-load associated product file
    std::string productFile = m_recipeManager->getProductFile();
    if (!productFile.empty()) {
      if (m_context->productPositionManager) {
        if (m_context->productPositionManager->LoadProduct(productFile)) {
          m_context->productPositionManager->ExportToMotionManager(*m_context->motionManager);

          std::string productName = m_context->productPositionManager->GetCurrentProductName();

          m_productStatusLabel->setText(QString("Product: %1")
            .arg(QString::fromStdString(productName)));
          m_productStatusLabel->setStyleSheet("font-size: 11px; color: #4ec9b0; font-weight: bold;");

          if (m_loadedProductLabel) {
            m_loadedProductLabel->setText(QString("Product: %1\nFile: %2")
              .arg(QString::fromStdString(productName))
              .arg(QString::fromStdString(productFile)));
            m_loadedProductLabel->setStyleSheet(
            "font-size: 11px; "
            "color: #4ec9b0; "
            "font-weight: bold; "
            "padding: 5px; "
            "border: 1px solid #4ec9b0; "
            "border-radius: 3px; "
            "background-color: #1e3a32;"
          );
          }

          std::cout << "[RecipePage] Auto-loaded product: " << productFile << std::endl;

          QMessageBox::information(this, "Recipe Loaded",
            QString("Recipe '%1' loaded successfully!\n\nAuto-loaded product: %2")
            .arg(QString::fromStdString(m_recipeManager->getRecipeName()))
            .arg(QString::fromStdString(productName)));
        }
        else {
          if (m_loadedProductLabel) {
            m_loadedProductLabel->setText(QString("⚠ Product file not found:\n%1")
              .arg(QString::fromStdString(productFile)));
            m_loadedProductLabel->setStyleSheet(
            "font-size: 11px; "
            "color: #f48771; "
            "padding: 5px; "
            "border: 1px solid #f48771; "
            "border-radius: 3px; "
            "background-color: #3d2121;"
          );
          }

          QMessageBox::warning(this, "Product Not Found",
            QString("Recipe loaded, but associated product file not found:\n%1\n\nPlease load the product manually.")
            .arg(QString::fromStdString(productFile)));
        }
      }
    }
    else {
      if (m_loadedProductLabel) {
        m_loadedProductLabel->setText("Product: None");
        m_loadedProductLabel->setStyleSheet(
        "font-size: 11px; "
        "color: #888888; "
        "padding: 5px; "
        "border: 1px solid #3f3f46; "
        "border-radius: 3px; "
        "background-color: #1e1e1e;"
      );
      }

      QMessageBox::information(this, "Success", "Recipe loaded successfully!");
    }
  }
  else {
    QMessageBox::critical(this, "Error", "Failed to load recipe file.");
  }
}

void RecipePage::onSaveRecipe()
{
  if (m_recipeManager->getStepCount() == 0) {
    QMessageBox::warning(this, "Empty Recipe", "Recipe is empty. Nothing to save.");
    return;
  }

  // ✅ Check if product is loaded
  if (!m_context->productPositionManager || !m_context->productPositionManager->HasLoadedProduct()) {
    QMessageBox::warning(this, "No Product Loaded",
      "Please load a product first before saving recipes.\n\n"
      "Use the 'Load Product' button in the main menu.");
    return;
  }

  // ✅ Check if recipe already has a file path
  std::string currentRecipePath = m_recipeManager->getCurrentFilePath();

  if (currentRecipePath.empty()) {
    // No path yet - prompt for save location (like Save As)
    onSaveRecipeAs();
    return;
  }

  // ✅ Recipe already has path - save directly without prompting
  if (m_recipeManager->saveRecipe(currentRecipePath)) {
    updateRecipeFileNameDisplay();  // ✅ NEW: Update recipe file name label
    // Show brief success message in status bar or small notification
    // Using information dialog but could be replaced with statusbar message
    QMessageBox::information(this, "Saved",
      QString("Recipe saved to:\n%1").arg(QString::fromStdString(currentRecipePath)));
  }
  else {
    QMessageBox::critical(this, "Error", "Failed to save recipe file.");
  }
}

void RecipePage::onSaveRecipeAs()
{
  if (m_recipeManager->getStepCount() == 0) {
    QMessageBox::warning(this, "Empty Recipe", "Recipe is empty. Nothing to save.");
    return;
  }

  // ✅ Check if product is loaded
  if (!m_context->productPositionManager || !m_context->productPositionManager->HasLoadedProduct()) {
    QMessageBox::warning(this, "No Product Loaded",
      "Please load a product first before saving recipes.\n\n"
      "Use the 'Load Product' button in the main menu.");
    return;
  }

  // ✅ Get product name and build path to product's recipes folder
  QString productName = QString::fromStdString(m_context->productPositionManager->GetCurrentProductName());
  QString recipesPath = QString("config/%1/recipes").arg(productName);

  // ✅ Get current recipe name for default
  QString currentName = QString::fromStdString(m_recipeManager->getRecipeName());

  // ✅ Use custom RecipeSaveAsDialog with product folder path
  QString filename = RecipeSaveAsDialog::getSaveFileName(this, recipesPath, currentName);

  if (filename.isEmpty()) return;

  if (m_recipeManager->saveRecipe(filename.toStdString())) {
    updateRecipeFileNameDisplay();  // ✅ NEW: Update recipe file name label
    // ✅ Also save to QSettings as last loaded recipe
    saveLastRecipeToSettings(filename);

    QMessageBox::information(this, "Success",
      QString("Recipe saved as:\n%1").arg(filename));
  }
  else {
    QMessageBox::critical(this, "Error", "Failed to save recipe file.");
  }
}

void RecipePage::onAddProcess()
{
  QTreeWidgetItem* item = m_processTemplateList->currentItem();
  if (!item) {
    QMessageBox::warning(this, "No Selection", "Please select a process template to add.");
    return;
  }

  // Only add if it's a process item (not a category header)
  if (!item->parent()) {
    QMessageBox::warning(this, "Invalid Selection", "Please select a process, not a category.");
    return;
  }

  m_recipeManager->addStep(item->text(0).toStdString());
  refreshRecipeList();
  selectStepByIndex(m_recipeManager->getStepCount() - 1);
}

void RecipePage::onSelectAllProcesses()
{
  // Enable all steps
  for (int i = 0; i < m_recipeManager->getStepCount(); ++i) {
    m_recipeManager->setStepEnabled(i, true);
  }
  refreshRecipeList();

  // Restore selection if there was one
  if (m_selectedStepIndex >= 0 && m_selectedStepIndex < m_recipeManager->getStepCount()) {
    selectStepByIndex(m_selectedStepIndex);
  }
}

void RecipePage::onDeselectAllProcesses()
{
  // Disable all steps
  for (int i = 0; i < m_recipeManager->getStepCount(); ++i) {
    m_recipeManager->setStepEnabled(i, false);
  }
  refreshRecipeList();

  // Restore selection if there was one
  if (m_selectedStepIndex >= 0 && m_selectedStepIndex < m_recipeManager->getStepCount()) {
    selectStepByIndex(m_selectedStepIndex);
  }
}

void RecipePage::onToggleSelectedProcesses()
{
  // Get all currently selected tree items
  QList<QTreeWidgetItem*> selectedItems = m_recipeTree->selectedItems();

  if (selectedItems.isEmpty()) {
    QMessageBox::information(this, "No Selection",
      "Please select one or more items first.\n\n"
      "Tip: Use Shift+Click to select a range, or Ctrl+Click to select multiple items.");
    return;
  }

  // Get step indices from selected items
  QSet<int> selectedIndices;
  for (QTreeWidgetItem* item : selectedItems) {
    int stepIndex = item->data(0, Qt::UserRole).toInt();
    selectedIndices.insert(stepIndex);
  }

  // Toggle the enabled state for each selected step
  for (int stepIndex : selectedIndices) {
    if (stepIndex >= 0 && stepIndex < m_recipeManager->getStepCount()) {
      const ProcessInstance* step = m_recipeManager->getStep(stepIndex);
      if (step) {
        // Toggle: if enabled, disable it; if disabled, enable it
        m_recipeManager->setStepEnabled(stepIndex, !step->enabled);
      }
    }
  }

  refreshRecipeList();

  // Restore the selection
  for (int stepIndex : selectedIndices) {
    selectStepByIndex(stepIndex);
  }
}

void RecipePage::onUpdateProductFile()
{
  // ✅ Step 1: Select new product file using FileSelectorDialog
  QString filePath = FileSelectorDialog::getOpenFileName(
    this,
    "config",  // Folder path
    "positions.*.json",  // File pattern: positions.[product_name].json
    "Select Product Positions File",  // Dialog title
    "position files",  // File type description
    true  // Recursive - search subdirectories
  );

  if (filePath.isEmpty()) {
    return;  // User cancelled
  }

  // ✅ Step 2: Update recipe's product file reference
  m_recipeManager->setProductFile(filePath.toStdString());

  // ✅ Step 3: Auto-save the recipe
  std::string currentRecipePath = m_recipeManager->getCurrentFilePath();

  if (currentRecipePath.empty()) {
    // Recipe hasn't been saved yet - prompt for save location
    QString saveFileName = QFileDialog::getSaveFileName(
      this, "Save Recipe", "recipes/", "Recipe Files (*.json)"
    );

    if (saveFileName.isEmpty()) {
      QMessageBox::warning(this, "Save Required",
        "Recipe must be saved to update the product file reference.\n"
        "Product file reference was not updated.");
      return;
    }

    if (!m_recipeManager->saveRecipe(saveFileName.toStdString())) {
      QMessageBox::critical(this, "Error", "Failed to save recipe file.");
      return;
    }
  } else {
    // Recipe already has a path - save directly
    if (!m_recipeManager->saveRecipe(currentRecipePath)) {
      QMessageBox::critical(this, "Error", "Failed to save recipe file.");
      return;
    }
  }

  // ✅ Step 4: Load the product positions into ProductPositionManager
  if (m_context->productPositionManager) {
    if (m_context->productPositionManager->LoadProduct(filePath.toStdString())) {
      m_context->productPositionManager->ExportToMotionManager(*m_context->motionManager);

      std::string productName = m_context->productPositionManager->GetCurrentProductName();

      // Update top status label
      m_productStatusLabel->setText(QString("Product: %1")
        .arg(QString::fromStdString(productName)));
      m_productStatusLabel->setStyleSheet("font-size: 11px; color: #4ec9b0; font-weight: bold;");

      // Update loaded product label in middle panel
      if (m_loadedProductLabel) {
        m_loadedProductLabel->setText(QString("Product: %1\nFile: %2")
          .arg(QString::fromStdString(productName))
          .arg(QString::fromStdString(m_recipeManager->getProductFile())));
        m_loadedProductLabel->setStyleSheet(
        "font-size: 11px; "
        "color: #27ae60; "
        "font-weight: bold; "
        "padding: 5px; "
        "border: 1px solid #27ae60; "
        "border-radius: 3px; "
        "background-color: #e8f5e9;"
      );
      }

      QMessageBox::information(this, "Success",
        QString("Product file updated to:\n%1\n\nRecipe saved successfully!")
        .arg(QString::fromStdString(productName)));
    } else {
      QMessageBox::warning(this, "Warning",
        "Product file reference updated in recipe, but failed to load product positions.\n"
        "Recipe was saved successfully.");
    }
  } else {
    QMessageBox::warning(this, "Warning",
      "Product file reference updated and recipe saved, but Product Manager is not available.");
  }
}

void RecipePage::onRemoveStep()
{
  if (m_selectedStepIndex < 0) return;

  // ✅ SUPPRESSED: Confirmation popup for faster workflow
  // QMessageBox::StandardButton reply = QMessageBox::question(
  //   this, "Remove Step", "Are you sure you want to remove this step?",
  //   QMessageBox::Yes | QMessageBox::No
  // );
  //
  // if (reply == QMessageBox::Yes) {

  // Remember the index being deleted
  int deletedIndex = m_selectedStepIndex;
  int totalSteps = m_recipeManager->getStepCount();

  // Remove the step
  m_recipeManager->removeStep(m_selectedStepIndex);
  refreshRecipeList();

  // ✅ Auto-select the next step (or previous if deleted the last one)
  int newTotalSteps = m_recipeManager->getStepCount();

  if (newTotalSteps > 0) {
    // If we deleted the last step, select the new last step
    if (deletedIndex >= newTotalSteps) {
      m_selectedStepIndex = newTotalSteps - 1;
    }
    else {
      // Otherwise, select the step that moved into this position
      m_selectedStepIndex = deletedIndex;
    }

    // Select the step in the tree and update the config form
    selectStepByIndex(m_selectedStepIndex);
    updateConfigurationForm();
  }
  else {
    // No steps left
    m_selectedStepIndex = -1;
    clearConfigurationForm();
  }

  // } // End of old if (reply == QMessageBox::Yes) block
}

void RecipePage::onCopySelectedSteps()
{
  // Get all selected items from the tree
  QList<QTreeWidgetItem*> selectedItems = m_recipeTree->selectedItems();

  if (selectedItems.isEmpty()) {
    QMessageBox::information(this, "No Selection",
      "Please select one or more process steps to copy.\n\n"
      "Tip: Use Shift+Click to select a range, or Ctrl+Click to select multiple items.");
    return;
  }

  // Clear the clipboard
  m_clipboard.clear();

  // Collect step indices and sort them
  QList<int> indices;
  for (QTreeWidgetItem* item : selectedItems) {
    int stepIndex = item->data(0, Qt::UserRole).toInt();
    indices.append(stepIndex);
  }
  std::sort(indices.begin(), indices.end());

  // Copy each step to clipboard
  for (int stepIndex : indices) {
    const ProcessInstance* step = m_recipeManager->getStep(stepIndex);
    if (step) {
      ClipboardStep clipStep;
      clipStep.processType = step->processType;
      clipStep.userNickname = step->userNickname;
      clipStep.enabled = step->enabled;
      clipStep.parameters = step->parameters;
      m_clipboard.push_back(clipStep);
    }
  }

  // Enable paste button
  if (m_pasteButton) {
    m_pasteButton->setEnabled(!m_clipboard.empty());
  }

  // Show feedback
  QString msg = QString("Copied %1 step(s) to clipboard").arg(m_clipboard.size());
  std::cout << "[RecipePage] " << msg.toStdString() << std::endl;

  // Brief visual feedback (optional tooltip or status message)
  m_copyButton->setToolTip(msg + "\nClick Paste to insert after current selection");
  QTimer::singleShot(2000, this, [this]() {
    if (m_copyButton) {
      m_copyButton->setToolTip("Copy selected process steps (Ctrl+C)");
    }
  });
}

void RecipePage::onPasteSteps()
{
  if (m_clipboard.empty()) {
    QMessageBox::information(this, "Empty Clipboard",
      "No process steps in clipboard. Copy some steps first.");
    return;
  }

  // Determine insertion point (after current selection, or at end)
  int insertIndex = m_recipeManager->getStepCount();  // Default: at end

  QList<QTreeWidgetItem*> selectedItems = m_recipeTree->selectedItems();
  if (!selectedItems.isEmpty()) {
    // Find the highest selected index and insert after it
    int maxIndex = -1;
    for (QTreeWidgetItem* item : selectedItems) {
      int stepIndex = item->data(0, Qt::UserRole).toInt();
      if (stepIndex > maxIndex) {
        maxIndex = stepIndex;
      }
    }
    insertIndex = maxIndex + 1;
  }

  // Insert each clipboard step
  int pastedCount = 0;
  for (const ClipboardStep& clipStep : m_clipboard) {
    // Create a nickname with "(copy)" suffix
    std::string nickname = clipStep.userNickname;
    if (nickname.find("(copy)") == std::string::npos) {
      nickname += " (copy)";
    }

    m_recipeManager->insertStepAt(insertIndex, clipStep.processType,
                                   nickname, clipStep.enabled, clipStep.parameters);
    insertIndex++;
    pastedCount++;
  }

  // Refresh the tree
  refreshRecipeList();

  // Select the first pasted step
  int firstPastedIndex = insertIndex - pastedCount;
  m_selectedStepIndex = firstPastedIndex;
  selectStepByIndex(m_selectedStepIndex);
  updateConfigurationForm();

  // Show feedback
  QString msg = QString("Pasted %1 step(s)").arg(pastedCount);
  std::cout << "[RecipePage] " << msg.toStdString() << " at index " << firstPastedIndex << std::endl;
}

void RecipePage::onMoveStepUp()
{
  if (m_recipeManager->moveStepUp(m_selectedStepIndex)) {
    m_selectedStepIndex--;  // Update index to follow the moved step
    refreshRecipeList();
    selectStepByIndex(m_selectedStepIndex);
  }
}

void RecipePage::onMoveStepDown()
{
  if (m_recipeManager->moveStepDown(m_selectedStepIndex)) {
    m_selectedStepIndex++;  // Update index to follow the moved step
    refreshRecipeList();
    selectStepByIndex(m_selectedStepIndex);
  }
}

void RecipePage::onRecipeStepSelected()
{
  QTreeWidgetItem* item = m_recipeTree->currentItem();
  if (!item) {
    clearConfigurationForm();
    return;
  }

  // Get step index from item data
  int index = item->data(0, Qt::UserRole).toInt();

  // ✅ FIX: Prevent switching away from progress panel during execution
  if (m_progressWidget && m_configScrollArea->widget() == m_progressWidget) {
    // Check if execution is still in progress
    if (m_recipeWorker && m_recipeWorker->isRunning()) {
      std::cout << "[RecipePage] Execution in progress - ignoring step selection" << std::endl;
      // Restore selection to prevent visual feedback
      m_recipeTree->blockSignals(true);
      if (m_selectedStepIndex >= 0) {
        selectStepByIndex(m_selectedStepIndex);
      }
      m_recipeTree->blockSignals(false);
      return;
    }
    std::cout << "[RecipePage] User selected step, switching from progress to config" << std::endl;
  }

  if (m_selectedStepIndex == index) {
    // ✅ Even if same step, if showing progress, switch to config (only if not running)
    if (m_progressWidget && m_configScrollArea->widget() == m_progressWidget) {
      if (!(m_recipeWorker && m_recipeWorker->isRunning())) {
        updateConfigurationForm();
      }
    }
    return;
  }

  m_selectedStepIndex = index;

  if (index < 0 || index >= m_recipeManager->getStepCount()) {
    clearConfigurationForm();
    return;
  }

  // ✅ This will automatically switch from progress to config
  updateConfigurationForm();
}

void RecipePage::onRecipeNameChanged(const QString& name)
{
  m_recipeManager->setRecipeName(name.toStdString());
}

void RecipePage::onNicknameChanged(const QString& nickname)
{
  if (m_selectedStepIndex < 0) return;
  if (!m_recipeManager) return;

  m_recipeManager->setStepNickname(m_selectedStepIndex, nickname.toStdString());

  // Update only the label widget in the current item (column 3) for fast typing response
  QTreeWidgetItem* currentItem = m_recipeTree->currentItem();
  if (currentItem) {
    const ProcessInstance* step = m_recipeManager->getStep(m_selectedStepIndex);
    if (step) {
      // Get the label widget from column 3 (Process Instance column)
      QWidget* widget = m_recipeTree->itemWidget(currentItem, 3);
      QLabel* processLabel = qobject_cast<QLabel*>(widget);
      if (processLabel) {
        // Build display text with appropriate prefix
        QString iconPrefix;
        bool isComment = (step->processType == "Comment");
        bool isBlockStart = RecipeManager::isBlockStart(step->processType);
        bool isBlockEnd = RecipeManager::isBlockEnd(step->processType);
        bool isElse = (step->processType == "Conditional Else");

        if (isComment) {
          iconPrefix = "### ";
        } else if (isBlockStart) {
          iconPrefix = QString::fromUtf8("\u25BC ");
        } else if (isBlockEnd) {
          iconPrefix = QString::fromUtf8("\u2514\u2500 ");
        } else if (isElse) {
          iconPrefix = QString::fromUtf8("\u251C\u2500 ");
        }

        // For comments, display the comment text parameter
        QString displayText;
        if (isComment) {
          displayText = iconPrefix + nickname;
        } else {
          displayText = iconPrefix + QString::fromStdString(step->userNickname);
        }

        if (!step->enabled) {
          displayText = "[DISABLED] " + displayText;
        }

        processLabel->setText(displayText);
      }
    }
  }
}

void RecipePage::onEnabledChanged(bool enabled)
{
  if (m_selectedStepIndex < 0) return;

  m_recipeManager->setStepEnabled(m_selectedStepIndex, enabled);
  refreshRecipeList();
  selectStepByIndex(m_selectedStepIndex);
}

void RecipePage::onParameterChanged()
{
  std::cout << "[RecipePage] onParameterChanged() called" << std::endl;

  if (m_selectedStepIndex < 0 || !m_currentConfigWidget) {
    std::cout << "[RecipePage]   Skipping - selectedIndex=" << m_selectedStepIndex
              << ", hasWidget=" << (m_currentConfigWidget != nullptr) << std::endl;
    return;
  }

  auto params = m_currentConfigWidget->getParameters();
  std::cout << "[RecipePage]   Updating " << params.size() << " parameters for step "
            << m_selectedStepIndex << std::endl;
  for (const auto& [key, value] : params) {
    std::cout << "[RecipePage]     " << key << " = '" << value << "'" << std::endl;
    m_recipeManager->setStepParameter(m_selectedStepIndex, key, value);
  }
}

void RecipePage::onProcessTemplateSelected(QTreeWidgetItem* item, int column)
{
  (void)column; // Unused
  if (item && item->parent()) { // Only process items (not category headers)
    QString processName = item->text(0);
    std::cout << "[RecipePage] Selected template: " << processName.toStdString() << std::endl;

    // ✅ Update selected process label
    if (m_selectedProcessLabel) {
      m_selectedProcessLabel->setText("Selected: " + processName);
    }
  } else {
    // Category header selected, clear selection
    if (m_selectedProcessLabel) {
      m_selectedProcessLabel->setText("Selected: (none)");
    }
  }
}

void RecipePage::refreshRecipeList()
{
  // Block signals during refresh
  m_recipeTree->blockSignals(true);

  // Store currently selected item index before clearing
  int previousSelection = m_selectedStepIndex;

  // Clear tree
  m_recipeTree->clear();

  // Stack to track parent nodes for nesting
  std::vector<QTreeWidgetItem*> parentStack;

  int stepIndex = 0;
  for (const auto& step : m_recipeManager->getAllSteps()) {
    QTreeWidgetItem* item = nullptr;
    QTreeWidgetItem* currentParent = parentStack.empty() ? nullptr : parentStack.back();

    // Determine if this step starts or ends a block
    bool isBlockStart = RecipeManager::isBlockStart(step->processType);
    bool isBlockEnd = RecipeManager::isBlockEnd(step->processType);
    bool isElse = (step->processType == "Conditional Else");

    // Create tree item
    if (currentParent) {
      item = new QTreeWidgetItem(currentParent);
    } else {
      item = new QTreeWidgetItem(m_recipeTree);
    }

    // Store step index in item data
    item->setData(0, Qt::UserRole, stepIndex);

    // Clear all column text to prevent any default text from showing
    for (int col = 0; col < 5; ++col) {
      item->setText(col, "");
    }

    // Column 0: Breakpoint checkbox - red when checked
    QCheckBox* breakpointChk = new QCheckBox();
    breakpointChk->setChecked(step->breakpoint);
    breakpointChk->setProperty("stepIndex", stepIndex);
    breakpointChk->setCursor(Qt::PointingHandCursor);
    breakpointChk->setToolTip(step->breakpoint ? "Breakpoint set - click to remove" : "Click to set breakpoint");

    // Style: red background when checked
    breakpointChk->setStyleSheet(
      "QCheckBox { spacing: 0px; }"
      "QCheckBox::indicator { width: 16px; height: 16px; }"
      "QCheckBox::indicator:unchecked { "
      "  background-color: #444444; "
      "  border: 1px solid #666666; "
      "  border-radius: 3px; "
      "}"
      "QCheckBox::indicator:checked { "
      "  background-color: #ff3333; "
      "  border: 1px solid #ff3333; "
      "  border-radius: 3px; "
      "}"
    );

    connect(breakpointChk, &QCheckBox::toggled, this, [this, stepIndex](bool checked) {
      ProcessInstance* step = m_recipeManager->getStep(stepIndex);
      if (step) {
        step->breakpoint = checked;
        refreshRecipeList();
        selectStepByIndex(m_selectedStepIndex);
      }
    });

    // Get background color for this row
    QString bgColorStr;
    if (!step->backgroundColor.empty()) {
      bgColorStr = QString::fromStdString(step->backgroundColor);
    } else {
      bgColorStr = "transparent";  // No background color
    }

    QWidget* breakpointWidget = new QWidget();
    breakpointWidget->setStyleSheet(QString("background-color: %1;").arg(bgColorStr));
    QHBoxLayout* bpLayout = new QHBoxLayout(breakpointWidget);
    bpLayout->addWidget(breakpointChk);
    bpLayout->setAlignment(Qt::AlignCenter);
    bpLayout->setContentsMargins(0, 0, 0, 0);
    m_recipeTree->setItemWidget(item, 0, breakpointWidget);

    // Column 1: Step number (use widget for background control)
    QLabel* stepNumLabel = new QLabel(QString::number(stepIndex + 1));
    stepNumLabel->setAlignment(Qt::AlignCenter);
    stepNumLabel->setStyleSheet(QString(
      "QLabel { "
      "  background-color: %1; "
      "  color: %2; "
      "  padding: 4px; "
      "  font-family: 'RobotoMono-Regular', 'Roboto Mono', 'Consolas', monospace; "
      "  font-weight: bold; "
      "  font-size: 14px; "
      "}"
    ).arg(bgColorStr).arg(step->enabled ? "#d4d4d4" : "#999999"));
    m_recipeTree->setItemWidget(item, 1, stepNumLabel);

    // Column 2: Process category symbol
    std::string category = m_recipeManager->getProcessCategory(step->processType);
    QString symbol = getCategorySymbol(category);
    QLabel* symbolLabel = new QLabel(symbol);
    symbolLabel->setAlignment(Qt::AlignCenter);
    symbolLabel->setStyleSheet(QString(
      "QLabel { "
      "  background-color: %1; "
      "  color: %2; "
      "  padding: 4px; "
      "  font-size: 14px; "
      "}"
    ).arg(bgColorStr).arg(step->enabled ? "#d4d4d4" : "#999999"));
    symbolLabel->setToolTip(QString::fromStdString(category));
    m_recipeTree->setItemWidget(item, 2, symbolLabel);

    // Column 3: Process name (use widget for background control)
    // Add icon prefix based on step type for visual hierarchy
    QString iconPrefix;
    bool isComment = (step->processType == "Comment");
    if (isComment) {
      iconPrefix = "### ";  // Comment prefix
    } else if (isBlockStart) {
      iconPrefix = QString::fromUtf8("\u25BC ");  // ▼ down arrow for block start
    } else if (isBlockEnd) {
      iconPrefix = QString::fromUtf8("\u2514\u2500 ");  // └─ end marker
    } else if (isElse) {
      iconPrefix = QString::fromUtf8("\u251C\u2500 ");  // ├─ else branch marker
    }

    // For comments, display the comment text parameter instead of nickname
    QString displayText;
    if (isComment) {
      auto it = step->parameters.find("text");
      if (it != step->parameters.end()) {
        displayText = iconPrefix + QString::fromStdString(it->second);
      } else {
        displayText = iconPrefix + QString::fromStdString(step->userNickname);
      }
    } else {
      displayText = iconPrefix + QString::fromStdString(step->userNickname);
    }
    if (!step->enabled) {
      displayText = "[DISABLED] " + displayText;
    }

    // Comment rows get a distinct green color for visual separation
    QString textColor;
    if (!step->enabled) {
      textColor = "#999999";
    } else if (isComment) {
      textColor = "#6a9955";  // Green color for comments (like code editor)
    } else {
      textColor = "#d4d4d4";
    }

    QLabel* processLabel = new QLabel(displayText);
    processLabel->setStyleSheet(QString(
      "QLabel { "
      "  background-color: %1; "
      "  color: %2; "
      "  padding: 4px 8px; "
      "  font-family: 'RobotoMono-Regular', 'Roboto Mono', 'Consolas', monospace; "
      "  font-weight: bold; "
      "  font-size: 14px; "
      "  %3"
      "}"
    ).arg(bgColorStr)
     .arg(textColor)
     .arg(step->enabled ? (isComment ? "font-style: italic;" : "") : "text-decoration: line-through;"));
    m_recipeTree->setItemWidget(item, 3, processLabel);

    // Column 4: Run checkbox widget
    QWidget* checkboxWidget = new QWidget();
    checkboxWidget->setStyleSheet(QString("background-color: %1;").arg(bgColorStr));
    QCheckBox* checkbox = new QCheckBox();
    checkbox->setChecked(step->enabled);
    checkbox->setProperty("stepIndex", stepIndex);

    // Add checkmark text that appears when checked
    checkbox->setText(step->enabled ? "✓" : "");
    checkbox->setStyleSheet(
      "QCheckBox { "
      "  font-size: 16px; "
      "  font-weight: bold; "
      "  color: white; "
      "  background: transparent; "
      "}"
    );

    QHBoxLayout* checkLayout = new QHBoxLayout(checkboxWidget);
    checkLayout->addWidget(checkbox);
    checkLayout->setAlignment(Qt::AlignCenter);
    checkLayout->setContentsMargins(0, 0, 0, 0);

    connect(checkbox, &QCheckBox::toggled, this, [this, stepIndex](bool checked) {
      m_recipeManager->setStepEnabled(stepIndex, checked);
      refreshRecipeList();
      // Restore selection
      selectStepByIndex(m_selectedStepIndex);
    });

    m_recipeTree->setItemWidget(item, 4, checkboxWidget);

    // Manage parent stack for nesting
    if (isBlockStart) {
      // This item becomes a parent for subsequent items
      parentStack.push_back(item);
      item->setExpanded(true);  // Auto-expand by default
    }
    else if (isElse) {
      // Else is at the same level as If, so we stay with same parent
      // But we need to pop the If's child container and stay at the If level
      if (!parentStack.empty()) {
        // We're inside an If block. The Else should be a sibling of the If's children
        // Actually, the Else should close the Then branch and open the Else branch
        // For now, keep it simple: Else appears at same level
      }
    }
    else if (isBlockEnd) {
      // Pop from stack
      if (!parentStack.empty()) {
        parentStack.pop_back();
      }
    }

    stepIndex++;
  }

  // Restore selection
  selectStepByIndex(previousSelection);

  m_recipeTree->blockSignals(false);
}

void RecipePage::selectStepByIndex(int index)
{
  if (index < 0 || index >= m_recipeManager->getStepCount()) {
    return;
  }

  // Find the tree item with matching step index
  QTreeWidgetItemIterator it(m_recipeTree);
  while (*it) {
    int itemIndex = (*it)->data(0, Qt::UserRole).toInt();
    if (itemIndex == index) {
      m_recipeTree->setCurrentItem(*it);
      m_recipeTree->scrollToItem(*it);
      return;
    }
    ++it;
  }
}

void RecipePage::updateRecipeFileNameDisplay()
{
  // Get current recipe file path
  std::string currentPath = m_recipeManager->getCurrentFilePath();

  if (currentPath.empty()) {
    // No file loaded yet
    m_recipeFileLabel->setText("No recipe file");
    m_recipeFileLabel->setStyleSheet(
      "font-size: 11px; "
      "color: #888888; "
      "padding: 4px; "
      "background-color: #1e1e1e; "
      "border: 1px solid #3f3f46; "
      "border-radius: 3px;"
    );
  }
  else {
    // Extract just the filename from full path
    QString fullPath = QString::fromStdString(currentPath);
    QFileInfo fileInfo(fullPath);
    QString fileName = fileInfo.fileName();

    m_recipeFileLabel->setText(QString("Recipe: %1").arg(fileName));
    m_recipeFileLabel->setStyleSheet(
      "font-size: 11px; "
      "color: #4ec9b0; "
      "font-weight: bold; "
      "padding: 4px; "
      "background-color: #1e3a32; "
      "border: 1px solid #4ec9b0; "
      "border-radius: 3px;"
    );
  }
}

void RecipePage::updateConfigurationForm()
{
  const ProcessInstance* instance = m_recipeManager->getStep(m_selectedStepIndex);
  if (!instance) {
    clearConfigurationForm();
    return;
  }

  // Safe widget cleanup
  if (m_currentConfigWidget) {
    m_configScrollArea->takeWidget();
    delete m_currentConfigWidget;
    m_currentConfigWidget = nullptr;
  }

  // ============================================================================
  // Create configuration widget using plugin architecture
  // ============================================================================
  auto& registry = ProcessPluginRegistry::Instance();
  if (registry.HasPlugin(instance->processType)) {
    m_currentConfigWidget = new PluginBasedConfigWidget(m_context, instance->processType);
    std::cout << "[RecipePage] Created PluginBasedConfigWidget for: "
              << instance->processType << std::endl;
  }
  else {
    std::cerr << "[RecipePage] No plugin found for process type: " << instance->processType << std::endl;
    clearConfigurationForm();
    return;
  }


  // Connect signals from the custom widget
  connect(m_currentConfigWidget, &ProcessConfigWidget::parameterChanged,
    this, &RecipePage::onParameterChanged);
  connect(m_currentConfigWidget, &ProcessConfigWidget::nicknameChanged,
    this, &RecipePage::onNicknameChanged);
  connect(m_currentConfigWidget, &ProcessConfigWidget::enabledChanged,
    this, &RecipePage::onEnabledChanged);

  // Load data into the widget
  m_currentConfigWidget->loadFromInstance(instance);

  // Set the widget in scroll area
  m_configScrollArea->setWidget(m_currentConfigWidget);

  std::cout << "[RecipePage] Created " << instance->processType
    << " config widget for: " << instance->userNickname << std::endl;
}

void RecipePage::clearConfigurationForm()
{
  // ✅ FIX: Safe widget cleanup
  if (m_currentConfigWidget) {
    m_configScrollArea->takeWidget();  // Remove from scroll area
    delete m_currentConfigWidget;      // Delete immediately
    m_currentConfigWidget = nullptr;
  }

  QWidget* placeholder = new QWidget();
  QVBoxLayout* layout = new QVBoxLayout(placeholder);

  QLabel* label = new QLabel("Select a process step to configure");
  label->setAlignment(Qt::AlignCenter);
  label->setStyleSheet("color: #888888; font-size: 14px; padding: 50px; background-color: #1e1e1e;");

  layout->addWidget(label);
  m_configScrollArea->setWidget(placeholder);
}

// ============================================================================
// SEQUENCE ENGINE SETUP
// ============================================================================

void RecipePage::setupSequenceEngine()
{
  std::cout << "[RecipePage] Setting up SequenceEngine..." << std::endl;

  m_sequenceEngine = new SequenceEngine(m_context);
  m_sequenceEngine->SetLogger(m_context->logManager);
  m_sequenceEngine->BuildDeviceRegistry();

  // Set blocking prompt callback
  m_sequenceEngine->SetUserPromptCallback(
    [this](const std::string& title, const std::string& message) -> bool {
    bool result = false;

    // Check if we're already on the UI thread
    if (QThread::currentThread() == QApplication::instance()->thread()) {
      // Already on UI thread - call directly to avoid deadlock
      result = SequencePromptDialog::ShowPrompt(
        QString::fromStdString(title),
        QString::fromStdString(message),
        this
      );
    } else {
      // Called from worker thread - use BlockingQueuedConnection
      QMetaObject::invokeMethod(this, [this, title, message, &result]() {
        result = SequencePromptDialog::ShowPrompt(
          QString::fromStdString(title),
          QString::fromStdString(message),
          this
        );
      }, Qt::BlockingQueuedConnection);
    }

    return result;
  }
  );

  // Set non-blocking notification callback
  m_sequenceEngine->SetUserNotificationCallback(
    [this](const std::string& title, const std::string& message) {
    QMetaObject::invokeMethod(this, [this, title, message]() {
      SequencePromptDialog::ShowNotification(
        QString::fromStdString(title),
        QString::fromStdString(message),
        this
      );
    }, Qt::QueuedConnection);  // Non-blocking!
  }
  );

  // Set text input callback - uses integrated UserInputWidget (thread-aware)
  m_sequenceEngine->SetUserInputCallback(
    [this](const std::string& title, const std::string& prompt, const std::string& defaultValue) -> std::pair<bool, std::string> {
    std::pair<bool, std::string> result;

    // Check if we're already on the UI thread
    if (QThread::currentThread() == QApplication::instance()->thread()) {
      // Already on UI thread - call directly to avoid deadlock
      // Take the current widget out of the scroll area
      QWidget* oldWidget = m_configScrollArea->takeWidget();

      // Show the user input widget in the scroll area
      m_configScrollArea->setWidget(m_userInputWidget);
      m_userInputWidget->showInput(
        QString::fromStdString(title),
        QString::fromStdString(prompt),
        QString::fromStdString(defaultValue)
      );

      std::cout << "[RecipePage] Showing user input widget (UI thread)" << std::endl;

      // Process pending events to ensure widget is displayed before blocking
      QApplication::processEvents();

      // Block until user responds (event loop pattern)
      QEventLoop loop;
      m_promptEventLoop = &loop;
      loop.exec();
      m_promptEventLoop = nullptr;

      result = std::make_pair(m_userInputAccepted, m_userInputText.toStdString());
    } else {
      // Called from worker thread - use BlockingQueuedConnection
      QMetaObject::invokeMethod(this, [this, title, prompt, defaultValue, &result]() {
        // Take the current widget out of the scroll area
        QWidget* oldWidget = m_configScrollArea->takeWidget();

        // Show the user input widget in the scroll area
        m_configScrollArea->setWidget(m_userInputWidget);
        m_userInputWidget->showInput(
          QString::fromStdString(title),
          QString::fromStdString(prompt),
          QString::fromStdString(defaultValue)
        );

        std::cout << "[RecipePage] Showing user input widget (worker thread)" << std::endl;

        // Block until user responds (event loop pattern)
        QEventLoop loop;
        m_promptEventLoop = &loop;
        loop.exec();
        m_promptEventLoop = nullptr;

        result = std::make_pair(m_userInputAccepted, m_userInputText.toStdString());
      }, Qt::BlockingQueuedConnection);
    }

    return result;
  }
  );

  // Set manual align point callback - uses integrated ManualAlignPointWidget (thread-aware)
  m_sequenceEngine->SetManualAlignPointCallback(
    [this](const std::string& pointName, const std::string& message,
           bool enableX, bool enableY, bool enableZ) -> bool {
    bool result = false;

    // Check if we're already on the UI thread
    if (QThread::currentThread() == QApplication::instance()->thread()) {
      // Already on UI thread - call directly to avoid deadlock
      // Take the current widget out of the scroll area
      QWidget* oldWidget = m_configScrollArea->takeWidget();

      // Show the manual align point widget in the scroll area
      m_configScrollArea->setWidget(m_manualAlignPointWidget);
      m_manualAlignPointWidget->showAlignment(
        QString::fromStdString(pointName),
        QString::fromStdString(message),
        enableX, enableY, enableZ
      );

      std::cout << "[RecipePage] Showing manual align point widget (UI thread)" << std::endl;

      // Process pending events to ensure widget is displayed before blocking
      QApplication::processEvents();

      // Block until user responds (event loop pattern)
      QEventLoop loop;
      m_promptEventLoop = &loop;
      loop.exec();
      m_promptEventLoop = nullptr;

      result = m_manualAlignPointResponse;
    } else {
      // Called from worker thread - use BlockingQueuedConnection
      QMetaObject::invokeMethod(this, [this, pointName, message, enableX, enableY, enableZ, &result]() {
        // Take the current widget out of the scroll area
        QWidget* oldWidget = m_configScrollArea->takeWidget();

        // Show the manual align point widget in the scroll area
        m_configScrollArea->setWidget(m_manualAlignPointWidget);
        m_manualAlignPointWidget->showAlignment(
          QString::fromStdString(pointName),
          QString::fromStdString(message),
          enableX, enableY, enableZ
        );

        std::cout << "[RecipePage] Showing manual align point widget (worker thread)" << std::endl;

        // Block until user responds (event loop pattern)
        QEventLoop loop;
        m_promptEventLoop = &loop;
        loop.exec();
        m_promptEventLoop = nullptr;

        result = m_manualAlignPointResponse;
      }, Qt::BlockingQueuedConnection);
    }

    return result;
  }
  );

  // Set manual adjust callback - uses integrated ManualAdjustWidget (thread-aware)
  m_sequenceEngine->SetManualAdjustCallback(
    [this](const std::string& message, const std::string& deviceName,
           double stepSizeUm, bool enableX, bool enableY, bool enableZ) -> bool {
    bool result = false;

    // Check if we're already on the UI thread
    if (QThread::currentThread() == QApplication::instance()->thread()) {
      // Already on UI thread - call directly to avoid deadlock
      // Take the current widget out of the scroll area
      QWidget* oldWidget = m_configScrollArea->takeWidget();

      // Show the manual adjust widget in the scroll area
      m_configScrollArea->setWidget(m_manualAdjustWidget);
      m_manualAdjustWidget->showAdjustment(
        QString::fromStdString(message),
        QString::fromStdString(deviceName),
        stepSizeUm,
        enableX, enableY, enableZ
      );

      std::cout << "[RecipePage] Showing manual adjust widget (UI thread)" << std::endl;

      // Process pending events to ensure widget is displayed before blocking
      QApplication::processEvents();

      // Block until user responds (event loop pattern)
      QEventLoop loop;
      m_promptEventLoop = &loop;
      loop.exec();
      m_promptEventLoop = nullptr;

      result = m_manualAdjustResponse;
    } else {
      // Called from worker thread - use BlockingQueuedConnection
      QMetaObject::invokeMethod(this, [this, message, deviceName, stepSizeUm, enableX, enableY, enableZ, &result]() {
        // Take the current widget out of the scroll area
        QWidget* oldWidget = m_configScrollArea->takeWidget();

        // Show the manual adjust widget in the scroll area
        m_configScrollArea->setWidget(m_manualAdjustWidget);
        m_manualAdjustWidget->showAdjustment(
          QString::fromStdString(message),
          QString::fromStdString(deviceName),
          stepSizeUm,
          enableX, enableY, enableZ
        );

        std::cout << "[RecipePage] Showing manual adjust widget (worker thread)" << std::endl;

        // Block until user responds (event loop pattern)
        QEventLoop loop;
        m_promptEventLoop = &loop;
        loop.exec();
        m_promptEventLoop = nullptr;

        result = m_manualAdjustResponse;
      }, Qt::BlockingQueuedConnection);
    }

    return result;
  }
  );

  std::cout << "[RecipePage] SequenceEngine ready" << std::endl;
}

// ============================================================================
// RECIPE EXECUTION
// ============================================================================

void RecipePage::onRunRecipe()
{
  // Validate recipe first
  std::vector<std::string> errors;
  if (!m_recipeManager->validateRecipe(errors)) {
    QString errorMsg = "Recipe validation failed:\n\n";
    for (const auto& err : errors) {
      errorMsg += "• " + QString::fromStdString(err) + "\n";
    }
    QMessageBox::critical(this, "Validation Error", errorMsg);
    return;
  }

  // Check if already running
  if (m_recipeWorker && m_recipeWorker->isRunning()) {
    QMessageBox::warning(this, "Recipe Running",
      "A recipe is already executing. Please wait for it to complete.");
    return;
  }

  QString recipeName = QString::fromStdString(m_recipeManager->getRecipeName());
  int stepCount = m_recipeManager->getStepCount();

  // ✅ SUPPRESSED: Confirmation popup before execution
  // QMessageBox::StandardButton reply = QMessageBox::question(
  //   this, "Run Recipe",
  //   QString("Execute recipe '%1' with %2 step(s)?").arg(recipeName).arg(stepCount),
  //   QMessageBox::Yes | QMessageBox::No
  // );
  //
  // if (reply != QMessageBox::Yes) return;

  // ✅ STEP 1: Save recipe to file (if not saved or modified)
  std::string recipeFilePath = m_recipeManager->getCurrentFilePath();

  if (recipeFilePath.empty()) {
    // Recipe not saved yet - prompt to save
    QMessageBox::StandardButton saveReply = QMessageBox::question(
      this, "Save Recipe",
      "Recipe must be saved before execution. Save now?",
      QMessageBox::Yes | QMessageBox::No
    );

    if (saveReply != QMessageBox::Yes) {
      return;
    }

    // Trigger save dialog
    onSaveRecipe();

    // Check if saved successfully
    recipeFilePath = m_recipeManager->getCurrentFilePath();
    if (recipeFilePath.empty()) {
      QMessageBox::warning(this, "Save Required",
        "Recipe must be saved before execution.");
      return;
    }
  }
  else {
    // Recipe already saved - save current state
    if (!m_recipeManager->saveRecipe(recipeFilePath)) {
      QMessageBox::critical(this, "Save Error",
        "Failed to save recipe before execution.");
      return;
    }
  }

  std::cout << "[RecipePage] Starting recipe execution: "
    << recipeName.toStdString() << std::endl;
  std::cout << "[RecipePage] Recipe file: " << recipeFilePath << std::endl;

  // Switch to progress panel
  showProgressPanel();

  // Disable run button while executing
  m_runRecipeButton->setEnabled(false);
  m_runRecipeButton->setText("⏳ Recipe Running...");

  // ✅ STEP 2: Create RecipeWorker with new signature
  m_recipeWorker = new RecipeWorker(
    m_sequenceEngine,
    m_processFactory,
    m_context->productPositionManager,
    m_context->motionManager,
    recipeFilePath,  // ✅ Pass file path instead of RecipeManager
    this
  );

  // ✅ STEP 3: Set UserPrompt callback (using signal + QEventLoop pattern)
  connect(this, &RecipePage::showUserPrompt,
    this, &RecipePage::onShowUserPrompt);

  m_recipeWorker->setUserPromptCallback(
    [this](const std::string& title, const std::string& message) -> bool {
    // Emit signal to UI thread to show user prompt in execution area
    emit showUserPrompt(QString::fromStdString(title), QString::fromStdString(message), true);

    // Block until user responds using event loop
    QEventLoop loop;
    m_promptEventLoop = &loop;
    loop.exec();
    m_promptEventLoop = nullptr;

    return m_userPromptResponse;
  }
  );

  // ✅ STEP 4: Set ManualAdjust callback
  connect(this, &RecipePage::showManualAdjust,
    this, &RecipePage::onShowManualAdjust);

  m_recipeWorker->setManualAdjustCallback(
    [this](const std::string& message, const std::string& deviceName, double stepSizeUm,
           bool enableX, bool enableY, bool enableZ) -> bool {
    // Emit signal to UI thread to show manual adjust widget
    emit showManualAdjust(QString::fromStdString(message), QString::fromStdString(deviceName),
                          stepSizeUm, enableX, enableY, enableZ);

    // Block until user responds using event loop
    QEventLoop loop;
    m_promptEventLoop = &loop;
    loop.exec();
    m_promptEventLoop = nullptr;

    return m_manualAdjustResponse;
  }
  );

  // ✅ STEP 5: Set UserInput callback (for AskForInput commands like DUT serial number)
  m_recipeWorker->setUserInputCallback(
    [this](const std::string& title, const std::string& prompt, const std::string& defaultValue) -> std::pair<bool, std::string> {
    std::pair<bool, std::string> result;

    // Check if we're already on the UI thread
    if (QThread::currentThread() == QApplication::instance()->thread()) {
      // Already on UI thread - call directly to avoid deadlock
      // Take the current widget out of the scroll area
      QWidget* oldWidget = m_configScrollArea->takeWidget();

      // Show the user input widget in the scroll area
      m_configScrollArea->setWidget(m_userInputWidget);
      m_userInputWidget->showInput(
        QString::fromStdString(title),
        QString::fromStdString(prompt),
        QString::fromStdString(defaultValue)
      );

      std::cout << "[RecipePage] Showing user input widget for RecipeWorker (UI thread)" << std::endl;

      // Process pending events to ensure widget is displayed before blocking
      QApplication::processEvents();

      // Block until user responds (event loop pattern)
      QEventLoop loop;
      m_promptEventLoop = &loop;
      loop.exec();
      m_promptEventLoop = nullptr;

      result = std::make_pair(m_userInputAccepted, m_userInputText.toStdString());
    } else {
      // Called from worker thread - use BlockingQueuedConnection
      QMetaObject::invokeMethod(this, [this, title, prompt, defaultValue, &result]() {
        // Take the current widget out of the scroll area
        QWidget* oldWidget = m_configScrollArea->takeWidget();

        // Show the user input widget in the scroll area
        m_configScrollArea->setWidget(m_userInputWidget);
        m_userInputWidget->showInput(
          QString::fromStdString(title),
          QString::fromStdString(prompt),
          QString::fromStdString(defaultValue)
        );

        std::cout << "[RecipePage] Showing user input widget for RecipeWorker (worker thread)" << std::endl;

        // Block until user responds (event loop pattern)
        QEventLoop loop;
        m_promptEventLoop = &loop;
        loop.exec();
        m_promptEventLoop = nullptr;

        result = std::make_pair(m_userInputAccepted, m_userInputText.toStdString());
      }, Qt::BlockingQueuedConnection);
    }

    return result;
  }
  );

  // Connect RAW DATA signals to UI interpreters
  connect(m_recipeWorker, &RecipeWorker::executionStarted,
    this, &RecipePage::onExecutionStarted);
  connect(m_recipeWorker, &RecipeWorker::stepBuildStarted,
    this, &RecipePage::onStepBuildStarted);
  connect(m_recipeWorker, &RecipeWorker::stepBuildCompleted,
    this, &RecipePage::onStepBuildCompleted);
  connect(m_recipeWorker, &RecipeWorker::sequenceExecutionStarted,
    this, &RecipePage::onSequenceExecutionStarted);

  connect(m_recipeWorker, &RecipeWorker::executionCompleted,
    this, &RecipePage::onExecutionCompleted);
  connect(m_recipeWorker, &RecipeWorker::executionStopped,
    this, &RecipePage::onExecutionStopped);
  connect(m_recipeWorker, &RecipeWorker::logMessage,
    this, &RecipePage::onWorkerLogMessage);
  connect(m_recipeWorker, &RecipeWorker::waitProgress,
    this, &RecipePage::onWaitProgress);

  // Step execution events (pub-sub from SequenceEngine)
  connect(m_recipeWorker, &RecipeWorker::stepExecutionStarted,
    this, &RecipePage::onStepExecutionStarted);
  connect(m_recipeWorker, &RecipeWorker::stepExecutionCompleted,
    this, &RecipePage::onStepExecutionCompleted);

  // Breakpoint/pause events
  connect(m_recipeWorker, &RecipeWorker::executionPaused,
    this, &RecipePage::onExecutionPaused);

  // Build breakpoints vector from ProcessInstance breakpoint flags
  std::vector<bool> breakpoints;
  for (int i = 0; i < m_recipeManager->getStepCount(); ++i) {
    ProcessInstance* step = m_recipeManager->getStep(i);
    if (step && step->enabled) {
      breakpoints.push_back(step->breakpoint);
    }
  }
  m_recipeWorker->setBreakpoints(breakpoints);

  connect(m_recipeWorker, &RecipeWorker::finished,
    m_recipeWorker, &QObject::deleteLater);

  // Start execution
  m_recipeWorker->start();
}

// ✅ NEW: Run only the selected step (for quick testing)
void RecipePage::onRunSelectedStep()
{
  // Check if a step is selected
  QList<QTreeWidgetItem*> selectedItems = m_recipeTree->selectedItems();
  if (selectedItems.isEmpty()) {
    QMessageBox::warning(this, "No Step Selected",
      "Please select a step to run.");
    return;
  }

  // Get the selected step index
  QTreeWidgetItem* selectedItem = selectedItems.first();
  int stepIndex = m_recipeTree->indexOfTopLevelItem(selectedItem);

  if (stepIndex < 0 || stepIndex >= m_recipeManager->getStepCount()) {
    QMessageBox::warning(this, "Invalid Selection",
      "Selected step index is invalid.");
    return;
  }

  // Get the step
  ProcessInstance* step = m_recipeManager->getStep(stepIndex);
  if (!step) {
    QMessageBox::warning(this, "Error", "Failed to retrieve step.");
    return;
  }

  // Check if step is enabled
  if (!step->enabled) {
    QMessageBox::warning(this, "Step Disabled",
      QString("Step '%1' is disabled. Enable it first to run.")
      .arg(QString::fromStdString(step->userNickname)));
    return;
  }

  // Check if already running
  if (m_recipeWorker && m_recipeWorker->isRunning()) {
    QMessageBox::warning(this, "Execution Running",
      "A recipe is already executing. Please wait for it to complete.");
    return;
  }

  // Get step info for display
  QString stepNickname = QString::fromStdString(step->userNickname);
  QString stepType = QString::fromStdString(step->processType);

  std::cout << "[RecipePage] Running single step: " << stepNickname.toStdString()
            << " (type: " << stepType.toStdString() << ")" << std::endl;

  // ✅ NEW: Create a temporary recipe with just this one step
  RecipeManager tempRecipe;
  tempRecipe.setRecipeName("TempSingleStep");

  // Copy the selected step to temporary recipe using insertStepAt
  tempRecipe.insertStepAt(
    0,                           // index
    step->processType,           // process type
    step->userNickname,          // nickname
    true,                        // enabled
    step->parameters             // parameters
  );

  // Save to temporary file (use current directory to avoid path issues)
  std::string tempRecipePath = "temp_single_step.json";
  if (!tempRecipe.saveRecipe(tempRecipePath)) {
    QMessageBox::critical(this, "Error",
      "Failed to create temporary recipe file for single step execution.");
    return;
  }

  std::cout << "[RecipePage] Created temporary recipe: " << tempRecipePath << std::endl;

  // Switch to progress panel
  showProgressPanel();

  // Disable buttons during execution
  m_runRecipeButton->setEnabled(false);
  m_runSelectedStepButton->setEnabled(false);
  m_runSelectedStepButton->setText("⏳ Running Step...");

  // ✅ NEW: Use RecipeWorker for background thread execution (same as Run Recipe)
  m_recipeWorker = new RecipeWorker(
    m_sequenceEngine,
    m_processFactory,
    m_context->productPositionManager,
    m_context->motionManager,
    tempRecipePath,
    this
  );

  // Set external context if available (for sharing context between steps)
  if (m_currentContext) {
    m_recipeWorker->setExternalContext(m_currentContext);
  }

  // Connect signals for UI updates (same as onRunRecipe and onStepButtonClicked)
  connect(m_recipeWorker, &RecipeWorker::executionStarted,
    this, &RecipePage::onExecutionStarted);
  connect(m_recipeWorker, &RecipeWorker::stepBuildStarted,
    this, &RecipePage::onStepBuildStarted);
  connect(m_recipeWorker, &RecipeWorker::stepBuildCompleted,
    this, &RecipePage::onStepBuildCompleted);
  connect(m_recipeWorker, &RecipeWorker::sequenceExecutionStarted,
    this, &RecipePage::onSequenceExecutionStarted);
  connect(m_recipeWorker, &RecipeWorker::executionCompleted,
    this, &RecipePage::onExecutionCompleted);
  connect(m_recipeWorker, &RecipeWorker::executionStopped,
    this, &RecipePage::onExecutionStopped);
  connect(m_recipeWorker, &RecipeWorker::logMessage,
    this, &RecipePage::onWorkerLogMessage);
  connect(m_recipeWorker, &RecipeWorker::waitProgress,
    this, &RecipePage::onWaitProgress);
  connect(m_recipeWorker, &RecipeWorker::stepExecutionStarted,
    this, &RecipePage::onStepExecutionStarted);
  connect(m_recipeWorker, &RecipeWorker::stepExecutionCompleted,
    this, &RecipePage::onStepExecutionCompleted);
  connect(m_recipeWorker, &RecipeWorker::finished,
    m_recipeWorker, &QObject::deleteLater);

  // Set up callbacks (SequenceEngine callbacks persist, but worker-specific ones need to be set)
  connect(this, &RecipePage::showUserPrompt,
    this, &RecipePage::onShowUserPrompt);

  m_recipeWorker->setUserPromptCallback(
    [this](const std::string& title, const std::string& message) -> bool {
    emit showUserPrompt(QString::fromStdString(title), QString::fromStdString(message), true);
    QEventLoop loop;
    m_promptEventLoop = &loop;
    loop.exec();
    m_promptEventLoop = nullptr;
    return m_userPromptResponse;
  });

  // ✅ Set ManualAdjust callback (same as Run Recipe)
  connect(this, &RecipePage::showManualAdjust,
    this, &RecipePage::onShowManualAdjust);

  m_recipeWorker->setManualAdjustCallback(
    [this](const std::string& message, const std::string& deviceName, double stepSizeUm,
           bool enableX, bool enableY, bool enableZ) -> bool {
    // Emit signal to UI thread to show manual adjust widget
    emit showManualAdjust(QString::fromStdString(message), QString::fromStdString(deviceName),
                          stepSizeUm, enableX, enableY, enableZ);

    // Block until user responds using event loop
    QEventLoop loop;
    m_promptEventLoop = &loop;
    loop.exec();
    m_promptEventLoop = nullptr;

    return m_manualAdjustResponse;
  });

  // ✅ Set UserInput callback (for AskForInput commands like DUT serial number)
  m_recipeWorker->setUserInputCallback(
    [this](const std::string& title, const std::string& prompt, const std::string& defaultValue) -> std::pair<bool, std::string> {
    std::pair<bool, std::string> result;

    // Check if we're already on the UI thread
    if (QThread::currentThread() == QApplication::instance()->thread()) {
      // Already on UI thread - call directly to avoid deadlock
      // Take the current widget out of the scroll area
      QWidget* oldWidget = m_configScrollArea->takeWidget();

      // Show the user input widget in the scroll area
      m_configScrollArea->setWidget(m_userInputWidget);
      m_userInputWidget->showInput(
        QString::fromStdString(title),
        QString::fromStdString(prompt),
        QString::fromStdString(defaultValue)
      );

      std::cout << "[RecipePage] Showing user input widget for RecipeWorker (UI thread)" << std::endl;

      // Process pending events to ensure widget is displayed before blocking
      QApplication::processEvents();

      // Block until user responds (event loop pattern)
      QEventLoop loop;
      m_promptEventLoop = &loop;
      loop.exec();
      m_promptEventLoop = nullptr;

      result = std::make_pair(m_userInputAccepted, m_userInputText.toStdString());
    } else {
      // Called from worker thread - use BlockingQueuedConnection
      QMetaObject::invokeMethod(this, [this, title, prompt, defaultValue, &result]() {
        // Take the current widget out of the scroll area
        QWidget* oldWidget = m_configScrollArea->takeWidget();

        // Show the user input widget in the scroll area
        m_configScrollArea->setWidget(m_userInputWidget);
        m_userInputWidget->showInput(
          QString::fromStdString(title),
          QString::fromStdString(prompt),
          QString::fromStdString(defaultValue)
        );

        std::cout << "[RecipePage] Showing user input widget for RecipeWorker (worker thread)" << std::endl;

        // Block until user responds (event loop pattern)
        QEventLoop loop;
        m_promptEventLoop = &loop;
        loop.exec();
        m_promptEventLoop = nullptr;

        result = std::make_pair(m_userInputAccepted, m_userInputText.toStdString());
      }, Qt::BlockingQueuedConnection);
    }

    return result;
  });

  // Start background thread execution ✅
  m_recipeWorker->start();

  std::cout << "[RecipePage] Started RecipeWorker for single step execution (non-blocking)" << std::endl;
}

// ============================================================================
// ✅ STEP-BY-STEP EXECUTION
// ============================================================================

void RecipePage::onStepButtonClicked()
{
  // Check if already running - if paused, this acts as "step to next"
  if (m_recipeWorker && m_recipeWorker->isRunning()) {
    // Recipe is running and paused - step to next
    std::cout << "[RecipePage] Step button clicked - continuing to next step" << std::endl;
    m_recipeWorker->requestContinue();
    m_continueButton->setEnabled(false);  // Disable until paused again
    return;
  }

  // Start recipe in step-by-step mode
  // Validate recipe first
  std::vector<std::string> errors;
  if (!m_recipeManager->validateRecipe(errors)) {
    QString errorMsg = "Recipe validation failed:\n\n";
    for (const auto& err : errors) {
      errorMsg += "• " + QString::fromStdString(err) + "\n";
    }
    QMessageBox::critical(this, "Validation Error", errorMsg);
    return;
  }

  // Save recipe if needed (same logic as onRunRecipe)
  std::string recipeFilePath = m_recipeManager->getCurrentFilePath();
  if (recipeFilePath.empty()) {
    QMessageBox::StandardButton saveReply = QMessageBox::question(
      this, "Save Recipe",
      "Recipe must be saved before execution. Save now?",
      QMessageBox::Yes | QMessageBox::No
    );
    if (saveReply != QMessageBox::Yes) return;
    onSaveRecipe();
    recipeFilePath = m_recipeManager->getCurrentFilePath();
    if (recipeFilePath.empty()) {
      QMessageBox::warning(this, "Save Required", "Recipe must be saved before execution.");
      return;
    }
  } else {
    if (!m_recipeManager->saveRecipe(recipeFilePath)) {
      QMessageBox::critical(this, "Save Error", "Failed to save recipe before execution.");
      return;
    }
  }

  std::cout << "[RecipePage] Starting step-by-step execution" << std::endl;

  // Switch to progress panel
  showProgressPanel();

  // Update button states
  m_runRecipeButton->setEnabled(false);
  m_runRecipeButton->setText("⏳ Step Mode...");
  m_stepButton->setText("Next Step");

  // Create RecipeWorker
  m_recipeWorker = new RecipeWorker(
    m_sequenceEngine,
    m_processFactory,
    m_context->productPositionManager,
    m_context->motionManager,
    recipeFilePath,
    this
  );

  // Enable step-by-step mode
  m_recipeWorker->setStepByStepMode(true);

  // Build breakpoints vector from ProcessInstance breakpoint flags
  std::vector<bool> breakpoints;
  for (int i = 0; i < m_recipeManager->getStepCount(); ++i) {
    ProcessInstance* step = m_recipeManager->getStep(i);
    if (step && step->enabled) {
      breakpoints.push_back(step->breakpoint);
    }
  }
  m_recipeWorker->setBreakpoints(breakpoints);

  // Connect signals (same as onRunRecipe)
  connect(m_recipeWorker, &RecipeWorker::executionStarted,
    this, &RecipePage::onExecutionStarted);
  connect(m_recipeWorker, &RecipeWorker::stepBuildStarted,
    this, &RecipePage::onStepBuildStarted);
  connect(m_recipeWorker, &RecipeWorker::stepBuildCompleted,
    this, &RecipePage::onStepBuildCompleted);
  connect(m_recipeWorker, &RecipeWorker::sequenceExecutionStarted,
    this, &RecipePage::onSequenceExecutionStarted);
  connect(m_recipeWorker, &RecipeWorker::executionCompleted,
    this, &RecipePage::onExecutionCompleted);
  connect(m_recipeWorker, &RecipeWorker::executionStopped,
    this, &RecipePage::onExecutionStopped);
  connect(m_recipeWorker, &RecipeWorker::logMessage,
    this, &RecipePage::onWorkerLogMessage);
  connect(m_recipeWorker, &RecipeWorker::waitProgress,
    this, &RecipePage::onWaitProgress);
  connect(m_recipeWorker, &RecipeWorker::stepExecutionStarted,
    this, &RecipePage::onStepExecutionStarted);
  connect(m_recipeWorker, &RecipeWorker::stepExecutionCompleted,
    this, &RecipePage::onStepExecutionCompleted);
  connect(m_recipeWorker, &RecipeWorker::executionPaused,
    this, &RecipePage::onExecutionPaused);
  connect(m_recipeWorker, &RecipeWorker::finished,
    m_recipeWorker, &QObject::deleteLater);

  // Start execution
  m_recipeWorker->start();
}

void RecipePage::onContinueButtonClicked()
{
  if (!m_recipeWorker || !m_recipeWorker->isRunning()) {
    std::cout << "[RecipePage] Continue clicked but no running worker" << std::endl;
    return;
  }

  std::cout << "[RecipePage] Continue button clicked - running to end" << std::endl;

  // Disable step-by-step mode and continue running to end (or next breakpoint)
  m_recipeWorker->requestRunToEnd();

  // Update button states
  m_continueButton->setEnabled(false);
  m_stepButton->setText("Step");
  m_runRecipeButton->setText("⏳ Running...");
}

void RecipePage::onExecutionPaused(int stepIndex, QString stepName, bool isBreakpoint)
{
  std::cout << "[RecipePage] Execution paused at step " << (stepIndex + 1)
            << ": " << stepName.toStdString()
            << (isBreakpoint ? " (breakpoint)" : " (step-by-step)") << std::endl;

  // Enable continue button
  m_continueButton->setEnabled(true);

  // Update progress widget status
  if (m_progressWidget) {
    QString statusMsg = isBreakpoint ?
      QString("⏸️ Paused at breakpoint: %1").arg(stepName) :
      QString("⏸️ Step %1: %2").arg(stepIndex + 1).arg(stepName);
    m_progressWidget->setStatusText(statusMsg);
  }

  // Update step button text
  m_stepButton->setText("Next Step");
}

// Legacy slots (kept for compatibility but not used with new system)
void RecipePage::onRecipeProgress(const QString& message)
{
  std::cout << "[RecipePage] Progress: " << message.toStdString() << std::endl;
}

void RecipePage::onRecipeFinished(bool success, const QString& message)
{
  std::cout << "[RecipePage] Recipe finished: " << message.toStdString() << std::endl;
  m_recipeWorker = nullptr;
}

bool RecipePage::executeRecipe()
{
  if (!m_sequenceEngine) {
    std::cerr << "[RecipePage] SequenceEngine not initialized!" << std::endl;
    return false;
  }

  std::string recipeName = m_recipeManager->getRecipeName();
  SequenceBuilder* sequence = m_sequenceEngine->CreateSequence(recipeName);

  buildSequenceFromRecipe(sequence);

  // Create execution context for value capture and conditionals
  auto context = std::make_unique<SequenceContext>();
  bool success = m_sequenceEngine->ExecuteSequence(sequence, context.get());

  // Log execution context summary
  if (context && m_context->logManager) {
    auto summary = context->GetSummary();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      summary.endTime - summary.startTime).count();

    m_context->logManager->Info("========================================", "RecipePage");
    m_context->logManager->Info("Execution Context Summary:", "RecipePage");
    m_context->logManager->Info("  Duration: " + std::to_string(duration) + " ms", "RecipePage");
    m_context->logManager->Info("  Steps Executed: " + std::to_string(summary.totalSteps), "RecipePage");
    m_context->logManager->Info("  Values Captured: " + std::to_string(summary.capturedValues.size()), "RecipePage");

    // Log all captured values
    if (!summary.capturedValues.empty()) {
      m_context->logManager->Info("  Captured Values:", "RecipePage");
      for (const auto& pair : summary.capturedValues) {
        const std::string& key = pair.first;
        const SequenceContext::Value& value = pair.second;

        if (value.type == SequenceContext::ValueType::NUMERIC) {
          m_context->logManager->Info("    - " + key + " = " + std::to_string(value.numericValue), "RecipePage");
        } else if (value.type == SequenceContext::ValueType::STRING) {
          m_context->logManager->Info("    - " + key + " = \"" + value.stringValue + "\"", "RecipePage");
        } else if (value.type == SequenceContext::ValueType::BOOLEAN) {
          m_context->logManager->Info("    - " + key + " = " + (value.booleanValue ? "true" : "false"), "RecipePage");
        }
      }
    }

    // Save context to JSON file for debugging (in logs folder)
    std::string timestamp = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::string filename = "logs/context_" + recipeName + "_" + timestamp + ".json";
    context->SaveToFile(filename);
    m_context->logManager->Info("  Context saved to: " + filename, "RecipePage");

    m_context->logManager->Info("========================================", "RecipePage");
  }

  delete sequence;

  return success;
}

void RecipePage::buildSequenceFromRecipe(SequenceBuilder* sequence)
{
  int enabledCount = 0;
  int totalCount = 0;

  for (const auto& step : m_recipeManager->getAllSteps()) {
    totalCount++;

    if (!step->enabled) {
      std::cout << "[RecipePage] Skipping disabled step: "
        << step->userNickname << std::endl;
      continue;
    }

    enabledCount++;

    std::cout << "[RecipePage] Building step " << enabledCount << ": "
      << step->userNickname << " (Type: " << step->processType << ")" << std::endl;

    std::string errorMsg;
    if (!m_processFactory->BuildStep(sequence, step.get(), errorMsg)) {
      std::cerr << "[RecipePage] " << errorMsg << std::endl;
      m_context->logManager->Error(errorMsg, "RecipePage");
      throw std::runtime_error(errorMsg);
    }
  }

  std::cout << "[RecipePage] Sequence built successfully: "
    << enabledCount << " enabled steps out of "
    << totalCount << " total steps" << std::endl;
}

// ============================================================================
// ✅ UI INTERPRETERS - Convert raw data to UI updates
// ============================================================================

void RecipePage::onExecutionStarted(int totalEnabledSteps)
{
  if (m_progressWidget) {
    m_progressWidget->setTotalSteps(totalEnabledSteps);
    m_progressWidget->setCurrentStep(0, "Preparing...");  // Start at 0
    m_progressWidget->setStatusText(QString("Building sequence with %1 step(s)...")
      .arg(totalEnabledSteps));
  }
  std::cout << "[RecipePage] Execution started: " << totalEnabledSteps << " steps" << std::endl;
}


void RecipePage::onStepBuildStarted(int stepIndex, QString stepName, QString stepType)
{
  // ✅ During build phase: Don't update progress bar yet, just status
  if (m_progressWidget) {
    m_progressWidget->setStatusText(QString("Building: %1").arg(stepName));
  }

  std::cout << "[RecipePage] Building step " << (stepIndex + 1)
    << ": " << stepName.toStdString()
    << " (" << stepType.toStdString() << ")" << std::endl;
}


void RecipePage::onStepBuildCompleted(int stepIndex)
{
  std::cout << "[RecipePage] Step " << (stepIndex + 1) << " built" << std::endl;
}


void RecipePage::onSequenceExecutionStarted()
{
  if (m_progressWidget) {
    // ✅ Now show that execution is happening (indeterminate progress)
    m_progressWidget->setCurrentStep(1, "Executing all steps...");
    m_progressWidget->setStatusText("Running sequence - please wait...");
  }
  std::cout << "[RecipePage] Sequence execution started" << std::endl;
}






void RecipePage::onExecutionCompleted(bool success, QString message)
{
  std::cout << "[RecipePage] Execution completed: " << message.toStdString() << std::endl;

  // ✅ Clear step highlighting when execution completes
  clearExecutingStepHighlight();

  // ✅ Safer pointer check - verify widget still exists and is valid
  if (m_progressWidget && !m_progressWidget->isHidden()) {
    // Only access if widget is still the current widget
    if (m_configScrollArea->widget() == m_progressWidget) {
      m_progressWidget->stopTimer();

      // Show final state
      int totalSteps = m_progressWidget->property("totalSteps").toInt();
      m_progressWidget->setCurrentStep(totalSteps, "Complete");
      m_progressWidget->setStatusText(success ? "✓ Completed!" : "✗ Failed");

      // Show completion hint and disable stop button
      m_progressWidget->showCompletionHint();
    }
  }

  m_runRecipeButton->setEnabled(true);
  m_runRecipeButton->setText("▶ Run Recipe");

  // ✅ Reset "Run Selected Step" button
  m_runSelectedStepButton->setEnabled(true);
  m_runSelectedStepButton->setText("▶ Run Selected Step");

  // Reset step-by-step button states
  m_stepButton->setText("Step");
  m_continueButton->setEnabled(false);

  // ✅ SUPPRESSED: Result popup after execution
  // Show result dialog
  // if (success) {
  //   QMessageBox::information(this, "Success", message);
  //   m_context->logManager->Info(message.toStdString(), "RecipePage");
  // }
  // else {
  //   QMessageBox::critical(this, "Execution Error", message);
  //   m_context->logManager->Error(message.toStdString(), "RecipePage");
  // }

  // ✅ Log to LogManager instead (no popup)
  if (success) {
    m_context->logManager->Info(message.toStdString(), "RecipePage");
  }
  else {
    m_context->logManager->Error(message.toStdString(), "RecipePage");
  }

  // ✅ Update vision result image if available
  updateVisionResultImage();

  // ✅ NEW: Auto-dismiss progress panel after 3 seconds
  QTimer::singleShot(3000, this, [this]() {
    // ✅ FIX: Only dismiss if no execution is running (prevents dismissing new execution)
    if (m_recipeWorker && m_recipeWorker->isRunning()) {
      std::cout << "[RecipePage] Skipping auto-dismiss - new execution is running" << std::endl;
      return;
    }

    // Only switch if user hasn't manually selected a step
    if (m_progressWidget && m_configScrollArea->widget() == m_progressWidget) {
      std::cout << "[RecipePage] Auto-dismissing progress panel" << std::endl;

      // Switch back to last selected config or placeholder
      if (m_selectedStepIndex >= 0 && m_selectedStepIndex < m_recipeManager->getStepCount()) {
        updateConfigurationForm();
      }
      else {
        clearConfigurationForm();
      }
    }
  });

  m_recipeWorker = nullptr;
}


void RecipePage::onExecutionStopped()
{
  // ✅ Clear step highlighting when execution is stopped
  clearExecutingStepHighlight();

  // ✅ Re-enable all execution buttons
  m_runRecipeButton->setEnabled(true);
  m_runRecipeButton->setText("▶ Run Recipe");

  m_runSelectedStepButton->setEnabled(true);
  m_runSelectedStepButton->setText("▶ Run Selected Step");

  // Reset step-by-step button states
  m_stepButton->setText("Step");
  m_continueButton->setEnabled(false);

  if (m_progressWidget) {
    m_progressWidget->setStatusText("Stopped by user");
  }
  std::cout << "[RecipePage] Execution stopped" << std::endl;
}

void RecipePage::onWorkerLogMessage(QString message)
{
  if (m_progressWidget) {
    m_progressWidget->setStatusText(message);
  }
}

void RecipePage::onWaitProgress(int remainingMs, int totalMs)
{
  if (m_progressWidget) {
    m_progressWidget->setWaitCountdown(remainingMs, totalMs);
  }
}

// ============================================================================
// ✅ STEP EXECUTION EVENTS (PUB-SUB FROM SEQUENCE ENGINE)
// ============================================================================

void RecipePage::onStepExecutionStarted(int stepIndex, QString stepName, QString stepType)
{
  // Highlight the currently executing step in the recipe tree
  highlightExecutingStep(stepIndex);

  // Update progress widget with accurate step progress
  if (m_progressWidget) {
    m_progressWidget->setCurrentStep(stepIndex + 1, stepName);  // stepIndex is 0-based, display is 1-based
    m_progressWidget->setStatusText(QString("Executing step %1: %2").arg(stepIndex + 1).arg(stepName));
  }
}

void RecipePage::onStepExecutionCompleted(int stepIndex, bool success)
{
  // Note: We don't clear highlight here - it will be updated when next step starts
  // or cleared when execution completes
  Q_UNUSED(stepIndex);
  Q_UNUSED(success);
}

void RecipePage::highlightExecutingStep(int stepIndex)
{
  // Find the enabled step index in the tree
  // The tree shows ALL steps, but stepIndex is for ENABLED steps only
  // We need to map enabled step index to actual tree item index

  int enabledCount = 0;
  int targetTreeIndex = -1;

  // Use RecipeManager to check if steps are enabled (checkbox widget state is not accessible via checkState)
  int totalSteps = m_recipeManager->getStepCount();

  for (int i = 0; i < totalSteps; ++i) {
    ProcessInstance* step = m_recipeManager->getStep(i);
    if (!step) continue;

    // Check if this step is enabled using RecipeManager
    if (step->enabled) {
      if (enabledCount == stepIndex) {
        targetTreeIndex = i;
        break;
      }
      enabledCount++;
    }
  }

  // Clear previous highlight and set new one
  for (int i = 0; i < m_recipeTree->topLevelItemCount(); ++i) {
    QTreeWidgetItem* item = m_recipeTree->topLevelItem(i);
    if (!item) continue;

    // Get step data for background color
    ProcessInstance* step = m_recipeManager->getStep(i);
    if (!step) continue;

    QString bgColorStr;
    if (!step->backgroundColor.empty()) {
      bgColorStr = QString::fromStdString(step->backgroundColor);
    } else {
      bgColorStr = "transparent";  // No background color
    }

    if (i == targetTreeIndex) {
      // Highlight executing step with bold yellow text on brightened background
      QString highlightBgColor = "#555555";  // Gray background for executing step visibility

      // Update column 1 (step number label)
      QWidget* col1Widget = m_recipeTree->itemWidget(item, 1);
      if (col1Widget) {
        QLabel* label = qobject_cast<QLabel*>(col1Widget);
        if (label) {
          label->setStyleSheet(QString(
            "QLabel { "
            "  background-color: %1; "
            "  color: #ffff00; "
            "  padding: 4px; "
            "  font-family: 'RobotoMono-Regular', 'Roboto Mono', 'Consolas', monospace; "
            "  font-weight: bold; "
            "  font-size: 14px; "
            "}"
          ).arg(highlightBgColor));
        }
      }

      // Update column 2 (process name label)
      QWidget* col2Widget = m_recipeTree->itemWidget(item, 2);
      if (col2Widget) {
        QLabel* label = qobject_cast<QLabel*>(col2Widget);
        if (label) {
          label->setStyleSheet(QString(
            "QLabel { "
            "  background-color: %1; "
            "  color: #ffff00; "
            "  padding: 4px 8px; "
            "  font-family: 'RobotoMono-Regular', 'Roboto Mono', 'Consolas', monospace; "
            "  font-weight: bold; "
            "  font-size: 14px; "
            "}"
          ).arg(highlightBgColor));
        }
      }

      // Also scroll to make it visible
      m_recipeTree->scrollToItem(item);
    }
    else {
      // Restore normal colors for other items
      QString textColor = step->enabled ? "#d4d4d4" : "#999999";

      // Restore column 1 (step number label)
      QWidget* col1Widget = m_recipeTree->itemWidget(item, 1);
      if (col1Widget) {
        QLabel* label = qobject_cast<QLabel*>(col1Widget);
        if (label) {
          label->setStyleSheet(QString(
            "QLabel { "
            "  background-color: %1; "
            "  color: %2; "
            "  padding: 4px; "
            "  font-family: 'RobotoMono-Regular', 'Roboto Mono', 'Consolas', monospace; "
            "  font-weight: bold; "
            "  font-size: 14px; "
            "}"
          ).arg(bgColorStr).arg(textColor));
        }
      }

      // Restore column 2 (process name label)
      QWidget* col2Widget = m_recipeTree->itemWidget(item, 2);
      if (col2Widget) {
        QLabel* label = qobject_cast<QLabel*>(col2Widget);
        if (label) {
          label->setStyleSheet(QString(
            "QLabel { "
            "  background-color: %1; "
            "  color: %2; "
            "  padding: 4px 8px; "
            "  font-family: 'RobotoMono-Regular', 'Roboto Mono', 'Consolas', monospace; "
            "  font-weight: bold; "
            "  font-size: 14px; "
            "  %3"
            "}"
          ).arg(bgColorStr)
           .arg(textColor)
           .arg(step->enabled ? "" : "text-decoration: line-through;"));
        }
      }
    }
  }
}

void RecipePage::clearExecutingStepHighlight()
{
  // Clear all highlighting - restore original background colors
  for (int i = 0; i < m_recipeTree->topLevelItemCount(); ++i) {
    QTreeWidgetItem* item = m_recipeTree->topLevelItem(i);
    if (!item) continue;

    // Get step data for background color
    ProcessInstance* step = m_recipeManager->getStep(i);
    if (!step) continue;

    QString bgColorStr;
    if (!step->backgroundColor.empty()) {
      bgColorStr = QString::fromStdString(step->backgroundColor);
    } else {
      bgColorStr = "transparent";  // No background color
    }

    QString textColor = step->enabled ? "#d4d4d4" : "#999999";

    // Restore column 1 (step number label)
    QWidget* col1Widget = m_recipeTree->itemWidget(item, 1);
    if (col1Widget) {
      QLabel* label = qobject_cast<QLabel*>(col1Widget);
      if (label) {
        label->setStyleSheet(QString(
          "QLabel { "
          "  background-color: %1; "
          "  color: %2; "
          "  padding: 4px; "
          "  font-family: 'RobotoMono-Regular', 'Roboto Mono', 'Consolas', monospace; "
          "  font-weight: bold; "
          "  font-size: 14px; "
          "}"
        ).arg(bgColorStr).arg(textColor));
      }
    }

    // Restore column 2 (process name label)
    QWidget* col2Widget = m_recipeTree->itemWidget(item, 2);
    if (col2Widget) {
      QLabel* label = qobject_cast<QLabel*>(col2Widget);
      if (label) {
        label->setStyleSheet(QString(
          "QLabel { "
          "  background-color: %1; "
          "  color: %2; "
          "  padding: 4px 8px; "
          "  font-family: 'RobotoMono-Regular', 'Roboto Mono', 'Consolas', monospace; "
          "  font-weight: bold; "
          "  font-size: 14px; "
          "  %3"
          "}"
        ).arg(bgColorStr)
         .arg(textColor)
         .arg(step->enabled ? "" : "text-decoration: line-through;"));
      }
    }
  }
}

void RecipePage::onStopRecipeRequested()
{
  std::cout << "[RecipePage] ⚠️ EMERGENCY STOP requested!" << std::endl;

  // Stop recipe execution if running
  if (m_recipeWorker && m_recipeWorker->isRunning()) {
    m_recipeWorker->requestStop();
    if (m_progressWidget) {
      m_progressWidget->setStatusText("Stopping...");
    }
  }

  // Emergency stop all motors
  if (m_context && m_context->motionManager) {
    std::cout << "[RecipePage] Calling EmergencyStopAll on motion manager" << std::endl;
    m_context->motionManager->EmergencyStopAll();
  }

  std::cout << "[RecipePage] Emergency stop completed" << std::endl;
}

// ============================================================================
// ✅ PANEL SWITCHING
// ============================================================================

void RecipePage::showProgressPanel()
{
  // ✅ ULTRA SAFE: Clear everything first
  if (m_currentConfigWidget) {
    m_configScrollArea->takeWidget();
    m_currentConfigWidget->deleteLater();
    m_currentConfigWidget = nullptr;
  }

  // ✅ ULTRA SAFE: Clear scroll area completely and disconnect old widget
  QWidget* oldWidget = m_configScrollArea->takeWidget();
  if (oldWidget) {
    // DON'T delete reusable widgets (manual adjust widget)
    if (oldWidget != m_manualAdjustWidget) {
      oldWidget->disconnect();
      oldWidget->deleteLater();
    }
  }

  // QPointer automatically nulls itself when widget is deleted, no manual reset needed

  // Create fresh widget
  m_progressWidget = new RecipeProgressWidget();
  connect(m_progressWidget, &RecipeProgressWidget::stopRequested,
    this, &RecipePage::onStopRecipeRequested);
  connect(m_progressWidget, &RecipeProgressWidget::userPromptResponse,
    this, &RecipePage::onUserPromptResponse);

  m_progressWidget->resetTimer();
  m_progressWidget->setStatusText("Initializing recipe execution...");
  m_progressWidget->startTimer();

  m_configScrollArea->setWidget(m_progressWidget);
}


void RecipePage::showConfigPanel()
{
  // ✅ ULTRA SAFE: Clear scroll area completely
  QWidget* oldWidget = m_configScrollArea->takeWidget();
  if (oldWidget) {
    // DON'T delete reusable widgets (manual adjust widget)
    if (oldWidget != m_manualAdjustWidget) {
      oldWidget->disconnect();
      oldWidget->deleteLater();
    }
  }

  // QPointer automatically nulls itself, no manual reset needed

  // Show config
  if (m_selectedStepIndex >= 0 && m_selectedStepIndex < m_recipeManager->getStepCount()) {
    updateConfigurationForm();
  }
  else {
    clearConfigurationForm();
  }
}

// ============================================================================
// PRODUCT LOADING
// ============================================================================

void RecipePage::onLoadProductClicked()
{
  if (!m_context->productPositionManager) {
    QMessageBox::critical(this, "Error", "Product manager not available");
    return;
  }

  // ✅ Determine starting folder based on current product
  QString startFolder = "config";
  if (m_context->productPositionManager->HasLoadedProduct()) {
    // Start in current product's folder
    QString productName = QString::fromStdString(m_context->productPositionManager->GetCurrentProductName());
    startFolder = QString("config/%1").arg(productName);
  }

  // ✅ Use FileSelectorDialog with recursive search for subdirectories
  QString filePath = FileSelectorDialog::getOpenFileName(
    this,
    startFolder,  // Folder path: config/[product_name]/ or config/
    "positions.*.json",  // File pattern: positions.[product_name].json
    "Select Product Positions File",  // Dialog title
    "position files",  // File type description
    true  // Recursive - search subdirectories
  );

  if (filePath.isEmpty()) {
    return;
  }

  if (m_context->productPositionManager->LoadProduct(filePath.toStdString())) {
    m_context->productPositionManager->ExportToMotionManager(*m_context->motionManager);

    std::string productName = m_context->productPositionManager->GetCurrentProductName();

    m_productStatusLabel->setText(QString("Product: %1")
      .arg(QString::fromStdString(productName)));
    m_productStatusLabel->setStyleSheet("font-size: 11px; color: #4ec9b0; font-weight: bold;");

    if (m_loadedProductLabel) {
      m_loadedProductLabel->setText(QString("Product: %1\nFile: %2")
        .arg(QString::fromStdString(productName))
        .arg(filePath));
      m_loadedProductLabel->setStyleSheet(
      "font-size: 11px; "
      "color: #27ae60; "
      "font-weight: bold; "
      "padding: 5px; "
      "border: 1px solid #27ae60; "
      "border-radius: 3px; "
      "background-color: #e8f5e9;"
    );
    }

    m_recipeManager->setProductFile(filePath.toStdString());

    std::cout << "[RecipePage] Loaded product from: " << filePath.toStdString() << std::endl;

    QMessageBox::information(this, "Success",
      QString("Product '%1' loaded successfully!\n\nThis product will be auto-loaded with this recipe.")
      .arg(QString::fromStdString(productName)));
  }
  else {
    QMessageBox::critical(this, "Error",
      "Failed to load positions from:\n" + filePath);
  }
}

// ============================================================================
// SETTINGS PERSISTENCE
// ============================================================================

void RecipePage::saveLastRecipeToSettings(const QString& recipePath)
{
  QSettings settings("Fabrinet", "BobcatEngine");
  settings.setValue("RecipePage/lastRecipe", recipePath);
}

void RecipePage::loadLastRecipeFromSettings()
{
  QSettings settings("Fabrinet", "BobcatEngine");
  QString lastRecipePath = settings.value("RecipePage/lastRecipe", "").toString();

  if (lastRecipePath.isEmpty()) {
    return;
  }

  if (!QFile::exists(lastRecipePath)) {
    return; // File was deleted
  }

  if (!m_recipeManager->loadRecipe(lastRecipePath.toStdString())) {
    return; // Failed to load
  }

  m_recipeNameEdit->setText(QString::fromStdString(m_recipeManager->getRecipeName()));
  refreshRecipeList();
  clearConfigurationForm();
  updateRecipeFileNameDisplay();  // ✅ NEW: Update recipe file name label

  std::cout << "[RecipePage] Successfully auto-loaded recipe: "
            << m_recipeManager->getRecipeName() << std::endl;

  // ✅ Auto-load product positions file from recipe
  std::string productFile = m_recipeManager->getProductFile();
  if (!productFile.empty()) {
    if (m_context->productPositionManager) {
      if (m_context->productPositionManager->LoadProduct(productFile)) {
        m_context->productPositionManager->ExportToMotionManager(*m_context->motionManager);

        std::string productName = m_context->productPositionManager->GetCurrentProductName();

        m_productStatusLabel->setText(QString("Product: %1")
          .arg(QString::fromStdString(productName)));
        m_productStatusLabel->setStyleSheet("font-size: 11px; color: #4ec9b0; font-weight: bold;");

        if (m_loadedProductLabel) {
          m_loadedProductLabel->setText(QString("Product: %1\nFile: %2")
            .arg(QString::fromStdString(productName))
            .arg(QString::fromStdString(productFile)));
          m_loadedProductLabel->setStyleSheet(
          "font-size: 11px; "
          "color: #27ae60; "
          "font-weight: bold; "
          "padding: 5px; "
          "border: 1px solid #27ae60; "
          "border-radius: 3px; "
          "background-color: #e8f5e9;"
        );
        }

        std::cout << "[RecipePage] Auto-loaded product positions: " << productFile << std::endl;
      }
      else {
        if (m_loadedProductLabel) {
          m_loadedProductLabel->setText(QString("⚠ Product file not found:\n%1")
          .arg(QString::fromStdString(productFile)));
        m_loadedProductLabel->setStyleSheet(
          "font-size: 11px; "
          "color: #d32f2f; "
          "padding: 5px; "
          "border: 1px solid #d32f2f; "
          "border-radius: 3px; "
          "background-color: #ffebee;"
        );
        }

        std::cout << "[RecipePage] Warning: Product file not found: " << productFile << std::endl;
      }
    }
  }
  else {
    // No product file associated with recipe
    m_productStatusLabel->setText("Product: None");
    m_productStatusLabel->setStyleSheet("font-size: 11px; color: #888888;");

    if (m_loadedProductLabel) {
      m_loadedProductLabel->setText("Product: None");
      m_loadedProductLabel->setStyleSheet(
        "font-size: 11px; "
        "color: #666; "
        "padding: 5px; "
        "border: 1px solid #ddd; "
        "border-radius: 3px; "
        "background-color: #f9f9f9;"
      );
    }

    std::cout << "[RecipePage] No product file associated with recipe" << std::endl;
  }
}

// ============================================================================
// MANUAL ADJUST HANDLING
// ============================================================================

void RecipePage::onShowManualAdjust(QString message, QString deviceName, double stepSizeUm,
                                    bool enableX, bool enableY, bool enableZ)
{
  // Switch from current view to manual adjust view in the scroll area
  QWidget* oldWidget = m_configScrollArea->takeWidget();
  // Don't delete the old widget - it's either progress (QPointer) or config (will be recreated)
  // We'll just replace it in the scroll area

  // Show the manual adjust widget in the scroll area
  m_configScrollArea->setWidget(m_manualAdjustWidget);
  m_manualAdjustWidget->showAdjustment(message, deviceName, stepSizeUm, enableX, enableY, enableZ);

  std::cout << "[RecipePage] Showing manual adjust widget" << std::endl;
}

void RecipePage::onManualAdjustResponse(bool completed)
{
  // Store the user's response
  m_manualAdjustResponse = completed;

  std::cout << "[RecipePage] Manual adjust response: " << (completed ? "OK" : "Cancel") << std::endl;

  // Hide the manual adjust widget
  m_manualAdjustWidget->hideAdjustment();

  // Remove from scroll area
  m_configScrollArea->takeWidget();

  // Switch back to progress widget if it exists
  if (m_progressWidget && !m_progressWidget.isNull()) {
    m_configScrollArea->setWidget(m_progressWidget);
    std::cout << "[RecipePage] Switched back to progress widget" << std::endl;
  }
  else {
    std::cout << "[RecipePage] No progress widget to switch back to" << std::endl;
  }

  // Exit the event loop to unblock the callback
  if (m_promptEventLoop) {
    m_promptEventLoop->quit();
  }
}

void RecipePage::onShowManualAlignPoint(QString pointName, QString message,
                                        bool enableX, bool enableY, bool enableZ)
{
  // Switch from current view to manual align point view in the scroll area
  QWidget* oldWidget = m_configScrollArea->takeWidget();
  // Don't delete the old widget - it's either progress (QPointer) or config (will be recreated)
  // We'll just replace it in the scroll area

  // Show the manual align point widget in the scroll area
  m_configScrollArea->setWidget(m_manualAlignPointWidget);
  m_manualAlignPointWidget->showAlignment(pointName, message, enableX, enableY, enableZ);

  std::cout << "[RecipePage] Showing manual align point widget for point: "
            << pointName.toStdString() << std::endl;
}

void RecipePage::onManualAlignPointResponse(bool completed)
{
  // Store the user's response
  m_manualAlignPointResponse = completed;

  std::cout << "[RecipePage] Manual align point response: " << (completed ? "Continue" : "Cancel") << std::endl;

  // Hide the manual align point widget
  m_manualAlignPointWidget->hideAlignment();

  // Remove from scroll area
  m_configScrollArea->takeWidget();

  // Switch back to progress widget if it exists
  if (m_progressWidget && !m_progressWidget.isNull()) {
    m_configScrollArea->setWidget(m_progressWidget);
    std::cout << "[RecipePage] Switched back to progress widget" << std::endl;
  }
  else {
    std::cout << "[RecipePage] No progress widget to switch back to" << std::endl;
  }

  // Exit the event loop to unblock the callback
  if (m_promptEventLoop) {
    m_promptEventLoop->quit();
  }
}

void RecipePage::onUserInputResponse(bool accepted, QString inputText)
{
  // Store the user's response
  m_userInputAccepted = accepted;
  m_userInputText = inputText;

  std::cout << "[RecipePage] User input response: " << (accepted ? "OK" : "Cancel") << std::endl;

  // Hide the user input widget
  m_userInputWidget->hideInput();

  // Remove from scroll area
  m_configScrollArea->takeWidget();

  // Switch back to progress widget if it exists
  if (m_progressWidget && !m_progressWidget.isNull()) {
    m_configScrollArea->setWidget(m_progressWidget);
    std::cout << "[RecipePage] Switched back to progress widget" << std::endl;
  }

  // Exit the event loop to unblock the callback
  if (m_promptEventLoop) {
    m_promptEventLoop->quit();
  }
}

// ============================================================================
// USER PROMPT HANDLING
// ============================================================================

void RecipePage::onShowUserPrompt(QString title, QString message, bool allowCancel)
{
  // Display the prompt in the progress widget (execution area)
  if (m_progressWidget && !m_progressWidget.isNull()) {
    m_progressWidget->showUserPrompt(title, message, allowCancel);
    std::cout << "[RecipePage] Showing user prompt in progress widget: " << title.toStdString() << std::endl;
  }
  else {
    std::cout << "[RecipePage] WARNING: No progress widget to show user prompt" << std::endl;
    // Auto-accept if no progress widget available
    onUserPromptResponse(true);
  }
}

void RecipePage::onUserPromptResponse(bool confirmed)
{
  // Store the user's response
  m_userPromptResponse = confirmed;

  std::cout << "[RecipePage] User prompt response: " << (confirmed ? "OK" : "Cancel") << std::endl;

  // Clear the prompt from progress widget
  if (m_progressWidget && !m_progressWidget.isNull()) {
    m_progressWidget->clearUserPrompt();
  }

  // Exit the event loop to unblock the callback
  if (m_promptEventLoop) {
    m_promptEventLoop->quit();
  }
}

// ============================================================================
// CONTEXT VIEWER
// ============================================================================

void RecipePage::onShowContextViewer()
{
  if (!m_contextViewer) {
    m_contextViewer = new ContextViewerWindow(this);
    connect(m_contextViewer, &ContextViewerWindow::refreshRequested,
            this, &RecipePage::onContextRefreshRequested);
  }

  // Update with current context
  if (m_currentContext) {
    m_contextViewer->updateFromContext(m_currentContext);
  }

  // Show and bring to front
  m_contextViewer->show();
  m_contextViewer->raise();
  m_contextViewer->activateWindow();

  std::cout << "[RecipePage] Context viewer shown" << std::endl;
}

void RecipePage::onContextRefreshRequested()
{
  if (m_contextViewer && m_currentContext) {
    m_contextViewer->updateFromContext(m_currentContext);
    std::cout << "[RecipePage] Context viewer refreshed" << std::endl;
  }
}

void RecipePage::updateVisionResultImage()
{
#ifdef HAS_OPENCV
  if (!m_visionResultImageLabel) {
    return;
  }

  // ✅ Load from temporary file to avoid cross-thread context access issues
  // This is consistent with RunProgramPage behavior
  std::string tempImagePath = "logs/vision_result_temp.png";
  cv::Mat resultImage = cv::imread(tempImagePath, cv::IMREAD_COLOR);

  if (resultImage.empty()) {
    std::cout << "[RecipePage] No vision result image file found at: " << tempImagePath << std::endl;
    return;
  }

  std::cout << "[RecipePage] Loaded vision result from file: " << resultImage.cols << "x" << resultImage.rows << std::endl;

  // Convert cv::Mat to QImage (BGR -> RGB)
  cv::Mat rgbImage;
  cv::cvtColor(resultImage, rgbImage, cv::COLOR_BGR2RGB);
  QImage qImage(rgbImage.data, rgbImage.cols, rgbImage.rows,
                static_cast<int>(rgbImage.step), QImage::Format_RGB888);
  qImage = qImage.copy();  // Deep copy to decouple from cv::Mat memory

  // Scale to fit the label while maintaining aspect ratio
  QPixmap pixmap = QPixmap::fromImage(qImage);
  pixmap = pixmap.scaled(m_visionResultImageLabel->size(),
                         Qt::KeepAspectRatio, Qt::SmoothTransformation);

  m_visionResultImageLabel->setPixmap(pixmap);
  m_visionResultImageLabel->setStyleSheet(
    "QLabel { "
    "  background-color: #0f0f0f; "
    "  border: 1px solid #FFA500; "
    "}"
  );

  std::cout << "[RecipePage] Vision result image updated: "
            << resultImage.cols << "x" << resultImage.rows << std::endl;
#else
  // No OpenCV support
  if (m_visionResultImageLabel) {
    m_visionResultImageLabel->setText("OpenCV not available");
  }
#endif
}