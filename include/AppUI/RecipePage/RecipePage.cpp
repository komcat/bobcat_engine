// RecipePage.cpp
#include "RecipePage.h"
#include "RecipeManager.h"
#include "ApplicationContext.h"
#include "SequenceEngine.h"
#include "SequenceContext.h"
#include "GripperSequenceHelpers.h"
#include "ProcessSequenceFactory.h"
#include "ProcessConfigWidget.h"
#include "product_position_manager.h"
#include "RecipeWorker.h"
#include "RecipeProgressWidget.h"  // ✅ NEW
#include "SequencePromptDialog.h"
#include "LogManager.h"
#include "FileSelectorDialog.h"  // ✅ Custom file selector
#include "RecipeSaveAsDialog.h"  // ✅ Custom save dialog
#include <QSplitter>
#include <QLabel>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QFileDialog>
#include <QFrame>
#include <QMetaObject>
#include <QThread>
#include <QTimer>  // ✅ NEW
#include <QApplication>
#include <QHeaderView>
#include <QCheckBox>
#include <QListWidget>
#include <QSet>
#include <QSettings>
#include <QFile>
#include <iostream>
#include <chrono>

RecipePage::RecipePage(ApplicationContext* context, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_recipeManager(new RecipeManager())
  , m_sequenceEngine(nullptr)
  , m_processFactory(new ProcessSequenceFactory())
  , m_selectedStepIndex(-1)
  , m_currentConfigWidget(nullptr)
  , m_recipeWorker(nullptr)
  , m_loadedProductLabel(nullptr)
  , m_progressWidget(nullptr)  // ✅ NEW
{
  setupUI();
  setupSequenceEngine();

  // ✅ Auto-load last recipe from settings
  loadLastRecipeFromSettings();

  // Check if product already loaded
  if (m_context->productPositionManager &&
    m_context->productPositionManager->HasLoadedProduct()) {
    std::string productName = m_context->productPositionManager->GetCurrentProductName();
    m_productStatusLabel->setText(QString("Product: %1")
      .arg(QString::fromStdString(productName)));
    m_productStatusLabel->setStyleSheet("font-size: 11px; color: #27ae60; font-weight: bold;");
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

  delete m_recipeManager;
  delete m_sequenceEngine;
  delete m_processFactory;
  std::cout << "[RecipePage] Destroyed" << std::endl;
}

void RecipePage::setupUI()
{
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
    "color: #2d3e5f; "
    "padding: 0px; "
    "margin: 0px;"
  );
  titleLabel->setAlignment(Qt::AlignCenter);
  titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  titleLayout->addWidget(titleLabel);

  m_productStatusLabel = new QLabel("No product loaded");
  m_productStatusLabel->setStyleSheet(
    "font-size: 11px; "
    "color: #666; "
    "padding: 4px;"
  );
  titleLayout->addWidget(m_productStatusLabel);

  m_loadProductButton = new QPushButton("📁 Load Product");
  m_loadProductButton->setFixedHeight(28);
  m_loadProductButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #27ae60; "
    "  color: white; "
    "  border-radius: 3px; "
    "  font-size: 11px; "
    "  font-weight: bold; "
    "  padding: 4px 12px; "
    "} "
    "QPushButton:hover { background-color: #2ecc71; }"
  );
  connect(m_loadProductButton, &QPushButton::clicked, this, &RecipePage::onLoadProductClicked);
  titleLayout->addWidget(m_loadProductButton);

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
  title->setStyleSheet("font-size: 14px; font-weight: bold; color: #2d3e5f;");
  mainLayout->addWidget(title);

  // ========== HORIZONTAL SPLIT: List on left, Button on right ==========
  QHBoxLayout* contentLayout = new QHBoxLayout();
  contentLayout->setSpacing(5);

  // LEFT: Process list
  m_processTemplateList = new QListWidget();
  m_processTemplateList->setStyleSheet(
    "QListWidget { "
    "  background-color: white; "
    "  border: 1px solid #c0c0c0; "
    "  border-radius: 4px; "
    "  font-size: 12px; "
    "} "
    "QListWidget::item { "
    "  padding: 5px; "
    "} "
    "QListWidget::item:selected { "
    "  background-color: #5a8fd6; "
    "  color: white; "
    "}"
  );

  for (const auto& type : RecipeManager::getAvailableProcessTypes()) {
    m_processTemplateList->addItem(QString::fromStdString(type));
  }

  connect(m_processTemplateList, &QListWidget::itemClicked,
    this, &RecipePage::onProcessTemplateSelected);
  connect(m_processTemplateList, &QListWidget::itemDoubleClicked,
    [this](QListWidgetItem*) { onAddProcess(); });

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
    "  font-size: 20px; "
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
}

void RecipePage::createMiddlePanel(QWidget* middlePanel)
{
  QVBoxLayout* layout = new QVBoxLayout(middlePanel);
  layout->setContentsMargins(5, 5, 5, 5);

  QLabel* title = new QLabel("Recipe Steps");
  title->setStyleSheet("font-size: 14px; font-weight: bold; color: #2d3e5f;");
  layout->addWidget(title);

  QHBoxLayout* nameLayout = new QHBoxLayout();
  QLabel* nameLabel = new QLabel("Name:");
  nameLabel->setStyleSheet("font-weight: bold;");
  m_recipeNameEdit = new QLineEdit(QString::fromStdString(m_recipeManager->getRecipeName()));
  connect(m_recipeNameEdit, &QLineEdit::textChanged, this, &RecipePage::onRecipeNameChanged);
  nameLayout->addWidget(nameLabel);
  nameLayout->addWidget(m_recipeNameEdit);
  layout->addLayout(nameLayout);

  m_loadedProductLabel = new QLabel("Product: None");
  m_loadedProductLabel->setStyleSheet(
    "font-size: 11px; "
    "color: #666; "
    "padding: 5px; "
    "border: 1px solid #ddd; "
    "border-radius: 3px; "
    "background-color: #f9f9f9;"
  );
  m_loadedProductLabel->setWordWrap(true);
  layout->addWidget(m_loadedProductLabel);

  // ========== TOP BUTTON ROW ==========
  QHBoxLayout* topButtons = new QHBoxLayout();
  topButtons->setSpacing(5);

  m_newButton = new QPushButton("New");
  m_newButton->setMinimumHeight(32);
  m_newButton->setStyleSheet(
    "QPushButton { background-color: #3498db; color: white; font-weight: bold; border-radius: 3px; }"
    "QPushButton:hover { background-color: #2980b9; }"
  );
  connect(m_newButton, &QPushButton::clicked, this, &RecipePage::onNewRecipe);
  topButtons->addWidget(m_newButton);

  m_loadButton = new QPushButton("Load");
  m_loadButton->setMinimumHeight(32);
  m_loadButton->setStyleSheet(
    "QPushButton { background-color: #3498db; color: white; font-weight: bold; border-radius: 3px; }"
    "QPushButton:hover { background-color: #2980b9; }"
  );
  connect(m_loadButton, &QPushButton::clicked, this, &RecipePage::onLoadRecipe);
  topButtons->addWidget(m_loadButton);

  m_saveButton = new QPushButton("Save");
  m_saveButton->setMinimumHeight(32);
  m_saveButton->setStyleSheet(
    "QPushButton { background-color: #27ae60; color: white; font-weight: bold; border-radius: 3px; }"
    "QPushButton:hover { background-color: #229954; }"
  );
  connect(m_saveButton, &QPushButton::clicked, this, &RecipePage::onSaveRecipe);
  topButtons->addWidget(m_saveButton);

  // ✅ NEW: Save As button
  m_saveAsButton = new QPushButton("Save As...");
  m_saveAsButton->setMinimumHeight(32);
  m_saveAsButton->setStyleSheet(
    "QPushButton { background-color: #16a085; color: white; font-weight: bold; border-radius: 3px; }"
    "QPushButton:hover { background-color: #1abc9c; }"
  );
  connect(m_saveAsButton, &QPushButton::clicked, this, &RecipePage::onSaveRecipeAs);
  topButtons->addWidget(m_saveAsButton);

  topButtons->addSpacing(10);

  m_moveUpButton = new QPushButton("↑ Up");
  m_moveUpButton->setMinimumHeight(32);
  m_moveUpButton->setStyleSheet(
    "QPushButton { background-color: #95a5a6; color: white; font-weight: bold; border-radius: 3px; }"
    "QPushButton:hover { background-color: #7f8c8d; }"
  );
  connect(m_moveUpButton, &QPushButton::clicked, this, &RecipePage::onMoveStepUp);
  topButtons->addWidget(m_moveUpButton);

  m_moveDownButton = new QPushButton("↓ Down");
  m_moveDownButton->setMinimumHeight(32);
  m_moveDownButton->setStyleSheet(
    "QPushButton { background-color: #95a5a6; color: white; font-weight: bold; border-radius: 3px; }"
    "QPushButton:hover { background-color: #7f8c8d; }"
  );
  connect(m_moveDownButton, &QPushButton::clicked, this, &RecipePage::onMoveStepDown);
  topButtons->addWidget(m_moveDownButton);

  topButtons->addSpacing(10);

  m_removeButton = new QPushButton("Remove");
  m_removeButton->setMinimumHeight(32);
  m_removeButton->setStyleSheet(
    "QPushButton { background-color: #e74c3c; color: white; font-weight: bold; border-radius: 3px; }"
    "QPushButton:hover { background-color: #c0392b; }"
  );
  connect(m_removeButton, &QPushButton::clicked, this, &RecipePage::onRemoveStep);
  topButtons->addWidget(m_removeButton);

  layout->addLayout(topButtons);

  // ========== SELECT/DESELECT BUTTONS ==========
  QHBoxLayout* selectionButtons = new QHBoxLayout();
  selectionButtons->setSpacing(5);

  m_selectAllButton = new QPushButton("☑ Select All");
  m_selectAllButton->setMinimumHeight(28);
  m_selectAllButton->setStyleSheet(
    "QPushButton { background-color: #16a085; color: white; font-weight: bold; border-radius: 3px; font-size: 11px; }"
    "QPushButton:hover { background-color: #1abc9c; }"
  );
  connect(m_selectAllButton, &QPushButton::clicked, this, &RecipePage::onSelectAllProcesses);
  selectionButtons->addWidget(m_selectAllButton);

  m_deselectAllButton = new QPushButton("☐ Deselect All");
  m_deselectAllButton->setMinimumHeight(28);
  m_deselectAllButton->setStyleSheet(
    "QPushButton { background-color: #95a5a6; color: white; font-weight: bold; border-radius: 3px; font-size: 11px; }"
    "QPushButton:hover { background-color: #7f8c8d; }"
  );
  connect(m_deselectAllButton, &QPushButton::clicked, this, &RecipePage::onDeselectAllProcesses);
  selectionButtons->addWidget(m_deselectAllButton);

  m_toggleSelectedButton = new QPushButton("⇄ Toggle Selected");
  m_toggleSelectedButton->setMinimumHeight(28);
  m_toggleSelectedButton->setToolTip("Toggle Run checkboxes for currently selected (highlighted) rows");
  m_toggleSelectedButton->setStyleSheet(
    "QPushButton { background-color: #f39c12; color: white; font-weight: bold; border-radius: 3px; font-size: 11px; }"
    "QPushButton:hover { background-color: #e67e22; }"
  );
  connect(m_toggleSelectedButton, &QPushButton::clicked, this, &RecipePage::onToggleSelectedProcesses);
  selectionButtons->addWidget(m_toggleSelectedButton);

  selectionButtons->addStretch();

  // ✅ NEW: Update Product File button
  m_updateProductFileButton = new QPushButton("📦 Update Product File");
  m_updateProductFileButton->setMinimumHeight(28);
  m_updateProductFileButton->setToolTip("Update the product positions file reference in this recipe");
  m_updateProductFileButton->setStyleSheet(
    "QPushButton { background-color: #8e44ad; color: white; font-weight: bold; border-radius: 3px; font-size: 11px; }"
    "QPushButton:hover { background-color: #9b59b6; }"
  );
  connect(m_updateProductFileButton, &QPushButton::clicked, this, &RecipePage::onUpdateProductFile);
  selectionButtons->addWidget(m_updateProductFileButton);

  layout->addLayout(selectionButtons);

  // ========== TABLE VIEW ==========
  m_recipeTable = new QTableWidget();
  m_recipeTable->setColumnCount(3);
  m_recipeTable->setHorizontalHeaderLabels({ "#", "Process Instance", "Run" });
  m_recipeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_recipeTable->setSelectionMode(QAbstractItemView::ExtendedSelection);  // ✅ Enable Shift+Click, Ctrl+Click
  m_recipeTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_recipeTable->verticalHeader()->setVisible(false);
  m_recipeTable->setShowGrid(true);

  // Set column widths
  m_recipeTable->setColumnWidth(0, 40);   // # column
  m_recipeTable->setColumnWidth(2, 50);   // Run checkbox column
  m_recipeTable->horizontalHeader()->setStretchLastSection(false);
  m_recipeTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);

  m_recipeTable->setStyleSheet(
    "QTableWidget { "
    "  background-color: white; "
    "  border: 1px solid #c0c0c0; "
    "  border-radius: 4px; "
    "  font-size: 12px; "
    "  gridline-color: #e0e0e0; "
    "} "
    "QTableWidget::item:selected { "
    "  background-color: #5a8fd6; "
    "  color: white; "
    "} "
    "QHeaderView::section { "
    "  background-color: #ecf0f1; "
    "  padding: 5px; "
    "  border: 1px solid #bdc3c7; "
    "  font-weight: bold; "
    "  font-size: 11px; "
    "}"
  );

  connect(m_recipeTable, &QTableWidget::itemSelectionChanged,
    this, &RecipePage::onRecipeStepSelected);
  connect(m_recipeTable, &QTableWidget::cellChanged,
    this, &RecipePage::onTableCellChanged);

  layout->addWidget(m_recipeTable);

  // ========== RUN BUTTON ==========
  m_runRecipeButton = new QPushButton("▶ Run Recipe");
  m_runRecipeButton->setMinimumHeight(50);
  m_runRecipeButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #ff8c00; "
    "  color: white; "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "  border-radius: 5px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #ff7700; "
    "}"
  );
  connect(m_runRecipeButton, &QPushButton::clicked, this, &RecipePage::onRunRecipe);
  layout->addWidget(m_runRecipeButton);
}

void RecipePage::createRightPanel(QWidget* rightPanel)
{
  QVBoxLayout* layout = new QVBoxLayout(rightPanel);
  layout->setContentsMargins(5, 5, 5, 5);

  QLabel* title = new QLabel("Process Configuration");
  title->setStyleSheet("font-size: 14px; font-weight: bold; color: #2d3e5f;");
  layout->addWidget(title);

  m_configScrollArea = new QScrollArea();
  m_configScrollArea->setWidgetResizable(true);
  m_configScrollArea->setStyleSheet(
    "QScrollArea { background-color: white; border: 1px solid #c0c0c0; border-radius: 4px; }"
  );

  layout->addWidget(m_configScrollArea);

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
}

void RecipePage::onLoadRecipe()
{
  // ✅ Use FileSelectorDialog to restrict to recipes folder
  QString filename = FileSelectorDialog::getOpenFileName(
    this,
    "recipes",  // Folder path
    "*.json",  // File pattern
    "Load Recipe",  // Dialog title
    "recipe files"  // File type description
  );

  if (filename.isEmpty()) return;

  if (m_recipeManager->loadRecipe(filename.toStdString())) {
    m_recipeNameEdit->setText(QString::fromStdString(m_recipeManager->getRecipeName()));
    refreshRecipeList();
    clearConfigurationForm();

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
          m_productStatusLabel->setStyleSheet("font-size: 11px; color: #27ae60; font-weight: bold;");

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

          std::cout << "[RecipePage] Auto-loaded product: " << productFile << std::endl;

          QMessageBox::information(this, "Recipe Loaded",
            QString("Recipe '%1' loaded successfully!\n\nAuto-loaded product: %2")
            .arg(QString::fromStdString(m_recipeManager->getRecipeName()))
            .arg(QString::fromStdString(productName)));
        }
        else {
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

          QMessageBox::warning(this, "Product Not Found",
            QString("Recipe loaded, but associated product file not found:\n%1\n\nPlease load the product manually.")
            .arg(QString::fromStdString(productFile)));
        }
      }
    }
    else {
      m_loadedProductLabel->setText("Product: None");
      m_loadedProductLabel->setStyleSheet(
        "font-size: 11px; "
        "color: #666; "
        "padding: 5px; "
        "border: 1px solid #ddd; "
        "border-radius: 3px; "
        "background-color: #f9f9f9;"
      );

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

  // ✅ Check if recipe already has a file path
  std::string currentRecipePath = m_recipeManager->getCurrentFilePath();

  if (currentRecipePath.empty()) {
    // No path yet - prompt for save location (like Save As)
    onSaveRecipeAs();
    return;
  }

  // ✅ Recipe already has path - save directly without prompting
  if (m_recipeManager->saveRecipe(currentRecipePath)) {
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

  // ✅ Get current recipe name for default
  QString currentName = QString::fromStdString(m_recipeManager->getRecipeName());

  // ✅ Use custom RecipeSaveAsDialog instead of QFileDialog
  QString filename = RecipeSaveAsDialog::getSaveFileName(this, currentName);

  if (filename.isEmpty()) return;

  if (m_recipeManager->saveRecipe(filename.toStdString())) {
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
  QListWidgetItem* item = m_processTemplateList->currentItem();
  if (!item) {
    QMessageBox::warning(this, "No Selection", "Please select a process template to add.");
    return;
  }

  m_recipeManager->addStep(item->text().toStdString());
  refreshRecipeList();
  m_recipeTable->selectRow(m_recipeManager->getStepCount() - 1);
}

void RecipePage::onSelectAllProcesses()
{
  // Check all checkboxes in the "Run" column
  for (int row = 0; row < m_recipeTable->rowCount(); ++row) {
    m_recipeManager->setStepEnabled(row, true);
  }
  refreshRecipeList();

  // Restore selection if there was one
  if (m_selectedStepIndex >= 0 && m_selectedStepIndex < m_recipeTable->rowCount()) {
    m_recipeTable->selectRow(m_selectedStepIndex);
  }
}

void RecipePage::onDeselectAllProcesses()
{
  // Uncheck all checkboxes in the "Run" column
  for (int row = 0; row < m_recipeTable->rowCount(); ++row) {
    m_recipeManager->setStepEnabled(row, false);
  }
  refreshRecipeList();

  // Restore selection if there was one
  if (m_selectedStepIndex >= 0 && m_selectedStepIndex < m_recipeTable->rowCount()) {
    m_recipeTable->selectRow(m_selectedStepIndex);
  }
}

void RecipePage::onToggleSelectedProcesses()
{
  // Get all currently selected (highlighted) rows
  QList<QTableWidgetItem*> selectedItems = m_recipeTable->selectedItems();

  if (selectedItems.isEmpty()) {
    QMessageBox::information(this, "No Selection",
      "Please select one or more rows first.\n\n"
      "Tip: Use Shift+Click to select a range, or Ctrl+Click to select multiple rows.");
    return;
  }

  // Get unique row indices from selected items
  QSet<int> selectedRows;
  for (QTableWidgetItem* item : selectedItems) {
    selectedRows.insert(item->row());
  }

  // Toggle the enabled state for each selected row
  for (int row : selectedRows) {
    if (row >= 0 && row < m_recipeManager->getStepCount()) {
      const ProcessInstance* step = m_recipeManager->getStep(row);
      if (step) {
        // Toggle: if enabled, disable it; if disabled, enable it
        m_recipeManager->setStepEnabled(row, !step->enabled);
      }
    }
  }

  refreshRecipeList();

  // Restore the selection
  for (int row : selectedRows) {
    m_recipeTable->selectRow(row);
  }
}

void RecipePage::onUpdateProductFile()
{
  // ✅ Step 1: Select new product file using FileSelectorDialog
  QString filePath = FileSelectorDialog::getOpenFileName(
    this,
    "config",  // Folder path
    "*positions.json",  // File pattern
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
      m_productStatusLabel->setStyleSheet("font-size: 11px; color: #27ae60; font-weight: bold;");

      // Update loaded product label in middle panel
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

  QMessageBox::StandardButton reply = QMessageBox::question(
    this, "Remove Step", "Are you sure you want to remove this step?",
    QMessageBox::Yes | QMessageBox::No
  );

  if (reply == QMessageBox::Yes) {
    m_recipeManager->removeStep(m_selectedStepIndex);
    refreshRecipeList();
    clearConfigurationForm();
    m_selectedStepIndex = -1;
  }
}

void RecipePage::onMoveStepUp()
{
  if (m_recipeManager->moveStepUp(m_selectedStepIndex)) {
    m_selectedStepIndex--;  // Update index to follow the moved step
    refreshRecipeList();
    m_recipeTable->selectRow(m_selectedStepIndex);
  }
}

void RecipePage::onMoveStepDown()
{
  if (m_recipeManager->moveStepDown(m_selectedStepIndex)) {
    m_selectedStepIndex++;  // Update index to follow the moved step
    refreshRecipeList();
    m_recipeTable->selectRow(m_selectedStepIndex);
  }
}

void RecipePage::onRecipeStepSelected()
{
  int index = m_recipeTable->currentRow();

  // ✅ NEW: If progress widget is showing, switch to config panel
  if (m_progressWidget && m_configScrollArea->widget() == m_progressWidget) {
    std::cout << "[RecipePage] User selected step, switching from progress to config" << std::endl;
  }

  if (m_selectedStepIndex == index) {
    // ✅ Even if same step, if showing progress, switch to config
    if (m_progressWidget && m_configScrollArea->widget() == m_progressWidget) {
      updateConfigurationForm();
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

void RecipePage::onTableCellChanged(int row, int column)
{
  // This slot is currently not used as checkboxes handle their own state
  // Kept for future extensibility if needed
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

  if (m_selectedStepIndex < m_recipeTable->rowCount()) {
    QTableWidgetItem* item = m_recipeTable->item(m_selectedStepIndex, 1);
    if (item) {
      const ProcessInstance* step = m_recipeManager->getStep(m_selectedStepIndex);
      if (step) {
        QString displayText = QString::fromStdString(step->userNickname);
        if (!step->enabled) {
          displayText = "[DISABLED] " + displayText;
        }
        item->setText(displayText);
      }
    }
  }
}

void RecipePage::onEnabledChanged(bool enabled)
{
  if (m_selectedStepIndex < 0) return;

  m_recipeManager->setStepEnabled(m_selectedStepIndex, enabled);
  refreshRecipeList();
  m_recipeTable->selectRow(m_selectedStepIndex);
}

void RecipePage::onParameterChanged()
{
  if (m_selectedStepIndex < 0 || !m_currentConfigWidget) return;

  auto params = m_currentConfigWidget->getParameters();
  for (const auto& [key, value] : params) {
    m_recipeManager->setStepParameter(m_selectedStepIndex, key, value);
  }
}

void RecipePage::onProcessTemplateSelected(QListWidgetItem* item)
{
  std::cout << "[RecipePage] Selected template: " << item->text().toStdString() << std::endl;
}

void RecipePage::refreshRecipeList()
{
  // Block signals to prevent triggering cellChanged during population
  m_recipeTable->blockSignals(true);

  m_recipeTable->setRowCount(0);

  int row = 0;
  for (const auto& step : m_recipeManager->getAllSteps()) {
    m_recipeTable->insertRow(row);

    // Column 0: Instance number
    QTableWidgetItem* numItem = new QTableWidgetItem(QString::number(row + 1));
    numItem->setTextAlignment(Qt::AlignCenter);
    numItem->setFlags(numItem->flags() & ~Qt::ItemIsEditable);
    m_recipeTable->setItem(row, 0, numItem);

    // Column 1: Process instance name
    QString displayText = QString::fromStdString(step->userNickname);
    if (!step->enabled) {
      displayText = "[DISABLED] " + displayText;
    }

    QTableWidgetItem* nameItem = new QTableWidgetItem(displayText);
    nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);

    if (!step->enabled) {
      nameItem->setForeground(QColor("#999999"));
      QFont font = nameItem->font();
      font.setStrikeOut(true);
      nameItem->setFont(font);
    }

    m_recipeTable->setItem(row, 1, nameItem);

    // Column 2: Checkbox for run selection
    QWidget* checkboxWidget = new QWidget();
    QCheckBox* checkbox = new QCheckBox();
    checkbox->setChecked(step->enabled);
    checkbox->setProperty("row", row);  // Store row index for callback

    QHBoxLayout* checkLayout = new QHBoxLayout(checkboxWidget);
    checkLayout->addWidget(checkbox);
    checkLayout->setAlignment(Qt::AlignCenter);
    checkLayout->setContentsMargins(0, 0, 0, 0);

    connect(checkbox, &QCheckBox::toggled, this, [this, row](bool checked) {
      m_recipeManager->setStepEnabled(row, checked);
      refreshRecipeList();
      m_recipeTable->selectRow(m_selectedStepIndex);
    });

    m_recipeTable->setCellWidget(row, 2, checkboxWidget);

    row++;
  }

  m_recipeTable->blockSignals(false);
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

  // Create appropriate widget based on process type
  if (instance->processType == "Pick") {
    m_currentConfigWidget = new PickConfigWidget(m_context);
  }
  else if (instance->processType == "Place") {
    m_currentConfigWidget = new PlaceConfigWidget(m_context);
  }
  else if (instance->processType == "Pick and Place") {
    m_currentConfigWidget = new PickAndPlaceConfigWidget(m_context);
  }
  else if (instance->processType == "Move to Position") {
    m_currentConfigWidget = new MoveToPositionConfigWidget(m_context);
  }
  else if (instance->processType == "Move Relative") {
    m_currentConfigWidget = new MoveRelativeConfigWidget(m_context);
  }
  else if (instance->processType == "Set Output") {
    m_currentConfigWidget = new SetOutputConfigWidget(m_context);
  }
  // ============================================================================
  // ✅ NEW PROCESS WIDGETS
  // ============================================================================
  else if (instance->processType == "Wait") {
    m_currentConfigWidget = new WaitConfigWidget(m_context);
  }
  // ← ADD THIS:
  else if (instance->processType == "Ask User") {
    m_currentConfigWidget = new AskUserConfigWidget(m_context);
  }
  else if (instance->processType == "Move Relative Multi-Axis") {
    m_currentConfigWidget = new MoveRelativeMultiAxisConfigWidget(m_context);
  }
  else if (instance->processType == "Move to Position with Offset") {
    m_currentConfigWidget = new MoveToPositionWithOffsetConfigWidget(m_context);
  }
  // ============================================================================
  // ✅ PNEUMATIC PROCESSES
  // ============================================================================
  else if (instance->processType == "Slide Extend") {
    m_currentConfigWidget = new SlideExtendConfigWidget(m_context);
  }
  else if (instance->processType == "Slide Retract") {
    m_currentConfigWidget = new SlideRetractConfigWidget(m_context);
  }
  // ============================================================================
  // ✅ TEC and LASER PROCESSES
  // ============================================================================
  else if (instance->processType == "Set TEC Temp") {
    m_currentConfigWidget = new SetTECTempConfigWidget(m_context);
  }
  else if (instance->processType == "Set Laser Current") {
    m_currentConfigWidget = new SetLaserCurrentConfigWidget(m_context);
  }
  // ============================================================================
  // ✅ DATASTORE PROCESS
  // ============================================================================
  else if (instance->processType == "Read DataStore") {
    m_currentConfigWidget = new ReadDataStoreConfigWidget(m_context);
  }
  // ============================================================================
  // ✅ TEC READ PROCESSES
  // ============================================================================
  else if (instance->processType == "Read Temperature") {
    m_currentConfigWidget = new ReadTemperatureConfigWidget(m_context);
  }
  else if (instance->processType == "Read Laser Current") {
    m_currentConfigWidget = new ReadLaserCurrentConfigWidget(m_context);
  }
  // ============================================================================
  // ✅ PSU PROCESSES
  // ============================================================================
  else if (instance->processType == "PSU Output On/Off") {
    m_currentConfigWidget = new PSUOutputConfigWidget(m_context);
  }
  else if (instance->processType == "PSU Set Voltage") {
    m_currentConfigWidget = new PSUSetVoltageConfigWidget(m_context);
  }
  else if (instance->processType == "PSU Set Current") {
    m_currentConfigWidget = new PSUSetCurrentConfigWidget(m_context);
  }
  else if (instance->processType == "PSU Read Voltage") {
    m_currentConfigWidget = new PSUReadVoltageConfigWidget(m_context);
  }
  else if (instance->processType == "PSU Read Current") {
    m_currentConfigWidget = new PSUReadCurrentConfigWidget(m_context);
  }
  else {
    std::cerr << "[RecipePage] Unknown process type: " << instance->processType << std::endl;
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
  label->setStyleSheet("color: #888; font-size: 14px; padding: 50px;");

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
    QMetaObject::invokeMethod(this, [this, title, message, &result]() {
      result = SequencePromptDialog::ShowPrompt(
        QString::fromStdString(title),
        QString::fromStdString(message),
        this
      );
    }, Qt::BlockingQueuedConnection);
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

  QMessageBox::StandardButton reply = QMessageBox::question(
    this, "Run Recipe",
    QString("Execute recipe '%1' with %2 step(s)?").arg(recipeName).arg(stepCount),
    QMessageBox::Yes | QMessageBox::No
  );

  if (reply != QMessageBox::Yes) return;

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

  // ✅ STEP 3: Set UserPrompt callback
  m_recipeWorker->setUserPromptCallback(
    [this](const std::string& title, const std::string& message) -> bool {
    bool result = false;
    QMetaObject::invokeMethod(this, [this, title, message, &result]() {
      result = SequencePromptDialog::ShowPrompt(
        QString::fromStdString(title),
        QString::fromStdString(message),
        this
      );
    }, Qt::BlockingQueuedConnection);
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
  connect(m_recipeWorker, &RecipeWorker::finished,
    m_recipeWorker, &QObject::deleteLater);

  // Start execution
  m_recipeWorker->start();
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

  // Show result dialog
  if (success) {
    QMessageBox::information(this, "Success", message);
    m_context->logManager->Info(message.toStdString(), "RecipePage");
  }
  else {
    QMessageBox::critical(this, "Execution Error", message);
    m_context->logManager->Error(message.toStdString(), "RecipePage");
  }

  // ✅ NEW: Auto-dismiss progress panel after 3 seconds
  QTimer::singleShot(3000, this, [this]() {
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
  if (m_progressWidget) {
    m_progressWidget->setStatusText("⏹ Stopped by user");
  }
  std::cout << "[RecipePage] Execution stopped" << std::endl;
}

void RecipePage::onWorkerLogMessage(QString message)
{
  if (m_progressWidget) {
    m_progressWidget->setStatusText(message);
  }
}

void RecipePage::onStopRecipeRequested()
{
  if (!m_recipeWorker || !m_recipeWorker->isRunning()) {
    return;
  }

  QMessageBox::StandardButton reply = QMessageBox::question(
    this, "Stop Recipe",
    "Stop recipe execution after current step?",
    QMessageBox::Yes | QMessageBox::No
  );

  if (reply == QMessageBox::Yes) {
    m_recipeWorker->requestStop();
    if (m_progressWidget) {
      m_progressWidget->setStatusText("Stopping...");
    }
  }
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

  // ✅ ULTRA SAFE: Clear scroll area completely and disconnect old progress widget
  QWidget* oldWidget = m_configScrollArea->takeWidget();
  if (oldWidget) {
    // Disconnect any signals from old widget before deleting
    oldWidget->disconnect();
    oldWidget->deleteLater();
  }

  // QPointer automatically nulls itself when widget is deleted, no manual reset needed

  // Create fresh widget
  m_progressWidget = new RecipeProgressWidget();
  connect(m_progressWidget, &RecipeProgressWidget::stopRequested,
    this, &RecipePage::onStopRecipeRequested);

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
    oldWidget->disconnect();
    oldWidget->deleteLater();
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

  // ✅ Use FileSelectorDialog with recursive search for subdirectories (SIPHOG, BOBCAT, etc.)
  QString filePath = FileSelectorDialog::getOpenFileName(
    this,
    "config",  // Folder path
    "*positions.json",  // File pattern
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
    m_productStatusLabel->setStyleSheet("font-size: 11px; color: #27ae60; font-weight: bold;");

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

  // Auto-load associated product file
  std::string productFile = m_recipeManager->getProductFile();
  if (!productFile.empty()) {
    if (m_context->productPositionManager) {
      if (m_context->productPositionManager->LoadProduct(productFile)) {
        m_context->productPositionManager->ExportToMotionManager(*m_context->motionManager);

        std::string productName = m_context->productPositionManager->GetCurrentProductName();

        m_productStatusLabel->setText(QString("Product: %1")
          .arg(QString::fromStdString(productName)));
        m_productStatusLabel->setStyleSheet("font-size: 11px; color: #27ae60; font-weight: bold;");

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

        std::cout << "[RecipePage] Auto-loaded recipe and product on startup" << std::endl;
      }
    }
  }
}