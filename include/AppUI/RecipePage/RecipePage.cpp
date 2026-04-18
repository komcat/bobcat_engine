// RecipePage.cpp
#include "RecipePage.h"
#include "RecipeManager.h"
#include "Theme.h"
#include "ThemeButtonStyles.h"
#include "RecipePageStyles.h"        // NEW: Centralized styles (now bound to Theme::Color::*)
#include "RecipeTreeItemFactory.h"   // NEW: Tree item factory
#include "RecipeClipboard.h"         // NEW: Clipboard
#include "RecipeRunWindow.h"         // NEW: Floating execution window
#include "ProcessVisibilityDialog.h" // NEW: Hide/show process dialog
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
  , m_clipboard(std::make_unique<RecipeClipboard>())  // ✅ NEW: Clipboard for copy/paste
  , m_quickAddMode(false)            // Quick add mode off by default
  , m_quickAddSelectedCategory(-1)   // No category selected
  , m_quickAddOverlay(nullptr)       // Overlay created on demand
  , m_contextVariablePreview(nullptr) // Context variable preview widget
  , m_pageState(PageState::Editing)  // Start in editing mode
  , m_middleTabWidget(nullptr)       // Middle panel tab widget
  , m_recipeListWidget(nullptr)      // Recipe list widget
  , m_refreshRecipeListButton(nullptr) // Refresh button
  , m_recipeCountLabel(nullptr)      // Recipe count label
{
  // Load the hidden process set BEFORE setupUI so the initial tree
  // populate already excludes them.
  loadHiddenProcessesFromDisk();

  setupUI();
  setupSequenceEngine();

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
    populateRecipeListWidget();
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

  QWidget* leftPanel = new QWidget();
  createLeftPanel(leftPanel);
  m_mainSplitter->addWidget(leftPanel);

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

  // Apply initial 20% / 40% / 40% sizing.  resizeEvent keeps it pinned on resize.
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
  const int middleWidth = (totalWidth * 40) / 100;
  const int rightWidth  = totalWidth - leftWidth - middleWidth;
  m_mainSplitter->setSizes({ leftWidth, middleWidth, rightWidth });
}

void RecipePage::resizeEvent(QResizeEvent* event)
{
  QWidget::resizeEvent(event);
  applyPanelSizes();
}

void RecipePage::createLeftPanel(QWidget* leftPanel)
{
  QVBoxLayout* mainLayout = new QVBoxLayout(leftPanel);
  mainLayout->setContentsMargins(5, 5, 5, 5);
  mainLayout->setSpacing(5);

  QLabel* title = new QLabel("Available Processes");
  title->setStyleSheet(RecipeStyles::sectionLabel());
  mainLayout->addWidget(title);

  // Search box + gear button to open the visibility manager
  QHBoxLayout* searchRow = new QHBoxLayout();
  searchRow->setSpacing(4);
  searchRow->setContentsMargins(0, 0, 0, 0);

  QLineEdit* searchBox = new QLineEdit();
  searchBox->setPlaceholderText("Search processes...");
  searchBox->setStyleSheet(RecipeStyles::searchBox());
  searchRow->addWidget(searchBox, 1);

  QPushButton* visibilityBtn = new QPushButton(QString::fromUtf8("\xE2\x9A\x99"));  // ⚙ gear
  visibilityBtn->setFixedSize(28, 28);
  visibilityBtn->setToolTip("Manage process visibility (hide/show old or experimental processes)");
  visibilityBtn->setStyleSheet(RecipeStyles::smallToolButton());
  connect(visibilityBtn, &QPushButton::clicked,
    this, &RecipePage::onShowVisibilityDialog);
  searchRow->addWidget(visibilityBtn);

  mainLayout->addLayout(searchRow);

  // Quick tip for keyboard shortcut
  QLabel* quickTip = new QLabel("Tip: Press Ctrl+B for quick add");
  quickTip->setStyleSheet(QString(
    "QLabel { "
    "  color: %1; "
    "  font-size: 10px; "
    "  font-style: italic; "
    "  padding: 2px 0px; "
    "}"
  ).arg(QLatin1String(Theme::Color::textSecondary)));
  mainLayout->addWidget(quickTip);

  // ========== Selected process label + Add button row ==========
  QHBoxLayout* selectedRow = new QHBoxLayout();
  selectedRow->setSpacing(5);

  m_selectedProcessLabel = new QLabel("Selected: (none)");
  m_selectedProcessLabel->setFixedHeight(50);
  m_selectedProcessLabel->setStyleSheet(RecipeStyles::selectedProcessLabel());
  m_selectedProcessLabel->setWordWrap(true);
  m_selectedProcessLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  selectedRow->addWidget(m_selectedProcessLabel, 1);  // Stretch to fill

  // Add button next to selected label
  m_addButton = new QPushButton("»");
  m_addButton->setFixedSize(35, 50);  // Match height of selected label
  m_addButton->setToolTip("Add selected process to recipe (or double-click item)");
  m_addButton->setStyleSheet(RecipeStyles::addProcessButton());
  connect(m_addButton, &QPushButton::clicked, this, &RecipePage::onAddProcess);
  selectedRow->addWidget(m_addButton);

  mainLayout->addLayout(selectedRow);

  // ========== Process tree (full width now) ==========
  m_processTemplateList = new QTreeWidget();
  m_processTemplateList->setHeaderHidden(true);
  m_processTemplateList->setStyleSheet(RecipeStyles::processTemplateTree());
  m_processTemplateList->setRootIsDecorated(true);  // Show expand/collapse arrows
  m_processTemplateList->setIndentation(20);        // Indentation for child items

  // Populate tree (delegates to a reusable method so the visibility dialog
  // and the showEvent hot-reload can call it too).
  refreshProcessTemplateList();

  connect(m_processTemplateList, &QTreeWidget::itemClicked,
    this, &RecipePage::onProcessTemplateSelected);
  connect(m_processTemplateList, &QTreeWidget::itemDoubleClicked,
    [this](QTreeWidgetItem* item, int) {
      if (item && item->parent()) { // Only process items (not category headers)
        onAddProcess();
      }
    });

  mainLayout->addWidget(m_processTemplateList, 1);  // Add with stretch factor

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

      // Hide category if no children are visible.  Note: categories that
      // were already hidden by refreshProcessTemplateList (because every
      // child was filtered out) stay hidden too.
      categoryItem->setHidden(!categoryHasVisibleChildren);
    }
  });
}

// =======================================================================
// Process visibility (hide old/experimental processes)
// =======================================================================

void RecipePage::loadHiddenProcessesFromDisk()
{
  m_hiddenProcesses = ProcessVisibilityDialog::loadHiddenFromDisk();
  std::cout << "[RecipePage] Loaded " << m_hiddenProcesses.size()
            << " hidden processes" << std::endl;
}

void RecipePage::refreshProcessTemplateList()
{
  if (!m_processTemplateList) {
    return;
  }

  m_processTemplateList->clear();

  for (const auto& category : RecipeManager::getProcessCategories()) {
    QTreeWidgetItem* categoryItem = new QTreeWidgetItem(m_processTemplateList);
    categoryItem->setText(0, QString::fromStdString(category));
    categoryItem->setFlags(categoryItem->flags() & ~Qt::ItemIsSelectable);

    QFont font = categoryItem->font(0);
    font.setBold(true);
    categoryItem->setFont(0, font);
    categoryItem->setForeground(0, QColor(RecipeStyles::Colors::TEXT_CATEGORY()));
    categoryItem->setBackground(0, QColor(RecipeStyles::Colors::BG_MEDIUM()));

    for (const auto& processType : RecipeManager::getProcessesByCategory(category)) {
      if (m_hiddenProcesses.count(processType) > 0) {
        continue;  // Hidden by user via the visibility manager
      }
      QTreeWidgetItem* processItem = new QTreeWidgetItem(categoryItem);
      processItem->setText(0, QString::fromStdString(processType));
      processItem->setData(0, Qt::UserRole, QString::fromStdString(processType));
    }

    if (categoryItem->childCount() == 0) {
      // Every process in this category is hidden — collapse the header too.
      categoryItem->setHidden(true);
    } else {
      categoryItem->setExpanded(false);
    }
  }
}

void RecipePage::onShowVisibilityDialog()
{
  // Snapshot the registry into a flat process-name list + a category lookup
  std::vector<std::string> allTypes;
  for (const auto& cat : RecipeManager::getProcessCategories()) {
    for (const auto& p : RecipeManager::getProcessesByCategory(cat)) {
      allTypes.push_back(p);
    }
  }
  auto categoryLookup = [](const std::string& proc) -> std::string {
    return RecipeManager::getProcessCategory(proc);
  };

  ProcessVisibilityDialog dlg(allTypes, m_hiddenProcesses, categoryLookup, this);

  // Live refresh of the tree behind the modal dialog as the user clicks
  connect(&dlg, &ProcessVisibilityDialog::visibilityChanged,
    this, [this](const std::unordered_set<std::string>& newHidden) {
      m_hiddenProcesses = newHidden;
      refreshProcessTemplateList();
    });

  dlg.exec();

  // Defensive: in case the dialog closed without firing the signal, sync
  // the final state.
  m_hiddenProcesses = dlg.hiddenProcesses();
  refreshProcessTemplateList();
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
  QWidget* recipesTab = new QWidget();
  QWidget* stepsTab = new QWidget();
  QWidget* contextTab = new QWidget();
  QWidget* visionResultTab = new QWidget();

  createRecipesTab(recipesTab);
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

  m_middleTabWidget->addTab(recipesTab, "Recipes");
  m_middleTabWidget->addTab(stepsTab, "Steps");
  m_middleTabWidget->addTab(contextTab, "Context");
  m_middleTabWidget->addTab(visionResultTab, "Vision Result");

  // Start on Steps tab by default
  m_middleTabWidget->setCurrentIndex(1);

  layout->addWidget(m_middleTabWidget, 1);
}

void RecipePage::createRecipesTab(QWidget* recipesTab)
{
  QVBoxLayout* layout = new QVBoxLayout(recipesTab);
  layout->setContentsMargins(5, 5, 5, 5);
  layout->setSpacing(5);

  // Header row: count label + Refresh button
  QHBoxLayout* headerLayout = new QHBoxLayout();
  m_recipeCountLabel = new QLabel("0 recipe(s)");
  m_recipeCountLabel->setStyleSheet(QString("font-size: 12px; color: %1;")
                                      .arg(QLatin1String(Theme::Color::textSecondary)));
  headerLayout->addWidget(m_recipeCountLabel);
  headerLayout->addStretch();

  m_refreshRecipeListButton = new QPushButton("Refresh");
  m_refreshRecipeListButton->setToolTip("Refresh recipe list");
  m_refreshRecipeListButton->setFixedSize(70, 28);
  m_refreshRecipeListButton->setStyleSheet(RecipeStyles::smallToolButton());
  connect(m_refreshRecipeListButton, &QPushButton::clicked, this, &RecipePage::onRefreshRecipeList);
  headerLayout->addWidget(m_refreshRecipeListButton);

  layout->addLayout(headerLayout);

  // Filter/search box
  m_recipeFilterEdit = new QLineEdit();
  m_recipeFilterEdit->setPlaceholderText("Filter recipes...");
  m_recipeFilterEdit->setClearButtonEnabled(true);
  m_recipeFilterEdit->setStyleSheet(RecipeStyles::searchBox());
  connect(m_recipeFilterEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
    // Iterate all top-level items (folders + uncategorized recipes)
    for (int i = 0; i < m_recipeListWidget->topLevelItemCount(); ++i) {
      QTreeWidgetItem* topItem = m_recipeListWidget->topLevelItem(i);
      if (!topItem) continue;
      QString itemType = topItem->data(0, Qt::UserRole + 1).toString();
      if (itemType == "folder") {
        // Check each child recipe
        bool anyVisible = false;
        for (int j = 0; j < topItem->childCount(); ++j) {
          QTreeWidgetItem* child = topItem->child(j);
          if (!child) continue;
          bool match = text.isEmpty() || child->text(0).contains(text, Qt::CaseInsensitive);
          child->setHidden(!match);
          if (match) anyVisible = true;
        }
        // Hide folder if all children are hidden and filter is active
        topItem->setHidden(!text.isEmpty() && !anyVisible);
      } else {
        // Root-level recipe item
        bool match = text.isEmpty() || topItem->text(0).contains(text, Qt::CaseInsensitive);
        topItem->setHidden(!match);
      }
    }
  });
  layout->addWidget(m_recipeFilterEdit);

  // Recipe tree widget (replaces flat QListWidget)
  m_recipeListWidget = new QTreeWidget();
  m_recipeListWidget->setColumnCount(1);
  m_recipeListWidget->setHeaderHidden(true);
  m_recipeListWidget->setStyleSheet(QString(
    "QTreeWidget { "
    "  background-color: %1; "
    "  color: %2; "
    "  border: 1px solid %3; "
    "  border-radius: 3px; "
    "  font-size: 14px; "
    "} "
    "QTreeWidget::item { "
    "  padding: 7px 8px; "
    "  border-bottom: 1px solid %4; "
    "} "
    "QTreeWidget::item:selected { "
    "  background-color: %5; "
    "  color: %6; "
    "} "
    "QTreeWidget::item:hover { "
    "  background-color: %7; "
    "} "
    "QTreeWidget::branch { "
    "  background-color: %1; "
    "} "
    "QTreeWidget::branch:has-siblings:!adjoins-item { "
    "  border-image: none; "
    "} "
    "QTreeWidget::branch:open:has-children { "
    "  image: url(none); "
    "} "
    "QTreeWidget::branch:closed:has-children { "
    "  image: url(none); "
    "}"
  ).arg(QLatin1String(Theme::Color::bgDeep),         // %1
        QLatin1String(Theme::Color::textPrimary),    // %2
        QLatin1String(Theme::Color::borderSubtle),   // %3
        QLatin1String(Theme::Color::bgPrimary),      // %4 row separator
        QLatin1String(Theme::Color::primary),        // %5 selected bg
        QLatin1String(Theme::Color::textOnPrimary),  // %6 selected text
        QLatin1String(Theme::Color::bgHover)));      // %7 hover
  m_recipeListWidget->setSelectionMode(QAbstractItemView::SingleSelection);
  m_recipeListWidget->setDragDropMode(QAbstractItemView::InternalMove);
  m_recipeListWidget->setDefaultDropAction(Qt::MoveAction);
  m_recipeListWidget->setAcceptDrops(true);
  m_recipeListWidget->setContextMenuPolicy(Qt::CustomContextMenu);
  m_recipeListWidget->viewport()->installEventFilter(this);
  connect(m_recipeListWidget, &QTreeWidget::itemDoubleClicked, this, &RecipePage::onRecipeListItemDoubleClicked);
  connect(m_recipeListWidget, &QTreeWidget::customContextMenuRequested, this, &RecipePage::onRecipeListContextMenu);
  connect(m_recipeListWidget, &QTreeWidget::currentItemChanged, this,
      [this](QTreeWidgetItem* current, QTreeWidgetItem* /*previous*/) {
          if (!current) {
              m_removeFolderButton->setEnabled(false);
              m_copyRecipeButton->setEnabled(false);
              m_deleteRecipeButton->setEnabled(false);
              return;
          }
          bool isFolder = current->data(0, Qt::UserRole + 1).toString() == "folder";
          bool isRecipe = current->data(0, Qt::UserRole + 1).toString() == "recipe";
          m_removeFolderButton->setEnabled(isFolder);
          m_copyRecipeButton->setEnabled(isRecipe);
          m_deleteRecipeButton->setEnabled(isRecipe);
      });
  layout->addWidget(m_recipeListWidget, 1);

  // === Action buttons bar ===
  QHBoxLayout* actionBar = new QHBoxLayout();
  actionBar->setSpacing(4);

  // Compact action button QSS used by the 4 small folder/recipe buttons.
  // Themed via Theme::Color so the palette applies uniformly.
  const QString compactPrimary = QString(
      "QPushButton { background-color: %1; color: white; border: none; "
      "border-radius: 3px; padding: 2px 8px; font-size: 11px; font-weight: bold; }"
      "QPushButton:hover { background-color: %2; }"
      "QPushButton:disabled { background-color: %3; color: %4; }"
    ).arg(QLatin1String(Theme::Color::primary),
          QLatin1String(Theme::Color::primaryHover),
          QLatin1String(Theme::Color::disabledBg),
          QLatin1String(Theme::Color::textDisabled));

  const QString compactNeutral = QString(
      "QPushButton { background-color: %1; color: %2; border: none; "
      "border-radius: 3px; padding: 2px 8px; font-size: 11px; font-weight: bold; }"
      "QPushButton:hover { background-color: %3; }"
      "QPushButton:disabled { background-color: %4; color: %5; }"
    ).arg(QLatin1String(Theme::Color::bgSurface),
          QLatin1String(Theme::Color::textPrimary),
          QLatin1String(Theme::Color::bgHover),
          QLatin1String(Theme::Color::disabledBg),
          QLatin1String(Theme::Color::textDisabled));

  const QString compactDanger = QString(
      "QPushButton { background-color: %1; color: white; border: none; "
      "border-radius: 3px; padding: 2px 8px; font-size: 11px; font-weight: bold; }"
      "QPushButton:hover { background-color: %2; }"
      "QPushButton:disabled { background-color: %3; color: %4; }"
    ).arg(QLatin1String(Theme::Color::danger),
          QLatin1String(Theme::Color::dangerHover),
          QLatin1String(Theme::Color::disabledBg),
          QLatin1String(Theme::Color::textDisabled));

  m_createFolderButton = new QPushButton("+ Folder");
  m_createFolderButton->setFixedHeight(26);
  m_createFolderButton->setStyleSheet(compactPrimary);
  connect(m_createFolderButton, &QPushButton::clicked, this, &RecipePage::onCreateFolder);

  m_removeFolderButton = new QPushButton("- Folder");
  m_removeFolderButton->setFixedHeight(26);
  m_removeFolderButton->setEnabled(false);
  m_removeFolderButton->setStyleSheet(compactNeutral);
  connect(m_removeFolderButton, &QPushButton::clicked, this, [this]() {
      QTreeWidgetItem* item = m_recipeListWidget->currentItem();
      if (item && item->data(0, Qt::UserRole + 1).toString() == "folder") {
          onDeleteFolder(item);
      }
  });

  m_copyRecipeButton = new QPushButton("Copy");
  m_copyRecipeButton->setFixedHeight(26);
  m_copyRecipeButton->setEnabled(false);
  m_copyRecipeButton->setStyleSheet(compactNeutral);
  connect(m_copyRecipeButton, &QPushButton::clicked, this, &RecipePage::onCopyRecipe);

  m_deleteRecipeButton = new QPushButton("Delete");
  m_deleteRecipeButton->setFixedHeight(26);
  m_deleteRecipeButton->setEnabled(false);
  m_deleteRecipeButton->setStyleSheet(compactDanger);
  connect(m_deleteRecipeButton, &QPushButton::clicked, this, &RecipePage::onDeleteSelectedRecipe);

  actionBar->addWidget(m_createFolderButton);
  actionBar->addWidget(m_removeFolderButton);
  actionBar->addStretch();
  actionBar->addWidget(m_copyRecipeButton);
  actionBar->addWidget(m_deleteRecipeButton);

  layout->addLayout(actionBar);

  // Help text
  QLabel* helpLabel = new QLabel("Double-click to load | Right-click for options | Drag to move");
  helpLabel->setStyleSheet(QString("font-size: 10px; color: %1; font-style: italic;")
                             .arg(QLatin1String(Theme::Color::textSecondary)));
  helpLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(helpLabel);
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
  m_recipeTree->setColumnWidth(2, 400);
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
  populateRecipeListWidget();

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
    populateRecipeListWidget();
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
    saveLastRecipeToSettings(filename);

    QMessageBox::information(this, "Success",
      QString("Recipe saved as:\n%1").arg(filename));
  }
  else {
    QMessageBox::critical(this, "Error", "Failed to save recipe file.");
  }

  // Refresh recipe list and step list (to show new unique IDs)
  populateRecipeListWidget();
  refreshRecipeList();
}

// ============================================================================
// RECIPES TAB METHODS
// ============================================================================

QString RecipePage::getRecipeFolderPath()
{
  if (!m_context->productPositionManager || !m_context->productPositionManager->HasLoadedProduct()) {
    return QString();
  }

  QString productName = QString::fromStdString(m_context->productPositionManager->GetCurrentProductName());
  return QString("config/%1/recipes").arg(productName);
}

void RecipePage::populateRecipeListWidget()
{
  if (!m_recipeListWidget) return;

  m_recipeListWidget->clear();

  QString folderPath = getRecipeFolderPath();
  if (folderPath.isEmpty()) {
    m_recipeCountLabel->setText("No product loaded");
    return;
  }

  // Ensure the root recipes folder exists
  QDir().mkpath(folderPath);

  QDir rootDir(folderPath);
  if (!rootDir.exists()) {
    m_recipeCountLabel->setText("0 recipe(s)");
    return;
  }

  // Helper lambda: read recipe display name from JSON (fallback to baseName)
  auto readRecipeName = [](const QFileInfo& fi) -> QString {
    QFile f(fi.absoluteFilePath());
    if (f.open(QIODevice::ReadOnly)) {
      QByteArray data = f.readAll();
      f.close();
      QJsonDocument doc = QJsonDocument::fromJson(data);
      if (doc.isObject()) {
        QString jsonName = doc.object().value("recipe_name").toString();
        if (!jsonName.isEmpty()) return jsonName;
      }
    }
    return fi.baseName();
  };

  // Helper lambda: build a recipe QTreeWidgetItem
  auto makeRecipeItem = [&](const QFileInfo& fi) -> QTreeWidgetItem* {
    QString recipeName = readRecipeName(fi);
    QTreeWidgetItem* recipeItem = new QTreeWidgetItem();
    recipeItem->setText(0, recipeName);
    recipeItem->setData(0, Qt::UserRole, fi.absoluteFilePath());
    recipeItem->setData(0, Qt::UserRole + 1, QString("recipe"));
    recipeItem->setToolTip(0, QString("%1\nModified: %2")
      .arg(fi.fileName())
      .arg(fi.lastModified().toString("yyyy-MM-dd hh:mm")));
    recipeItem->setFlags(recipeItem->flags() | Qt::ItemIsDragEnabled);
    recipeItem->setFlags(recipeItem->flags() & ~Qt::ItemIsDropEnabled);
    return recipeItem;
  };

  int totalRecipes = 0;

  // 1. Scan for subdirectories -> create folder items
  QDir subdirScanner(folderPath);
  subdirScanner.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
  subdirScanner.setSorting(QDir::Name);
  QFileInfoList subDirs = subdirScanner.entryInfoList();

  for (const QFileInfo& dirInfo : subDirs) {
    QDir subDir(dirInfo.absoluteFilePath());
    subDir.setNameFilters(QStringList() << "*.json");
    subDir.setFilter(QDir::Files);
    subDir.setSorting(QDir::Time | QDir::Reversed);
    QFileInfoList subFiles = subDir.entryInfoList();

    // Create folder item
    QTreeWidgetItem* folderItem = new QTreeWidgetItem();
    folderItem->setText(0, QString("%1  (%2)").arg(dirInfo.fileName()).arg(subFiles.size()));
    folderItem->setData(0, Qt::UserRole, dirInfo.absoluteFilePath());
    folderItem->setData(0, Qt::UserRole + 1, QString("folder"));
    folderItem->setFlags((folderItem->flags() | Qt::ItemIsDropEnabled) & ~Qt::ItemIsDragEnabled);
    QFont folderFont = folderItem->font(0);
    folderFont.setBold(true);
    folderItem->setFont(0, folderFont);
    folderItem->setForeground(0, QColor(QLatin1String(Theme::Color::warning)));  // amber/orange tint for folders

    for (const QFileInfo& fi : subFiles) {
      QTreeWidgetItem* recipeItem = makeRecipeItem(fi);
      folderItem->addChild(recipeItem);
      ++totalRecipes;
    }

    m_recipeListWidget->addTopLevelItem(folderItem);
    folderItem->setExpanded(true);
  }

  // 2. Scan root dir for *.json (uncategorized recipes)
  rootDir.setNameFilters(QStringList() << "*.json");
  rootDir.setFilter(QDir::Files);
  rootDir.setSorting(QDir::Time | QDir::Reversed);
  QFileInfoList rootFiles = rootDir.entryInfoList();

  for (const QFileInfo& fi : rootFiles) {
    QTreeWidgetItem* recipeItem = makeRecipeItem(fi);
    m_recipeListWidget->addTopLevelItem(recipeItem);
    ++totalRecipes;
  }

  m_recipeCountLabel->setText(QString("%1 recipe(s)").arg(totalRecipes));

  // Re-apply current filter text
  if (m_recipeFilterEdit && !m_recipeFilterEdit->text().isEmpty()) {
    QString text = m_recipeFilterEdit->text();
    for (int i = 0; i < m_recipeListWidget->topLevelItemCount(); ++i) {
      QTreeWidgetItem* topItem = m_recipeListWidget->topLevelItem(i);
      if (!topItem) continue;
      QString itemType = topItem->data(0, Qt::UserRole + 1).toString();
      if (itemType == "folder") {
        bool anyVisible = false;
        for (int j = 0; j < topItem->childCount(); ++j) {
          QTreeWidgetItem* child = topItem->child(j);
          if (!child) continue;
          bool match = child->text(0).contains(text, Qt::CaseInsensitive);
          child->setHidden(!match);
          if (match) anyVisible = true;
        }
        topItem->setHidden(!anyVisible);
      } else {
        topItem->setHidden(!topItem->text(0).contains(text, Qt::CaseInsensitive));
      }
    }
  }
}

void RecipePage::onRefreshRecipeList()
{
  populateRecipeListWidget();
}

void RecipePage::onRecipeListItemDoubleClicked(QTreeWidgetItem* item, int /*column*/)
{
  if (!item) return;
  // Skip folder items
  if (item->data(0, Qt::UserRole + 1).toString() == "folder") return;
  onLoadSelectedRecipe();
}

void RecipePage::onRecipeListContextMenu(const QPoint& pos)
{
  QTreeWidgetItem* item = m_recipeListWidget->itemAt(pos);

  // Themed context menu — same colors as the dropdown menus above.
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

  QMenu contextMenu(this);
  contextMenu.setStyleSheet(menuStyle);

  if (!item) {
    // Empty space: only option is to create a new folder
    QAction* newFolderAction = contextMenu.addAction("New Folder...");
    QAction* selected = contextMenu.exec(m_recipeListWidget->mapToGlobal(pos));
    if (selected == newFolderAction) {
      onCreateFolder();
    }
    return;
  }

  QString itemType = item->data(0, Qt::UserRole + 1).toString();

  if (itemType == "folder") {
    QAction* newFolderAction  = contextMenu.addAction("New Folder...");
    QAction* renameFolderAction = contextMenu.addAction("Rename Folder...");
    contextMenu.addSeparator();
    QAction* deleteFolderAction = contextMenu.addAction("Delete Folder");

    QAction* selected = contextMenu.exec(m_recipeListWidget->mapToGlobal(pos));
    if (selected == newFolderAction) {
      onCreateFolder();
    } else if (selected == renameFolderAction) {
      onRenameFolder(item);
    } else if (selected == deleteFolderAction) {
      onDeleteFolder(item);
    }
  } else {
    // Recipe item
    QAction* loadAction   = contextMenu.addAction("Load Recipe");
    contextMenu.addSeparator();
    QAction* renameAction = contextMenu.addAction("Rename...");
    QAction* deleteAction = contextMenu.addAction("Delete");
    contextMenu.addSeparator();

    // Build "Move to >" submenu with all folder names + "Uncategorized"
    QMenu* moveMenu = contextMenu.addMenu("Move to");
    moveMenu->setStyleSheet(menuStyle);

    QStringList folderNames;
    for (int i = 0; i < m_recipeListWidget->topLevelItemCount(); ++i) {
      QTreeWidgetItem* top = m_recipeListWidget->topLevelItem(i);
      if (top && top->data(0, Qt::UserRole + 1).toString() == "folder") {
        // Strip the count suffix " (N)" to get the raw folder name
        QString displayText = top->text(0);
        int parenIdx = displayText.lastIndexOf("  (");
        QString folderName = (parenIdx >= 0) ? displayText.left(parenIdx) : displayText;
        folderNames << folderName;
      }
    }

    QList<QAction*> moveActions;
    for (const QString& fn : folderNames) {
      moveActions << moveMenu->addAction(fn);
    }
    if (!moveActions.isEmpty()) moveMenu->addSeparator();
    QAction* moveToRootAction = moveMenu->addAction("Uncategorized (root)");

    QAction* selected = contextMenu.exec(m_recipeListWidget->mapToGlobal(pos));

    if (selected == loadAction) {
      onLoadSelectedRecipe();
    } else if (selected == renameAction) {
      onRenameSelectedRecipe();
    } else if (selected == deleteAction) {
      onDeleteSelectedRecipe();
    } else if (selected == moveToRootAction) {
      QString srcPath = item->data(0, Qt::UserRole).toString();
      QString recipeName = item->text(0);
      if (confirmRecipeMove(recipeName, "Uncategorized (root)")) {
        moveRecipeToFolder(srcPath, QString());
      }
    } else {
      // Check if it's one of the folder move actions
      for (int i = 0; i < moveActions.size(); ++i) {
        if (selected == moveActions[i]) {
          QString srcPath = item->data(0, Qt::UserRole).toString();
          QString recipeName = item->text(0);
          if (confirmRecipeMove(recipeName, folderNames[i])) {
            moveRecipeToFolder(srcPath, folderNames[i]);
          }
          break;
        }
      }
    }
  }
}

void RecipePage::onLoadSelectedRecipe()
{
  QTreeWidgetItem* item = m_recipeListWidget->currentItem();
  if (!item) return;
  // Skip folder items
  if (item->data(0, Qt::UserRole + 1).toString() == "folder") return;

  QString filePath = item->data(0, Qt::UserRole).toString();
  if (filePath.isEmpty()) return;

  if (m_recipeManager->loadRecipe(filePath.toStdString())) {
    QFileInfo loadedFileInfo(filePath);
    m_recipeNameEdit->setText(loadedFileInfo.baseName());
    refreshRecipeList();
    clearConfigurationForm();
    updateRecipeFileNameDisplay();

    // Save to settings for persistence
    saveLastRecipeToSettings(filePath);

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
    saveLastRecipeToSettings(filePath);

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

void RecipePage::onDeleteSelectedRecipe()
{
  QTreeWidgetItem* item = m_recipeListWidget->currentItem();
  if (!item) return;
  // Skip folder items
  if (item->data(0, Qt::UserRole + 1).toString() == "folder") return;

  QString filePath = item->data(0, Qt::UserRole).toString();
  QString recipeName = item->text(0);

  // Check for usage in programs and as sub-recipe
  QStringList programUsages = findRecipeUsageInPrograms(filePath);
  QStringList subRecipeUsages = findRecipeUsageAsSubRecipe(filePath);

  // Build confirmation message
  QString message = QString("Are you sure you want to delete '%1'?\n").arg(recipeName);

  if (!programUsages.isEmpty()) {
    message += "\nThis recipe is used in the following program(s):\n";
    for (const auto& usage : programUsages) {
      message += "  - " + usage + "\n";
    }
  }

  if (!subRecipeUsages.isEmpty()) {
    message += "\nThis recipe is used as a Sub-Recipe in:\n";
    for (const auto& usage : subRecipeUsages) {
      message += "  - " + usage + "\n";
    }
  }

  if (!programUsages.isEmpty() || !subRecipeUsages.isEmpty()) {
    message += "\nDeleting it will break these references.";
  }

  message += "\nThis action cannot be undone.";

  QMessageBox::StandardButton reply = QMessageBox::question(
    this,
    "Delete Recipe",
    message,
    QMessageBox::Yes | QMessageBox::No,
    QMessageBox::No
  );

  if (reply != QMessageBox::Yes) return;

  if (QFile::remove(filePath)) {
    populateRecipeListWidget();
    std::cout << "[RecipePage] Deleted recipe: " << filePath.toStdString() << std::endl;
  }
  else {
    QMessageBox::critical(this, "Error", "Failed to delete recipe file.");
  }
}

QStringList RecipePage::findRecipeUsageInPrograms(const QString& recipeFilePath)
{
  QStringList usages;
  if (!m_context || !m_context->productPositionManager ||
      !m_context->productPositionManager->HasLoadedProduct()) {
    return usages;
  }

  QString productName = QString::fromStdString(
    m_context->productPositionManager->GetCurrentProductName());
  QString programsPath = QString("config/%1/programs").arg(productName);

  QDir programsDir(programsPath);
  if (!programsDir.exists()) return usages;

  QStringList filters;
  filters << "*.json";
  QFileInfoList programFiles = programsDir.entryInfoList(filters, QDir::Files);

  QFileInfo targetInfo(recipeFilePath);
  QString targetCanonical = targetInfo.canonicalFilePath();

  for (const auto& progFileInfo : programFiles) {
    QFile file(progFileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) continue;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) continue;

    QJsonObject root = doc.object();
    QString programName = root["program_name"].toString(progFileInfo.baseName());

    // Scan all recipe slots for references to this recipe
    QJsonArray slotsArray = root["recipe_slots"].toArray();
    bool foundInProgram = false;
    for (int si = 0; si < slotsArray.size() && !foundInProgram; ++si) {
      QJsonObject slotObj = slotsArray[si].toObject();
      QJsonArray recipes = slotObj["recipes"].toArray();
      for (int ri = 0; ri < recipes.size(); ++ri) {
        QJsonObject recipeObj = recipes[ri].toObject();
        QString refPath = recipeObj["recipe_file_path"].toString();
        QFileInfo refInfo(refPath);
        if (refInfo.canonicalFilePath() == targetCanonical ||
            refInfo.absoluteFilePath() == targetInfo.absoluteFilePath()) {
          usages.append(programName);
          foundInProgram = true;
          break;
        }
      }
    }
  }

  return usages;
}

QStringList RecipePage::findRecipeUsageAsSubRecipe(const QString& recipeFilePath)
{
  QStringList usages;
  QString recipeFolderPath = getRecipeFolderPath();
  if (recipeFolderPath.isEmpty()) return usages;

  QFileInfo targetInfo(recipeFilePath);
  QString targetAbsolute = targetInfo.absoluteFilePath();

  // Scan all recipe JSON files recursively
  QDirIterator it(recipeFolderPath, QStringList() << "*.json",
    QDir::Files, QDirIterator::Subdirectories);

  for (int scanCount = 0; it.hasNext() && scanCount < 500; ++scanCount) {
    it.next();
    // Skip the recipe being deleted
    if (QFileInfo(it.filePath()).absoluteFilePath() == targetAbsolute) continue;

    QFile file(it.filePath());
    if (!file.open(QIODevice::ReadOnly)) continue;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) continue;

    QJsonObject root = doc.object();
    QString recipeName = root["recipe_name"].toString(QFileInfo(it.filePath()).baseName());

    // Check each step for Sub-Recipe type with matching recipe_file
    QJsonArray steps = root["steps"].toArray();
    for (int si = 0; si < steps.size(); ++si) {
      QJsonObject stepObj = steps[si].toObject();
      if (stepObj["type"].toString() == "Sub-Recipe") {
        QJsonObject params = stepObj["parameters"].toObject();
        QString subRecipePath = params["recipe_file"].toString();
        if (!subRecipePath.isEmpty()) {
          QFileInfo subInfo(subRecipePath);
          if (subInfo.absoluteFilePath() == targetAbsolute ||
              subInfo.canonicalFilePath() == targetInfo.canonicalFilePath()) {
            usages.append(recipeName);
            break;  // one match per recipe is enough
          }
        }
      }
    }
  }

  return usages;
}

void RecipePage::onRenameSelectedRecipe()
{
  QTreeWidgetItem* item = m_recipeListWidget->currentItem();
  if (!item) return;
  // Skip folder items
  if (item->data(0, Qt::UserRole + 1).toString() == "folder") return;

  QString filePath = item->data(0, Qt::UserRole).toString();
  QString currentName = item->text(0);

  bool ok;
  QString newName = QInputDialog::getText(
    this,
    "Rename Recipe",
    "Enter new recipe name:",
    QLineEdit::Normal,
    currentName,
    &ok
  );

  if (!ok || newName.isEmpty() || newName == currentName) return;

  // Validate filename
  QString invalidChars = "<>:\"/\\|?*";
  for (QChar c : invalidChars) {
    if (newName.contains(c)) {
      QMessageBox::warning(this, "Invalid Name",
        "Recipe name contains invalid characters.\n\nInvalid characters: < > : \" / \\ | ? *");
      return;
    }
  }

  QFileInfo oldFileInfo(filePath);
  QString newFilePath = oldFileInfo.absolutePath() + "/" + newName + ".json";

  // Check if file already exists
  if (QFile::exists(newFilePath)) {
    QMessageBox::warning(this, "Name Exists",
      QString("A recipe named '%1' already exists.").arg(newName));
    return;
  }

  // Update recipe name in JSON content
  QFile file(filePath);
  if (file.open(QIODevice::ReadOnly)) {
    QByteArray fileData = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(fileData);
    if (doc.isObject()) {
      QJsonObject obj = doc.object();
      obj["recipe_name"] = newName;

      QFile newFile(newFilePath);
      if (newFile.open(QIODevice::WriteOnly)) {
        newFile.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
        newFile.close();

        // Remove old file
        QFile::remove(filePath);

        populateRecipeListWidget();
        std::cout << "[RecipePage] Renamed recipe: " << currentName.toStdString()
                  << " -> " << newName.toStdString() << std::endl;
        return;
      }
    }
  }

  QMessageBox::critical(this, "Error", "Failed to rename recipe file.");
}

void RecipePage::onCopyRecipe()
{
  QTreeWidgetItem* item = m_recipeListWidget->currentItem();
  if (!item || item->data(0, Qt::UserRole + 1).toString() != "recipe") return;

  QString srcPath = item->data(0, Qt::UserRole).toString();
  QFileInfo fi(srcPath);
  QString dstPath = fi.absolutePath() + "/" + fi.baseName() + "_copy." + fi.suffix();

  // Avoid overwriting existing copies
  int counter = 1;
  for (int i = 0; i < 100; ++i) {
    if (!QFile::exists(dstPath)) break;
    ++counter;
    dstPath = fi.absolutePath() + "/" + fi.baseName() + "_copy" + QString::number(counter) + "." + fi.suffix();
  }

  if (QFile::copy(srcPath, dstPath)) {
    std::cout << "[RecipePage] Copied recipe to: " << dstPath.toStdString() << std::endl;
    populateRecipeListWidget();
  } else {
    QMessageBox::warning(this, "Copy Failed", "Failed to copy recipe file.");
  }
}

// ---------------------------------------------------------------------------
// Folder management
// ---------------------------------------------------------------------------

void RecipePage::onCreateFolder()
{
  bool ok = false;
  QString name = QInputDialog::getText(this, "New Folder", "Folder name:", QLineEdit::Normal, QString(), &ok);
  if (!ok || name.trimmed().isEmpty()) return;

  name = name.trimmed();
  QRegularExpression invalidChars("[<>:\"/\\\\|?*]");
  if (name.contains(invalidChars)) {
    QMessageBox::warning(this, "Invalid Name", "Folder name contains invalid characters:\n< > : \" / \\ | ? *");
    return;
  }

  QString folderPath = getRecipeFolderPath() + "/" + name;
  std::error_code ec;
  std::filesystem::create_directory(folderPath.toStdString(), ec);
  if (ec) {
    QMessageBox::warning(this, "Error",
      QString("Failed to create folder: %1").arg(QString::fromStdString(ec.message())));
    return;
  }

  std::cout << "[RecipePage] Created folder: " << folderPath.toStdString() << std::endl;
  populateRecipeListWidget();
}

void RecipePage::onRenameFolder(QTreeWidgetItem* folderItem)
{
  if (!folderItem) return;

  // Extract current folder name (strip the " (N)" count suffix)
  QString displayText = folderItem->text(0);
  int parenIdx = displayText.lastIndexOf("  (");
  QString currentName = (parenIdx >= 0) ? displayText.left(parenIdx) : displayText;

  bool ok = false;
  QString newName = QInputDialog::getText(this, "Rename Folder", "New folder name:",
    QLineEdit::Normal, currentName, &ok);
  if (!ok || newName.trimmed().isEmpty() || newName.trimmed() == currentName) return;

  newName = newName.trimmed();
  QRegularExpression invalidChars("[<>:\"/\\\\|?*]");
  if (newName.contains(invalidChars)) {
    QMessageBox::warning(this, "Invalid Name", "Folder name contains invalid characters:\n< > : \" / \\ | ? *");
    return;
  }

  QString basePath = getRecipeFolderPath();
  QString oldPath  = basePath + "/" + currentName;
  QString newPath  = basePath + "/" + newName;

  if (QDir(newPath).exists()) {
    QMessageBox::warning(this, "Name Exists",
      QString("A folder named '%1' already exists.").arg(newName));
    return;
  }

  std::error_code ec;
  std::filesystem::rename(oldPath.toStdString(), newPath.toStdString(), ec);
  if (ec) {
    QMessageBox::warning(this, "Error",
      QString("Failed to rename folder: %1").arg(QString::fromStdString(ec.message())));
    return;
  }

  std::cout << "[RecipePage] Renamed folder: " << currentName.toStdString()
            << " -> " << newName.toStdString() << std::endl;
  populateRecipeListWidget();
}

void RecipePage::onDeleteFolder(QTreeWidgetItem* folderItem)
{
  if (!folderItem) return;

  if (folderItem->childCount() > 0) {
    QMessageBox::warning(this, "Folder Not Empty",
      "Cannot delete a folder that contains recipes.\n\nMove or delete all recipes first.");
    return;
  }

  // Extract raw folder name
  QString displayText = folderItem->text(0);
  int parenIdx = displayText.lastIndexOf("  (");
  QString folderName = (parenIdx >= 0) ? displayText.left(parenIdx) : displayText;

  QMessageBox::StandardButton reply = QMessageBox::question(this,
    "Delete Folder",
    QString("Are you sure you want to delete the empty folder '%1'?").arg(folderName),
    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (reply != QMessageBox::Yes) return;

  QString folderPath = getRecipeFolderPath() + "/" + folderName;
  std::error_code ec;
  std::filesystem::remove(folderPath.toStdString(), ec);
  if (ec) {
    QMessageBox::warning(this, "Error",
      QString("Failed to delete folder: %1").arg(QString::fromStdString(ec.message())));
    return;
  }

  std::cout << "[RecipePage] Deleted folder: " << folderPath.toStdString() << std::endl;
  populateRecipeListWidget();
}

void RecipePage::moveRecipeToFolder(const QString& srcPath, const QString& targetFolderName)
{
  QFileInfo srcInfo(srcPath);
  if (!srcInfo.exists()) {
    QMessageBox::warning(this, "Error", "Source recipe file not found.");
    return;
  }

  QString destDir = getRecipeFolderPath();
  if (!targetFolderName.isEmpty()) {
    destDir += "/" + targetFolderName;
    // Ensure the target folder exists
    QDir().mkpath(destDir);
  }

  QString destPath = destDir + "/" + srcInfo.fileName();

  if (QFile::exists(destPath)) {
    QMessageBox::warning(this, "Name Conflict",
      QString("A recipe file named '%1' already exists in the target folder.").arg(srcInfo.fileName()));
    return;
  }

  std::error_code ec;
  std::filesystem::rename(srcPath.toStdString(), destPath.toStdString(), ec);
  if (ec) {
    QMessageBox::warning(this, "Error",
      QString("Failed to move recipe: %1").arg(QString::fromStdString(ec.message())));
    return;
  }

  std::cout << "[RecipePage] Moved recipe: " << srcPath.toStdString()
            << " -> " << destPath.toStdString() << std::endl;
  populateRecipeListWidget();
}

bool RecipePage::confirmRecipeMove(const QString& recipeName, const QString& targetFolder)
{
  QMessageBox msgBox(this);
  msgBox.setWindowTitle("Move Recipe?");
  msgBox.setIcon(QMessageBox::Warning);
  msgBox.setText(QString("Move \"%1\" to folder \"%2\"?").arg(recipeName, targetFolder));
  msgBox.setInformativeText(
    "Caution: If this recipe is referenced in any Run Program,\n"
    "you will need to update the program list on the Run Process Page.");
  msgBox.setStandardButtons(QMessageBox::Cancel);
  QPushButton* moveButton = msgBox.addButton("Move", QMessageBox::AcceptRole);
  msgBox.setDefaultButton(QMessageBox::Cancel);
  msgBox.exec();
  return (msgBox.clickedButton() == moveButton);
}

// ---------------------------------------------------------------------------
// Event filter: intercept QTreeWidget drop events for physical file moves
// ---------------------------------------------------------------------------

bool RecipePage::eventFilter(QObject* watched, QEvent* event)
{
  if (watched == m_recipeListWidget->viewport() && event->type() == QEvent::Drop) {
    QDropEvent* dropEvent = static_cast<QDropEvent*>(event);

    // Identify the dragged item (currently selected before drop)
    QTreeWidgetItem* draggedItem = m_recipeListWidget->currentItem();
    if (!draggedItem) return true;  // consume, no-op

    // Only allow moving recipe items (not folders)
    if (draggedItem->data(0, Qt::UserRole + 1).toString() != "recipe") return true;

    QString srcPath = draggedItem->data(0, Qt::UserRole).toString();
    QString recipeName = draggedItem->text(0);

    // Find the item under the drop position
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QTreeWidgetItem* targetItem = m_recipeListWidget->itemAt(dropEvent->position().toPoint());
#else
    QTreeWidgetItem* targetItem = m_recipeListWidget->itemAt(dropEvent->pos());
#endif

    QString targetFolderName;  // empty = root

    if (targetItem) {
      QString targetType = targetItem->data(0, Qt::UserRole + 1).toString();
      if (targetType == "folder") {
        // Dropped onto a folder header: extract raw folder name
        QString displayText = targetItem->text(0);
        int parenIdx = displayText.lastIndexOf("  (");
        targetFolderName = (parenIdx >= 0) ? displayText.left(parenIdx) : displayText;
      } else if (targetType == "recipe") {
        // Dropped onto a sibling recipe: move to same parent folder
        QTreeWidgetItem* parent = targetItem->parent();
        if (parent && parent->data(0, Qt::UserRole + 1).toString() == "folder") {
          QString displayText = parent->text(0);
          int parenIdx = displayText.lastIndexOf("  (");
          targetFolderName = (parenIdx >= 0) ? displayText.left(parenIdx) : displayText;
        }
        // else: dropped at root level — targetFolderName stays empty
      }
    }
    // else: dropped on empty space — root

    // Don't move if source and destination are the same folder
    QFileInfo srcInfo(srcPath);
    QString currentFolder = srcInfo.dir().canonicalPath();
    QString targetDir = QDir(getRecipeFolderPath()).canonicalPath();
    if (!targetFolderName.isEmpty()) {
      targetDir += "/" + targetFolderName;
    }
    if (QDir(currentFolder) == QDir(targetDir)) {
      return true;  // already in that folder, consume event silently
    }

    QString targetLabel = targetFolderName.isEmpty() ? "Uncategorized (root)" : targetFolderName;
    if (confirmRecipeMove(recipeName, targetLabel)) {
      moveRecipeToFolder(srcPath, targetFolderName);
    }

    dropEvent->accept();
    return true;  // consumed — prevent QTreeWidget's built-in reorder
  }
  return QWidget::eventFilter(watched, event);
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

  // Insert at current selection + 1 (after selected), or at end if nothing selected
  int insertIndex = (m_selectedStepIndex >= 0) ? m_selectedStepIndex + 1 : m_recipeManager->getStepCount();
  m_recipeManager->insertStep(insertIndex, item->text(0).toStdString());
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
           double stepSizeUm, bool enableX, bool enableY, bool enableZ) -> bool {
    bool result = false;

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
        // Show the manual adjust widget in the floating run window
        if (m_runWindow) {
          m_runWindow->setActiveWidget(m_manualAdjustWidget);
        }
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

  // ✅ STEP 2: Create RecipeWorker with new signature
  m_recipeWorker = new RecipeWorker(
    m_sequenceEngine,
    m_processFactory,
    m_context->productPositionManager,
    m_context->motionManager,
    recipeFilePath,  // ✅ Pass file path instead of RecipeManager
    this
  );

  // ✅ Pass execution context to worker for context viewer
  m_recipeWorker->setExternalContext(m_currentContext);

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
      // Show the user input widget in the floating run window
      if (m_runWindow) {
        m_runWindow->setActiveWidget(m_userInputWidget);
      }
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
        // Show the user input widget in the floating run window
        if (m_runWindow) {
          m_runWindow->setActiveWidget(m_userInputWidget);
        }
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

  // Safety-critical: DirectConnection ensures immediate stop on the signaling
  // thread without event loop delay — required for E-stop responsiveness.
  if (m_context->safetyWatchManager) {
    connect(m_context->safetyWatchManager, &SafetyWatchManager::safetyTriggered,
            m_recipeWorker, [this](QString /*title*/, QString /*message*/, bool critical) {
      if (critical && m_recipeWorker) {
        m_recipeWorker->requestStop();
      }
    }, Qt::DirectConnection);
  }

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

  // Copy the selected step to temporary recipe, preserving the original uniqueId
  // so that calibration data saved using this UID can be retrieved later
  tempRecipe.insertStepAt(
    0,                           // index
    step->processType,           // process type
    step->userNickname,          // nickname
    true,                        // enabled
    step->parameters,            // parameters
    step->uniqueId               // preserve original UID
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

  // ✅ NEW: Use RecipeWorker for background thread execution (same as Run Recipe)
  m_recipeWorker = new RecipeWorker(
    m_sequenceEngine,
    m_processFactory,
    m_context->productPositionManager,
    m_context->motionManager,
    tempRecipePath,
    this
  );

  // ✅ Pass execution context to worker for context viewer
  m_recipeWorker->setExternalContext(m_currentContext);

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
      // Show the user input widget in the floating run window
      if (m_runWindow) {
        m_runWindow->setActiveWidget(m_userInputWidget);
      }
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
        // Show the user input widget in the floating run window
        if (m_runWindow) {
          m_runWindow->setActiveWidget(m_userInputWidget);
        }
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

  // Safety-critical: DirectConnection ensures immediate stop on the signaling
  // thread without event loop delay — required for E-stop responsiveness.
  if (m_context->safetyWatchManager) {
    connect(m_context->safetyWatchManager, &SafetyWatchManager::safetyTriggered,
            m_recipeWorker, [this](QString /*title*/, QString /*message*/, bool critical) {
      if (critical && m_recipeWorker) {
        m_recipeWorker->requestStop();
      }
    }, Qt::DirectConnection);
  }

  // Start background thread execution ✅
  m_recipeWorker->start();

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

  if (!buildSequenceFromRecipe(sequence)) {
    delete sequence;
    return false;
  }

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

bool RecipePage::buildSequenceFromRecipe(SequenceBuilder* sequence)
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
      QMessageBox::critical(this, "Recipe Build Error",
        QString::fromStdString("Failed to build step '" + step->userNickname + "':\n" + errorMsg));
      return false;
    }
  }

  std::cout << "[RecipePage] Sequence built successfully: "
    << enabledCount << " enabled steps out of "
    << totalCount << " total steps" << std::endl;
  return true;
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
    if (m_recipeWorker && m_recipeWorker->isRunning()) {
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

  m_recipeWorker = nullptr;
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
  if (m_recipeWorker && m_recipeWorker->isRunning()) {
    m_recipeWorker->requestStop();
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
    if (m_recipeListWidget) m_recipeListWidget->setEnabled(isEditing);
    if (m_refreshRecipeListButton) m_refreshRecipeListButton->setEnabled(isEditing);
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

  // Process template list (middle panel)
  if (m_processTemplateList) m_processTemplateList->setEnabled(isEditing);
  if (m_addButton) m_addButton->setEnabled(isEditing);

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
                                    bool enableX, bool enableY, bool enableZ)
{
  // Show the manual adjust widget in the floating run window
  if (m_runWindow) {
    m_runWindow->setActiveWidget(m_manualAdjustWidget);
  }
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
// QUICK ADD MODE (Ctrl+B shortcut)
//=============================================================================

void RecipePage::keyPressEvent(QKeyEvent* event)
{
  // Ctrl+B to activate quick add mode
  if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_B) {
    if (!m_quickAddMode) {
      activateQuickAddMode();
    } else {
      deactivateQuickAddMode();
    }
    event->accept();
    return;
  }

  // If in quick add mode, handle number keys
  if (m_quickAddMode) {
    int key = event->key();

    // Escape to cancel
    if (key == Qt::Key_Escape) {
      deactivateQuickAddMode();
      event->accept();
      return;
    }

    // Number keys 1-9 and 0
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
      int num = (key == Qt::Key_0) ? 10 : (key - Qt::Key_0);  // 0 maps to 10

      if (m_quickAddSelectedCategory < 0) {
        // First key press: select category
        handleQuickAddCategoryKey(num);
      } else {
        // Second key press: select process
        handleQuickAddProcessKey(num);
      }
      event->accept();
      return;
    }
  }

  // Pass to base class
  QWidget::keyPressEvent(event);
}

void RecipePage::activateQuickAddMode()
{
  m_quickAddMode = true;
  m_quickAddSelectedCategory = -1;

  // Create overlay if not exists
  if (!m_quickAddOverlay) {
    m_quickAddOverlay = new QLabel(this);
    m_quickAddOverlay->setAlignment(Qt::AlignCenter);
    m_quickAddOverlay->setStyleSheet(QString(
      "QLabel { "
      "  background-color: %1; "
      "  color: white; "
      "  font-size: 14px; "
      "  font-weight: bold; "
      "  border: 2px solid %2; "
      "  border-radius: 8px; "
      "  padding: 15px; "
      "}"
    ).arg(QLatin1String(Theme::Color::primary),
          QLatin1String(Theme::Color::primaryHover)));
  }

  updateQuickAddDisplay();
  m_quickAddOverlay->show();
  m_quickAddOverlay->raise();

  // Make sure we can receive key events
  setFocus();

  std::cout << "[RecipePage] Quick Add Mode activated - press category number (1-9)" << std::endl;
}

void RecipePage::deactivateQuickAddMode()
{
  m_quickAddMode = false;
  m_quickAddSelectedCategory = -1;

  if (m_quickAddOverlay) {
    m_quickAddOverlay->hide();
  }

  std::cout << "[RecipePage] Quick Add Mode deactivated" << std::endl;
}

void RecipePage::handleQuickAddCategoryKey(int key)
{
  auto categories = RecipeManager::getProcessCategories();
  int categoryIndex = key - 1;  // 1-based to 0-based

  if (categoryIndex < 0 || categoryIndex >= static_cast<int>(categories.size())) {
    std::cout << "[RecipePage] Invalid category key: " << key << std::endl;
    return;
  }

  m_quickAddSelectedCategory = categoryIndex;
  updateQuickAddDisplay();

  std::cout << "[RecipePage] Selected category: " << categories[categoryIndex]
            << " - press process number (1-9)" << std::endl;
}

void RecipePage::handleQuickAddProcessKey(int key)
{
  auto categories = RecipeManager::getProcessCategories();

  if (m_quickAddSelectedCategory < 0 ||
      m_quickAddSelectedCategory >= static_cast<int>(categories.size())) {
    deactivateQuickAddMode();
    return;
  }

  std::string selectedCategory = categories[m_quickAddSelectedCategory];
  auto allProcesses = RecipeManager::getProcessesByCategory(selectedCategory);

  // Filter out hidden processes so the index matches what the user sees
  std::vector<std::string> processes;
  processes.reserve(allProcesses.size());
  for (const auto& p : allProcesses) {
    if (m_hiddenProcesses.count(p) == 0) {
      processes.push_back(p);
    }
  }

  int processIndex = key - 1;  // 1-based to 0-based

  if (processIndex < 0 || processIndex >= static_cast<int>(processes.size())) {
    std::cout << "[RecipePage] Invalid process key: " << key << std::endl;
    deactivateQuickAddMode();
    return;
  }

  std::string processType = processes[processIndex];

  // Add the process to the recipe (addStep uses default parameters)
  m_recipeManager->addStep(processType);

  // Refresh and select the new step
  refreshRecipeList();
  m_selectedStepIndex = m_recipeManager->getStepCount() - 1;
  selectStepByIndex(m_selectedStepIndex);
  updateConfigurationForm();

  std::cout << "[RecipePage] Quick Add: Added '" << processType << "'" << std::endl;

  // Deactivate quick add mode
  deactivateQuickAddMode();
}

void RecipePage::updateQuickAddDisplay()
{
  if (!m_quickAddOverlay) return;

  QString text;
  auto categories = RecipeManager::getProcessCategories();

  if (m_quickAddSelectedCategory < 0) {
    // Show category list
    text = "<b>QUICK ADD MODE</b> (Ctrl+B)<br><br>";
    text += "<b>Select Category:</b><br><br>";

    int num = 1;
    for (const auto& category : categories) {
      if (num <= 9) {
        text += QString("<b>%1</b> - %2<br>")
          .arg(num)
          .arg(QString::fromStdString(category));
      }
      num++;
      if (num > 9) break;  // Only show first 9
    }

    text += "<br><i>Press ESC to cancel</i>";
  } else {
    // Show process list for selected category (filtered by hidden set)
    std::string selectedCategory = categories[m_quickAddSelectedCategory];
    auto allProcesses = RecipeManager::getProcessesByCategory(selectedCategory);
    std::vector<std::string> processes;
    processes.reserve(allProcesses.size());
    for (const auto& p : allProcesses) {
      if (m_hiddenProcesses.count(p) == 0) {
        processes.push_back(p);
      }
    }

    text = QString("<b>QUICK ADD: %1</b><br><br>")
      .arg(QString::fromStdString(selectedCategory));
    text += "<b>Select Process:</b><br><br>";

    int num = 1;
    for (const auto& process : processes) {
      if (num <= 9) {
        text += QString("<b>%1</b> - %2<br>")
          .arg(num)
          .arg(QString::fromStdString(process));
      }
      num++;
      if (num > 9) break;  // Only show first 9
    }

    if (processes.size() > 9) {
      text += QString("<i>... and %1 more (use mouse)</i><br>").arg(processes.size() - 9);
    }

    text += "<br><i>Press ESC to cancel</i>";
  }

  m_quickAddOverlay->setText(text);

  // Position overlay in center of the widget
  m_quickAddOverlay->adjustSize();
  int x = (width() - m_quickAddOverlay->width()) / 2;
  int y = (height() - m_quickAddOverlay->height()) / 2;
  m_quickAddOverlay->move(x, y);
}

// ============================================================================
// HOT RELOAD - SHOW EVENT
// ============================================================================

void RecipePage::showEvent(QShowEvent* event)
{
  QWidget::showEvent(event);

  // Skip reload if recipe is currently executing
  if (m_recipeWorker && m_recipeWorker->isRunning()) {
    std::cout << "[RecipePage] Skipping hot reload - recipe is executing" << std::endl;
    return;
  }

  // Pick up any external edits to hidden_processes(.local).json and refresh
  // the available-processes tree.
  loadHiddenProcessesFromDisk();
  refreshProcessTemplateList();

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
  if (m_recipeWorker && m_recipeWorker->isRunning()) {
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

  // Recipe list tree — inline QSS (7 args)
  m_recipeListWidget->setStyleSheet(QString(
    "QTreeWidget { "
    "  background-color: %1; "
    "  color: %2; "
    "  border: 1px solid %3; "
    "  border-radius: 3px; "
    "  font-size: 14px; "
    "} "
    "QTreeWidget::item { "
    "  padding: 7px 8px; "
    "  border-bottom: 1px solid %4; "
    "} "
    "QTreeWidget::item:selected { "
    "  background-color: %5; "
    "  color: %6; "
    "} "
    "QTreeWidget::item:hover { "
    "  background-color: %7; "
    "} "
    "QTreeWidget::branch { "
    "  background-color: %1; "
    "} "
    "QTreeWidget::branch:has-siblings:!adjoins-item { "
    "  border-image: none; "
    "} "
    "QTreeWidget::branch:open:has-children { "
    "  image: url(none); "
    "} "
    "QTreeWidget::branch:closed:has-children { "
    "  image: url(none); "
    "}"
  ).arg(QLatin1String(Theme::Color::bgDeep),
        QLatin1String(Theme::Color::textPrimary),
        QLatin1String(Theme::Color::borderSubtle),
        QLatin1String(Theme::Color::bgPrimary),
        QLatin1String(Theme::Color::primary),
        QLatin1String(Theme::Color::textOnPrimary),
        QLatin1String(Theme::Color::bgHover)));

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

  // Folder / recipe action buttons — compact inline QSS patterns
  const QString compactPrimary = QString(
      "QPushButton { background-color: %1; color: white; border: none; "
      "border-radius: 3px; padding: 2px 8px; font-size: 11px; font-weight: bold; }"
      "QPushButton:hover { background-color: %2; }"
      "QPushButton:disabled { background-color: %3; color: %4; }"
    ).arg(QLatin1String(Theme::Color::primary),
          QLatin1String(Theme::Color::primaryHover),
          QLatin1String(Theme::Color::disabledBg),
          QLatin1String(Theme::Color::textDisabled));

  const QString compactNeutral = QString(
      "QPushButton { background-color: %1; color: %2; border: none; "
      "border-radius: 3px; padding: 2px 8px; font-size: 11px; font-weight: bold; }"
      "QPushButton:hover { background-color: %3; }"
      "QPushButton:disabled { background-color: %4; color: %5; }"
    ).arg(QLatin1String(Theme::Color::bgSurface),
          QLatin1String(Theme::Color::textPrimary),
          QLatin1String(Theme::Color::bgHover),
          QLatin1String(Theme::Color::disabledBg),
          QLatin1String(Theme::Color::textDisabled));

  const QString compactDanger = QString(
      "QPushButton { background-color: %1; color: white; border: none; "
      "border-radius: 3px; padding: 2px 8px; font-size: 11px; font-weight: bold; }"
      "QPushButton:hover { background-color: %2; }"
      "QPushButton:disabled { background-color: %3; color: %4; }"
    ).arg(QLatin1String(Theme::Color::danger),
          QLatin1String(Theme::Color::dangerHover),
          QLatin1String(Theme::Color::disabledBg),
          QLatin1String(Theme::Color::textDisabled));

  m_createFolderButton->setStyleSheet(compactPrimary);
  m_removeFolderButton->setStyleSheet(compactNeutral);
  m_copyRecipeButton->setStyleSheet(compactNeutral);
  m_deleteRecipeButton->setStyleSheet(compactDanger);

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