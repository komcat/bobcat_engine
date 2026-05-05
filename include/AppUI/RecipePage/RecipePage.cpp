// RecipePage.cpp
#include "RecipePage.h"
#include "RecipeManager.h"
#include "Theme.h"
#include "ThemeButtonStyles.h"
#include "RecipePageStyles.h"        // NEW: Centralized styles (now bound to Theme::Color::*)
#include "RecipeTreeItemFactory.h"   // NEW: Tree item factory
#include "RecipeClipboard.h"         // NEW: Clipboard
#include "RecipeRunWindow.h"         // NEW: Floating execution window
#include "RecipePageSettingsStore.h" // NEW: QSettings I/O for last-recipe persistence
#include "RecipeBrowserWidget.h"     // NEW: Self-contained "Recipes" tab
#include "ProcessCatalogWidget.h"    // NEW: Self-contained "Available Processes" left panel
#include "RecipeExecutionController.h"  // NEW: Owns RecipeWorker lifetime + signal forwarding
#include "QuickAddController.h"          // NEW: Ctrl+B two-key quick-add UX
#include "ApplicationContext.h"
#include "settings/ApplicationSettings.h"
#include "safety/SafetyWatchManager.h"
#include "unified_motion_manager.h"
#include "SequenceEngine.h"
#include "SequenceBuilder.h"
#include "SequenceContext.h"
#include "GripperSequenceHelpers.h"
#include "ProcessSequenceFactory.h"
#include "ProcessConfigWidget.h"
#include "ProcessPluginRegistry.h"
#include "IProcessPlugin.h"
#include "product_position_manager.h"
#include "RecipeWorker.h"
#include "RecipeProgressWidget.h"
#include "ManualAdjustWidget.h"
#include "../RunProgramPage/ManualAlignPointWidget.h"
#include "../RunProgramPage/UserInputWidget.h"
#include "SequencePromptDialog.h"
#include "LogManager.h"
#include "FileSelectorDialog.h"
#include "RecipeSaveAsDialog.h"
#include "ContextViewerWindow.h"
#include "ContextVariablePreviewWidget.h"
#include "GdiWindow/GdiCameraWindowManager.h"
#include <QSplitter>
#include <QLabel>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QFrame>
#include <QMetaObject>
#include <QThread>
#include <QTimer>
#include <QEventLoop>
#include <QApplication>
#include <QClipboard>
#include <QHeaderView>
#include <QCheckBox>
#include <QColorDialog>
#include <QMenu>
#include <QListWidget>
#include <QTreeWidget>
#include <QRegularExpression>
#include <QDropEvent>
#include <QMimeData>
#include <QSet>
#include <QSettings>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDirIterator>
#include <iostream>
#include <chrono>
#include <filesystem>

#ifdef HAS_OPENCV
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <QImage>
#include <QPixmap>
#endif

namespace {

// QSS builders for the runtime "product status" label that swaps between
// loaded / not-found / neutral states. Themed via Theme::Color::* so the
// palette propagates to all states from one place.
QString productLabelLoadedQss()
{
    return QString(
        "font-size: 11px; "
        "color: %1; "
        "font-weight: bold; "
        "padding: 5px; "
        "border: 1px solid %1; "
        "border-radius: 3px; "
        "background-color: %2;"
    ).arg(QLatin1String(Theme::Color::accentSoft),  // bright green text + border
          QLatin1String(Theme::Color::bgDeep));     // deep bg
}

QString productLabelErrorQss()
{
    return QString(
        "font-size: 11px; "
        "color: %1; "
        "padding: 5px; "
        "border: 1px solid %1; "
        "border-radius: 3px; "
        "background-color: %2;"
    ).arg(QLatin1String(Theme::Color::danger),    // red text + border
          QLatin1String(Theme::Color::bgDeep));   // deep neutral bg
}

QString productLabelNeutralQss()
{
    return QString(
        "font-size: 11px; "
        "color: %1; "
        "padding: 5px; "
        "background-color: %2;"
    ).arg(QLatin1String(Theme::Color::textSecondary),
          QLatin1String(Theme::Color::bgDeep));
}

// Same family as productLabelLoadedQss but used for the recipe-file label
// (no font-weight bold). Kept distinct so it can diverge later if needed.
QString recipeFileLabelLoadedQss()
{
    return QString(
        "font-size: 11px; "
        "color: %1; "
        "font-weight: bold; "
        "padding: 4px; "
        "background-color: %2; "
        "border: 1px solid %1; "
        "border-radius: 3px;"
    ).arg(QLatin1String(Theme::Color::accentSoft),
          QLatin1String(Theme::Color::bgDeep));
}

QString recipeFileLabelNeutralQss()
{
    return QString(
        "font-size: 11px; "
        "color: %1; "
        "padding: 4px; "
        "background-color: %2; "
        "border: 1px solid %3; "
        "border-radius: 3px;"
    ).arg(QLatin1String(Theme::Color::textSecondary),
          QLatin1String(Theme::Color::bgDeep),
          QLatin1String(Theme::Color::borderSubtle));
}

}  // namespace

RecipePage::RecipePage(ApplicationContext* context, TransformationMatrixLoader* matrixLoader, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_matrixLoader(matrixLoader)
  , m_recipeManager(new RecipeManager())
  , m_sequenceEngine(nullptr)
  , m_processFactory(new ProcessSequenceFactory())
  , m_selectedStepIndex(-1)
  , m_currentConfigWidget(nullptr)
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
  , m_clipboard(std::make_unique<RecipeClipboard>())  // ✅ NEW: Clipboard for copy/paste
  , m_contextVariablePreview(nullptr) // Context variable preview widget
  , m_pageState(PageState::Editing)  // Start in editing mode
  , m_middleTabWidget(nullptr)       // Middle panel tab widget
{
  setupUI();
  setupSequenceEngine();

  // Owns RecipeWorker lifetime + forwards its signals so we connect once
  // (here) instead of re-wiring on every execution.
  m_executionController = new RecipeExecutionController(this);
  connect(m_executionController, &RecipeExecutionController::executionStarted,
          this, &RecipePage::onExecutionStarted);
  connect(m_executionController, &RecipeExecutionController::stepBuildStarted,
          this, &RecipePage::onStepBuildStarted);
  connect(m_executionController, &RecipeExecutionController::stepBuildCompleted,
          this, &RecipePage::onStepBuildCompleted);
  connect(m_executionController, &RecipeExecutionController::sequenceExecutionStarted,
          this, &RecipePage::onSequenceExecutionStarted);
  connect(m_executionController, &RecipeExecutionController::executionCompleted,
          this, &RecipePage::onExecutionCompleted);
  connect(m_executionController, &RecipeExecutionController::executionStopped,
          this, &RecipePage::onExecutionStopped);
  connect(m_executionController, &RecipeExecutionController::logMessage,
          this, &RecipePage::onWorkerLogMessage);
  connect(m_executionController, &RecipeExecutionController::waitProgress,
          this, &RecipePage::onWaitProgress);
  connect(m_executionController, &RecipeExecutionController::stepExecutionStarted,
          this, &RecipePage::onStepExecutionStarted);
  connect(m_executionController, &RecipeExecutionController::stepExecutionCompleted,
          this, &RecipePage::onStepExecutionCompleted);
  connect(m_executionController, &RecipeExecutionController::executionPaused,
          this, &RecipePage::onExecutionPaused);

  // Quick add (Ctrl+B) controller. Forwards a chosen process type back
  // to the page for insertion + UI refresh.
  m_quickAddController = new QuickAddController(this, m_recipeManager, m_processCatalog, this);
  connect(m_quickAddController, &QuickAddController::processSelected,
          this, [this](const QString& processType) {
            m_recipeManager->addStep(processType.toStdString());
            refreshRecipeList();
            m_selectedStepIndex = m_recipeManager->getStepCount() - 1;
            selectStepByIndex(m_selectedStepIndex);
            updateConfigurationForm();
            std::cout << "[RecipePage] Quick Add: Added '"
                      << processType.toStdString() << "'" << std::endl;
          });

  // Initialize tree item factory after m_recipeTree is created in setupUI
  m_treeItemFactory = std::make_unique<RecipeTreeItemFactory>(
    m_recipeTree,
    m_recipeManager,
    // Enabled callback - optimized to update single item only
    [this](int stepIndex, bool enabled) {
      m_recipeManager->setStepEnabled(stepIndex, enabled);
      updateSingleStepItem(stepIndex);  // Fast: only update this item
      // Update config form if this is the selected step
      if (stepIndex == m_selectedStepIndex) {
        updateConfigurationForm();
      }
    }
  );

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
    m_productStatusLabel->setStyleSheet(RecipeStyles::productStatusLabelLoaded());

    // Populate recipe list for loaded product
    if (m_recipeBrowser) m_recipeBrowser->refresh();
  }

  std::cout << "[RecipePage] Initialized" << std::endl;
}

RecipePage::~RecipePage()
{
  // Clean up worker thread (owned by m_executionController; sync shutdown
  // here so the wait() happens before our other members are destroyed).
  if (m_executionController) {
    m_executionController->ShutdownSync();
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
  // Set dark theme background for entire page (using centralized styles)
  setStyleSheet(RecipeStyles::pageBackground());

  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(5, 0, 5, 5);
  mainLayout->setSpacing(0);

  QHBoxLayout* titleLayout = new QHBoxLayout();
  titleLayout->setSpacing(5);
  titleLayout->setContentsMargins(5, 5, 5, 5);

  m_backButton = new QPushButton("← Back to Main Menu");
  m_backButton->setFixedHeight(28);
  m_backButton->setStyleSheet(RecipeStyles::backButton());
  connect(m_backButton, &QPushButton::clicked, this, &RecipePage::onBackButtonClicked);
  titleLayout->addWidget(m_backButton);

  QLabel* titleLabel = new QLabel("Recipe Editor");
  titleLabel->setStyleSheet(RecipeStyles::pageTitleLabel());
  titleLabel->setAlignment(Qt::AlignCenter);
  titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  titleLayout->addWidget(titleLabel);

  // Recipe file name label
  m_recipeFileLabel = new QLabel("No recipe loaded");
  m_recipeFileLabel->setStyleSheet(RecipeStyles::recipeFileLabel());
  titleLayout->addWidget(m_recipeFileLabel);

  m_productStatusLabel = new QLabel("No product loaded");
  m_productStatusLabel->setStyleSheet(RecipeStyles::productStatusLabel());
  titleLayout->addWidget(m_productStatusLabel);

  // Execute buttons (Run Recipe / Run Selected / Breakpoint / CV / STOP) are
  // constructed later in createStepsTab() so they live directly under the
  // recipe Name field instead of in the page title bar.

  titleLayout->addSpacing(m_backButton->width());

  mainLayout->addLayout(titleLayout);

  m_mainSplitter = new QSplitter(Qt::Horizontal);
  m_mainSplitter->setOpaqueResize(false);
  m_mainSplitter->setHandleWidth(1);          // Hairline handles (visually unobtrusive)
  m_mainSplitter->setChildrenCollapsible(false);

  m_processCatalog = new ProcessCatalogWidget();
  connect(m_processCatalog, &ProcessCatalogWidget::addProcessRequested,
          this, &RecipePage::onAddProcessRequested);
  m_mainSplitter->addWidget(m_processCatalog);

  QWidget* middlePanel = new QWidget();
  createMiddlePanel(middlePanel);
  m_mainSplitter->addWidget(middlePanel);

  QWidget* rightPanel = new QWidget();
  createRightPanel(rightPanel);
  m_mainSplitter->addWidget(rightPanel);

  // Lock the splitter handles so the user cannot drag the panels.
  // Handle 0 is unused; handles 1 and 2 separate widgets 0|1 and 1|2.
  if (auto* h1 = m_mainSplitter->handle(1)) h1->setEnabled(false);
  if (auto* h2 = m_mainSplitter->handle(2)) h2->setEnabled(false);

  mainLayout->addWidget(m_mainSplitter, 1);

  // Apply initial 20% / 30% / 50% sizing.  resizeEvent keeps it pinned on resize.
  applyPanelSizes();
}

void RecipePage::applyPanelSizes()
{
  if (!m_mainSplitter) {
    return;
  }
  // Compute pixel sizes from the current splitter width.
  // Falls back to the page width if the splitter has not been laid out yet.
  int totalWidth = m_mainSplitter->width();
  if (totalWidth <= 0) {
    totalWidth = width();
  }
  if (totalWidth <= 0) {
    return;
  }

  const int leftWidth   = (totalWidth * 20) / 100;
  const int rightWidth  = (totalWidth * 50) / 100;
  const int middleWidth = totalWidth - leftWidth - rightWidth;
  m_mainSplitter->setSizes({ leftWidth, middleWidth, rightWidth });
}

void RecipePage::resizeEvent(QResizeEvent* event)
{
  QWidget::resizeEvent(event);
  applyPanelSizes();
}

void RecipePage::createMiddlePanel(QWidget* middlePanel)
{
  QVBoxLayout* layout = new QVBoxLayout(middlePanel);
  layout->setContentsMargins(5, 5, 5, 5);
  layout->setSpacing(5);

  QLabel* title = new QLabel("Recipe Steps");
  title->setStyleSheet(RecipeStyles::titleLabel());
  layout->addWidget(title);

  // ========== TAB WIDGET ==========
  m_middleTabWidget = new QTabWidget();
  m_middleTabWidget->setStyleSheet(QString(
    "QTabWidget::pane { border: none; background-color: %1; }"
    "QTabBar { background-color: %1; border: none; }"
    "QTabBar::tab {"
    "  background-color: %2;"
    "  color: %3;"
    "  padding: 8px 16px;"
    "  margin-right: 2px;"
    "  border: none;"
    "  font-size: 12px;"
    "  font-weight: bold;"
    "  border-top-left-radius: 3px;"
    "  border-top-right-radius: 3px;"
    "  border-bottom: 3px solid transparent;"
    "}"
    "QTabBar::tab:selected {"
    "  background-color: %4;"
    "  color: %5;"
    "  font-weight: bold;"
    "  border-bottom: 3px solid %4;"
    "}"
    "QTabBar::tab:hover {"
    "  background-color: %6;"
    "  color: %7;"
    "}"
  ).arg(QLatin1String(Theme::Color::bgPrimary),     // %1 pane bg
        QLatin1String(Theme::Color::bgDeep),        // %2 inactive tab
        QLatin1String(Theme::Color::textSecondary), // %3 inactive text
        QLatin1String(Theme::Color::primary),       // %4 selected bg
        QLatin1String(Theme::Color::textOnPrimary), // %5 selected text
        QLatin1String(Theme::Color::bgHover),       // %6 hover
        QLatin1String(Theme::Color::textPrimary))); // %7 hover text

  // Create tabs
  m_recipeBrowser = new RecipeBrowserWidget(m_context, m_recipeManager);
  connect(m_recipeBrowser, &RecipeBrowserWidget::loadRecipeRequested,
          this, &RecipePage::onRecipeLoadRequested);
  QWidget* stepsTab = new QWidget();
  QWidget* contextTab = new QWidget();
  QWidget* visionResultTab = new QWidget();

  createStepsTab(stepsTab);

  // ===== Context Variables tab =====
  {
    QVBoxLayout* contextLayout = new QVBoxLayout(contextTab);
    contextLayout->setContentsMargins(5, 5, 5, 5);

    m_contextVariablePreview = new ContextVariablePreviewWidget();
    connect(m_contextVariablePreview, &ContextVariablePreviewWidget::variableClicked,
      [](const QString& varName) {
        // Copy variable name to clipboard when clicked
        QApplication::clipboard()->setText(varName);
        std::cout << "[RecipePage] Copied to clipboard: " << varName.toStdString() << std::endl;
      });
    contextLayout->addWidget(m_contextVariablePreview, 1);
  }

  // ===== Vision Result tab =====
  {
    QVBoxLayout* visionTabLayout = new QVBoxLayout(visionResultTab);
    visionTabLayout->setContentsMargins(5, 5, 5, 5);

    m_visionResultGroup = new QGroupBox("Vision Result");
    m_visionResultGroup->setStyleSheet(QString(
      "QGroupBox { "
      "  font-weight: bold; "
      "  color: %1; "
      "  border: 1px solid %2; "
      "  border-radius: 4px; "
      "  margin-top: 5px; "
      "  padding-top: 10px; "
      "} "
      "QGroupBox::title { "
      "  subcontrol-origin: margin; "
      "  left: 10px; "
      "  padding: 0 5px; "
      "}"
    ).arg(QLatin1String(Theme::Color::accent),
          QLatin1String(Theme::Color::borderSubtle)));
    QVBoxLayout* visionLayout = new QVBoxLayout(m_visionResultGroup);
    visionLayout->setContentsMargins(5, 15, 5, 5);

    m_visionResultImageLabel = new QLabel("No vision result");
    m_visionResultImageLabel->setAlignment(Qt::AlignCenter);
    m_visionResultImageLabel->setMinimumHeight(150);
    m_visionResultImageLabel->setStyleSheet(QString(
      "QLabel { "
      "  background-color: %1; "
      "  border: 1px solid %2; "
      "  color: %3; "
      "  font-style: italic; "
      "}"
    ).arg(QLatin1String(Theme::Color::bgDeep),
          QLatin1String(Theme::Color::borderSubtle),
          QLatin1String(Theme::Color::textSecondary)));
    visionLayout->addWidget(m_visionResultImageLabel);

    visionTabLayout->addWidget(m_visionResultGroup, 1);
  }

  m_middleTabWidget->addTab(m_recipeBrowser, "Recipes");
  m_middleTabWidget->addTab(stepsTab, "Steps");
  m_middleTabWidget->addTab(contextTab, "Context");
  m_middleTabWidget->addTab(visionResultTab, "Vision Result");

  // Start on Steps tab by default
  m_middleTabWidget->setCurrentIndex(1);

  layout->addWidget(m_middleTabWidget, 1);
}

void RecipePage::createStepsTab(QWidget* stepsTab)
{
  QVBoxLayout* layout = new QVBoxLayout(stepsTab);
  layout->setContentsMargins(5, 5, 5, 5);
  layout->setSpacing(5);

  QHBoxLayout* nameLayout = new QHBoxLayout();
  nameLayout->setSpacing(5);
  QLabel* nameLabel = new QLabel("Name:");
  nameLabel->setStyleSheet(RecipeStyles::sectionLabel());
  m_recipeNameEdit = new QLineEdit(QString::fromStdString(m_recipeManager->getRecipeName()));
  m_recipeNameEdit->setStyleSheet(QString(
    "QLineEdit { "
    "  background-color: %1; "
    "  color: %2; "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "  padding: 6px 10px; "
    "  border: 2px solid %3; "
    "  border-radius: 4px; "
    "} "
    "QLineEdit:focus { "
    "  border: 2px solid %4; "
    "  background-color: %5; "
    "}"
  ).arg(QLatin1String(Theme::Color::bgPrimary),
        QLatin1String(Theme::Color::textPrimary),
        QLatin1String(Theme::Color::primary),
        QLatin1String(Theme::Color::primaryHover),
        QLatin1String(Theme::Color::bgDeep)));
  connect(m_recipeNameEdit, &QLineEdit::textChanged, this, &RecipePage::onRecipeNameChanged);
  nameLayout->addWidget(nameLabel);
  nameLayout->addWidget(m_recipeNameEdit, 1);

  // Quick Save button — success green family
  QPushButton* quickSaveButton = new QPushButton("Save");
  quickSaveButton->setToolTip("Save recipe (Ctrl+S)");
  quickSaveButton->setFixedSize(60, 32);
  quickSaveButton->setStyleSheet(Theme::Buttons::bezelSuccess());
  connect(quickSaveButton, &QPushButton::clicked, this, &RecipePage::onSaveRecipe);
  nameLayout->addWidget(quickSaveButton);

  layout->addLayout(nameLayout);

  // ========== EXECUTE BUTTONS ROW (under the Name field) ==========
  QHBoxLayout* executeRow = new QHBoxLayout();
  executeRow->setSpacing(5);

  // Run Recipe button — warning orange family ("go" action)
  m_runRecipeButton = new QPushButton("Run Recipe");
  m_runRecipeButton->setFixedHeight(28);
  m_runRecipeButton->setStyleSheet(Theme::Buttons::bezelWarning());
  connect(m_runRecipeButton, &QPushButton::clicked, this, &RecipePage::onRunRecipe);
  executeRow->addWidget(m_runRecipeButton);

  // Run Selected button — primary blue family
  m_runSelectedStepButton = new QPushButton("Run Selected");
  m_runSelectedStepButton->setFixedHeight(28);
  m_runSelectedStepButton->setStyleSheet(Theme::Buttons::bezelPrimary());
  connect(m_runSelectedStepButton, &QPushButton::clicked, this, &RecipePage::onRunSelectedStep);
  executeRow->addWidget(m_runSelectedStepButton);

  // Breakpoint button — danger red family ("halt here")
  m_breakpointButton = new QPushButton("B");
  m_breakpointButton->setToolTip("Toggle breakpoint on selected step");
  m_breakpointButton->setFixedSize(40, 28);
  m_breakpointButton->setStyleSheet(Theme::Buttons::bezelDanger());
  connect(m_breakpointButton, &QPushButton::clicked, this, &RecipePage::onBreakpointButtonClicked);
  executeRow->addWidget(m_breakpointButton);

  // Context Viewer button
  m_contextViewerButton = new QPushButton("CV");
  m_contextViewerButton->setToolTip("Context Viewer - View execution context variables");
  m_contextViewerButton->setFixedSize(40, 28);
  m_contextViewerButton->setStyleSheet(RecipeStyles::contextViewerButton());
  connect(m_contextViewerButton, &QPushButton::clicked, this, &RecipePage::onShowContextViewer);
  executeRow->addWidget(m_contextViewerButton);

  executeRow->addStretch(1);

  // STOP button (emergency stop for motors and sequence) - on the far right
  m_stopButton = new QPushButton("⚠ STOP");
  m_stopButton->setObjectName("stopButton");
  m_stopButton->setToolTip("Emergency stop - halt all motion and sequence execution");
  m_stopButton->setFixedSize(70, 28);
  m_stopButton->setStyleSheet(RecipeStyles::emergencyStopButton());
  connect(m_stopButton, &QPushButton::clicked, this, &RecipePage::onStopRecipeRequested);
  executeRow->addWidget(m_stopButton);

  layout->addLayout(executeRow);

  // ========== DROPDOWN MENUS ROW ==========
  QHBoxLayout* menuRow = new QHBoxLayout();
  menuRow->setSpacing(3);

  // Common dropdown button style — neutral palette family
  const QString menuButtonStyle = QString(
    "QPushButton { "
    "  background-color: %1; "
    "  color: white; "
    "  font-weight: bold; "
    "  padding: 6px 12px; "
    "  min-height: 28px; "
    "  border: 1px solid %2; "
    "  border-radius: 3px; "
    "} "
    "QPushButton:hover { "
    "  background-color: %3; "
    "} "
    "QPushButton:pressed, QPushButton:menu-open { "
    "  background-color: %4; "
    "} "
    "QPushButton::menu-indicator { "
    "  subcontrol-origin: padding; "
    "  subcontrol-position: center right; "
    "  right: 5px; "
    "}"
  ).arg(QLatin1String(Theme::Color::bgSurface),
        QLatin1String(Theme::Color::borderNormal),
        QLatin1String(Theme::Color::bgHover),
        QLatin1String(Theme::Color::bgPrimary));

  const QString menuStyle = QString(
    "QMenu { "
    "  background-color: %1; "
    "  border: 1px solid %2; "
    "  padding: 4px; "
    "} "
    "QMenu::item { "
    "  background-color: transparent; "
    "  padding: 6px 20px; "
    "  color: %3; "
    "} "
    "QMenu::item:selected { "
    "  background-color: %4; "
    "  color: %5; "
    "} "
    "QMenu::separator { "
    "  height: 1px; "
    "  background: %2; "
    "  margin: 4px 10px; "
    "}"
  ).arg(QLatin1String(Theme::Color::bgDeep),
        QLatin1String(Theme::Color::borderNormal),
        QLatin1String(Theme::Color::textPrimary),
        QLatin1String(Theme::Color::primary),
        QLatin1String(Theme::Color::textOnPrimary));

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

  editMenu->addAction("Copy", QKeySequence::Copy, this, &RecipePage::onCopySelectedSteps);
  editMenu->addAction("Paste", QKeySequence::Paste, this, &RecipePage::onPasteSteps);
  editMenu->addSeparator();
  editMenu->addAction("Move Up", QKeySequence(Qt::CTRL | Qt::Key_Up), this, &RecipePage::onMoveStepUp);
  editMenu->addAction("Move Down", QKeySequence(Qt::CTRL | Qt::Key_Down), this, &RecipePage::onMoveStepDown);
  editMenu->addAction("Move to Line...", QKeySequence(Qt::CTRL | Qt::Key_G), this, &RecipePage::onMoveStepToLine);
  editMenu->addSeparator();
  editMenu->addAction("Remove", QKeySequence::Delete, this, &RecipePage::onRemoveStep);

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

  selectMenu->addAction("Select All", QKeySequence::SelectAll, this, &RecipePage::onSelectAllProcesses);
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
  m_recipeTree->setHeaderLabels({ "#", "", "Process Instance", "Details", "Run" });
  m_recipeTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_recipeTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_recipeTree->setRootIsDecorated(false);  // Hide expand arrows for clean look
  m_recipeTree->setIndentation(24);          // Indent for nesting clarity

  // Performance optimizations
  m_recipeTree->setUniformRowHeights(true);  // Major performance boost - enables height caching
  m_recipeTree->viewport()->setAttribute(Qt::WA_StaticContents);  // Reduce repaints

  m_recipeTree->header()->setStretchLastSection(false);
  m_recipeTree->header()->setSectionResizeMode(0, QHeaderView::Fixed);             // Step number + stripe
  m_recipeTree->setColumnWidth(0, 38);
  m_recipeTree->header()->setSectionResizeMode(1, QHeaderView::Fixed);             // Symbol column fixed
  m_recipeTree->setColumnWidth(1, 30);
  m_recipeTree->header()->setSectionResizeMode(2, QHeaderView::Fixed);             // Process Instance fixed
  m_recipeTree->setColumnWidth(2, 200);
  m_recipeTree->header()->setSectionResizeMode(3, QHeaderView::Stretch);            // Details stretches
  m_recipeTree->header()->setSectionResizeMode(4, QHeaderView::Fixed);             // Run checkbox fixed
  m_recipeTree->setColumnWidth(4, 50);

  m_recipeTree->setStyleSheet(RecipeStyles::recipeStepsTree());

  connect(m_recipeTree, &QTreeWidget::itemSelectionChanged,
    this, &RecipePage::onRecipeStepSelected);

  layout->addWidget(m_recipeTree, 1);
}

void RecipePage::createRightPanel(QWidget* rightPanel)
{
  QVBoxLayout* layout = new QVBoxLayout(rightPanel);
  layout->setContentsMargins(5, 5, 5, 5);

  QLabel* title = new QLabel("Process Configuration");
  title->setStyleSheet(RecipeStyles::sectionLabel());
  layout->addWidget(title);

  // Process parameters now occupy the full right panel.  Context Variables and
  // Vision Result have been moved into tabs in the middle panel.
  m_configScrollArea = new QScrollArea();
  m_configScrollArea->setWidgetResizable(true);
  m_configScrollArea->setStyleSheet(RecipeStyles::configScrollArea());
  layout->addWidget(m_configScrollArea, 1);  // Stretch factor 1 to fill available space

  // Create the floating execution window.  Parented to `this` so destruction
  // cascades, but flagged Qt::Window so it's a real top-level window.
  m_runWindow = new RecipeRunWindow(this);

  // Create manual adjust widget and register it with the run window
  m_manualAdjustWidget = new ManualAdjustWidget(
    m_context,
    m_matrixLoader
  );
  m_manualAdjustWidget->setStyleSheet(
    QString("background-color: %1;").arg(RecipeStyles::Colors::BG_MEDIUM()));

  // Connect manual adjust response
  connect(m_manualAdjustWidget, &ManualAdjustWidget::responseReceived,
    this, &RecipePage::onManualAdjustResponse);
  m_runWindow->setActiveWidget(m_manualAdjustWidget);

  // Create manual align point widget and register it with the run window
  m_manualAlignPointWidget = new ManualAlignPointWidget(
    m_context->motionManager,
    m_context
  );
  m_manualAlignPointWidget->setStyleSheet(
    QString("background-color: %1;").arg(RecipeStyles::Colors::BG_MEDIUM()));

  // Connect manual align point response
  connect(m_manualAlignPointWidget, &ManualAlignPointWidget::responseReceived,
    this, &RecipePage::onManualAlignPointResponse);
  m_runWindow->setActiveWidget(m_manualAlignPointWidget);

  // Create user input widget and register it with the run window
  m_userInputWidget = new UserInputWidget();
  m_userInputWidget->setStyleSheet(QString("background-color: %1;")
                                     .arg(QLatin1String(Theme::Color::bgPrimary)));

  // Apply input alignment from application settings (same as prompt)
  if (m_context && m_context->applicationSettings) {
    std::string alignStr = m_context->applicationSettings->GetString("UI", "PromptAlignment", "Center");
    if (alignStr == "TopLeft") {
      m_userInputWidget->setAlignment(UserInputWidget::Alignment::TopLeft);
    }
  }

  // Connect user input response
  connect(m_userInputWidget, &UserInputWidget::responseReceived,
    this, &RecipePage::onUserInputResponse);
  m_runWindow->setActiveWidget(m_userInputWidget);

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
    m_productStatusLabel->setStyleSheet(RecipeStyles::productStatusLabelLoaded());

    if (m_loadedProductLabel) {
      m_loadedProductLabel->setText(QString("Product: %1\nFile: %2")
        .arg(QString::fromStdString(productName))
        .arg(QString::fromStdString(productPath)));
      m_loadedProductLabel->setStyleSheet(productLabelLoadedQss());
    }

    std::cout << "[RecipePage] Product changed to: " << productName << std::endl;
  }

  // Refresh recipe list for new product
  if (m_recipeBrowser) m_recipeBrowser->refresh();

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
    RecipePageSettingsStore::SaveLastRecipePath(filename);

    // Auto-load associated product file
    std::string productFile = m_recipeManager->getProductFile();
    if (!productFile.empty()) {
      if (m_context->productPositionManager) {
        if (m_context->productPositionManager->LoadProduct(productFile)) {
          m_context->productPositionManager->ExportToMotionManager(*m_context->motionManager);

          std::string loadedProductName = m_context->productPositionManager->GetCurrentProductName();

          m_productStatusLabel->setText(QString("Product: %1")
            .arg(QString::fromStdString(loadedProductName)));
          m_productStatusLabel->setStyleSheet(RecipeStyles::productStatusLabelLoaded());

          if (m_loadedProductLabel) {
            m_loadedProductLabel->setText(QString("Product: %1\nFile: %2")
              .arg(QString::fromStdString(loadedProductName))
              .arg(QString::fromStdString(productFile)));
            m_loadedProductLabel->setStyleSheet(productLabelLoadedQss());
          }

          std::cout << "[RecipePage] Auto-loaded product: " << productFile << std::endl;

          QMessageBox::information(this, "Recipe Loaded",
            QString("Recipe '%1' loaded successfully!\n\nAuto-loaded product: %2")
            .arg(QString::fromStdString(m_recipeManager->getRecipeName()))
            .arg(productName));
        }
        else {
          if (m_loadedProductLabel) {
            m_loadedProductLabel->setText(QString("⚠ Product file not found:\n%1")
              .arg(QString::fromStdString(productFile)));
            m_loadedProductLabel->setStyleSheet(productLabelErrorQss());
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
        m_loadedProductLabel->setStyleSheet(productLabelNeutralQss());
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
    updateRecipeFileNameDisplay();  // Update recipe file name label
    // Refresh recipe list after save
    if (m_recipeBrowser) m_recipeBrowser->refresh();
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

  // Extract filename without path and extension to use as recipe name
  QFileInfo fileInfo(filename);
  QString newRecipeName = fileInfo.completeBaseName();

  // Update recipe name to match the saved file's name
  m_recipeManager->setRecipeName(newRecipeName.toStdString());
  m_recipeNameEdit->setText(newRecipeName);

  // Regenerate unique IDs so the new file is an independent copy
  m_recipeManager->regenerateAllUniqueIds();

  if (m_recipeManager->saveRecipe(filename.toStdString())) {
    updateRecipeFileNameDisplay();  // Update recipe file name label
    // Also save to QSettings as last loaded recipe
    RecipePageSettingsStore::SaveLastRecipePath(filename);

    QMessageBox::information(this, "Success",
      QString("Recipe saved as:\n%1").arg(filename));
  }
  else {
    QMessageBox::critical(this, "Error", "Failed to save recipe file.");
  }

  // Refresh recipe list and step list (to show new unique IDs)
  if (m_recipeBrowser) m_recipeBrowser->refresh();
  refreshRecipeList();
}

// ============================================================================
// RECIPE LOAD (request from RecipeBrowserWidget)
// ============================================================================

void RecipePage::onRecipeLoadRequested(const QString& filePath)
{
  if (filePath.isEmpty()) return;

  if (m_recipeManager->loadRecipe(filePath.toStdString())) {
    QFileInfo loadedFileInfo(filePath);
    m_recipeNameEdit->setText(loadedFileInfo.baseName());
    refreshRecipeList();
    clearConfigurationForm();
    updateRecipeFileNameDisplay();

    // Save to settings for persistence
    RecipePageSettingsStore::SaveLastRecipePath(filePath);

    // Auto-load associated product file
    std::string productFile = m_recipeManager->getProductFile();
    if (!productFile.empty()) {
      if (m_context->productPositionManager) {
        if (m_context->productPositionManager->LoadProduct(productFile)) {
          m_context->productPositionManager->ExportToMotionManager(*m_context->motionManager);

          std::string productName = m_context->productPositionManager->GetCurrentProductName();

          m_productStatusLabel->setText(QString("Product: %1")
            .arg(QString::fromStdString(productName)));
          m_productStatusLabel->setStyleSheet(RecipeStyles::productStatusLabelLoaded());

          if (m_loadedProductLabel) {
            m_loadedProductLabel->setText(QString("Product: %1\nFile: %2")
              .arg(QString::fromStdString(productName))
              .arg(QString::fromStdString(productFile)));
            m_loadedProductLabel->setStyleSheet(productLabelLoadedQss());
          }

          std::cout << "[RecipePage] Auto-loaded product: " << productFile << std::endl;
        }
      }
    }

    // Switch to Steps tab after loading
    if (m_middleTabWidget) {
      m_middleTabWidget->setCurrentIndex(1);
    }

    std::cout << "[RecipePage] Loaded recipe from list: " << filePath.toStdString() << std::endl;
  }
  else {
    QMessageBox::critical(this, "Error", "Failed to load recipe file.");
  }
}

void RecipePage::loadRecipeFromFile(const QString& filePath)
{
  if (filePath.isEmpty()) return;

  if (m_recipeManager->loadRecipe(filePath.toStdString())) {
    m_recipeNameEdit->setText(QString::fromStdString(m_recipeManager->getRecipeName()));
    refreshRecipeList();
    clearConfigurationForm();
    updateRecipeFileNameDisplay();
    RecipePageSettingsStore::SaveLastRecipePath(filePath);

    // Auto-load associated product file
    std::string productFile = m_recipeManager->getProductFile();
    if (!productFile.empty()) {
      if (m_context->productPositionManager) {
        if (m_context->productPositionManager->LoadProduct(productFile)) {
          m_context->productPositionManager->ExportToMotionManager(*m_context->motionManager);

          std::string productName = m_context->productPositionManager->GetCurrentProductName();

          m_productStatusLabel->setText(QString("Product: %1")
            .arg(QString::fromStdString(productName)));
          m_productStatusLabel->setStyleSheet(RecipeStyles::productStatusLabelLoaded());

          if (m_loadedProductLabel) {
            m_loadedProductLabel->setText(QString("Product: %1\nFile: %2")
              .arg(QString::fromStdString(productName))
              .arg(QString::fromStdString(productFile)));
            m_loadedProductLabel->setStyleSheet(productLabelLoadedQss());
          }

          std::cout << "[RecipePage] Auto-loaded product: " << productFile << std::endl;
        }
      }
    }

    // Switch to Steps tab after loading
    if (m_middleTabWidget) {
      m_middleTabWidget->setCurrentIndex(1);
    }

    std::cout << "[RecipePage] Loaded recipe from file: " << filePath.toStdString() << std::endl;
  }
  else {
    QMessageBox::critical(this, "Error",
      QString("Failed to load recipe file:\n%1").arg(filePath));
  }
}

void RecipePage::onAddProcessRequested(const QString& processType)
{
  if (processType.isEmpty()) return;

  // Insert at current selection + 1 (after selected), or at end if nothing selected
  int insertIndex = (m_selectedStepIndex >= 0) ? m_selectedStepIndex + 1 : m_recipeManager->getStepCount();
  m_recipeManager->insertStep(insertIndex, processType.toStdString());
  refreshRecipeList();
  selectStepByIndex(insertIndex);
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
      m_productStatusLabel->setStyleSheet(RecipeStyles::productStatusLabelLoaded());

      // Update loaded product label in middle panel
      if (m_loadedProductLabel) {
        m_loadedProductLabel->setText(QString("Product: %1\nFile: %2")
          .arg(QString::fromStdString(productName))
          .arg(QString::fromStdString(m_recipeManager->getProductFile())));
        m_loadedProductLabel->setStyleSheet(productLabelLoadedQss());
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

  // Collect step indices and sort them
  QList<int> indices;
  for (QTreeWidgetItem* item : selectedItems) {
    int stepIndex = item->data(0, Qt::UserRole).toInt();
    indices.append(stepIndex);
  }
  std::sort(indices.begin(), indices.end());

  // Collect steps to copy
  std::vector<const ProcessInstance*> stepsToCopy;
  for (int stepIndex : indices) {
    const ProcessInstance* step = m_recipeManager->getStep(stepIndex);
    if (step) {
      stepsToCopy.push_back(step);
    }
  }

  // Use clipboard to copy steps
  m_clipboard->copySteps(stepsToCopy);

  // Enable paste button
  if (m_pasteButton) {
    m_pasteButton->setEnabled(m_clipboard->hasContent());
  }

  // Show feedback
  QString msg = QString::fromStdString(m_clipboard->getSummary());
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
  if (!m_clipboard->hasContent()) {
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
  for (const auto& clipStep : m_clipboard->getSteps()) {
    std::string nickname = clipStep.userNickname;

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
  int oldIndex = m_selectedStepIndex;
  if (m_recipeManager->moveStepUp(oldIndex)) {
    int newIndex = oldIndex - 1;
    m_selectedStepIndex = newIndex;
    // Fast update: only update the two swapped items
    updateTreeItemsForSwap(newIndex, oldIndex);
    selectStepByIndex(m_selectedStepIndex);
  }
}

void RecipePage::onMoveStepDown()
{
  int oldIndex = m_selectedStepIndex;
  if (m_recipeManager->moveStepDown(oldIndex)) {
    int newIndex = oldIndex + 1;
    m_selectedStepIndex = newIndex;
    // Fast update: only update the two swapped items
    updateTreeItemsForSwap(oldIndex, newIndex);
    selectStepByIndex(m_selectedStepIndex);
  }
}

void RecipePage::onMoveStepToLine()
{
  if (m_selectedStepIndex < 0) {
    QMessageBox::warning(this, "No Selection", "Please select a step to move.");
    return;
  }

  int stepCount = m_recipeManager->getStepCount();
  if (stepCount <= 1) {
    return;  // Nothing to move
  }

  // Show dialog with 1-based line numbers for user-friendly display
  bool ok;
  int targetLine = QInputDialog::getInt(
    this,
    "Move to Line",
    QString("Move step from line %1 to line (1-%2):").arg(m_selectedStepIndex + 1).arg(stepCount),
    m_selectedStepIndex + 1,  // Default: current position
    1,                        // Min: line 1
    stepCount,                // Max: last line
    1,                        // Step
    &ok
  );

  if (!ok) {
    return;  // User cancelled
  }

  // Convert 1-based line to 0-based index
  int targetIndex = targetLine - 1;

  if (m_recipeManager->moveStepTo(m_selectedStepIndex, targetIndex)) {
    m_selectedStepIndex = targetIndex;
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
    if (m_executionController && m_executionController->IsRunning()) {
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
      if (!(m_executionController && m_executionController->IsRunning())) {
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
      // Use factory to update only the process name label
      m_treeItemFactory->updateProcessNameLabel(currentItem, step);
    }
  }
}

void RecipePage::onEnabledChanged(bool enabled)
{
  if (m_selectedStepIndex < 0) return;

  m_recipeManager->setStepEnabled(m_selectedStepIndex, enabled);
  updateSingleStepItem(m_selectedStepIndex);  // Fast: only update this item
}

void RecipePage::onParameterChanged()
{
  // Verify signal is from current widget (prevent stale signal processing)
  QObject* signalSender = sender();
  if (signalSender && signalSender != m_currentConfigWidget) {
    return;
  }

  if (m_selectedStepIndex < 0 || !m_currentConfigWidget) {
    return;
  }

  auto params = m_currentConfigWidget->getParameters();

  if (params.empty()) {
    std::cout << "[RecipePage] WARNING: No parameters returned from widget!" << std::endl;
    return;
  }

  for (const auto& [key, value] : params) {
    m_recipeManager->setStepParameter(m_selectedStepIndex, key, value);
  }

  // Refresh the tree row so the inline Details column reflects the new params.
  updateSingleStepItem(m_selectedStepIndex);
}

void RecipePage::refreshRecipeList()
{
  // Block signals and updates during refresh for performance
  m_recipeTree->blockSignals(true);
  m_recipeTree->setUpdatesEnabled(false);

  // Store currently selected item index before clearing
  int previousSelection = m_selectedStepIndex;

  // Clear tree
  m_recipeTree->clear();

  // Stack to track parent nodes for nesting
  std::vector<QTreeWidgetItem*> parentStack;

  int stepIndex = 0;
  for (const auto& step : m_recipeManager->getAllSteps()) {
    QTreeWidgetItem* currentParent = parentStack.empty() ? nullptr : parentStack.back();

    // Determine if this step starts or ends a block
    bool isBlockStart = RecipeManager::isBlockStart(step->processType);
    bool isBlockEnd = RecipeManager::isBlockEnd(step->processType);
    bool isElse = (step->processType == "Conditional Else");

    // Use factory to create tree item with all widgets
    QTreeWidgetItem* item = m_treeItemFactory->createStepItem(step.get(), stepIndex, currentParent);

    // Manage parent stack for nesting
    if (isBlockStart) {
      // This item becomes a parent for subsequent items
      parentStack.push_back(item);
      item->setExpanded(true);  // Auto-expand by default
    }
    else if (isElse) {
      // Else is at the same level as If, so we stay with same parent
      // For now, keep it simple: Else appears at same level
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

  // Re-enable updates and signals
  m_recipeTree->setUpdatesEnabled(true);
  m_recipeTree->blockSignals(false);

  // Update context variable preview
  updateContextVariablePreview();
}

void RecipePage::updateContextVariablePreview()
{
  if (m_contextVariablePreview) {
    m_contextVariablePreview->updateFromRecipe(m_recipeManager->getAllSteps());
  }
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

QTreeWidgetItem* RecipePage::findTreeItemByIndex(int stepIndex)
{
  QTreeWidgetItemIterator it(m_recipeTree);
  while (*it) {
    int itemIndex = (*it)->data(0, Qt::UserRole).toInt();
    if (itemIndex == stepIndex) {
      return *it;
    }
    ++it;
  }
  return nullptr;
}

void RecipePage::updateTreeItemsForSwap(int index1, int index2)
{
  // Find both tree items
  QTreeWidgetItem* item1 = findTreeItemByIndex(index1);
  QTreeWidgetItem* item2 = findTreeItemByIndex(index2);

  if (!item1 || !item2) {
    // Fallback to full refresh if items not found
    refreshRecipeList();
    return;
  }

  // Get the step data (already swapped in RecipeManager)
  const ProcessInstance* step1 = m_recipeManager->getStep(index1);
  const ProcessInstance* step2 = m_recipeManager->getStep(index2);

  if (!step1 || !step2) {
    refreshRecipeList();
    return;
  }

  // Block signals during update
  m_recipeTree->blockSignals(true);

  // Update both items with their new data
  m_treeItemFactory->updateStepItem(item1, step1, index1);
  m_treeItemFactory->updateStepItem(item2, step2, index2);

  m_recipeTree->blockSignals(false);
}

void RecipePage::updateSingleStepItem(int stepIndex)
{
  // Find the tree item for this step
  QTreeWidgetItem* item = findTreeItemByIndex(stepIndex);

  if (!item) {
    // Fallback to full refresh if item not found
    refreshRecipeList();
    return;
  }

  // Get the step data
  const ProcessInstance* step = m_recipeManager->getStep(stepIndex);

  if (!step) {
    refreshRecipeList();
    return;
  }

  // Block signals during update
  m_recipeTree->blockSignals(true);

  // Update only this item
  m_treeItemFactory->updateStepItem(item, step, stepIndex);

  m_recipeTree->blockSignals(false);
}

void RecipePage::updateRecipeFileNameDisplay()
{
  // Get current recipe file path
  std::string currentPath = m_recipeManager->getCurrentFilePath();

  if (currentPath.empty()) {
    // No file loaded yet
    m_recipeFileLabel->setText("No recipe file");
    m_recipeFileLabel->setStyleSheet(recipeFileLabelNeutralQss());
  }
  else {
    // Extract just the filename from full path
    QString fullPath = QString::fromStdString(currentPath);
    QFileInfo fileInfo(fullPath);
    QString fileName = fileInfo.fileName();

    m_recipeFileLabel->setText(QString("Recipe: %1").arg(fileName));
    m_recipeFileLabel->setStyleSheet(recipeFileLabelLoadedQss());
  }
}

void RecipePage::updateConfigurationForm()
{
  const ProcessInstance* instance = m_recipeManager->getStep(m_selectedStepIndex);
  if (!instance) {
    clearConfigurationForm();
    return;
  }

  // Safe widget cleanup - disconnect signals first to prevent stale signal delivery
  if (m_currentConfigWidget) {
    disconnect(m_currentConfigWidget, &ProcessConfigWidget::parameterChanged,
      this, &RecipePage::onParameterChanged);
    disconnect(m_currentConfigWidget, &ProcessConfigWidget::nicknameChanged,
      this, &RecipePage::onNicknameChanged);
    disconnect(m_currentConfigWidget, &ProcessConfigWidget::enabledChanged,
      this, &RecipePage::onEnabledChanged);
    m_configScrollArea->takeWidget();
    delete m_currentConfigWidget;
    m_currentConfigWidget = nullptr;
  }

  // ============================================================================
  // Create configuration widget using plugin architecture
  // ============================================================================
  auto& registry = ProcessPluginRegistry::Instance();
  if (registry.HasPlugin(instance->processType)) {
    IProcessPlugin* plugin = registry.GetPlugin(instance->processType);

    // Check if plugin prefers table-based UI
    if (plugin && plugin->PrefersTableUI()) {
      m_currentConfigWidget = new TableBasedConfigWidget(m_context, instance->processType);
      std::cout << "[RecipePage] Created TableBasedConfigWidget for: "
                << instance->processType << std::endl;
    }
    else {
      m_currentConfigWidget = new PluginBasedConfigWidget(m_context, instance->processType);
      std::cout << "[RecipePage] Created PluginBasedConfigWidget for: "
                << instance->processType << std::endl;
    }
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
  // ✅ FIX: Safe widget cleanup - disconnect signals first
  if (m_currentConfigWidget) {
    disconnect(m_currentConfigWidget, &ProcessConfigWidget::parameterChanged,
      this, &RecipePage::onParameterChanged);
    disconnect(m_currentConfigWidget, &ProcessConfigWidget::nicknameChanged,
      this, &RecipePage::onNicknameChanged);
    disconnect(m_currentConfigWidget, &ProcessConfigWidget::enabledChanged,
      this, &RecipePage::onEnabledChanged);
    m_configScrollArea->takeWidget();  // Remove from scroll area
    delete m_currentConfigWidget;      // Delete immediately
    m_currentConfigWidget = nullptr;
  }

  QWidget* placeholder = new QWidget();
  QVBoxLayout* layout = new QVBoxLayout(placeholder);

  QLabel* label = new QLabel("Select a process step to configure");
  label->setAlignment(Qt::AlignCenter);
  label->setStyleSheet(QString("color: %1; font-size: 14px; padding: 50px; background-color: %2;")
                         .arg(QLatin1String(Theme::Color::textSecondary),
                              QLatin1String(Theme::Color::bgDeep)));

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
  m_sequenceEngine->SetScriptEngine(m_context->scriptEngine);

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
      // Show the user input widget in the floating run window
      if (m_runWindow) {
        m_runWindow->setActiveWidget(m_userInputWidget);
      }
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
        // Show the user input widget in the floating run window
        if (m_runWindow) {
          m_runWindow->setActiveWidget(m_userInputWidget);
        }
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
      // Show the manual align point widget in the floating run window
      if (m_runWindow) {
        m_runWindow->setActiveWidget(m_manualAlignPointWidget);
      }
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
        // Show the manual align point widget in the floating run window
        if (m_runWindow) {
          m_runWindow->setActiveWidget(m_manualAlignPointWidget);
        }
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
           double stepSizeUm, bool enableX, bool enableY, bool enableZ,
           const std::vector<std::string>& imagePaths) -> bool {
    bool result = false;

    QStringList qpaths;
    qpaths.reserve(static_cast<int>(imagePaths.size()));
    for (const auto& p : imagePaths) qpaths << QString::fromStdString(p);

    // Check if we're already on the UI thread
    if (QThread::currentThread() == QApplication::instance()->thread()) {
      // Already on UI thread - call directly to avoid deadlock
      // Show the manual adjust widget in the floating run window
      if (m_runWindow) {
        m_runWindow->setActiveWidget(m_manualAdjustWidget);
      }
      m_manualAdjustWidget->showAdjustment(
        QString::fromStdString(message),
        QString::fromStdString(deviceName),
        stepSizeUm,
        enableX, enableY, enableZ,
        qpaths
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
      QMetaObject::invokeMethod(this, [this, message, deviceName, stepSizeUm, enableX, enableY, enableZ, qpaths, &result]() {
        // Show the manual adjust widget in the floating run window
        if (m_runWindow) {
          m_runWindow->setActiveWidget(m_manualAdjustWidget);
        }
        m_manualAdjustWidget->showAdjustment(
          QString::fromStdString(message),
          QString::fromStdString(deviceName),
          stepSizeUm,
          enableX, enableY, enableZ,
          qpaths
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

void RecipePage::installRecipeWorkerCallbacks(RecipeWorker* worker)
{
  if (!worker) return;

  // UserPrompt — emits a signal to the UI thread, then blocks the worker
  // thread on a local QEventLoop until the page slot pushes the response
  // back via the m_promptEventLoop pointer.
  connect(this, &RecipePage::showUserPrompt,
          this, &RecipePage::onShowUserPrompt);

  worker->setUserPromptCallback(
    [this](const std::string& title, const std::string& message) -> bool {
      emit showUserPrompt(QString::fromStdString(title),
                          QString::fromStdString(message), true);
      QEventLoop loop;
      m_promptEventLoop = &loop;
      loop.exec();
      m_promptEventLoop = nullptr;
      return m_userPromptResponse;
    });

  // ManualAdjust — same emit-then-block pattern.
  connect(this, &RecipePage::showManualAdjust,
          this, &RecipePage::onShowManualAdjust);

  worker->setManualAdjustCallback(
    [this](const std::string& message, const std::string& deviceName, double stepSizeUm,
           bool enableX, bool enableY, bool enableZ,
           const std::vector<std::string>& imagePaths) -> bool {
      QStringList qpaths;
      qpaths.reserve(static_cast<int>(imagePaths.size()));
      for (const auto& p : imagePaths) qpaths << QString::fromStdString(p);
      emit showManualAdjust(QString::fromStdString(message),
                            QString::fromStdString(deviceName),
                            stepSizeUm, enableX, enableY, enableZ, qpaths);
      QEventLoop loop;
      m_promptEventLoop = &loop;
      loop.exec();
      m_promptEventLoop = nullptr;
      return m_manualAdjustResponse;
    });

  // UserInput — thread-aware. Direct UI-thread call when already on UI
  // thread (avoids deadlock); BlockingQueuedConnection to UI thread when
  // called from the worker thread.
  worker->setUserInputCallback(
    [this](const std::string& title, const std::string& prompt,
           const std::string& defaultValue) -> std::pair<bool, std::string> {
      std::pair<bool, std::string> result;

      if (QThread::currentThread() == QApplication::instance()->thread()) {
        if (m_runWindow) {
          m_runWindow->setActiveWidget(m_userInputWidget);
        }
        m_userInputWidget->showInput(QString::fromStdString(title),
                                     QString::fromStdString(prompt),
                                     QString::fromStdString(defaultValue));

        std::cout << "[RecipePage] Showing user input widget for RecipeWorker (UI thread)" << std::endl;
        QApplication::processEvents();

        QEventLoop loop;
        m_promptEventLoop = &loop;
        loop.exec();
        m_promptEventLoop = nullptr;

        result = std::make_pair(m_userInputAccepted, m_userInputText.toStdString());
      } else {
        QMetaObject::invokeMethod(this, [this, title, prompt, defaultValue, &result]() {
          if (m_runWindow) {
            m_runWindow->setActiveWidget(m_userInputWidget);
          }
          m_userInputWidget->showInput(QString::fromStdString(title),
                                       QString::fromStdString(prompt),
                                       QString::fromStdString(defaultValue));

          std::cout << "[RecipePage] Showing user input widget for RecipeWorker (worker thread)" << std::endl;

          QEventLoop loop;
          m_promptEventLoop = &loop;
          loop.exec();
          m_promptEventLoop = nullptr;

          result = std::make_pair(m_userInputAccepted, m_userInputText.toStdString());
        }, Qt::BlockingQueuedConnection);
      }

      return result;
    });
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
  if (m_executionController && m_executionController->IsRunning()) {
    QMessageBox::warning(this, "Recipe Running",
      "A recipe is already executing. Please wait for it to complete.");
    return;
  }

  QString recipeName = QString::fromStdString(m_recipeManager->getRecipeName());

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

  // Switch to executing state - disables editing controls
  setPageState(PageState::Executing);

  // Switch to progress panel
  showProgressPanel();

  // Update run button text
  m_runRecipeButton->setText("⏳ Recipe Running...");

  // ✅ Create execution context for context viewer
  if (m_currentContext) {
    delete m_currentContext;
  }
  m_currentContext = new SequenceContext();

  // Build breakpoints vector from ProcessInstance breakpoint flags (only
  // enabled steps land in the worker's enabled-step indexing).
  std::vector<bool> breakpoints;
  for (int i = 0; i < m_recipeManager->getStepCount(); ++i) {
    ProcessInstance* step = m_recipeManager->getStep(i);
    if (step && step->enabled) {
      breakpoints.push_back(step->breakpoint);
    }
  }

  m_executionController->Start(
    m_sequenceEngine,
    m_processFactory,
    m_context->productPositionManager,
    m_context->motionManager,
    m_context->safetyWatchManager,
    recipeFilePath,
    m_currentContext,
    breakpoints,
    [this](RecipeWorker* worker) { installRecipeWorkerCallbacks(worker); });
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

  // Get the selected step index (stored in UserRole data, not tree position)
  QTreeWidgetItem* selectedItem = selectedItems.first();
  int stepIndex = selectedItem->data(0, Qt::UserRole).toInt();

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
  if (m_executionController && m_executionController->IsRunning()) {
    QMessageBox::warning(this, "Execution Running",
      "A recipe is already executing. Please wait for it to complete.");
    return;
  }

  // Get step info for display
  QString stepNickname = QString::fromStdString(step->userNickname);
  QString stepType = QString::fromStdString(step->processType);

  std::cout << "[RecipePage] Running single step: " << stepNickname.toStdString()
            << " (type: " << stepType.toStdString() << ")" << std::endl;

  // Create a temporary recipe with just this one step.
  RecipeManager tempRecipe;
  tempRecipe.setRecipeName("TempSingleStep");

  // Preserve the original uniqueId so calibration data saved under this
  // UID remains retrievable.
  tempRecipe.insertStepAt(
    0,
    step->processType,
    step->userNickname,
    true,
    step->parameters,
    step->uniqueId
  );

  // Save to temporary file (use current directory to avoid path issues)
  std::string tempRecipePath = "temp_single_step.json";
  if (!tempRecipe.saveRecipe(tempRecipePath)) {
    QMessageBox::critical(this, "Error",
      "Failed to create temporary recipe file for single step execution.");
    return;
  }

  std::cout << "[RecipePage] Created temporary recipe: " << tempRecipePath << std::endl;

  // Switch to executing state - disables editing controls
  setPageState(PageState::Executing);

  // Switch to progress panel
  showProgressPanel();

  // Update button text
  m_runSelectedStepButton->setText("⏳ Running Step...");

  // ✅ Create execution context for context viewer (if not already exists)
  if (!m_currentContext) {
    m_currentContext = new SequenceContext();
  }

  // No breakpoints for single-step runs.
  std::vector<bool> breakpoints;

  m_executionController->Start(
    m_sequenceEngine,
    m_processFactory,
    m_context->productPositionManager,
    m_context->motionManager,
    m_context->safetyWatchManager,
    tempRecipePath,
    m_currentContext,
    breakpoints,
    [this](RecipeWorker* worker) { installRecipeWorkerCallbacks(worker); });

  std::cout << "[RecipePage] Started RecipeWorker for single step execution (non-blocking)" << std::endl;
}

// ============================================================================
// ✅ STEP-BY-STEP EXECUTION
// ============================================================================

void RecipePage::onBreakpointButtonClicked()
{
  // Get selected step
  QList<QTreeWidgetItem*> selectedItems = m_recipeTree->selectedItems();
  if (selectedItems.isEmpty()) {
    QMessageBox::information(this, "No Selection", "Please select a step to toggle breakpoint.");
    return;
  }

  // Toggle breakpoint on selected step
  QTreeWidgetItem* item = selectedItems.first();
  int row = m_recipeTree->indexOfTopLevelItem(item);
  if (row < 0 || row >= m_recipeManager->getStepCount()) {
    return;
  }

  ProcessInstance* step = m_recipeManager->getStep(row);
  if (step) {
    step->breakpoint = !step->breakpoint;
    std::cout << "[RecipePage] Toggled breakpoint on step " << (row + 1)
              << " (" << step->userNickname << "): "
              << (step->breakpoint ? "ON" : "OFF") << std::endl;

    // Update the single item to show updated breakpoint state
    updateSingleStepItem(row);
  }
}

void RecipePage::onExecutionPaused(int stepIndex, QString stepName, bool isBreakpoint)
{
  std::cout << "[RecipePage] Execution paused at step " << (stepIndex + 1)
            << ": " << stepName.toStdString()
            << (isBreakpoint ? " (breakpoint)" : " (step-by-step)") << std::endl;

  // Update progress widget status
  if (m_progressWidget) {
    QString statusMsg = QString("⏸️ Paused at breakpoint: %1").arg(stepName);
    m_progressWidget->setStatusText(statusMsg);
  }
}

// Legacy slots (kept for compatibility but not used with new system)
void RecipePage::onRecipeProgress(const QString& message)
{
  std::cout << "[RecipePage] Progress: " << message.toStdString() << std::endl;
}

void RecipePage::onRecipeFinished(bool /*success*/, const QString& message)
{
  std::cout << "[RecipePage] Recipe finished: " << message.toStdString() << std::endl;
  // Worker lifetime is managed by m_executionController.
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

  // Switch back to editing state - re-enables all editing controls
  setPageState(PageState::Editing);

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

  // ✅ Auto-dismiss the floating run window after 3 seconds
  QTimer::singleShot(3000, this, [this]() {
    // Only dismiss if no execution is running (prevents dismissing new execution)
    if (m_executionController && m_executionController->IsRunning()) {
      std::cout << "[RecipePage] Skipping auto-dismiss - new execution is running" << std::endl;
      return;
    }
    // Already closed?
    if (!m_runWindow || !m_runWindow->isRunning()) {
      return;
    }

    std::cout << "[RecipePage] Auto-dismissing run window" << std::endl;
    showConfigPanel();   // Hides the run window and tears down m_progressWidget
  });
  // Worker lifetime is managed by m_executionController.
}


void RecipePage::onExecutionStopped()
{
  // ✅ Clear step highlighting when execution is stopped
  clearExecutingStepHighlight();

  // Switch back to editing state - re-enables all editing controls
  setPageState(PageState::Editing);

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

void RecipePage::onStepExecutionStarted(int stepIndex, QString stepName, QString /*stepType*/)
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
      // Highlight executing step: warning-orange text on a deep accent bg so
      // it stands out against the normal row color while staying on-palette.
      const QString highlightBgColor = QLatin1String(Theme::Color::primaryPressed);
      const QString highlightTextColor = QLatin1String(Theme::Color::warning);

      // Update column 1 (step number label)
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
          ).arg(highlightBgColor, highlightTextColor));
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
            "  color: %2; "
            "  padding: 4px 8px; "
            "  font-family: 'RobotoMono-Regular', 'Roboto Mono', 'Consolas', monospace; "
            "  font-weight: bold; "
            "  font-size: 14px; "
            "}"
          ).arg(highlightBgColor, highlightTextColor));
        }
      }

      // Also scroll to make it visible
      m_recipeTree->scrollToItem(item);
    }
    else {
      // Restore normal colors for other items
      QString textColor = step->enabled ? QLatin1String(Theme::Color::textPrimary)
                                        : QLatin1String(Theme::Color::textSecondary);

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

    QString textColor = step->enabled ? QLatin1String(Theme::Color::textPrimary)
                                      : QLatin1String(Theme::Color::textSecondary);

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
  if (m_executionController && m_executionController->IsRunning()) {
    m_executionController->RequestStop();
    if (m_progressWidget) {
      m_progressWidget->setStatusText("Stopping...");
    }
  }

  // Emergency stop all motors
  if (m_context && m_context->motionManager) {
    std::cout << "[RecipePage] Calling EmergencyStopAll on motion manager" << std::endl;
    (void)m_context->motionManager->EmergencyStopAll();
  }

  std::cout << "[RecipePage] Emergency stop completed" << std::endl;
}

// ============================================================================
// ✅ PAGE STATE MANAGEMENT
// ============================================================================

void RecipePage::setPageState(PageState state)
{
  m_pageState = state;

  bool isEditing = (state == PageState::Editing);

  std::cout << "[RecipePage] Page state changed to: "
            << (isEditing ? "Editing" : "Executing") << std::endl;

  // Recipe tree - disable selection and interaction during execution
  if (m_recipeTree) m_recipeTree->setEnabled(isEditing);

  // Recipes tab - disable during execution
  if (m_middleTabWidget) {
    // Disable Recipes tab (index 0) during execution
    m_middleTabWidget->setTabEnabled(0, isEditing);
    if (m_recipeBrowser) m_recipeBrowser->setEnabled(isEditing);
  }

  // File operations
  if (m_newButton) m_newButton->setEnabled(isEditing);
  if (m_loadButton) m_loadButton->setEnabled(isEditing);
  if (m_saveButton) m_saveButton->setEnabled(isEditing);
  if (m_saveAsButton) m_saveAsButton->setEnabled(isEditing);

  // Step manipulation
  if (m_moveUpButton) m_moveUpButton->setEnabled(isEditing);
  if (m_moveDownButton) m_moveDownButton->setEnabled(isEditing);
  if (m_removeButton) m_removeButton->setEnabled(isEditing);
  if (m_copyButton) m_copyButton->setEnabled(isEditing);
  if (m_pasteButton) m_pasteButton->setEnabled(isEditing);

  // Process selection
  if (m_selectAllButton) m_selectAllButton->setEnabled(isEditing);
  if (m_deselectAllButton) m_deselectAllButton->setEnabled(isEditing);
  if (m_toggleSelectedButton) m_toggleSelectedButton->setEnabled(isEditing);

  // Process catalog (left panel)
  if (m_processCatalog) m_processCatalog->setEnabled(isEditing);

  // Other controls
  if (m_breakpointButton) m_breakpointButton->setEnabled(isEditing);
  if (m_updateProductFileButton) m_updateProductFileButton->setEnabled(isEditing);
  if (m_recipeNameEdit) m_recipeNameEdit->setEnabled(isEditing);

  // Run buttons - special handling
  if (isEditing) {
    if (m_runRecipeButton) {
      m_runRecipeButton->setEnabled(true);
      m_runRecipeButton->setText("▶ Run Recipe");
    }
    if (m_runSelectedStepButton) {
      m_runSelectedStepButton->setEnabled(true);
      m_runSelectedStepButton->setText("▶ Run Selected Step");
    }
  }
  // Note: During execution, run buttons text is managed by execution handlers
}

// ============================================================================
// ✅ PANEL SWITCHING
// ============================================================================

void RecipePage::showProgressPanel()
{
  // Tear down any previous progress widget.  QPointer auto-nulls itself
  // once Qt actually deletes the object.
  if (m_progressWidget && !m_progressWidget.isNull()) {
    m_progressWidget->deleteLater();
  }

  // Create a fresh progress widget for this run.
  m_progressWidget = new RecipeProgressWidget();
  connect(m_progressWidget, &RecipeProgressWidget::stopRequested,
    this, &RecipePage::onStopRecipeRequested);
  connect(m_progressWidget, &RecipeProgressWidget::userPromptResponse,
    this, &RecipePage::onUserPromptResponse);

  m_progressWidget->resetTimer();
  m_progressWidget->setStatusText("Initializing recipe execution...");
  m_progressWidget->startTimer();

  // Show in the floating run window.  The right panel of RecipePage is no
  // longer touched during execution — it keeps showing Process Configuration.
  if (m_runWindow) {
    m_runWindow->setActiveWidget(m_progressWidget);
    m_runWindow->openForRun();
  }
}


void RecipePage::showConfigPanel()
{
  // Hide the floating run window and tear down the progress widget.
  if (m_runWindow) {
    m_runWindow->closeAfterRun();
  }
  if (m_progressWidget && !m_progressWidget.isNull()) {
    m_progressWidget->deleteLater();
  }
  // The right panel was never replaced — no swap needed here.
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
    m_productStatusLabel->setStyleSheet(RecipeStyles::productStatusLabelLoaded());

    if (m_loadedProductLabel) {
      m_loadedProductLabel->setText(QString("Product: %1\nFile: %2")
        .arg(QString::fromStdString(productName))
        .arg(filePath));
      m_loadedProductLabel->setStyleSheet(productLabelLoadedQss());
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

void RecipePage::loadLastRecipeFromSettings()
{
  const QString lastRecipePath = RecipePageSettingsStore::LoadLastRecipePath();

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
        m_productStatusLabel->setStyleSheet(RecipeStyles::productStatusLabelLoaded());

        if (m_loadedProductLabel) {
          m_loadedProductLabel->setText(QString("Product: %1\nFile: %2")
            .arg(QString::fromStdString(productName))
            .arg(QString::fromStdString(productFile)));
          m_loadedProductLabel->setStyleSheet(productLabelLoadedQss());
        }

        std::cout << "[RecipePage] Auto-loaded product positions: " << productFile << std::endl;
      }
      else {
        if (m_loadedProductLabel) {
          m_loadedProductLabel->setText(QString("⚠ Product file not found:\n%1")
          .arg(QString::fromStdString(productFile)));
          m_loadedProductLabel->setStyleSheet(productLabelErrorQss());
        }

        std::cout << "[RecipePage] Warning: Product file not found: " << productFile << std::endl;
      }
    }
  }
  else {
    // No product file associated with recipe
    m_productStatusLabel->setText("Product: None");
    m_productStatusLabel->setStyleSheet(QString("font-size: 11px; color: %1;")
                                          .arg(QLatin1String(Theme::Color::textSecondary)));

    if (m_loadedProductLabel) {
      m_loadedProductLabel->setText("Product: None");
      m_loadedProductLabel->setStyleSheet(productLabelNeutralQss());
    }

    std::cout << "[RecipePage] No product file associated with recipe" << std::endl;
  }
}

// ============================================================================
// MANUAL ADJUST HANDLING
// ============================================================================

void RecipePage::onShowManualAdjust(QString message, QString deviceName, double stepSizeUm,
                                    bool enableX, bool enableY, bool enableZ,
                                    QStringList imagePaths)
{
  // Show the manual adjust widget in the floating run window
  if (m_runWindow) {
    m_runWindow->setActiveWidget(m_manualAdjustWidget);
  }
  m_manualAdjustWidget->showAdjustment(message, deviceName, stepSizeUm,
                                        enableX, enableY, enableZ, imagePaths);

  std::cout << "[RecipePage] Showing manual adjust widget" << std::endl;
}

void RecipePage::onManualAdjustResponse(bool completed)
{
  // Store the user's response
  m_manualAdjustResponse = completed;

  std::cout << "[RecipePage] Manual adjust response: " << (completed ? "OK" : "Cancel") << std::endl;

  // Hide the manual adjust widget
  m_manualAdjustWidget->hideAdjustment();

  // Switch the run window back to the progress widget if it exists
  if (m_runWindow && m_progressWidget && !m_progressWidget.isNull()) {
    m_runWindow->setActiveWidget(m_progressWidget);
    std::cout << "[RecipePage] Switched back to progress widget" << std::endl;
  }

  // Exit the event loop to unblock the callback
  if (m_promptEventLoop) {
    m_promptEventLoop->quit();
  }
}

void RecipePage::onShowManualAlignPoint(QString pointName, QString message,
                                        bool enableX, bool enableY, bool enableZ)
{
  // Show the manual align point widget in the floating run window
  if (m_runWindow) {
    m_runWindow->setActiveWidget(m_manualAlignPointWidget);
  }
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

  // Switch the run window back to the progress widget if it exists
  if (m_runWindow && m_progressWidget && !m_progressWidget.isNull()) {
    m_runWindow->setActiveWidget(m_progressWidget);
    std::cout << "[RecipePage] Switched back to progress widget" << std::endl;
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

  // Switch the run window back to the progress widget if it exists
  if (m_runWindow && m_progressWidget && !m_progressWidget.isNull()) {
    m_runWindow->setActiveWidget(m_progressWidget);
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

void RecipePage::injectLiveBeamStreamData()
{
  // Inject live beam stream data into context (available even without running a recipe)
  if (!m_context || !m_context->gdiCameraWindowManager) return;
  if (!m_context->gdiCameraWindowManager->isBeamStreamActive()) return;

  // Create context if it doesn't exist yet (no recipe has been run)
  if (!m_currentContext) {
    m_currentContext = new SequenceContext();
  }

  nlohmann::json result = m_context->gdiCameraWindowManager->getLatestBeamStreamResult();

  m_currentContext->SetValue("bs.active", "true");
  m_currentContext->SetValue("bs.fps", std::to_string(m_context->gdiCameraWindowManager->getBeamStreamFps()));

  if (result.contains("process_ms")) {
    m_currentContext->SetValue("bs.process_ms", std::to_string(result["process_ms"].get<double>()));
  }

  if (result.contains("beams") && result["beams"].is_array()) {
    const auto& beams = result["beams"];
    m_currentContext->SetValue("bs.beam.count", std::to_string(beams.size()));

    for (size_t i = 0; i < beams.size(); ++i) {
      std::string prefix = "bs.beam." + std::to_string(i) + ".";
      const auto& beam = beams[i];

      if (beam.contains("side")) {
        m_currentContext->SetValue(prefix + "label", beam["side"].get<std::string>());
      }
      m_currentContext->SetValue(prefix + "centroid_x_px", std::to_string(beam.value("centroid_x_px", 0.0)));
      m_currentContext->SetValue(prefix + "centroid_y_px", std::to_string(beam.value("centroid_y_px", 0.0)));
      if (beam.contains("d4sigma_x_px")) {
        m_currentContext->SetValue(prefix + "d4sigma_x_px", std::to_string(beam["d4sigma_x_px"].get<double>()));
      }
      if (beam.contains("d4sigma_y_px")) {
        m_currentContext->SetValue(prefix + "d4sigma_y_px", std::to_string(beam["d4sigma_y_px"].get<double>()));
      }
      if (beam.contains("d4sigma_x_um")) {
        m_currentContext->SetValue(prefix + "d4sigma_x_um", std::to_string(beam["d4sigma_x_um"].get<double>()));
      }
      if (beam.contains("d4sigma_y_um")) {
        m_currentContext->SetValue(prefix + "d4sigma_y_um", std::to_string(beam["d4sigma_y_um"].get<double>()));
      }
      if (beam.contains("peak_intensity")) {
        m_currentContext->SetValue(prefix + "peak_intensity", std::to_string(beam["peak_intensity"].get<double>()));
      }
      if (beam.contains("roi_x")) {
        m_currentContext->SetValue(prefix + "roi_x", std::to_string(beam["roi_x"].get<int>()));
      }
      if (beam.contains("roi_y")) {
        m_currentContext->SetValue(prefix + "roi_y", std::to_string(beam["roi_y"].get<int>()));
      }
      if (beam.contains("roi_w")) {
        m_currentContext->SetValue(prefix + "roi_w", std::to_string(beam["roi_w"].get<int>()));
      }
      if (beam.contains("roi_h")) {
        m_currentContext->SetValue(prefix + "roi_h", std::to_string(beam["roi_h"].get<int>()));
      }
    }

    // Convenience aliases for first beam
    if (!beams.empty()) {
      m_currentContext->SetValue("bs.beam.first.centroid_x_px", std::to_string(beams[0].value("centroid_x_px", 0.0)));
      m_currentContext->SetValue("bs.beam.first.centroid_y_px", std::to_string(beams[0].value("centroid_y_px", 0.0)));
      if (beams[0].contains("d4sigma_x_px")) {
        m_currentContext->SetValue("bs.beam.first.d4sigma_x_px", std::to_string(beams[0]["d4sigma_x_px"].get<double>()));
      }
      if (beams[0].contains("d4sigma_y_px")) {
        m_currentContext->SetValue("bs.beam.first.d4sigma_y_px", std::to_string(beams[0]["d4sigma_y_px"].get<double>()));
      }
      if (beams[0].contains("side")) {
        m_currentContext->SetValue("bs.beam.first.label", beams[0]["side"].get<std::string>());
      }
    }
  } else {
    m_currentContext->SetValue("bs.beam.count", "0");
  }
}

void RecipePage::onShowContextViewer()
{
  if (!m_contextViewer) {
    m_contextViewer = new ContextViewerWindow(this);
    connect(m_contextViewer, &ContextViewerWindow::refreshRequested,
            this, &RecipePage::onContextRefreshRequested);
  }

  // Inject live beam stream data before showing
  injectLiveBeamStreamData();

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
  // Inject live beam stream data on every refresh
  injectLiveBeamStreamData();

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
  m_visionResultImageLabel->setStyleSheet(QString(
    "QLabel { "
    "  background-color: %1; "
    "  border: 1px solid %2; "
    "}"
  ).arg(QLatin1String(Theme::Color::bgDeep),
        QLatin1String(Theme::Color::accent)));

  std::cout << "[RecipePage] Vision result image updated: "
            << resultImage.cols << "x" << resultImage.rows << std::endl;
#else
  // No OpenCV support
  if (m_visionResultImageLabel) {
    m_visionResultImageLabel->setText("OpenCV not available");
  }
#endif
}

//=============================================================================
// QUICK ADD MODE (Ctrl+B shortcut) — delegated to QuickAddController.
//=============================================================================

void RecipePage::keyPressEvent(QKeyEvent* event)
{
  if (m_quickAddController && m_quickAddController->HandleKeyPress(event)) {
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

// ============================================================================
// HOT RELOAD - SHOW EVENT
// ============================================================================

void RecipePage::showEvent(QShowEvent* event)
{
  QWidget::showEvent(event);

  // Skip reload if recipe is currently executing
  if (m_executionController && m_executionController->IsRunning()) {
    std::cout << "[RecipePage] Skipping hot reload - recipe is executing" << std::endl;
    return;
  }

  // Pick up any external edits to hidden_processes(.local).json and refresh
  // the available-processes tree.
  if (m_processCatalog) m_processCatalog->refresh();

  std::cout << "[RecipePage] Page activated - hot reloading position data..." << std::endl;

  // Reload product position data from disk to pick up any changes from Motion page
  if (m_context && m_context->productPositionManager &&
      m_context->productPositionManager->HasLoadedProduct()) {

    std::string productPath = m_context->productPositionManager->GetCurrentProductPath();
    std::string productName = m_context->productPositionManager->GetCurrentProductName();

    // Reload product data from disk
    if (m_context->productPositionManager->LoadProduct(productPath)) {
      // Re-export to motion manager so named positions are updated
      if (m_context->motionManager) {
        m_context->productPositionManager->ExportToMotionManager(*m_context->motionManager);
      }

      std::cout << "[RecipePage] Hot reload: Product '" << productName
                << "' reloaded with updated positions/graphs" << std::endl;

      // Refresh the current config widget to pick up new position names
      if (m_currentConfigWidget && m_selectedStepIndex >= 0) {
        updateConfigurationForm();
        std::cout << "[RecipePage] Hot reload: Config widget refreshed" << std::endl;
      }
    }
    else {
      std::cerr << "[RecipePage] Hot reload: Failed to reload product from '"
                << productPath << "'" << std::endl;
    }
  }
}

// ============================================================================
// UNSAVED CHANGES HANDLING
// ============================================================================

void RecipePage::onBackButtonClicked()
{
  // Check for unsaved changes before allowing navigation
  if (canLeavePage()) {
    emit backToMainMenu();
  }
}

bool RecipePage::canLeavePage()
{
  // Skip check if recipe is currently executing
  if (m_executionController && m_executionController->IsRunning()) {
    QMessageBox::warning(this, "Recipe Running",
      "Cannot leave while a recipe is executing.\nPlease stop the recipe first.");
    return false;
  }

  // Check for unsaved changes
  if (!m_recipeManager->hasUnsavedChanges()) {
    return true;  // No changes, can leave
  }

  // Show dialog asking user what to do
  QMessageBox msgBox(this);
  msgBox.setWindowTitle("Unsaved Changes");
  msgBox.setText("The recipe has unsaved changes.");
  msgBox.setInformativeText("Do you want to save your changes before leaving?");
  msgBox.setIcon(QMessageBox::Question);

  QPushButton* saveButton = msgBox.addButton("Save", QMessageBox::AcceptRole);
  QPushButton* discardButton = msgBox.addButton("Discard", QMessageBox::DestructiveRole);
  msgBox.addButton("Cancel", QMessageBox::RejectRole);

  msgBox.setDefaultButton(saveButton);

  msgBox.exec();

  QAbstractButton* clickedButton = msgBox.clickedButton();

  if (clickedButton == saveButton) {
    // Try to save the recipe
    std::string recipeFilePath = m_recipeManager->getCurrentFilePath();
    if (recipeFilePath.empty()) {
      // No existing file path, use Save As
      onSaveRecipeAs();
      // Check if save was successful (file path should now be set)
      if (m_recipeManager->getCurrentFilePath().empty()) {
        return false;  // Save was cancelled
      }
    } else {
      // Save to existing path
      if (!m_recipeManager->saveRecipe(recipeFilePath)) {
        QMessageBox::critical(this, "Save Failed",
          "Failed to save the recipe.\nPlease try again.");
        return false;
      }
      updateRecipeFileNameDisplay();
      std::cout << "[RecipePage] Recipe saved before navigation" << std::endl;
    }
    return true;  // Saved successfully, can leave
  }
  else if (clickedButton == discardButton) {
    std::cout << "[RecipePage] Discarding unsaved changes" << std::endl;
    return true;  // User chose to discard changes, can leave
  }
  else {
    // Cancel button or dialog closed
    return false;  // Stay on page
  }
}

// ============================================================================
// THEME APPLICATION
// ============================================================================

void RecipePage::applyTheme()
{
  // Page background
  setStyleSheet(RecipeStyles::pageBackground());

  // Tab widget — inline QSS using Theme::Color::* (7 args)
  m_middleTabWidget->setStyleSheet(QString(
    "QTabWidget::pane { border: none; background-color: %1; }"
    "QTabBar { background-color: %1; border: none; }"
    "QTabBar::tab {"
    "  background-color: %2;"
    "  color: %3;"
    "  padding: 8px 16px;"
    "  margin-right: 2px;"
    "  border: none;"
    "  font-size: 12px;"
    "  font-weight: bold;"
    "  border-top-left-radius: 3px;"
    "  border-top-right-radius: 3px;"
    "  border-bottom: 3px solid transparent;"
    "}"
    "QTabBar::tab:selected {"
    "  background-color: %4;"
    "  color: %5;"
    "  font-weight: bold;"
    "  border-bottom: 3px solid %4;"
    "}"
    "QTabBar::tab:hover {"
    "  background-color: %6;"
    "  color: %7;"
    "}"
  ).arg(QLatin1String(Theme::Color::bgPrimary),
        QLatin1String(Theme::Color::bgDeep),
        QLatin1String(Theme::Color::textSecondary),
        QLatin1String(Theme::Color::primary),
        QLatin1String(Theme::Color::textOnPrimary),
        QLatin1String(Theme::Color::bgHover),
        QLatin1String(Theme::Color::textPrimary)));

  // Recipes tab is owned by RecipeBrowserWidget — delegate theme refresh.
  if (m_recipeBrowser) m_recipeBrowser->applyTheme();

  // Recipe name edit — inline QSS (5 args)
  m_recipeNameEdit->setStyleSheet(QString(
    "QLineEdit { "
    "  background-color: %1; "
    "  color: %2; "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "  padding: 6px 10px; "
    "  border: 2px solid %3; "
    "  border-radius: 4px; "
    "} "
    "QLineEdit:focus { "
    "  border: 2px solid %4; "
    "  background-color: %5; "
    "}"
  ).arg(QLatin1String(Theme::Color::bgPrimary),
        QLatin1String(Theme::Color::textPrimary),
        QLatin1String(Theme::Color::primary),
        QLatin1String(Theme::Color::primaryHover),
        QLatin1String(Theme::Color::bgDeep)));

  // Bezel execute buttons — delegate to Theme::Buttons helpers
  m_runRecipeButton->setStyleSheet(Theme::Buttons::bezelWarning());
  m_runSelectedStepButton->setStyleSheet(Theme::Buttons::bezelPrimary());
  m_breakpointButton->setStyleSheet(Theme::Buttons::bezelDanger());

  // Vision result widgets — null-guarded inline QSS
  if (m_visionResultGroup) {
    m_visionResultGroup->setStyleSheet(QString(
      "QGroupBox { "
      "  font-weight: bold; "
      "  color: %1; "
      "  border: 1px solid %2; "
      "  border-radius: 4px; "
      "  margin-top: 5px; "
      "  padding-top: 10px; "
      "} "
      "QGroupBox::title { "
      "  subcontrol-origin: margin; "
      "  left: 10px; "
      "  padding: 0 5px; "
      "}"
    ).arg(QLatin1String(Theme::Color::accent),
          QLatin1String(Theme::Color::borderSubtle)));
  }

  if (m_visionResultImageLabel) {
    m_visionResultImageLabel->setStyleSheet(QString(
      "QLabel { "
      "  background-color: %1; "
      "  border: 1px solid %2; "
      "  color: %3; "
      "  font-style: italic; "
      "}"
    ).arg(QLatin1String(Theme::Color::bgDeep),
          QLatin1String(Theme::Color::borderSubtle),
          QLatin1String(Theme::Color::textSecondary)));
  }
}