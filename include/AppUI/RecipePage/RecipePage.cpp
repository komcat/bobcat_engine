// RecipePage.cpp
#include "RecipePage.h"
#include "RecipeManager.h"
#include "ApplicationContext.h"
#include "SequenceEngine.h"
#include "GripperSequenceHelpers.h"
#include "ProcessSequenceFactory.h"
#include "ProcessConfigWidget.h"
#include "product_position_manager.h"
#include "RecipeWorker.h"
#include "RecipeProgressWidget.h"  // ✅ NEW
#include "SequencePromptDialog.h"
#include "LogManager.h"
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
#include <iostream>

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
  QVBoxLayout* layout = new QVBoxLayout(leftPanel);
  layout->setContentsMargins(5, 5, 5, 5);

  QLabel* title = new QLabel("Available Processes");
  title->setStyleSheet("font-size: 14px; font-weight: bold; color: #2d3e5f;");
  layout->addWidget(title);

  m_processTemplateList = new QListWidget();
  m_processTemplateList->setStyleSheet(
    "QListWidget { background-color: white; border: 1px solid #c0c0c0; "
    "border-radius: 4px; font-size: 12px; } "
    "QListWidget::item:selected { background-color: #5a8fd6; color: white; }"
  );

  for (const auto& type : RecipeManager::getAvailableProcessTypes()) {
    m_processTemplateList->addItem(QString::fromStdString(type));
  }

  connect(m_processTemplateList, &QListWidget::itemClicked,
    this, &RecipePage::onProcessTemplateSelected);
  connect(m_processTemplateList, &QListWidget::itemDoubleClicked,
    [this](QListWidgetItem*) { onAddProcess(); });

  layout->addWidget(m_processTemplateList);

  m_addButton = new QPushButton("Add to Recipe →");
  m_addButton->setMinimumHeight(40);
  m_addButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #4CAF50; "
    "  color: white; "
    "  border-radius: 4px; "
    "  padding: 10px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { "
    "  background-color: #45a049; "
    "}"
  );
  connect(m_addButton, &QPushButton::clicked, this, &RecipePage::onAddProcess);
  layout->addWidget(m_addButton);
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

  m_recipeList = new QListWidget();
  m_recipeList->setStyleSheet(
    "QListWidget { background-color: white; border: 1px solid #c0c0c0; "
    "border-radius: 4px; font-size: 12px; } "
    "QListWidget::item:selected { background-color: #5a8fd6; color: white; }"
  );
  connect(m_recipeList, &QListWidget::currentRowChanged,
    this, &RecipePage::onRecipeStepSelected);
  layout->addWidget(m_recipeList);

  QHBoxLayout* fileButtons = new QHBoxLayout();

  m_newButton = new QPushButton("New");
  connect(m_newButton, &QPushButton::clicked, this, &RecipePage::onNewRecipe);
  fileButtons->addWidget(m_newButton);

  m_loadButton = new QPushButton("Load");
  connect(m_loadButton, &QPushButton::clicked, this, &RecipePage::onLoadRecipe);
  fileButtons->addWidget(m_loadButton);

  m_saveButton = new QPushButton("Save");
  connect(m_saveButton, &QPushButton::clicked, this, &RecipePage::onSaveRecipe);
  fileButtons->addWidget(m_saveButton);

  layout->addLayout(fileButtons);

  QHBoxLayout* stepButtons = new QHBoxLayout();

  m_moveUpButton = new QPushButton("↑");
  m_moveUpButton->setFixedWidth(50);
  connect(m_moveUpButton, &QPushButton::clicked, this, &RecipePage::onMoveStepUp);
  stepButtons->addWidget(m_moveUpButton);

  m_moveDownButton = new QPushButton("↓");
  m_moveDownButton->setFixedWidth(50);
  connect(m_moveDownButton, &QPushButton::clicked, this, &RecipePage::onMoveStepDown);
  stepButtons->addWidget(m_moveDownButton);

  stepButtons->addStretch();

  m_removeButton = new QPushButton("Remove");
  m_removeButton->setStyleSheet("QPushButton { background-color: #f44336; color: white; }");
  connect(m_removeButton, &QPushButton::clicked, this, &RecipePage::onRemoveStep);
  stepButtons->addWidget(m_removeButton);

  layout->addLayout(stepButtons);

  m_runRecipeButton = new QPushButton("▶ Run Recipe");
  m_runRecipeButton->setMinimumHeight(45);
  m_runRecipeButton->setStyleSheet("QPushButton { background-color: #ff8c00; color: white; }");
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
  QString filename = QFileDialog::getOpenFileName(
    this, "Load Recipe", "recipes/", "Recipe Files (*.json)"
  );

  if (filename.isEmpty()) return;

  if (m_recipeManager->loadRecipe(filename.toStdString())) {
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

  QString filename = QFileDialog::getSaveFileName(
    this, "Save Recipe", "recipes/", "Recipe Files (*.json)"
  );

  if (filename.isEmpty()) return;

  if (m_recipeManager->saveRecipe(filename.toStdString())) {
    QMessageBox::information(this, "Success", "Recipe saved successfully!");
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
  m_recipeList->setCurrentRow(m_recipeManager->getStepCount() - 1);
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
    refreshRecipeList();
    m_recipeList->setCurrentRow(m_selectedStepIndex - 1);
  }
}

void RecipePage::onMoveStepDown()
{
  if (m_recipeManager->moveStepDown(m_selectedStepIndex)) {
    refreshRecipeList();
    m_recipeList->setCurrentRow(m_selectedStepIndex + 1);
  }
}

void RecipePage::onRecipeStepSelected(int index)
{
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


void RecipePage::onRecipeNameChanged(const QString& name)
{
  m_recipeManager->setRecipeName(name.toStdString());
}

void RecipePage::onNicknameChanged(const QString& nickname)
{
  if (m_selectedStepIndex < 0) return;
  if (!m_recipeManager) return;

  m_recipeManager->setStepNickname(m_selectedStepIndex, nickname.toStdString());

  if (m_selectedStepIndex < m_recipeList->count()) {
    QListWidgetItem* item = m_recipeList->item(m_selectedStepIndex);
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
  m_recipeList->setCurrentRow(m_selectedStepIndex);
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
  m_recipeList->clear();

  for (const auto& step : m_recipeManager->getAllSteps()) {
    QString displayText = QString::fromStdString(step->userNickname);
    if (!step->enabled) {
      displayText = "[DISABLED] " + displayText;
    }

    QListWidgetItem* item = new QListWidgetItem(displayText);

    if (!step->enabled) {
      item->setForeground(QColor("#999999"));
      QFont font = item->font();
      font.setStrikeOut(true);
      item->setFont(font);
    }

    m_recipeList->addItem(item);
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

  m_sequenceEngine->SetUserPromptCallback(
    [this](const std::string& title, const std::string& message) -> bool {
    bool result = false;
    QEventLoop loop;

    QMetaObject::invokeMethod(this, [&]() {
      result = SequencePromptDialog::ShowPrompt(
        QString::fromStdString(title),
        QString::fromStdString(message),
        this
      );
      loop.quit();
    }, Qt::QueuedConnection);

    loop.exec();
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

  QMessageBox::StandardButton reply = QMessageBox::question(
    this, "Run Recipe",
    QString("Execute recipe '%1' with %2 step(s)?").arg(recipeName).arg(stepCount),
    QMessageBox::Yes | QMessageBox::No
  );

  if (reply != QMessageBox::Yes) return;

  std::cout << "[RecipePage] Starting recipe execution: "
    << recipeName.toStdString() << std::endl;

  // ✅ CRITICAL: Rebuild device registry before execution
  // This ensures all pneumatic slides and other devices are registered
  if (m_sequenceEngine) {
    std::cout << "[RecipePage] Rebuilding device registry..." << std::endl;
    m_sequenceEngine->BuildDeviceRegistry();
  }

  // Switch to progress panel
  showProgressPanel();

  // Disable run button while executing
  m_runRecipeButton->setEnabled(false);
  m_runRecipeButton->setText("⏳ Recipe Running...");

  // Create and start worker thread
  m_recipeWorker = new RecipeWorker(
    m_sequenceEngine,
    m_recipeManager,
    m_processFactory,
    this
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

  bool success = m_sequenceEngine->ExecuteSequence(sequence);

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

  if (m_progressWidget) {
    m_progressWidget->stopTimer();

    // Show final state
    int totalSteps = m_progressWidget->property("totalSteps").toInt();
    m_progressWidget->setCurrentStep(totalSteps, "Complete");
    m_progressWidget->setStatusText(success ? "✓ Completed!" : "✗ Failed");

    // Show completion hint and disable stop button
    m_progressWidget->showCompletionHint();
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

  // ✅ ULTRA SAFE: Clear scroll area completely
  QWidget* oldWidget = m_configScrollArea->takeWidget();
  if (oldWidget) {
    oldWidget->deleteLater();
  }

  // Reset pointer regardless
  m_progressWidget = nullptr;

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
    oldWidget->deleteLater();
  }

  // Reset pointer
  m_progressWidget = nullptr;

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

  QString filePath = QFileDialog::getOpenFileName(
    this,
    "Select Product Positions File",
    "./config",
    "Position Files (positions.json);;All Files (*.*)"
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