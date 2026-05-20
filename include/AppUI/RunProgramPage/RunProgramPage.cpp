// RunProgramPage.cpp
#include "RunProgramPage.h"
#include "ProgramSettingsStore.h"
#include "VisionHistoryWidget.h"
#include "SequenceContextViewer.h"
#include "ProgramEditPanel.h"
#include "SlotManagerWidget.h"
#include "RunPageLeftPanel.h"
#include "ExecutionStatusWidget.h"
#include "ApplicationContext.h"
#include "settings/ApplicationSettings.h"
#include "ProgramManager.h"
#include "ConditionExpressionEvaluator.h"
#include "safety/SafetyWatchManager.h"
#include "ProgramWorker.h"
#include "ProcessSequenceFactory.h"
#include "ExecutionController.h"
#include "UserInteractionOverlay.h"
#include "ScanControlWidget.h"
#include "ScanController.h"
#include "RollCorrectionWidget.h"
#include "MotionInspectorWidget.h"
#include "M4AASerialDataWidget.h"
#include "M4AngleWidget.h"
#include "Theme.h"
#include "ThemeButtonStyles.h"
#include "common/ScanActivityTracker.h"
#include "motions/unified_motion_manager.h"
#include "SequenceEngine.h"
#include "MotionCommandHandler.h"
#include "CameraClickToMoveWidget.h"
#include <future>
#include "PlacementRetraceWidget.h"
#include "PlacementCorrectionStore.h"
#include "../EdgeTransferTrainer/EdgeRetraceWidget.h"
#include "../EdgeTransferTrainer/EdgeTransferStore.h"
#include "PlacementCorrectionData.h"
#include <filesystem>
#include "ILogger.h"
#include "LogManager.h"
#include "DataStore.h"
#include "RecipeManager.h"
#include "product_position_manager.h"  // ✅ For product folder path
#include "FileSelectorDialog.h"  // ✅ Custom file selector
#include "SequenceContext.h"     // For context tab
#include "DependencyGraphWindow.h"  // For visual dependency graph
#include "LoopConfigDialog.h"        // For loop recipe configuration

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QScrollArea>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QTextEdit>
#include <QEventLoop>
#include <QMetaObject>
#include <QThread>
#include <QApplication>
#include <QHeaderView>
#include <QTableWidgetItem>
#include <QSettings>
#include <QSet>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDir>
#include <QClipboard>
#include <QDateTime>
#include <QRegularExpression>
#include <iostream>
#include <cmath>
#include <memory>  // For std::make_shared
#include <functional>  // For std::function

// Forward-declaration of the SI-prefix formatter defined in the anonymous
// namespace lower in this file. Needed because the scan-display throttle
// timer's lambda (in setup, ~line 740) calls it before its definition.
namespace { QString formatWithSIPrefix(double value); }
#include <QPointer>  // For safe widget reference

#ifdef HAS_OPENCV
#include "SequenceContext.h"
#include "GdiWindow/GdiCameraWindowManager.h"
#include "vision/VisionResult.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>  // For cv::imread
#include <QImage>
#include <QPixmap>
#endif

// Helper function to format numbers with SI prefixes for easier reading
static QString formatNumberWithSIPrefix(double value) {
  if (value == 0.0) return "0";

  double absValue = std::abs(value);
  QString sign = value < 0 ? "-" : "";

  struct SIPrefix { double threshold; const char* prefix; double divisor; };
  static const SIPrefix prefixes[] = {
    {1e12, "T", 1e12},
    {1e9,  "G", 1e9},
    {1e6,  "M", 1e6},
    {1e3,  "k", 1e3},
    {1.0,  "",  1.0},
    {1e-3, "m", 1e-3},
    {1e-6, "u", 1e-6},  // µ shown as u for compatibility
    {1e-9, "n", 1e-9},
    {1e-12, "p", 1e-12},
  };

  for (const auto& p : prefixes) {
    if (absValue >= p.threshold) {
      double scaled = absValue / p.divisor;
      // Format with up to 3 significant digits
      if (scaled >= 100) {
        return QString("%1%2%3").arg(sign).arg(scaled, 0, 'f', 0).arg(p.prefix);
      } else if (scaled >= 10) {
        return QString("%1%2%3").arg(sign).arg(scaled, 0, 'f', 1).arg(p.prefix);
      } else {
        return QString("%1%2%3").arg(sign).arg(scaled, 0, 'f', 2).arg(p.prefix);
      }
    }
  }

  // Very small numbers - use scientific notation
  return QString("%1%2").arg(sign).arg(absValue, 0, 'e', 2);
}

// Helper function to parse condition string and format numbers with SI prefixes
static QString formatConditionWithSIUnits(const QString& condition) {
  QString result = condition;

  // Regex to find decimal numbers (including scientific notation)
  QRegularExpression numberRegex(R"([-+]?(?:\d+\.?\d*|\d*\.?\d+)(?:[eE][-+]?\d+)?)");
  QRegularExpressionMatchIterator matchIter = numberRegex.globalMatch(condition);

  // Process matches in reverse order to preserve string positions
  QVector<QPair<int, int>> matches;  // start, length
  QVector<QString> replacements;

  while (matchIter.hasNext()) {
    QRegularExpressionMatch match = matchIter.next();
    QString numStr = match.captured(0);
    bool ok;
    double value = numStr.toDouble(&ok);

    if (ok && numStr.contains('.')) {  // Only format decimal numbers
      QString formatted = formatNumberWithSIPrefix(value);
      if (formatted != numStr) {  // Only replace if different
        matches.append({match.capturedStart(), match.capturedLength()});
        replacements.append(formatted);
      }
    }
  }

  // Replace in reverse order
  for (int i = matches.size() - 1; i >= 0; --i) {
    result.replace(matches[i].first, matches[i].second, replacements[i]);
  }

  return result;
}

RunProgramPage::RunProgramPage(ApplicationContext* context, TransformationMatrixLoader* matrixLoader, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_matrixLoader(matrixLoader)
  , m_programManager(new ProgramManager())
  , m_processFactory(nullptr)
  , m_currentRecipeIndex(-1)
  , m_scanWidget(nullptr)
  , m_dataStore(context->dataStore)
  , m_currentSubscribedChannel("")
  , m_currentContext(nullptr)
{
  // Create ProcessSequenceFactory
  m_processFactory = new ProcessSequenceFactory();

  // Initialize the status badge defaults from Theme so an "Idle" badge is
  // painted with palette colors before the first setProgramStatus() call.
  m_baseStatusBgColor = QLatin1String(Theme::Color::bgSurface);
  m_baseStatusFgColor = QLatin1String(Theme::Color::textDisabled);

  setupUI();

  // Now that setupUI() has built m_scanWidget, hand it to a ScanController
  // along with the page-level dependencies it needs. The controller owns
  // the ScanExecutor and forwards user actions / executor results between
  // them.
  ScanController::Deps scanDeps;
  scanDeps.dialogParent = this;
  scanDeps.context      = m_context;
  scanDeps.widget       = m_scanWidget;
  scanDeps.appendLog    = [this](const QString& msg) {
    if (m_logTextEdit) m_logTextEdit->appendPlainText(msg);
  };
  m_scanController = new ScanController(std::move(scanDeps), this);

  // Build the execution controller now that setupUI() has constructed
  // the overlay it depends on. Wire its forwarded signals to the
  // existing page-side execution slots.
  {
    ExecutionController::Deps execDeps;
    execDeps.context        = m_context;
    execDeps.programManager = m_programManager;
    execDeps.processFactory = m_processFactory;
    execDeps.overlay        = m_overlay;
    m_executionController = new ExecutionController(std::move(execDeps), this);

    connect(m_executionController, &ExecutionController::programExecutionStarted,
      this, &RunProgramPage::onProgramExecutionStarted);
    connect(m_executionController, &ExecutionController::recipeStarted,
      this, &RunProgramPage::onRecipeStarted);
    connect(m_executionController, &ExecutionController::recipeCompleted,
      this, &RunProgramPage::onRecipeCompleted);
    connect(m_executionController, &ExecutionController::recipeSkipped,
      this, &RunProgramPage::onRecipeSkipped);
    connect(m_executionController, &ExecutionController::programExecutionCompleted,
      this, &RunProgramPage::onProgramExecutionCompleted);
    connect(m_executionController, &ExecutionController::programStopped,
      this, &RunProgramPage::onProgramStopped);
    connect(m_executionController, &ExecutionController::logMessage,
      this, &RunProgramPage::onWorkerLogMessage);
    connect(m_executionController, &ExecutionController::stepBuildStarted,
      this, &RunProgramPage::onStepBuildStarted);
    connect(m_executionController, &ExecutionController::stepBuildCompleted,
      this, &RunProgramPage::onStepBuildCompleted);
    connect(m_executionController, &ExecutionController::sequenceExecutionStarted,
      this, &RunProgramPage::onSequenceExecutionStarted);
    connect(m_executionController, &ExecutionController::stepExecutionCompleted,
      this, &RunProgramPage::onStepExecutionCompleted);
    connect(m_executionController, &ExecutionController::waitProgress,
      this, &RunProgramPage::onWaitProgress);
    connect(m_executionController, &ExecutionController::actionStarted,
      this, &RunProgramPage::onActionStarted);
    connect(m_executionController, &ExecutionController::loopIterationStarted,
      this, &RunProgramPage::onLoopIterationStarted);
    connect(m_executionController, &ExecutionController::loopIterationCompleted,
      this, &RunProgramPage::onLoopIterationCompleted);
    connect(m_executionController, &ExecutionController::visionResultReady,
      this, &RunProgramPage::onVisionResultReady);

    // Snapshot the final SequenceContext just before the worker is
    // destroyed (so the Context tab shows captured values after exit).
    connect(m_executionController, &ExecutionController::workerAboutToFinalize,
      this, [this]() { updateContextTable(true); });

    // Retrace edit summary dialog (page owns the QMessageBox).
    connect(m_executionController, &ExecutionController::retraceEditSummaryRequested,
      this, &RunProgramPage::showRetraceEditSummary);
  }

  // Mirror the TopBar scan-busy indicator on the execution status badge so the
  // operator sees a SCANNING overlay on the RunProgramPage even when the TopBar
  // is offscreen. The tracker may emit from the SequenceEngine worker thread,
  // so route through QueuedConnection before touching the QLabel.
  if (m_context->scanActivityTracker) {
    connect(m_context->scanActivityTracker, &ScanActivityTracker::activeChanged,
      this, [this](bool active) {
        m_isScanActive = active;
        applyProgramStatusLabel();
      }, Qt::QueuedConnection);
    // Defensive sync in case a scan is already running when the page is built.
    m_isScanActive = m_context->scanActivityTracker->IsActive();
    applyProgramStatusLabel();
  }

  // Update product display
  if (m_context->productPositionManager && m_context->productPositionManager->HasLoadedProduct()) {
    std::string productName = m_context->productPositionManager->GetCurrentProductName();
    m_leftPanel->productLabel()->setText(QString("Product: %1").arg(QString::fromStdString(productName)));
  }

  // ✅ Auto-load last program from settings
  loadLastProgramFromSettings();

  // ✅ Clear any stale vision results from previous session
  clearVisionResult();
}

RunProgramPage::~RunProgramPage()
{
  // Stop the SequenceContextViewer's timers (live polling + CV auto-refresh).
  if (m_contextViewer) {
    m_contextViewer->StopLivePolling();
    m_contextViewer->StopCvAutoRefresh();
  }

  // ✅ ADDED: Unsubscribe from DataStore
  if (m_dataStore && !m_currentSubscribedChannel.isEmpty()) {
    m_dataStore->Unsubscribe(m_currentSubscribedChannel.toStdString());
  }

  // Tear down the ExecutionController explicitly BEFORE the
  // ProgramManager / ProcessSequenceFactory it depends on go away.
  // Its destructor stops + waits for the worker thread, which still
  // references the program manager during its final cleanup.
  delete m_executionController;
  m_executionController = nullptr;

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

  // Left panel (the back button, product banner, and File/Execute/Option
  // tabs all live inside RunPageLeftPanel; the page wires its widgets'
  // signals below).
  RunPageLeftPanel::Deps leftDeps;
  leftDeps.manager      = m_programManager;
  leftDeps.dialogParent = this;
  m_leftPanel = new RunPageLeftPanel(std::move(leftDeps));
  m_runSlotWidget = m_leftPanel->slotWidget();

  // Wire the inner widgets to page slots.
  connect(m_leftPanel->backButton(),    &QPushButton::clicked, this, &RunProgramPage::backToMainMenu);
  connect(m_leftPanel->newButton(),     &QPushButton::clicked, this, &RunProgramPage::onNewProgram);
  connect(m_leftPanel->loadButton(),    &QPushButton::clicked, this, &RunProgramPage::onLoadProgram);
  connect(m_leftPanel->saveButton(),    &QPushButton::clicked, this, &RunProgramPage::onSaveProgram);
  connect(m_leftPanel->saveAsButton(),  &QPushButton::clicked, this, &RunProgramPage::onSaveAsProgram);
  connect(m_leftPanel->editButton(),    &QPushButton::clicked, this, &RunProgramPage::onEditProgram);
  connect(m_leftPanel->executeButton(), &QPushButton::clicked, this, &RunProgramPage::onExecuteProgram);
  connect(m_leftPanel->configureLoopButton(), &QPushButton::clicked,
          this, &RunProgramPage::onConfigureLoop);
  connect(m_leftPanel->recentProgramsList(), &QListWidget::itemDoubleClicked,
          this, &RunProgramPage::onRecentProgramClicked);
  connect(m_leftPanel->recipeList(), &QListWidget::customContextMenuRequested,
          this, &RunProgramPage::onRecipeListContextMenu);
  connect(m_runSlotWidget, &SlotManagerWidget::activeSlotChanged,
          this, &RunProgramPage::onSlotChangedFromRun);

  // Center panel - Execution view (scanning is now a tab inside)
  createRightPanel();

  mainLayout->addWidget(m_leftPanel, 1);     // Left: 25%
  mainLayout->addWidget(m_rightStack, 3);    // Center: 75%

  // Start with edit widget visible
  showEditWidget();
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

  // Top bar with Close button + title.
  QHBoxLayout* topBarLayout = new QHBoxLayout();

  QPushButton* closeButton = new QPushButton(QString::fromUtf8("\xE2\x86\x90 Close"));
  closeButton->setMinimumHeight(35);
  closeButton->setStyleSheet(Theme::Buttons::bezelNeutral());

  QLabel* titleLabel = new QLabel("Edit Program");
  titleLabel->setStyleSheet(QString("font-size: 18px; font-weight: bold; color: %1;")
                              .arg(QLatin1String(Theme::Color::textPrimary)));

  topBarLayout->addWidget(closeButton);
  topBarLayout->addWidget(titleLabel);
  topBarLayout->addStretch();

  layout->addLayout(topBarLayout);

  connect(closeButton, &QPushButton::clicked, this, &RunProgramPage::showExecutionWidget);

  // Program name + Save (page-level: program metadata, not recipe-list edit).
  m_programNameGroup = new QGroupBox("Program Name");
  m_programNameGroup->setStyleSheet(Theme::Boxes::groupBoxDark());
  QVBoxLayout* programNameLayout = new QVBoxLayout(m_programNameGroup);

  QHBoxLayout* nameAndSaveLayout = new QHBoxLayout();

  m_programNameEdit = new QLineEdit();
  m_programNameEdit->setPlaceholderText("Enter program name...");
  m_programNameEdit->setMinimumHeight(35);
  m_programNameEdit->setStyleSheet("font-size: 14px; padding: 5px;");
  nameAndSaveLayout->addWidget(m_programNameEdit, 1);

  m_saveEditButton = new QPushButton(QString::fromUtf8("\xF0\x9F\x92\xBE Save"));
  m_saveEditButton->setMinimumHeight(35);
  m_saveEditButton->setMinimumWidth(100);
  m_saveEditButton->setStyleSheet(Theme::Buttons::bezelPrimary());
  nameAndSaveLayout->addWidget(m_saveEditButton);

  programNameLayout->addLayout(nameAndSaveLayout);
  layout->addWidget(m_programNameGroup);

  // textEdited fires only on user input (not setText), so it cleanly
  // distinguishes "user typed in the field" from "we re-populated the
  // field after a load/save".
  connect(m_programNameEdit, &QLineEdit::textEdited,
          this, [this](const QString&) { setProgramDirty(true); });

  // Edit-side Recipe Slot bar (combo + Rename/Add/Copy/Delete buttons).
  {
    SlotManagerWidget::Deps deps;
    deps.manager      = m_programManager;
    deps.dialogParent = this;
    m_editSlotWidget = new SlotManagerWidget(SlotManagerWidget::Mode::Edit, deps);
    connect(m_editSlotWidget, &SlotManagerWidget::activeSlotChanged,
            this, &RunProgramPage::onSlotChangedFromEdit);
    connect(m_editSlotWidget, &SlotManagerWidget::slotsModified,
            this, &RunProgramPage::onSlotsModified);
    layout->addWidget(m_editSlotWidget);
  }

  // The recipe-list editor (table + preview + Add/Remove/Move/Toggle/Manage Deps).
  ProgramEditPanel::Deps editDeps;
  editDeps.context = m_context;
  editDeps.manager = m_programManager;
  editDeps.currentProgramPath = [this]() { return m_currentProgramPath; };
  editDeps.isExecuting        = [this]() { return m_isExecuting; };
  m_editPanel = new ProgramEditPanel(std::move(editDeps), this);
  layout->addWidget(m_editPanel, 1);

  // Page actions emitted by the panel.
  connect(m_editPanel, &ProgramEditPanel::recipeListChanged,
          this, [this]() {
            updateRecipeList();
            updateSlotInfo();
            setProgramDirty(true);
          });
  connect(m_editPanel, &ProgramEditPanel::inspectMotionRequested,
          this, [this](int row) {
            const RecipeReference* recipe = m_programManager->getRecipe(row);
            if (recipe && m_motionInspectorWidget && m_motionInspectorTabIndex >= 0) {
              m_motionInspectorWidget->loadRecipe(
                  QString::fromStdString(recipe->recipeFilePath),
                  QString::fromStdString(recipe->displayName));
              showExecutionWidget();
              m_overlay->ShowDefaultPage();
              m_executionTabWidget->setCurrentIndex(m_motionInspectorTabIndex);
            }
          });
  connect(m_editPanel, &ProgramEditPanel::retraceEditRequested,
          this, [this](int row) { onStartRetraceEdit(row); });

  connect(m_saveEditButton, &QPushButton::clicked, this, &RunProgramPage::onSaveEdit);
}

// Stores the requested base state and re-applies the badge. Use this instead
// of touching m_executionStatus->programStatusLabel() directly so that an active scan overlay
// can hide the base state without losing it.
void RunProgramPage::setProgramStatus(const QString& text, const QString& bgColor, const QString& fgColor)
{
  m_baseStatusText = text;
  m_baseStatusBgColor = bgColor;
  m_baseStatusFgColor = fgColor;
  applyProgramStatusLabel();
}

// Paints the badge from m_baseStatus* unless a scan is active, in which case
// the amber "SCANNING" overlay takes priority. Called from setProgramStatus()
// and from the ScanActivityTracker signal handler.
void RunProgramPage::applyProgramStatusLabel()
{
  if (!m_executionStatus->programStatusLabel()) {
    return;
  }

  QString text;
  QString bg;
  QString fg;
  if (m_isScanActive) {
    // Match the TopBar accent badge so the operator can connect the two indicators.
    text = QString::fromUtf8("\xE2\x9A\xA1 SCANNING");
    bg = QLatin1String(Theme::Color::accent);
    fg = QLatin1String(Theme::Color::bgDeep);
  } else {
    text = m_baseStatusText;
    bg = m_baseStatusBgColor;
    fg = m_baseStatusFgColor;
  }

  m_executionStatus->programStatusLabel()->setText(text);
  m_executionStatus->programStatusLabel()->setStyleSheet(
    QString(
      "QLabel { "
      "  background-color: %1; "
      "  color: %2; "
      "  padding: 4px 12px; "
      "  border-radius: 4px; "
      "  font-weight: bold; "
      "  font-size: 18px; "
      "}"
    ).arg(bg, fg)
  );
}

void RunProgramPage::createExecutionWidget()
{
  m_executionWidget = new QWidget();
  QVBoxLayout* layout = new QVBoxLayout(m_executionWidget);
  layout->setContentsMargins(20, 20, 20, 20);

  // Status header strip (status badge + program info + command tracker
  // + action detail + progress bar). Its inner widgets are accessed
  // via typed getters where the page reacts to execution events.
  m_executionStatus = new ExecutionStatusWidget();
  layout->addWidget(m_executionStatus);

  // Initial paint — the badge reads from m_baseStatus* defaults.
  applyProgramStatusLabel();

  // === TAB WIDGET FOR LOG AND LIVE VIEW ===
  m_executionTabWidget = new QTabWidget();
  m_executionTabWidget->setStyleSheet(R"(
    QTabWidget::pane {
      border: 1px solid #3f3f46;
      border-radius: 4px;
      background-color: #1e1e1e;
    }
    QTabBar::tab {
      background-color: #2d2d2d;
      color: #a0a0a0;
      padding: 8px 16px;
      margin-right: 2px;
      border-top-left-radius: 4px;
      border-top-right-radius: 4px;
      font-size: 12px;
    }
    QTabBar::tab:selected {
      background-color: #0078d4;
      color: white;
    }
    QTabBar::tab:hover:!selected {
      background-color: #3d3d3d;
    }
  )");

  // === EXECUTION LOG TAB ===
  QWidget* logTab = new QWidget();
  QVBoxLayout* logTabLayout = new QVBoxLayout(logTab);
  logTabLayout->setContentsMargins(5, 5, 5, 5);

  QLabel* logLabel = new QLabel("Execution Log:");
  logLabel->setStyleSheet("font-weight: bold; color: #ffffff;");
  logTabLayout->addWidget(logLabel);

  // ✅ Using QPlainTextEdit for better performance with large logs
  m_logTextEdit = new QPlainTextEdit();
  m_logTextEdit->setReadOnly(true);
  m_logTextEdit->setMaximumBlockCount(200);  // Auto-limit to ~5 pages (oldest removed automatically)
  m_logTextEdit->setStyleSheet(
    "QPlainTextEdit { "
    "  background-color: #1e1e1e; "
    "  color: #d4d4d4; "
    "  border: 1px solid #3f3f46; "
    "  border-radius: 4px; "
    "  font-family: 'Consolas', 'Courier New', monospace; "
    "  font-size: 14px; "
    "}"
  );
  logTabLayout->addWidget(m_logTextEdit, 1);

  m_executionLogLabel = logLabel;

  m_executionTabWidget->addTab(logTab, "Execution Log");

  // === LIVE VIEW TAB ===
  QWidget* liveViewTab = new QWidget();
  QVBoxLayout* liveViewTabLayout = new QVBoxLayout(liveViewTab);
  liveViewTabLayout->setContentsMargins(5, 5, 5, 5);

  if (m_context->cameraManager && m_context->motionManager) {
    // Use unique subscriber ID for RunProgramPage's live view widget
    m_liveViewWidget = new CameraClickToMoveWidget(
      m_context->cameraManager,
      m_context->motionManager,
      nullptr,  // parent
      "RunProgramPage_LiveView"  // unique subscriber ID
    );
    m_liveViewWidget->setClickToMoveEnabled(false);  // Disable click-to-move by default
    m_liveViewWidget->setCrosshairEnabled(false);    // Disable crosshair by default
    if (m_context->cameraConfigManager) {
      m_liveViewWidget->setCameraConfigManager(m_context->cameraConfigManager);
    }
    liveViewTabLayout->addWidget(m_liveViewWidget, 1);

    // ✅ FIX: Schedule camera initialization after widget is added to layout
    QTimer::singleShot(1500, this, [this]() {
      if (m_liveViewWidget) {
        std::cout << "[RunProgramPage] Delayed init: calling selectFirstCameraDelayed()" << std::endl;
        m_liveViewWidget->selectFirstCameraDelayed();
      }
    });
  } else {
    QLabel* noCamera = new QLabel("Camera not available");
    noCamera->setAlignment(Qt::AlignCenter);
    noCamera->setStyleSheet("color: #888888; font-size: 14px;");
    liveViewTabLayout->addWidget(noCamera, 1);
    m_liveViewWidget = nullptr;
  }

  m_executionTabWidget->addTab(liveViewTab, "Live View");

  // === CONTEXT TAB (Index 2) ===
  m_contextViewer = new SequenceContextViewer();
  m_contextViewer->SetCvPopulator(
    [this](SequenceContext& target, QString& err) {
      return populateCvContext(target, err);
    });
  m_executionTabWidget->addTab(m_contextViewer, "Context");

  // ✅ Connect tab change to initialize camera when Live View tab is selected
  connect(m_executionTabWidget, &QTabWidget::currentChanged, this, [this](int index) {
    std::cout << "[RunProgramPage] Tab changed to index: " << index << std::endl;
    // Live View is at index 1
    if (index == 1 && m_liveViewWidget) {
      std::cout << "[RunProgramPage] Live View tab selected, triggering camera selection..." << std::endl;
      // Trigger delayed camera selection when Live View tab is shown
      m_liveViewWidget->selectFirstCameraDelayed();
    }
  });

  // The right-side overlay holds the execution tabs (default page) plus
  // 5 built-in interaction widgets (UserPrompt, ManualAdjust,
  // ManualAlignPoint, UserInput, RetraceEdit) and the page-owned
  // embedded retraces (Placement, EdgeTransfer) added via AddExtraPage.
  UserInteractionOverlay::Deps overlayDeps;
  overlayDeps.context      = m_context;
  overlayDeps.matrixLoader = m_matrixLoader;
  m_overlay = new UserInteractionOverlay(overlayDeps);
  m_overlay->SetDefaultPage(m_executionTabWidget);

  // Embedded placement retrace (page handles data load + persistence).
  m_placementRetraceWidget = new PlacementRetraceWidget(m_context);
  connect(m_placementRetraceWidget, &PlacementRetraceWidget::retraceCompleted,
    this, &RunProgramPage::onPlacementRetraceCompleted);
  connect(m_placementRetraceWidget, &PlacementRetraceWidget::retraceSkipped,
    this, &RunProgramPage::onPlacementRetraceSkipped);
  connect(m_placementRetraceWidget, &PlacementRetraceWidget::retraceEnded,
    this, &RunProgramPage::onPlacementRetraceEnded);
  // Manual tune offsets edited during a retrace — persist the active set
  // back to its source JSON so the offset is remembered next run. Skip
  // the disk write while a recipe is running to avoid overwriting the
  // set mid-recipe; the offset is still applied in memory.
  connect(m_placementRetraceWidget, &PlacementRetraceWidget::offsetsPersistRequested,
    this, [this]() {
      if (m_activePlacementStoreBasePath.empty()) return;
      if (m_isExecuting) {
        m_logTextEdit->appendPlainText(
          "[PlacementRetrace] Tune offset edit applied in-memory; disk save "
          "deferred (recipe is running).");
        return;
      }
      PlacementCorrectionStore store;
      store.SetBasePath(m_activePlacementStoreBasePath);
      store.SaveSet(m_activePlacementSet);
    });
  m_placementRetracePageIndex = m_overlay->AddExtraPage(m_placementRetraceWidget);

  // Embedded edge transfer retrace.
  m_edgeRetraceWidget = new EdgeRetraceWidget(m_context);
  m_edgeRetraceWidget->SetRecipeMode(true);
  connect(m_edgeRetraceWidget, &EdgeRetraceWidget::recipeOutcomeReady,
    this, &RunProgramPage::onEdgeTransferRetraceOutcome);
  m_edgeTransferRetracePageIndex = m_overlay->AddExtraPage(m_edgeRetraceWidget);

  // Cross-thread bridges for the embedded retrace callbacks (this -> this
  // connections). Connected once here to avoid duplicates on re-execution.
  connect(this, &RunProgramPage::showPlacementRetrace,
    this, &RunProgramPage::onShowPlacementRetrace);
  connect(this, &RunProgramPage::showEdgeTransferRetrace,
    this, &RunProgramPage::onShowEdgeTransferRetrace);

  layout->addWidget(m_overlay, 1);

  // === SCANNING TAB (Index 3) ===
  QWidget* scanningTab = new QWidget();
  QVBoxLayout* scanTabLayout = new QVBoxLayout(scanningTab);
  scanTabLayout->setContentsMargins(10, 10, 10, 10);
  scanTabLayout->setSpacing(10);

  // Scan widget
  m_scanWidget = new ScanControlWidget();

  // Populate channels from DataStore (initial load)
  refreshDataStoreChannels();

  // Set up periodic channel refresh (every 3 seconds)
  // This ensures new channels (e.g., SMU channels added when polling starts) appear in the dropdown
  m_channelRefreshTimer = new QTimer(this);
  connect(m_channelRefreshTimer, &QTimer::timeout, this, &RunProgramPage::refreshDataStoreChannels);
  m_channelRefreshTimer->start(3000);  // Refresh every 3 seconds

  // Scan-widget display throttle. SMU samples arrive at up to 20 Hz on the
  // DataStore worker thread; the callback just stashes the latest into
  // m_pendingScanValue. This 10 Hz UI-thread timer reads the latest and
  // pushes a single setDisplayText — coalescing two samples into one paint
  // dispatch and halving the MainWindow UpdateRequest rate. The timer also
  // gates on isVisible(), so when the Scanning tab is not selected it does
  // zero work.
  m_scanDisplayTimer = new QTimer(this);
  m_scanDisplayTimer->setObjectName("scanDisplayTimer");
  m_scanDisplayTimer->setInterval(100);  // 10 Hz
  connect(m_scanDisplayTimer, &QTimer::timeout, this, [this]() {
    if (!m_scanWidget || !m_scanWidget->isVisible()) return;
    if (!m_pendingScanDirty.exchange(false, std::memory_order_acquire)) return;
    const float v = m_pendingScanValue.load(std::memory_order_relaxed);
    m_scanWidget->setDisplayText(formatWithSIPrefix(static_cast<double>(v)));
  });
  m_scanDisplayTimer->start();

  scanTabLayout->addWidget(m_scanWidget);
  scanTabLayout->addStretch();  // Push controls to top

  m_executionTabWidget->addTab(scanningTab, "Scanning");

  // === VISION TAB (Index 4) ===
  QWidget* visionTab = new QWidget();
  QVBoxLayout* visionTabLayout = new QVBoxLayout(visionTab);
  visionTabLayout->setContentsMargins(10, 10, 10, 10);
  visionTabLayout->setSpacing(10);

  // Vision result image display + history navigation widget.
  m_visionHistoryWidget = new VisionHistoryWidget();
  visionTabLayout->addWidget(m_visionHistoryWidget, 1);

  m_executionTabWidget->addTab(visionTab, "Vision");

  // === ROLL CORRECTION TAB (Index 5) ===
  QWidget* rollCorrectionTab = new QWidget();
  QVBoxLayout* rollTabLayout = new QVBoxLayout(rollCorrectionTab);
  rollTabLayout->setContentsMargins(10, 10, 10, 10);
  rollTabLayout->setSpacing(10);

  m_rollCorrectionWidget = new RollCorrectionWidget();
  rollTabLayout->addWidget(m_rollCorrectionWidget);
  rollTabLayout->addStretch();

  // Populate the roll-correction channel dropdown immediately. The static
  // de-dup cache in refreshDataStoreChannels() means a plain re-call won't
  // push to the freshly-constructed widget, so we feed the current list directly.
  if (m_context && m_context->dataStore) {
    QStringList channels;
    for (const auto& ch : m_context->dataStore->GetAllChannelNames()) {
      channels << QString::fromStdString(ch);
    }
    m_rollCorrectionWidget->setAvailableChannels(channels);
  }

  m_executionTabWidget->addTab(rollCorrectionTab, "Roll Correction");

  // === MOTION INSPECTOR TAB ===
  // Populated on-demand when the user right-clicks a recipe and picks
  // "Inspect Motion". Starts empty.
  m_motionInspectorWidget = new MotionInspectorWidget();
  m_motionInspectorTabIndex = m_executionTabWidget->addTab(
    m_motionInspectorWidget, "Motion Inspector");

  // === M4AA TAB ===
  // M4AA branch-specific serial-number data viewer (replicates
  // serverPython/view_serial_data.py). Self-contained: it polls the active
  // product's data/{product}/ folder on its own 1 Hz timer.
  m_m4aaSerialDataWidget = new M4AASerialDataWidget(m_context);
  m_executionTabWidget->addTab(m_m4aaSerialDataWidget, "M4AA");

  // === M4 ANGLE TAB ===
  // Calculator: captures a1/a2/ma1/ma2 from gantry-main and shows the
  // hex-right U / W delta needed to align the measured line to the baseline.
  m_m4AngleWidget = new M4AngleWidget(m_context);
  m_executionTabWidget->addTab(m_m4AngleWidget, "M4 Angle");

  // === M4AA branch: hide tabs that aren't used in this flow ===
  // The widgets remain constructed (their signal wiring above stays valid);
  // setTabVisible only removes them from the tab bar. To re-expose any of
  // them later, flip the matching line to true.
  auto hideTabByText = [this](const QString& label) {
    for (int i = 0; i < m_executionTabWidget->count(); ++i) {
      if (m_executionTabWidget->tabText(i) == label) {
        m_executionTabWidget->setTabVisible(i, false);
        return;
      }
    }
  };
  hideTabByText("Live View");
  hideTabByText("Scanning");
  hideTabByText("Vision");
  hideTabByText("Roll Correction");

  // Connect roll correction signals
  connect(m_rollCorrectionWidget, &RollCorrectionWidget::registerCH1Requested,
    this, &RunProgramPage::onRegisterCH1Requested);
  connect(m_rollCorrectionWidget, &RollCorrectionWidget::registerCH2Requested,
    this, &RunProgramPage::onRegisterCH2Requested);
  connect(m_rollCorrectionWidget, &RollCorrectionWidget::moveToCH1Requested,
    this, &RunProgramPage::onMoveToCH1Requested);
  connect(m_rollCorrectionWidget, &RollCorrectionWidget::moveToCH2Requested,
    this, &RunProgramPage::onMoveToCH2Requested);
  connect(m_rollCorrectionWidget, &RollCorrectionWidget::correctWRequested,
    this, &RunProgramPage::onCorrectWRequested);
  connect(m_rollCorrectionWidget, &RollCorrectionWidget::fineGridScanRequested,
    this, &RunProgramPage::onFineGridScanRequested);
  connect(m_rollCorrectionWidget, &RollCorrectionWidget::stopScanRequested,
    this, &RunProgramPage::onStopGridScanRequested);
  connect(m_rollCorrectionWidget, &RollCorrectionWidget::clearRequested,
    this, &RunProgramPage::onRollCorrectionClearRequested);

  // Connect the channel-changed signal here (DataStore subscription
  // for live channel value display is page-level state). Scan-action
  // signals (scanRequested / stopRequested) and ScanExecutor result
  // signals are handled by ScanController, wired up after setupUI().
  connect(m_scanWidget, &ScanControlWidget::channelChanged,
    this, &RunProgramPage::onChannelChanged);

  // Trigger subscription for initial channel
  if (!m_scanWidget->currentChannel().isEmpty()) {
    onChannelChanged(m_scanWidget->currentChannel());
  }
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
    m_leftPanel->productLabel()->setText(QString("Product: %1").arg(QString::fromStdString(productName)));

    std::cout << "[RunProgramPage] Product changed to: " << productName << std::endl;

    // ✅ Load recent programs list
    loadRecentProgramsList();

    // ✅ Auto-load last program when product is loaded
    loadLastProgramFromSettings();
  } else {
    m_leftPanel->productLabel()->setText("Product: None");
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

void RunProgramPage::onNewProgram()
{
  // Check if product is loaded
  if (!m_context->productPositionManager || !m_context->productPositionManager->HasLoadedProduct()) {
    QMessageBox::warning(this, "No Product Loaded",
      "Please load a product first before creating a new program.\n\n"
      "Use the 'Load Product' button in the main menu.");
    return;
  }

  // Confirm if there's unsaved work
  if (m_programManager->getRecipeCount() > 0) {
    QMessageBox::StandardButton reply = QMessageBox::question(this, "Create New Program",
      "Creating a new program will clear the current program.\n\n"
      "Do you want to continue?",
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (reply != QMessageBox::Yes) {
      return;
    }
  }

  // Create new program
  m_programManager->newProgram();
  m_currentProgramPath.clear();

  // Update UI
  updateRecipeList();
  updateProgramInfo();

  // Switch to edit mode so user can start adding recipes
  showEditWidget();

  std::cout << "[RunProgramPage] New program created" << std::endl;
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
  updateLoopStatusLabel();  // ✅ Update loop status when loading program

  // Reset completion tracking when loading new program.
  if (m_executionController) m_executionController->ResetCompletedRecipes();

  // ✅ Update the program name edit field (for when user switches to edit mode)
  m_programNameEdit->setText(QString::fromStdString(m_programManager->getProgramName()));

  // Freshly-loaded program is by definition clean (matches disk).
  setProgramDirty(false);

  // ✅ Clear stale vision results when loading new program
  clearVisionResult();

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

  setProgramDirty(false);

  QMessageBox::information(this, "Success",
    QString("Program saved to:\n%1").arg(m_currentProgramPath));
}

void RunProgramPage::onSaveAsProgram()
{
  // Check if product is loaded
  if (!m_context->productPositionManager || !m_context->productPositionManager->HasLoadedProduct()) {
    QMessageBox::warning(this, "No Product Loaded",
      "Please load a product first before saving programs.\n\n"
      "Use the 'Load Product' button in the main menu.");
    return;
  }

  // Get product name and build path to product's programs folder
  QString productName = QString::fromStdString(m_context->productPositionManager->GetCurrentProductName());
  QString programsPath = QString("config/%1/programs").arg(productName);

  // Ensure programs folder exists
  QDir().mkpath(programsPath);

  // Always prompt for new file location
  QString filename = QFileDialog::getSaveFileName(
    this,
    "Save Program As",
    programsPath,  // Start in product's programs folder
    "Program Files (*.json)"
  );

  if (filename.isEmpty()) return;

  // Update current path to the new location
  m_currentProgramPath = filename;

  if (!m_programManager->saveProgram(m_currentProgramPath.toStdString())) {
    QMessageBox::critical(this, "Save Error", "Failed to save program");
    return;
  }

  setProgramDirty(false);

  // Update UI to reflect new file
  updateProgramInfo();

  QMessageBox::information(this, "Success",
    QString("Program saved to:\n%1").arg(m_currentProgramPath));
}

void RunProgramPage::onEditProgram()
{
  showEditWidget();
}

// ============================================================================
// SLOT SYSTEM BRIDGES
// SlotManagerWidget owns the slot operations themselves; these slots
// just propagate the change back to the page-level UI (the other slot
// widget, the recipe list, the edit panel, completion tracking).
// ============================================================================

void RunProgramPage::onSlotChangedFromRun(int index)
{
  if (m_editSlotWidget) m_editSlotWidget->SetSelectedSlot(index);
  updateRecipeList();
  updateSlotInfo();

  // Reset completion tracking when slot changes.
  if (m_executionController) m_executionController->ResetCompletedRecipes();
}

void RunProgramPage::onSlotChangedFromEdit(int index)
{
  if (m_runSlotWidget) m_runSlotWidget->SetSelectedSlot(index);
  updateRecipeList();
  updateSlotInfo();
  if (m_editPanel) m_editPanel->Reload();
}

void RunProgramPage::onSlotsModified()
{
  if (m_runSlotWidget)  m_runSlotWidget->Reload();
  if (m_editSlotWidget) m_editSlotWidget->Reload();
  updateSlotInfo();
  setProgramDirty(true);
}

void RunProgramPage::updateSlotInfo()
{
  if (!m_runSlotWidget || !m_programManager) return;
  m_runSlotWidget->SetRecipeCount(m_programManager->getRecipeCount());
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

  // ✅ Clear stale vision results before starting new execution
  clearVisionResult();

  bool autoRunMode = m_leftPanel->autoRunNextCheckBox()->isChecked();
  int selectedRecipeIndex = m_leftPanel->recipeList()->currentRow();

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

  m_executionController->EnsureCompletedRecipesSize();

  // Auto-run resets completion + clears any pending chain trigger so
  // the whole program runs from a clean slate.
  if (autoRunMode) {
    m_executionController->ResetCompletedRecipes();
    m_pendingChainRecipeIndex = -1;
  }

  // Single-recipe + dependencies: refuse to run if any predecessor has
  // not completed in this session.
  if (!autoRunMode && m_leftPanel->useDependenciesCheckBox()->isChecked()) {
    std::vector<DependencyLink> dependencies = m_programManager->getRecipeDependencies(selectedRecipeIndex);
    const auto& completed = m_executionController->CompletedRecipes();

    if (!dependencies.empty()) {
      QStringList missingPrereqs;
      for (const auto& dep : dependencies) {
        int depIndex = dep.predecessorIndex;
        if (depIndex >= 0 && depIndex < static_cast<int>(completed.size()) &&
            !completed[depIndex]) {
          const RecipeReference* depRecipe = m_programManager->getRecipe(depIndex);
          if (depRecipe) {
            missingPrereqs << QString("\xE2\x80\xA2 Recipe %1: %2")
              .arg(depIndex + 1)
              .arg(QString::fromStdString(depRecipe->displayName));
          }
        }
      }
      if (!missingPrereqs.isEmpty()) {
        const RecipeReference* recipe = m_programManager->getRecipe(selectedRecipeIndex);
        QString recipeName = recipe ? QString::fromStdString(recipe->displayName)
                                    : QString("Recipe %1").arg(selectedRecipeIndex + 1);
        QMessageBox::warning(this, "Prerequisites Not Met",
          QString("Cannot run '%1'.\n\nThe following prerequisites have not been completed:\n%2\n\nPlease run the prerequisite recipe(s) first.")
          .arg(recipeName)
          .arg(missingPrereqs.join("\n")));
        return;
      }
    }
  }

  // Refuse a disabled selection (production mode skips the legacy
  // "Execute Program?" confirmation dialog).
  const RecipeReference* recipe = m_programManager->getRecipe(selectedRecipeIndex);
  if (selectedRecipeIndex >= 0 && recipe && !recipe->enabled) {
    QMessageBox::warning(this, "Recipe Disabled",
      QString("Recipe '%1' is disabled. Please enable it first or select a different recipe.")
        .arg(QString::fromStdString(recipe->displayName)));
    return;
  }

  // Switch to execution view + update page-level UI state.
  showExecutionWidget();
  m_isExecuting = true;
  m_leftPanel->executeButton()->setText(QString::fromUtf8("\xE2\x8F\xB9 Stop Program"));
  m_leftPanel->executeButton()->setStyleSheet(Theme::Buttons::bezelDanger());
  setProgramStatus("Running", Theme::Color::success, Theme::Color::textPrimary);
  m_leftPanel->autoRunNextCheckBox()->setEnabled(false);

  bool loopEnabled = m_leftPanel->loopEnabledCheckBox()->isChecked() && m_programManager->hasLoopRecipes();
  m_isLoopExecution = loopEnabled;
  if (loopEnabled) m_currentLoopIteration = 0;

  // Hand off to the controller. The page hooks placement / edge
  // transfer retrace callbacks via the pre-start hook because they
  // need page-side state (m_promptEventLoop, response members and
  // the cross-thread show* signals).
  ExecutionController::StartParams startParams;
  startParams.autoRunMode         = autoRunMode;
  startParams.selectedRecipeIndex = selectedRecipeIndex;
  startParams.useDependencies     = m_leftPanel->useDependenciesCheckBox()->isChecked();
  startParams.loopEnabled         = loopEnabled;
  startParams.ignoreDependencies  = m_leftPanel->ignoreDependenciesCheckBox()->isChecked();

  m_executionController->Start(startParams,
    [this](ProgramWorker* worker) {
      worker->setPlacementRetraceCallback(
        [this](const PlacementRetraceParams& params) -> bool {
          emit showPlacementRetrace(QString::fromStdString(params.trainingSetName),
                                    params.autoStart, params.autoDetect, params.autoSaveContinue);
          QEventLoop loop;
          m_promptEventLoop = &loop;
          loop.exec();
          m_promptEventLoop = nullptr;
          return m_placementRetraceResponse;
        });

      worker->setEdgeTransferRetraceCallback(
        [this](const EdgeTransferRetraceParams& params) -> EdgeTransferRetraceOutcome {
          emit showEdgeTransferRetrace(QString::fromStdString(params.trainingSetName));
          QEventLoop loop;
          m_promptEventLoop = &loop;
          loop.exec();
          m_promptEventLoop = nullptr;
          return m_edgeRetraceResponse;
        });
    });

  // Start context refresh timer for real-time context tab updates.
  startContextRefreshTimer();
}

void RunProgramPage::onStopProgram()
{
  if (m_executionController && m_executionController->IsExecuting()) {
    m_executionController->RequestStop();
    m_executionStatus->executionStatusLabel()->setText("Stopping program...");
  }
}

void RunProgramPage::onProgramExecutionStarted(int totalRecipes)
{
  m_totalRecipes = totalRecipes;
  m_currentRecipeIndex = 0;

  // ✅ Set progress bar to percentage mode (0-100) for step-level tracking
  m_executionStatus->progressBar()->setRange(0, 100);
  m_executionStatus->progressBar()->setValue(0);

  m_executionStatus->executionStatusLabel()->setText(QString("Executing program with %1 recipes").arg(totalRecipes));
}

void RunProgramPage::onRecipeStarted(int recipeIndex, QString recipeName)
{
  m_currentRecipeIndex = recipeIndex;
  m_executionStatus->currentRecipeLabel()->setText(QString("Current Recipe: %1").arg(recipeName));
  m_executionStatus->executionStatusLabel()->setText(QString("Recipe %1 of %2: %3")
    .arg(recipeIndex + 1).arg(m_totalRecipes).arg(recipeName));

  // ✅ Reset step-level tracking for new recipe
  m_currentRecipeTotalSteps = 0;
  m_currentRecipeCompletedSteps = 0;
  m_executionStatus->progressBar()->setValue(0);

  // ✅ Reset command tracker
  m_recipeStepNames.clear();
  m_executionStatus->cmdPastLabel()->setText("-");
  m_executionStatus->cmdCurrentLabel()->setText("Starting...");
  m_executionStatus->cmdNext1Label()->setText("-");
  m_executionStatus->cmdNext2Label()->setText("-");
  if (m_executionStatus->actionDetailLabel()) m_executionStatus->actionDetailLabel()->setText("Starting...");

  // Update list item to show running state
  QListWidgetItem* item = m_leftPanel->recipeList()->item(recipeIndex);
  if (item) {
    QString baseText = QString("%1    %2")
      .arg(recipeIndex + 1, 2, 10, QChar('0'))
      .arg(recipeName);

    // Show dependencies if using dependency mode
    if (m_leftPanel->useDependenciesCheckBox()->isChecked()) {
      std::vector<DependencyLink> deps = m_programManager->getRecipeDependencies(recipeIndex);
      if (!deps.empty()) {
        QString depsStr = " [Deps: ";
        for (size_t i = 0; i < deps.size(); ++i) {
          if (i > 0) depsStr += ", ";
          depsStr += QString::number(deps[i].predecessorIndex + 1);
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
  // Progress bar reaches 100% naturally via stepExecutionCompleted signals

  // Update list item to show completed/failed state
  QListWidgetItem* item = m_leftPanel->recipeList()->item(recipeIndex);
  if (item) {
    const RecipeReference* recipe = m_programManager->getRecipe(recipeIndex);
    if (recipe) {
      QString baseText = QString("%1    %2")
        .arg(recipeIndex + 1, 2, 10, QChar('0'))
        .arg(QString::fromStdString(recipe->displayName));

      // Show dependencies if using dependency mode
      if (m_leftPanel->useDependenciesCheckBox()->isChecked()) {
        std::vector<DependencyLink> deps = m_programManager->getRecipeDependencies(recipeIndex);
        if (!deps.empty()) {
          QString depsStr = " [Deps: ";
          for (size_t i = 0; i < deps.size(); ++i) {
            if (i > 0) depsStr += ", ";
            depsStr += QString::number(deps[i].predecessorIndex + 1);
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

  // Completion tracking is updated inside ExecutionController before
  // this signal fires; see CompletedRecipes() below for read access.

  // ✅ Update vision result image if available
  // Skip during loop execution to avoid UI freeze - use deferred update instead
  if (!m_isLoopExecution) {
    updateVisionResultImage();
  } else {
    // Defer heavy image processing to avoid blocking UI during rapid loop iterations
    QTimer::singleShot(50, this, [this]() {
      if (m_isExecuting) {
        updateVisionResultImage();
      }
    });
  }

  if (!success) {
    m_executionStatus->executionStatusLabel()->setText(QString("Recipe failed: %1").arg(message));
  }

  // ✅ Chain Execution: Find and queue dependent recipes
  // Skip verbose logging during loop execution
  if (!m_isLoopExecution) {
    std::cout << "[RunProgramPage] Chain execution check: success=" << success
              << ", chainEnabled=" << m_leftPanel->chainExecutionCheckBox()->isChecked()
              << ", autoRunNext=" << m_leftPanel->autoRunNextCheckBox()->isChecked() << std::endl;
    std::cout.flush();
  }

  if (success && m_leftPanel->chainExecutionCheckBox()->isChecked() && !m_leftPanel->autoRunNextCheckBox()->isChecked()) {
    // Read the live context off the controller's worker for condition evaluation.
    ProgramWorker* liveWorker = m_executionController ? m_executionController->Worker() : nullptr;
    std::cout << "[RunProgramPage] Chain execution: liveWorker=" << liveWorker << std::endl;
    std::cout.flush();
    SequenceContext* execContext = liveWorker ? liveWorker->getCurrentContext() : nullptr;

    std::cout << "[RunProgramPage] Chain execution ENTERED, context=" << (execContext ? "valid" : "NULL") << std::endl;
    std::cout.flush();

    // Find recipes that depend on the just-completed recipe
    int recipeCount = m_programManager->getRecipeCount();

    for (int i = 0; i < recipeCount; ++i) {
      if (i == recipeIndex) continue;  // Skip self

      const RecipeReference* candidateRecipe = m_programManager->getRecipe(i);
      if (!candidateRecipe || !candidateRecipe->enabled) continue;  // Skip disabled recipes

      std::vector<DependencyLink> deps = m_programManager->getRecipeDependencies(i);

      // Check if this recipe depends on the just-completed recipe
      auto it = std::find_if(deps.begin(), deps.end(),
        [recipeIndex](const DependencyLink& d) { return d.predecessorIndex == recipeIndex; });

      if (it == deps.end()) continue;  // This recipe doesn't depend on the completed one

      // Check if ALL prerequisites are met (not just this one)
      bool allPrereqsMet = true;
      std::string failedCondition;
      QStringList metConditions;  // Collect conditions that were met

      const auto& completed = m_executionController->CompletedRecipes();
      for (const auto& dep : deps) {
        int predIndex = dep.predecessorIndex;

        // Check if predecessor is completed
        if (predIndex < 0 || predIndex >= static_cast<int>(completed.size()) ||
            !completed[predIndex]) {
          allPrereqsMet = false;
          break;
        }

        // Get predecessor recipe name for display
        const RecipeReference* predRecipe = m_programManager->getRecipe(predIndex);
        QString predName = predRecipe ? QString::fromStdString(predRecipe->displayName) : QString("Recipe %1").arg(predIndex + 1);

        // Evaluate condition if present
        if (!dep.condition.empty()) {
          if (execContext) {
            ConditionExpressionEvaluator evaluator;
            std::string errorMsg;

            // Debug: Log condition evaluation attempt
            std::cout << "[RunProgramPage] Evaluating condition: '" << dep.condition << "'" << std::endl;
            std::cout.flush();

            // Get the variable value for debugging
            if (execContext->HasValue("DarkCurrent")) {
              double val = execContext->GetNumeric("DarkCurrent", 0.0);
              std::cout << "[RunProgramPage] DarkCurrent value in context: " << val << std::endl;
              std::cout.flush();
            } else {
              std::cout << "[RunProgramPage] WARNING: DarkCurrent NOT found in context!" << std::endl;
              std::cout.flush();
            }

            bool conditionResult = evaluator.Evaluate(dep.condition, execContext, errorMsg);

            // Debug: Log evaluation result
            std::cout << "[RunProgramPage] Condition result: " << (conditionResult ? "TRUE" : "FALSE")
                      << (errorMsg.empty() ? "" : ", Error: " + errorMsg) << std::endl;
            std::cout.flush();

            if (!conditionResult) {
              allPrereqsMet = false;
              failedCondition = dep.condition + (errorMsg.empty() ? "" : " (error: " + errorMsg + ")");
              break;
            } else {
              // Condition was met - add to list with predecessor info (formatted with SI units)
              QString formattedCondition = formatConditionWithSIUnits(QString::fromStdString(dep.condition));
              metConditions.append(QString("  %1: %2 = TRUE").arg(predName).arg(formattedCondition));
            }
          } else {
            // No context available - can't evaluate condition, treat as failed
            allPrereqsMet = false;
            failedCondition = dep.condition + " (no context)";
            break;
          }
        } else {
          // Unconditional dependency - just show that the predecessor completed
          metConditions.append(QString("  %1: completed").arg(predName));
        }
      }

      if (allPrereqsMet) {
        // Show confirmation dialog with 10-second auto-continue
        QString nextRecipeName = QString::fromStdString(candidateRecipe->displayName);

        std::cout << "[RunProgramPage] Chain execution: Condition met for Recipe " << (i + 1)
                  << " (" << candidateRecipe->displayName << "), showing confirmation..." << std::endl;
        std::cout.flush();

        // Build message with prerequisites/conditions
        QString conditionsText;
        if (!metConditions.isEmpty()) {
          conditionsText = QString("\n\nPrerequisites met:\n%1").arg(metConditions.join("\n"));
        }

        // Create timed confirmation dialog
        QDialog* confirmDialog = new QDialog(this);
        confirmDialog->setWindowTitle("Chain Execution");
        confirmDialog->setModal(true);
        confirmDialog->setFixedSize(500, metConditions.isEmpty() ? 200 : 280);
        confirmDialog->setStyleSheet("QDialog { background-color: #2b2b2b; }");

        QVBoxLayout* layout = new QVBoxLayout(confirmDialog);

        // Message label
        QLabel* messageLabel = new QLabel(
          QString("Continue to next recipe?\n\nNext: Recipe %1 - %2%3")
          .arg(i + 1)
          .arg(nextRecipeName)
          .arg(conditionsText));
        messageLabel->setStyleSheet("color: white; font-size: 13px; padding: 10px;");
        messageLabel->setAlignment(Qt::AlignLeft);
        messageLabel->setWordWrap(true);
        layout->addWidget(messageLabel);

        // Get configurable timeout from application settings (default: 3 seconds)
        int timeoutSeconds = 3;  // Default fallback
        if (m_context && m_context->applicationSettings) {
          timeoutSeconds = m_context->applicationSettings->GetChainExecutionTimeoutSeconds();
        }

        // Countdown label
        QLabel* countdownLabel = new QLabel(QString("Auto-continue in %1 seconds...").arg(timeoutSeconds));
        countdownLabel->setStyleSheet("color: #f39c12; font-size: 12px; padding: 5px;");
        countdownLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(countdownLabel);

        // Buttons
        QHBoxLayout* buttonLayout = new QHBoxLayout();
        QPushButton* yesButton = new QPushButton("Continue");
        yesButton->setStyleSheet(
          "QPushButton { background-color: #27ae60; color: white; padding: 10px 30px; font-size: 14px; font-weight: bold; border-radius: 5px; }"
          "QPushButton:hover { background-color: #2ecc71; }");
        QPushButton* noButton = new QPushButton("Stop Chain");
        noButton->setStyleSheet(
          "QPushButton { background-color: #e74c3c; color: white; padding: 10px 30px; font-size: 14px; font-weight: bold; border-radius: 5px; }"
          "QPushButton:hover { background-color: #c0392b; }");
        buttonLayout->addWidget(yesButton);
        buttonLayout->addWidget(noButton);
        layout->addLayout(buttonLayout);

        // Timer for auto-continue
        int* countdown = new int(timeoutSeconds);
        QTimer* timer = new QTimer(confirmDialog);
        timer->setInterval(1000);

        connect(timer, &QTimer::timeout, [confirmDialog, countdownLabel, countdown, timer]() {
          (*countdown)--;
          if (*countdown <= 0) {
            timer->stop();
            confirmDialog->accept();  // Auto-accept after timeout
          } else {
            countdownLabel->setText(QString("Auto-continue in %1 seconds...").arg(*countdown));
          }
        });

        connect(yesButton, &QPushButton::clicked, confirmDialog, &QDialog::accept);
        connect(noButton, &QPushButton::clicked, confirmDialog, &QDialog::reject);

        timer->start();
        int result = confirmDialog->exec();
        timer->stop();
        delete countdown;
        delete confirmDialog;

        if (result == QDialog::Accepted) {
          // Store recipe index for chain execution
          int chainRecipeIndex = i;

          std::cout << "[RunProgramPage] Chain execution: User confirmed, will start Recipe " << (i + 1) << std::endl;
          std::cout.flush();

          // Log to execution log
          m_logTextEdit->appendPlainText(QString("→ Chain: Recipe %1 (%2) will run next")
            .arg(i + 1)
            .arg(nextRecipeName));

          // Trigger chain execution directly (don't wait for onProgramExecutionCompleted)
          // Use QTimer::singleShot to allow current event processing to complete first
          QTimer::singleShot(200, this, [this, chainRecipeIndex]() {
            if (!m_isExecuting && m_leftPanel->chainExecutionCheckBox()->isChecked()) {
              const RecipeReference* nextRecipe = m_programManager->getRecipe(chainRecipeIndex);
              std::cout << "[RunProgramPage] Chain execution: Starting Recipe " << (chainRecipeIndex + 1)
                        << " (" << (nextRecipe ? nextRecipe->displayName : "unknown") << ")" << std::endl;
              std::cout.flush();

              // Select the recipe in the list
              m_leftPanel->recipeList()->setCurrentRow(chainRecipeIndex);

              // Start execution
              QTimer::singleShot(100, this, [this]() {
                if (!m_isExecuting) {
                  onExecuteProgram();
                }
              });
            }
          });
        } else {
          // User cancelled chain execution
          std::cout << "[RunProgramPage] Chain execution: User cancelled" << std::endl;
          std::cout.flush();

          m_logTextEdit->appendPlainText(QString("✗ Chain execution stopped by user"));
        }

        break;  // Only process one recipe at a time
      } else if (!failedCondition.empty()) {
        // Log skipped due to condition
        std::cout << "[RunProgramPage] Chain execution: Recipe " << (i + 1)
                  << " skipped - condition not met: " << failedCondition << std::endl;
        std::cout.flush();

        m_logTextEdit->appendPlainText(QString("⊘ Recipe %1 skipped - condition not met")
          .arg(i + 1));

        // Mark as skipped in UI
        onRecipeSkipped(i, QString::fromStdString(candidateRecipe->displayName));
      }
    }
  }
}

void RunProgramPage::onRecipeSkipped(int recipeIndex, QString recipeName)
{
  // Update list item to show skipped state
  QListWidgetItem* item = m_leftPanel->recipeList()->item(recipeIndex);
  if (item) {
    QString baseText = QString("%1    %2")
      .arg(recipeIndex + 1, 2, 10, QChar('0'))
      .arg(recipeName);

    // Show dependencies if using dependency mode
    if (m_leftPanel->useDependenciesCheckBox()->isChecked()) {
      std::vector<DependencyLink> deps = m_programManager->getRecipeDependencies(recipeIndex);
      if (!deps.empty()) {
        QString depsStr = " [Deps: ";
        for (size_t i = 0; i < deps.size(); ++i) {
          if (i > 0) depsStr += ", ";
          depsStr += QString::number(deps[i].predecessorIndex + 1);
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
  std::cout << "[RunProgramPage] onProgramExecutionCompleted called: success=" << success
            << ", message=" << message.toStdString() << std::endl;
  std::cout.flush();

  // Stop context refresh timer
  stopContextRefreshTimer();

  std::cout << "[RunProgramPage] Setting m_isExecuting to false..." << std::endl;
  std::cout.flush();
  m_isExecuting     = false;
  m_isLoopExecution = false;
  // Retrace-edit mode flag lives on the controller; no page-side reset needed.

  // ✅ Reset command tracker
  m_executionStatus->cmdPastLabel()->setText("-");
  m_executionStatus->cmdCurrentLabel()->setText(success ? "Completed" : "Failed");
  m_executionStatus->cmdNext1Label()->setText("-");
  m_executionStatus->cmdNext2Label()->setText("-");
  if (m_executionStatus->actionDetailLabel()) m_executionStatus->actionDetailLabel()->setText(success ? "Completed" : "Failed");
  m_recipeStepNames.clear();

  std::cout << "[RunProgramPage] Updating execute button..." << std::endl;
  std::cout.flush();

  // Re-enable execute button
  updateExecuteButton();  // This sets setEnabled based on m_isExecuting flag
  m_leftPanel->executeButton()->setText("▶ Execute Program");
  m_leftPanel->executeButton()->setStyleSheet(Theme::Buttons::bezelSuccess());

  // Update program status indicator
  setProgramStatus("Idle", Theme::Color::bgSurface, Theme::Color::textDisabled);

  std::cout << "[RunProgramPage] Enabling auto-run checkbox..." << std::endl;
  std::cout.flush();
  m_leftPanel->autoRunNextCheckBox()->setEnabled(true);

  // Show result - only show errors, not success notifications
  if (!success) {
    std::cout << "[RunProgramPage] Showing error dialog..." << std::endl;
    std::cout.flush();
    QMessageBox::critical(this, "Execution Failed", message);
  }
  // Success: no notification popup (user can see completion in log)

  std::cout << "[RunProgramPage] About to call updateVisionResultImage()..." << std::endl;
  std::cout.flush();

  // ✅ Update vision result image if available (loads from temp file to avoid deadlock)
  updateVisionResultImage();

  std::cout << "[RunProgramPage] updateVisionResultImage() completed" << std::endl;
  std::cout.flush();

  // ✅ CRITICAL: Force UI repaint to ensure changes are visible
  std::cout << "[RunProgramPage] Forcing repaint..." << std::endl;
  std::cout.flush();
  m_leftPanel->executeButton()->update();
  m_leftPanel->executeButton()->repaint();
  this->update();

  // ⚠️ Don't call processEvents() here - it causes reentrancy issues
  // The Qt event loop will naturally process paint events

  std::cout << "[RunProgramPage] UI update completed" << std::endl;
  std::cout.flush();

  // Note: Chain execution is now triggered directly from onRecipeCompleted
  // to avoid signal ordering issues where this handler runs before
  // the chain recipe index is set.
}

void RunProgramPage::onProgramStopped()
{
  // Stop context refresh timer
  stopContextRefreshTimer();

  m_isExecuting             = false;
  m_isLoopExecution         = false;
  m_pendingChainRecipeIndex = -1;

  // ✅ Reset command tracker
  m_executionStatus->cmdPastLabel()->setText("-");
  m_executionStatus->cmdCurrentLabel()->setText("Stopped");
  m_executionStatus->cmdNext1Label()->setText("-");
  m_executionStatus->cmdNext2Label()->setText("-");
  if (m_executionStatus->actionDetailLabel()) m_executionStatus->actionDetailLabel()->setText("Stopped");
  m_recipeStepNames.clear();

  // ✅ Update status label to "Stopped"
  setProgramStatus("Stopped", Theme::Color::danger, Theme::Color::textPrimary);

  // Re-enable execute button
  updateExecuteButton();  // This sets setEnabled based on m_isExecuting flag
  m_leftPanel->executeButton()->setText("▶ Execute Program");
  m_leftPanel->executeButton()->setStyleSheet(Theme::Buttons::bezelSuccess());

  m_leftPanel->autoRunNextCheckBox()->setEnabled(true);

  // No confirmation dialog needed - stop button itself is the confirmation
}

void RunProgramPage::onWorkerLogMessage(QString message)
{
  // Forwarded by ExecutionController. Append to the execution log;
  // completion-marker detection / polling fallback / worker deletion
  // are all handled inside the controller.
  m_logTextEdit->appendPlainText(message);
  if (!m_isLoopExecution) {
    std::cout << "[RunProgramPage] " << message.toStdString() << std::endl;
  }
}

// ============================================================================
// STEP-LEVEL PROGRESS TRACKING
// ============================================================================

void RunProgramPage::onStepBuildStarted(int /*recipeIndex*/, int stepIndex, QString /*stepName*/, QString /*stepType*/)
{
  // Track total steps count (build phase tells us how many steps exist)
  if (stepIndex + 1 > m_currentRecipeTotalSteps) {
    m_currentRecipeTotalSteps = stepIndex + 1;
  }
  // Note: Progress bar is NOT updated here — it tracks execution, not build
}

void RunProgramPage::onStepBuildCompleted(int /*recipeIndex*/, int /*stepIndex*/)
{
  // Build phase completed for a step — no progress bar update
  // Progress bar is driven by stepExecutionCompleted signals instead
}

void RunProgramPage::onStepExecutionCompleted(int /*recipeIndex*/, int /*stepIndex*/, bool /*success*/)
{
  // A step has finished executing — advance the progress bar
  m_currentRecipeCompletedSteps++;

  if (m_currentRecipeTotalSteps > 0) {
    int progress = (m_currentRecipeCompletedSteps * 100) / m_currentRecipeTotalSteps;
    m_executionStatus->progressBar()->setValue(progress);
  }
}

void RunProgramPage::onActionStarted(int /*recipeIndex*/, QString current, QString next, QString afterNext)
{
  // Update the command tracker with the currently executing action and look-ahead
  // The current step (from onStepBuildStarted) becomes the previous
  QString prevCurrent = m_executionStatus->cmdCurrentLabel()->text();
  if (!prevCurrent.isEmpty() && prevCurrent != "Starting..." && prevCurrent != "-" && prevCurrent != "Completed") {
    m_executionStatus->cmdPastLabel()->setText(prevCurrent);
  }

  // Update current to show the executing action
  m_executionStatus->cmdCurrentLabel()->setText(current);

  // Show next and after-next from look-ahead
  m_executionStatus->cmdNext1Label()->setText(next.isEmpty() ? "-" : next);
  m_executionStatus->cmdNext2Label()->setText(afterNext.isEmpty() ? "-" : afterNext);

  // Mirror the current action into the visible single-row description.
  // `current` here is already a human-readable string built by
  // ActionInfo::toHumanReadable() in RecipeWorker.
  if (m_executionStatus->actionDetailLabel()) {
    m_executionStatus->actionDetailLabel()->setText(current.isEmpty() ? "-" : current);
  }
}

void RunProgramPage::onSequenceExecutionStarted(int /*recipeIndex*/)
{
  // All steps have been built, now executing
  // Progress bar will advance via stepExecutionCompleted signals
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

// === ROLL CORRECTION SLOTS ===

void RunProgramPage::onRegisterCH1Requested(QString deviceName)
{
  if (!m_context || !m_context->motionManager) return;

  auto pos = m_context->motionManager->GetCurrentPosition(deviceName.toStdString());
  m_rollCorrectionWidget->setCH1Position(deviceName, pos.x, pos.y, pos.z, pos.u, pos.v, pos.w);

  QString msg = QString("[RollCorrection] Registered CH1 peak for %1: X=%2 um")
    .arg(deviceName).arg(pos.x, 0, 'f', 4);
  m_logTextEdit->appendPlainText(msg);
}

void RunProgramPage::onRegisterCH2Requested(QString deviceName)
{
  if (!m_context || !m_context->motionManager) return;

  auto pos = m_context->motionManager->GetCurrentPosition(deviceName.toStdString());
  m_rollCorrectionWidget->setCH2Position(deviceName, pos.x, pos.y, pos.z, pos.u, pos.v, pos.w);

  QString msg = QString("[RollCorrection] Registered CH2 peak for %1: X=%2 um")
    .arg(deviceName).arg(pos.x, 0, 'f', 4);
  m_logTextEdit->appendPlainText(msg);
}

void RunProgramPage::onMoveToCH1Requested(QString deviceName)
{
  if (!m_context || !m_context->motionManager) return;

  auto pos = m_rollCorrectionWidget->getCH1Position(deviceName);
  if (!pos.valid) return;

  std::string dev = deviceName.toStdString();

  // Save current velocity, set to 1 mm/s for safe move
  double originalVelocity = 0.0;
  m_context->motionManager->GetSystemVelocity(dev, originalVelocity);
  m_context->motionManager->SetSystemVelocity(dev, 1.0);
  m_logTextEdit->appendPlainText(QString("[RollCorrection] %1 velocity: %2 -> 1.0 mm/s").arg(deviceName).arg(originalVelocity, 0, 'f', 2));

  m_logTextEdit->appendPlainText(QString("[RollCorrection] Moving %1 to CH1 peak position...").arg(deviceName));

  PositionStruct target;
  target.x = pos.x; target.y = pos.y; target.z = pos.z;
  target.u = pos.u; target.v = pos.v; target.w = pos.w;
  bool ok = m_context->motionManager->MoveToAbs6(dev, target, true);

  // Restore original velocity
  m_context->motionManager->SetSystemVelocity(dev, originalVelocity);
  m_logTextEdit->appendPlainText(QString("[RollCorrection] %1 velocity restored to %2 mm/s").arg(deviceName).arg(originalVelocity, 0, 'f', 2));

  m_logTextEdit->appendPlainText(ok ?
    QString("[RollCorrection] %1 moved to CH1 peak").arg(deviceName) :
    QString("[RollCorrection] FAILED to move %1 to CH1 peak").arg(deviceName));
}

void RunProgramPage::onMoveToCH2Requested(QString deviceName)
{
  if (!m_context || !m_context->motionManager) return;

  auto pos = m_rollCorrectionWidget->getCH2Position(deviceName);
  if (!pos.valid) return;

  std::string dev = deviceName.toStdString();

  // Save current velocity, set to 1 mm/s for safe move
  double originalVelocity = 0.0;
  m_context->motionManager->GetSystemVelocity(dev, originalVelocity);
  m_context->motionManager->SetSystemVelocity(dev, 1.0);
  m_logTextEdit->appendPlainText(QString("[RollCorrection] %1 velocity: %2 -> 1.0 mm/s").arg(deviceName).arg(originalVelocity, 0, 'f', 2));

  m_logTextEdit->appendPlainText(QString("[RollCorrection] Moving %1 to CH2 peak position...").arg(deviceName));

  PositionStruct target;
  target.x = pos.x; target.y = pos.y; target.z = pos.z;
  target.u = pos.u; target.v = pos.v; target.w = pos.w;
  bool ok = m_context->motionManager->MoveToAbs6(dev, target, true);

  // Restore original velocity
  m_context->motionManager->SetSystemVelocity(dev, originalVelocity);
  m_logTextEdit->appendPlainText(QString("[RollCorrection] %1 velocity restored to %2 mm/s").arg(deviceName).arg(originalVelocity, 0, 'f', 2));

  m_logTextEdit->appendPlainText(ok ?
    QString("[RollCorrection] %1 moved to CH2 peak").arg(deviceName) :
    QString("[RollCorrection] FAILED to move %1 to CH2 peak").arg(deviceName));
}

void RunProgramPage::onCorrectWRequested(QString deviceName, QString channel,
                                         double angleDeg, bool positiveW, double halfwayXmm)
{
  // ----- Pre-flight validation (UI thread) -----
  if (!m_context || !m_context->motionManager || !m_context->sequenceEngine || !m_context->dataStore) {
    m_logTextEdit->appendPlainText("[RollCorrection] ERROR: ApplicationContext incomplete");
    return;
  }
  if (m_rollCorrectionRunning) {
    m_logTextEdit->appendPlainText("[RollCorrection] ERROR: a correction is already running");
    return;
  }
  const bool bypassChannel = m_rollCorrectionWidget->bypassChannelRequirement();
  if (channel.isEmpty() && !bypassChannel) {
    m_logTextEdit->appendPlainText("[RollCorrection] ERROR: tracking channel required - select one in the dropdown");
    return;
  }

  // Verify the selected channel still exists in DataStore (only when not bypassing)
  if (!bypassChannel) {
    const auto names = m_context->dataStore->GetAllChannelNames();
    const std::string needle = channel.toStdString();
    bool found = false;
    for (const auto& n : names) {
      if (n == needle) { found = true; break; }
    }
    if (!found) {
      m_logTextEdit->appendPlainText(QString("[RollCorrection] ERROR: channel '%1' not present in DataStore").arg(channel));
      return;
    }
  }

  auto ch1Pos = m_rollCorrectionWidget->getCH1Position(deviceName);
  if (!ch1Pos.valid) {
    m_logTextEdit->appendPlainText(QString("[RollCorrection] ERROR: CH1 peak not registered for %1").arg(deviceName));
    return;
  }

  auto* handler = m_context->sequenceEngine->GetMotionCommandHandler();
  if (!handler) {
    m_logTextEdit->appendPlainText("[RollCorrection] ERROR: MotionCommandHandler unavailable");
    return;
  }

  // ----- Compute W sub-step splitting -----
  // Hard limit: 0.1 deg per sub-step. Ideal upper bound: 0.085 deg.
  constexpr double kIdealWStepUpper = 0.085;
  const double totalAbs = std::abs(angleDeg);
  int steps = static_cast<int>(std::ceil(totalAbs / kIdealWStepUpper));
  if (steps < 1) steps = 1;
  const double subStepAbs = totalAbs / static_cast<double>(steps);  // <= 0.085 deg
  const double signedSubStep = (positiveW ? +1.0 : -1.0) * subStepAbs;

  m_logTextEdit->appendPlainText(QString(
    "[RollCorrection] %1: total=%2%3 deg, splitting into %4 sub-step(s) of %5 deg each (channel=%6)")
    .arg(deviceName)
    .arg(positiveW ? "+" : "-")
    .arg(angleDeg, 0, 'f', 4)
    .arg(steps)
    .arg(subStepAbs, 0, 'f', 4)
    .arg(bypassChannel ? QStringLiteral("<bypassed>") : channel));

  // ----- Set up running state -----
  m_rollCorrectionContext = std::make_unique<SequenceContext>();
  m_rollCorrectionRunning = true;
  m_rollCorrectionWidget->setCorrectionRunning(true);

  // Snapshot values needed by the worker thread
  const std::string dev = deviceName.toStdString();
  const std::string channelStd = channel.toStdString();
  auto* mgr = m_context->motionManager;
  auto* ctx = m_rollCorrectionContext.get();
  RollCorrectionWidget::StoredPosition ch1Snapshot = ch1Pos;
  const double halfwayMm = halfwayXmm;
  const int totalSteps = steps;
  const double signedStepDeg = signedSubStep;
  const bool bypassChannelFlag = bypassChannel;

  // ----- Background worker -----
  auto future = std::async(std::launch::async,
    [handler, mgr, ctx, dev, channelStd, ch1Snapshot, halfwayMm, totalSteps, signedStepDeg, bypassChannelFlag]()
      -> std::pair<bool, std::string>
    {
      double originalVelocity = 0.0;
      mgr->GetSystemVelocity(dev, originalVelocity);
      mgr->SetSystemVelocity(dev, 0.1);  // 0.1 mm/s safe velocity for all moves

      auto restoreAndReturn = [&](bool ok, const std::string& msg) -> std::pair<bool, std::string> {
        mgr->SetSystemVelocity(dev, originalVelocity);
        return { ok, msg };
      };

      // Step 1: Move to CH1 peak (full 6-axis)
      PositionStruct ch1Target;
      ch1Target.x = ch1Snapshot.x; ch1Target.y = ch1Snapshot.y; ch1Target.z = ch1Snapshot.z;
      ch1Target.u = ch1Snapshot.u; ch1Target.v = ch1Snapshot.v; ch1Target.w = ch1Snapshot.w;
      if (!mgr->MoveToAbs6(dev, ch1Target, true)) {
        return restoreAndReturn(false, "FAILED move to CH1 peak");
      }

      if (ctx->IsStopRequested()) return restoreAndReturn(false, "stopped after CH1 move");

      // Step 2: Halfway X shift (subtract because halfway is (ch2-ch1)/2, move opposite)
      auto curr = mgr->GetCurrentPosition(dev);
      const double newX = curr.x - halfwayMm;
      if (!mgr->MoveToPosition(dev, "X", newX, true)) {
        return restoreAndReturn(false, "FAILED halfway X move");
      }

      if (ctx->IsStopRequested()) return restoreAndReturn(false, "stopped after halfway X");

      // Step 3..N: Stepwise W loop with XCANExtended re-peak after each W sub-step
      const std::map<std::string, std::string> scanParams = {
        {"axes", "XY"},
        {"step_sizes_um", "0.5"},
        {"channel", channelStd},
        {"settling_time_ms", "300"}
      };

      for (int i = 0; i < totalSteps; ++i) {
        if (ctx->IsStopRequested()) {
          return restoreAndReturn(false, "stopped at sub-step " + std::to_string(i + 1));
        }

        // Apply one W sub-step
        curr = mgr->GetCurrentPosition(dev);
        const double newW = curr.w + signedStepDeg;
        if (!mgr->MoveToPosition(dev, "W", newW, true)) {
          return restoreAndReturn(false, "FAILED W sub-step " + std::to_string(i + 1));
        }

        if (ctx->IsStopRequested()) {
          return restoreAndReturn(false, "stopped after W sub-step " + std::to_string(i + 1));
        }

        // Re-peak XY power on the selected channel (skipped when user bypassed
        // the channel requirement — W sub-steps still apply slowly, just no re-peak).
        if (!bypassChannelFlag) {
          std::string scanErr;
          const bool scanOk = handler->ExecuteCommand(
            mgr, dev, "XCANExtended", scanParams, scanErr, ctx);
          if (!scanOk) {
            return restoreAndReturn(false,
              "FAILED XCANExtended after W sub-step " + std::to_string(i + 1) + ": " + scanErr);
          }
        }
      }

      return restoreAndReturn(true, "complete (" + std::to_string(totalSteps) + " sub-steps)");
    });

  // ----- Poll for completion (UI thread, no blocking) -----
  auto* pollTimer = new QTimer(this);
  auto futurePtr = std::make_shared<std::future<std::pair<bool, std::string>>>(std::move(future));
  connect(pollTimer, &QTimer::timeout, this, [this, pollTimer, futurePtr, deviceName]() {
    if (futurePtr->wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
      pollTimer->stop();
      pollTimer->deleteLater();
      auto [ok, msg] = futurePtr->get();
      onRollCorrectionFinished(ok, deviceName, QString::fromStdString(msg));
    }
  });
  pollTimer->start(100);
}

void RunProgramPage::onRollCorrectionFinished(bool success, QString deviceName, QString message)
{
  m_rollCorrectionWidget->setCorrectionRunning(false);
  m_rollCorrectionContext.reset();
  m_rollCorrectionRunning = false;

  if (success) {
    m_logTextEdit->appendPlainText(QString("[RollCorrection] %1: %2").arg(deviceName, message));
  } else {
    m_logTextEdit->appendPlainText(QString("[RollCorrection] %1: %2").arg(deviceName, message));
  }
}

void RunProgramPage::onFineGridScanRequested(QString deviceName)
{
  if (!m_context || !m_context->motionManager || !m_context->sequenceEngine) return;

  auto* handler = m_context->sequenceEngine->GetMotionCommandHandler();
  if (!handler) {
    m_logTextEdit->appendPlainText("[RollCorrection] ERROR: MotionCommandHandler not available");
    return;
  }

  // Create a fresh context for stop control
  m_gridScanContext = std::make_unique<SequenceContext>();

  // Update UI state
  m_rollCorrectionWidget->setScanRunning(true);
  m_logTextEdit->appendPlainText(QString("[RollCorrection] Starting fine grid scan on %1...").arg(deviceName));

  // Grid Scan Dual Channel parameters matching screenshot config
  std::map<std::string, std::string> params;
  params["plane"] = "XY";
  params["range_axis1_um"] = "10";
  params["range_axis2_um"] = "10";
  params["direction_axis1"] = "centered";
  params["direction_axis2"] = "centered";
  params["step_size_um"] = "0.5";
  params["channel1"] = "SMU_SMU1S_Ch1";
  params["channel2"] = "SMU_SMU1S_Ch2";
  params["waveguide_spacing_um"] = std::to_string(static_cast<int>(m_rollCorrectionWidget->spacing()));
  params["settling_time_ms"] = "300";
  params["snake_pattern"] = "true";
  params["return_to_peak"] = "channel1";

  // Run on background thread (GridScanDualChannel is blocking)
  auto* ctx = m_gridScanContext.get();
  auto* mgr = m_context->motionManager;
  std::string dev = deviceName.toStdString();

  auto future = std::async(std::launch::async, [handler, mgr, dev, params, ctx]() {
    std::string errorMessage;
    bool ok = handler->ExecuteCommand(mgr, dev, "GridScanDualChannel", params, errorMessage, ctx);
    return std::make_pair(ok, errorMessage);
  });

  // Use a QTimer to poll for completion (avoid blocking UI)
  auto* pollTimer = new QTimer(this);
  auto futurePtr = std::make_shared<std::future<std::pair<bool, std::string>>>(std::move(future));
  connect(pollTimer, &QTimer::timeout, this, [this, pollTimer, futurePtr, deviceName]() {
    if (futurePtr->wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
      pollTimer->stop();
      pollTimer->deleteLater();
      auto [ok, errMsg] = futurePtr->get();
      onGridScanFinished(ok, deviceName, QString::fromStdString(errMsg));
    }
  });
  pollTimer->start(100);
}

void RunProgramPage::onStopGridScanRequested()
{
  if (m_gridScanContext) {
    m_logTextEdit->appendPlainText("[RollCorrection] Stopping grid scan...");
    m_gridScanContext->RequestStop();
  }
  if (m_rollCorrectionContext) {
    m_logTextEdit->appendPlainText("[RollCorrection] Stop requested - will halt before next sub-step");
    m_rollCorrectionContext->RequestStop();
  }
}

void RunProgramPage::onGridScanFinished(bool success, QString deviceName, QString errorMessage)
{
  m_rollCorrectionWidget->setScanRunning(false);
  m_gridScanContext.reset();

  if (success) {
    m_logTextEdit->appendPlainText(QString("[RollCorrection] Fine grid scan completed on %1").arg(deviceName));
  } else {
    m_logTextEdit->appendPlainText(QString("[RollCorrection] Fine grid scan stopped/failed on %1: %2")
      .arg(deviceName, errorMessage));
  }
}

void RunProgramPage::onRollCorrectionClearRequested(QString deviceName)
{
  m_rollCorrectionWidget->clearPositions(deviceName);
  m_logTextEdit->appendPlainText(QString("[RollCorrection] Cleared positions for %1").arg(deviceName));
}

// ✅ ADDED: Handle channel change from scan widget
// ✅ ADDED: Refresh DataStore channels in the dropdown
void RunProgramPage::refreshDataStoreChannels()
{
  if (!m_context || !m_context->dataStore) return;
  if (!m_scanWidget && !m_rollCorrectionWidget) return;

  // Get current selection to preserve it
  QString currentSelection = m_scanWidget ? m_scanWidget->currentChannel() : QString();

  // Get all available channels from DataStore
  QStringList channels;
  auto channelNames = m_context->dataStore->GetAllChannelNames();
  for (const auto& channel : channelNames) {
    channels << QString::fromStdString(channel);
  }

  // Only update if the channel list has changed
  static QStringList s_lastChannelList;
  if (channels != s_lastChannelList) {
    if (m_scanWidget) {
      m_scanWidget->setAvailableChannels(channels);
    }
    if (m_rollCorrectionWidget) {
      m_rollCorrectionWidget->setAvailableChannels(channels);
    }
    s_lastChannelList = channels;

    // ✅ Restore previous selection if it still exists
    if (m_scanWidget && !currentSelection.isEmpty() && channels.contains(currentSelection)) {
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

  // Subscribe to new channel. The callback runs on a DataStore worker
  // thread and must NOT touch Qt widgets directly. It just stashes the
  // latest value into atomics — the 10 Hz m_scanDisplayTimer on the UI
  // thread picks it up and pushes it to the label (see widget setup).
  // This decouples the SMU sample rate (up to 20 Hz) from label-repaint
  // rate (10 Hz), so the Qt camera viewport keeps its UI-thread budget
  // while the Scanning tab stays live.
  std::string channelStd = channel.toStdString();
  m_dataStore->Subscribe(channelStd,
    [this](const std::string& /*channelId*/, float value) {
    m_pendingScanValue.store(value, std::memory_order_relaxed);
    m_pendingScanDirty.store(true, std::memory_order_release);
  });

  // Show current value immediately
  float currentValue = m_dataStore->GetValue(channelStd);
  m_scanWidget->setDisplayText(formatWithSIPrefix(static_cast<double>(currentValue)));
}

// ============================================================================
// EDIT OPERATIONS
// ============================================================================

void RunProgramPage::onConfigureLoop()
{
  int recipeCount = m_programManager->getRecipeCount();
  if (recipeCount == 0) {
    QMessageBox::information(this, "No Recipes", "Please add recipes first before configuring loop");
    return;
  }

  // Open the loop configuration dialog
  LoopConfigDialog* dialog = new LoopConfigDialog(m_programManager, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);

  if (dialog->exec() == QDialog::Accepted) {
    // Update the loop status label
    updateLoopStatusLabel();

    // Update the loop indicator visibility
    m_leftPanel->loopIndicator()->setVisible(m_leftPanel->loopEnabledCheckBox()->isChecked() && m_programManager->hasLoopRecipes());

    // Refresh the recipe list to show loop indicators
    updateRecipeList();
  }
}

void RunProgramPage::updateLoopStatusLabel()
{
  if (!m_programManager) {
    m_leftPanel->loopStatusLabel()->setText("No program loaded");
    m_leftPanel->loopStatusLabel()->setStyleSheet("color: #888888; font-size: 11px; padding: 2px 5px;");
    return;
  }

  std::vector<int> loopRecipes = m_programManager->getLoopRecipeIndices();

  if (loopRecipes.empty()) {
    m_leftPanel->loopStatusLabel()->setText("No recipes configured for loop");
    m_leftPanel->loopStatusLabel()->setStyleSheet("color: #888888; font-size: 11px; padding: 2px 5px;");
  } else if (loopRecipes.size() == 1) {
    m_leftPanel->loopStatusLabel()->setText("1 recipe in loop");
    m_leftPanel->loopStatusLabel()->setStyleSheet("color: #4ec9b0; font-size: 11px; padding: 2px 5px;");
  } else {
    m_leftPanel->loopStatusLabel()->setText(QString("%1 recipes in loop").arg(loopRecipes.size()));
    m_leftPanel->loopStatusLabel()->setStyleSheet("color: #4ec9b0; font-size: 11px; padding: 2px 5px;");
  }
}

void RunProgramPage::onLoopIterationStarted(int iteration)
{
  m_currentLoopIteration = iteration;

  // Update the execution status to show loop iteration
  m_executionStatus->executionStatusLabel()->setText(QString("Loop Iteration %1").arg(iteration));

  // Update program status indicator
  setProgramStatus(QString("Loop #%1").arg(iteration),
                   Theme::Color::accentSoft, Theme::Color::bgDeep);

  // Reset progress bar for new iteration
  m_executionStatus->progressBar()->setValue(0);
}

void RunProgramPage::onLoopIterationCompleted(int iteration)
{
  m_currentLoopIteration = iteration;

  // Log iteration completion
  std::cout << "[RunProgramPage] Loop iteration " << iteration << " completed" << std::endl;
}

void RunProgramPage::setProgramDirty(bool dirty)
{
  if (m_programDirty == dirty) return;
  m_programDirty = dirty;
  if (m_programNameGroup) {
    // Standard convention: trailing asterisk means "unsaved changes".
    m_programNameGroup->setTitle(dirty ? "Program Name *" : "Program Name");
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

void RunProgramPage::onRecipeListContextMenu(const QPoint& pos)
{
  QListWidgetItem* item = m_leftPanel->recipeList()->itemAt(pos);
  if (!item) return;

  int row = m_leftPanel->recipeList()->row(item);
  if (row < 0 || row >= m_programManager->getRecipeCount()) return;

  // Create context menu
  QMenu contextMenu(this);
  contextMenu.setStyleSheet(
    "QMenu { "
    "  background-color: #2d2d30; "
    "  color: white; "
    "  border: 1px solid #3c3c3c; "
    "  padding: 5px; "
    "} "
    "QMenu::item { "
    "  padding: 8px 20px; "
    "} "
    "QMenu::item:selected { "
    "  background-color: #0e639c; "
    "}"
  );

  // Edit in Recipe Page option
  QAction* editAction = contextMenu.addAction("Edit in Recipe Page");
  editAction->setToolTip("Open this recipe in the Recipe editor");

  QAction* inspectMotionAction = contextMenu.addAction("Inspect Motion");
  inspectMotionAction->setToolTip("Show motion-related steps (velocity, axis, distance, position) in a table");

  contextMenu.addSeparator();

  // Add retrace edit option (only if not currently running)
  QAction* retraceAction = nullptr;
  if (!m_isExecuting) {
    retraceAction = contextMenu.addAction("Retrace Edit This Recipe...");
    retraceAction->setToolTip("Run through recipe and pause at each motion for position adjustment");
  } else {
    QAction* disabledAction = contextMenu.addAction("Retrace Edit (disabled during execution)");
    disabledAction->setEnabled(false);
  }

  QAction* selectedAction = contextMenu.exec(m_leftPanel->recipeList()->viewport()->mapToGlobal(pos));

  if (selectedAction == editAction) {
    const RecipeReference* recipe = m_programManager->getRecipe(row);
    if (recipe) {
      emit requestEditRecipe(QString::fromStdString(recipe->recipeFilePath));
    }
  }
  else if (selectedAction == inspectMotionAction) {
    const RecipeReference* recipe = m_programManager->getRecipe(row);
    if (recipe && m_motionInspectorWidget && m_motionInspectorTabIndex >= 0) {
      m_motionInspectorWidget->loadRecipe(
        QString::fromStdString(recipe->recipeFilePath),
        QString::fromStdString(recipe->displayName));
      showExecutionWidget();
      m_overlay->ShowDefaultPage();
      m_executionTabWidget->setCurrentIndex(m_motionInspectorTabIndex);
    }
  }
  else if (retraceAction && selectedAction == retraceAction) {
    onStartRetraceEdit(row);
  }
}

// ============================================================================
// RETRACE EDIT MODE
// ============================================================================

void RunProgramPage::onStartRetraceEdit(int recipeIndex)
{
  const RecipeReference* recipe = m_programManager->getRecipe(recipeIndex);
  if (!recipe) {
    QMessageBox::warning(this, "Error", "Recipe not found");
    return;
  }

  int result = QMessageBox::question(this, "Retrace Edit",
    QString("Retrace Edit will run '%1' and pause after each motion move.\n\n"
            "At each pause you can:\n"
            "  - Jog to adjust the position\n"
            "  - Save the adjusted position to config\n"
            "  - Skip to continue without saving\n"
            "  - Stop to exit retrace mode\n\n"
            "Continue?")
      .arg(QString::fromStdString(recipe->displayName)),
    QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
  if (result != QMessageBox::Yes) return;

  showExecutionWidget();
  m_logTextEdit->clear();
  m_logTextEdit->appendPlainText(QString("=== Retrace Edit: %1 ===")
    .arg(QString::fromStdString(recipe->displayName)));

  m_isExecuting        = true;
  m_currentRecipeIndex = recipeIndex;
  updateExecuteButton();

  // Controller takes over: builds the worker in retrace-edit mode,
  // wires the per-pause callback through the overlay's blocking
  // RequestRetraceEdit(), and emits programExecutionCompleted +
  // retraceEditSummaryRequested when done.
  m_executionController->StartRetraceEdit(recipeIndex);
}

void RunProgramPage::showRetraceEditSummary(int positionsVisited, int positionsSaved)
{
  QString summary = QString(
    "Retrace Edit Complete!\n\n"
    "Positions visited: %1\n"
    "Positions saved: %2\n\n")
    .arg(positionsVisited)
    .arg(positionsSaved);

  if (positionsSaved > 0) {
    summary += QString("The %1 saved position(s) have been written to:\n"
                       "  config/motor_devices.local.json\n\n"
                       "These positions will take effect immediately and\n"
                       "persist across application restarts.")
               .arg(positionsSaved);
  }

  QMessageBox::information(this, "Retrace Edit Complete", summary);
  // Retrace-edit mode flag lives on the controller; it resets on
  // programExecutionCompleted before this dialog runs.
}

// ============================================================================
// PLACEMENT CORRECTION RETRACE (overlay in execution stack)
// ============================================================================

void RunProgramPage::onShowPlacementRetrace(QString trainingSetName, bool autoStart,
                                             bool autoDetect, bool autoSaveContinue)
{
  // Resolve store base path from current product
  PlacementCorrectionStore store;
  if (m_context && m_context->productPositionManager &&
      m_context->productPositionManager->HasLoadedProduct()) {
    std::string productPath = m_context->productPositionManager->GetCurrentProductPath();
    std::filesystem::path productDir = std::filesystem::path(productPath).parent_path();
    m_activePlacementStoreBasePath = (productDir / "placement_correction").string();
  } else {
    m_activePlacementStoreBasePath = "config/placement_correction";
  }
  store.SetBasePath(m_activePlacementStoreBasePath);

  // Load the training set by name
  auto optSet = store.LoadSet(trainingSetName.toStdString());
  if (!optSet.has_value()) {
    m_logTextEdit->appendPlainText(
      QString("[PlacementRetrace] ERROR: Training set '%1' not found").arg(trainingSetName));
    m_placementRetraceResponse = false;
    if (m_promptEventLoop) {
      m_promptEventLoop->quit();
    }
    return;
  }

  m_activePlacementSet = optSet.value();
  m_activePlacementSet.Recalculate();

  // Configure widget with loaded data
  m_placementRetraceWidget->SetMode("recipe");
  m_placementRetraceWidget->SetData(&m_activePlacementSet);

  // Pass automation flags (must be after SetData which resets them)
  m_placementRetraceWidget->SetAutoDetect(autoDetect);
  m_placementRetraceWidget->SetAutoSaveContinue(autoSaveContinue);

  // Switch to placement retrace view (full right panel)
  m_overlay->setCurrentIndex(m_placementRetracePageIndex);

  // Auto-start the wizard if requested (skip Idle page)
  if (autoStart) {
    m_placementRetraceWidget->AutoStart();
  }
}

void RunProgramPage::onPlacementRetraceCompleted(const PlacementCorrectionSet& /*updatedSet*/)
{
  // Position saved — stay on retrace widget so user can "Move to Target" or click "Continue"
  m_placementRetraceResponse = true;

  m_logTextEdit->appendPlainText("[PlacementRetrace] Corrected position saved - waiting for user to Continue or Move to Target");
}

void RunProgramPage::onPlacementRetraceSkipped()
{
  // User clicked Continue (after save) or Skip (without save)
  if (!m_placementRetraceResponse) {
    m_logTextEdit->appendPlainText("[PlacementRetrace] Retrace skipped - continuing without correction");
  } else {
    m_logTextEdit->appendPlainText("[PlacementRetrace] Continuing to next step after save");
  }
  m_placementRetraceResponse = true;

  m_overlay->ShowDefaultPage();

  if (m_promptEventLoop) {
    m_promptEventLoop->quit();
  }
}

void RunProgramPage::onPlacementRetraceEnded()
{
  // User clicked End without saving
  m_placementRetraceResponse = false;

  m_logTextEdit->appendPlainText("[PlacementRetrace] Retrace ended without saving");

  // Switch back to execution tabs
  m_overlay->ShowDefaultPage();

  if (m_promptEventLoop) {
    m_promptEventLoop->quit();
  }
}

void RunProgramPage::onShowEdgeTransferRetrace(QString trainingSetName)
{
  // Resolve store base path from current product.
  EdgeTransferStore store;
  if (m_context && m_context->productPositionManager &&
      m_context->productPositionManager->HasLoadedProduct()) {
    std::string productPath = m_context->productPositionManager->GetCurrentProductPath();
    std::filesystem::path productDir = std::filesystem::path(productPath).parent_path();
    store.SetBasePath((productDir / "edge_transfer").string());
  } else {
    store.SetBasePath("config/edge_transfer");
  }

  auto optSet = store.LoadSet(trainingSetName.toStdString());
  if (!optSet.has_value()) {
    m_logTextEdit->appendPlainText(
      QString("[EdgeTransferRetrace] ERROR: set '%1' not found").arg(trainingSetName));
    m_edgeRetraceResponse = EdgeTransferRetraceOutcome::Cancelled;
    if (m_promptEventLoop) {
      m_promptEventLoop->quit();
    }
    return;
  }

  m_activeEdgeTransferSet = optSet.value();
  m_activeEdgeTransferSet.Recalculate();

  // Switch to the embedded edge transfer retrace view.
  m_overlay->setCurrentIndex(m_edgeTransferRetracePageIndex);

  // SetRecipeMode was already set once at construction; SetData will
  // auto-jump to WaitRefP1 and fire Auto-Nav (0.5 mm/s to taught P1).
  m_edgeRetraceWidget->SetData(m_activeEdgeTransferSet);
}

void RunProgramPage::onEdgeTransferRetraceOutcome(EdgeTransferRetraceOutcome outcome)
{
  m_edgeRetraceResponse = outcome;

  const char* outcomeStr = "cancelled";
  switch (outcome) {
    case EdgeTransferRetraceOutcome::Applied:   outcomeStr = "applied";   break;
    case EdgeTransferRetraceOutcome::Skipped:   outcomeStr = "skipped";   break;
    case EdgeTransferRetraceOutcome::Cancelled: outcomeStr = "cancelled"; break;
  }
  m_logTextEdit->appendPlainText(
    QString("[EdgeTransferRetrace] Outcome: %1").arg(outcomeStr));

  // Return to execution tabs and unblock the recipe worker.
  m_overlay->ShowDefaultPage();
  if (m_promptEventLoop) {
    m_promptEventLoop->quit();
  }
}

// ============================================================================
// UI UPDATES
// ============================================================================

void RunProgramPage::updateProgramInfo()
{
  QString programName = QString::fromStdString(m_programManager->getProgramName());
  m_leftPanel->programNameLabel()->setText(programName);

  // Update execution title label with program file name
  if (!programName.isEmpty() && programName != "Untitled Program") {
    m_executionStatus->executionTitleLabel()->setText("Program file: " + programName);
  } else {
    m_executionStatus->executionTitleLabel()->setText("Program Execution");
  }

  // Display program file path
  if (!m_currentProgramPath.isEmpty()) {
    m_leftPanel->programPathLabel()->setText("Path: " + m_currentProgramPath);
  } else {
    m_leftPanel->programPathLabel()->setText("");
  }

  // Update slot system UI (program load brought in a new slot list).
  if (m_runSlotWidget)  m_runSlotWidget->Reload();
  if (m_editSlotWidget) m_editSlotWidget->Reload();
  updateSlotInfo();
}

void RunProgramPage::updateRecipeList()
{
  // Run-side recipe list (the QListWidget on the left).
  m_leftPanel->recipeList()->clear();

  int recipeNumber = 1;
  for (const auto& recipe : m_programManager->getAllRecipes()) {
    QString displayText;
    QString loopIndicator = recipe->includeInLoop ? " [L]" : "";

    if (recipe->enabled) {
      QString typePrefix = recipe->type == RecipeReference::SCRIPT ? "[LUA] " : "";
      displayText = QString("%1    %2%3%4")
        .arg(recipeNumber, 2, 10, QChar('0'))
        .arg(typePrefix)
        .arg(QString::fromStdString(recipe->displayName))
        .arg(loopIndicator);
    }
    else {
      displayText = QString("%1    %2 (disabled)")
        .arg(recipeNumber, 2, 10, QChar('0'))
        .arg(QString::fromStdString(recipe->displayName));

      QListWidgetItem* item = new QListWidgetItem(displayText);
      item->setForeground(QColor(150, 150, 150));
      m_leftPanel->recipeList()->addItem(item);
      recipeNumber++;
      continue;
    }

    QListWidgetItem* item = new QListWidgetItem(displayText);
    if (recipe->includeInLoop) {
      item->setForeground(QColor(78, 201, 176));  // Teal color for loop recipes
    }
    m_leftPanel->recipeList()->addItem(item);
    recipeNumber++;
  }

  // Edit-side table is owned by ProgramEditPanel.
  if (m_editPanel) m_editPanel->Reload();

  updateExecuteButton();
  updateSlotInfo();
}

void RunProgramPage::updateExecuteButton()
{
  bool hasRecipes = m_programManager->getRecipeCount() > 0;
  m_leftPanel->executeButton()->setEnabled(hasRecipes && !m_isExecuting);
}

// ============================================================================
// SETTINGS PERSISTENCE
// ============================================================================

void RunProgramPage::saveLastProgramToSettings(const QString& programPath)
{
  ProgramSettingsStore::SaveLastProgramPath(programPath);
  std::cout << "[RunProgramPage] Saved last program to settings: "
            << programPath.toStdString() << std::endl;
}

void RunProgramPage::saveRecentPrograms(const QString& programPath)
{
  ProgramSettingsStore::PrependRecent(programPath, 10);
  std::cout << "[RunProgramPage] Updated recent programs list" << std::endl;
  // Re-render the UI list from the store.
  loadRecentProgramsList();
}

void RunProgramPage::loadRecentProgramsList()
{
  if (!m_leftPanel->recentProgramsList()) return;

  m_leftPanel->recentProgramsList()->clear();

  const QStringList recentPrograms = ProgramSettingsStore::LoadRecent();

  for (const QString& programPath : recentPrograms) {
    // Get file info
    QFileInfo fileInfo(programPath);
    if (!fileInfo.exists()) continue;  // Skip non-existent files

    // Get file name and modification time
    QString fileName = fileInfo.completeBaseName();  // Without extension
    QString modifiedTime = fileInfo.lastModified().toString("yyyy-MM-dd hh:mm");

    // Create list item with name and datetime
    QListWidgetItem* item = new QListWidgetItem();
    item->setText(QString("%1\n%2").arg(fileName, modifiedTime));
    item->setData(Qt::UserRole, programPath);  // Store full path
    item->setToolTip(programPath);

    m_leftPanel->recentProgramsList()->addItem(item);
  }

  std::cout << "[RunProgramPage] Loaded " << m_leftPanel->recentProgramsList()->count()
            << " recent programs" << std::endl;
}

void RunProgramPage::onRecentProgramClicked(QListWidgetItem* item)
{
  if (!item) return;

  QString programPath = item->data(Qt::UserRole).toString();
  if (programPath.isEmpty()) return;

  // Check if file exists
  QFileInfo fileInfo(programPath);
  if (!fileInfo.exists()) {
    QMessageBox::warning(this, "File Not Found",
      QString("The program file no longer exists:\n%1").arg(programPath));
    loadRecentProgramsList();  // Refresh list
    return;
  }

  std::cout << "[RunProgramPage] Loading recent program: "
            << programPath.toStdString() << std::endl;

  // Load the program
  if (!m_programManager->loadProgram(programPath.toStdString())) {
    QMessageBox::critical(this, "Load Error",
      QString("Failed to load program:\n%1").arg(programPath));
    return;
  }

  m_currentProgramPath = programPath;

  // Save to settings for persistence
  saveLastProgramToSettings(programPath);
  saveRecentPrograms(programPath);

  updateProgramInfo();
  updateRecipeList();
  updateLoopStatusLabel();
  setProgramDirty(false);

  std::cout << "[RunProgramPage] Successfully loaded recent program" << std::endl;
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

  const QString lastProgramPath = ProgramSettingsStore::LoadLastProgramPath();

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
  setProgramDirty(false);

  std::cout << "[RunProgramPage] ✅ Successfully auto-loaded: "
            << m_programManager->getProgramName() << std::endl;
  std::cout << "[RunProgramPage] ===============================================" << std::endl;
}

void RunProgramPage::updateVisionResultImage()
{
  if (m_visionHistoryWidget) m_visionHistoryWidget->RefreshFromTempFile();
}

// Clear stale vision results — drops temp file and history.
void RunProgramPage::clearVisionResult()
{
  if (m_visionHistoryWidget) m_visionHistoryWidget->ClearAndResetDisplay();
}

// ============================================================================
// VISION HISTORY — adapter: forward worker signal into the widget
// ============================================================================

void RunProgramPage::onVisionResultReady(QString imagePath)
{
  std::cout << "[RunProgramPage] 🖼️ Vision result ready: "
            << imagePath.toStdString() << std::endl;
  if (m_visionHistoryWidget) m_visionHistoryWidget->AddImage(imagePath);
}

// ============================================================================
// CONTEXT TAB — thin forwards into SequenceContextViewer + the CV populator
// (which lives on the page because it talks to the camera/beam managers).
// ============================================================================

void RunProgramPage::startContextRefreshTimer()
{
  if (!m_contextViewer) return;
  // Hand the live worker (created by the controller for this run) to the
  // viewer before live polling starts.
  m_contextViewer->SetWorker(m_executionController ? m_executionController->Worker() : nullptr);
  m_contextViewer->StartLivePolling(m_isLoopExecution ? 1000 : 500);
}

void RunProgramPage::stopContextRefreshTimer()
{
  if (m_contextViewer) m_contextViewer->StopLivePolling();
}

void RunProgramPage::updateContextTable(bool forceUpdate)
{
  if (!m_contextViewer) return;
  m_contextViewer->SetWorker(m_executionController ? m_executionController->Worker() : nullptr);
  if (forceUpdate) m_contextViewer->ForceRefresh();
}

bool RunProgramPage::populateCvContext(SequenceContext& target, QString& errorMsg)
{
  if (!m_context || !m_context->gdiCameraWindowManager) {
    errorMsg = "GDI Camera Window not available";
    return false;
  }

  bool hasVision = false;
  bool hasBeamStream  = m_context->gdiCameraWindowManager->isBeamStreamActive();
  bool hasBeamStream2 = m_context->gdiCameraWindowManager2 &&
                        m_context->gdiCameraWindowManager2->isBeamStreamActive();

#ifdef HAS_OPENCV
  hasVision = m_context->gdiCameraWindowManager->isVisionProcessingActive();
#endif

  if (!hasVision && !hasBeamStream && !hasBeamStream2) {
    errorMsg = "No live data source active (vision / beam stream)";
    return false;
  }

#ifdef HAS_OPENCV
  if (hasVision) {
    VisionResult result = m_context->gdiCameraWindowManager->getLatestVisionResult();
    target.SetValue("cv.result.success", result.success ? "true" : "false");
    target.SetValue("cv.result.plugin", result.pluginName);
    target.SetValue("cv.result.processing_time_ms", std::to_string(result.processingTimeMs));
    for (const auto& [key, val] : result.measurements) {
      target.SetValue("cv.result." + key, std::to_string(val));
    }
    target.SetValue("cv.point.count", std::to_string(result.points.size()));
    for (size_t i = 0; i < result.points.size(); ++i) {
      std::string prefix = "cv.point." + std::to_string(i) + ".";
      const auto& pt = result.points[i];
      target.SetValue(prefix + "x",          std::to_string(pt.pixelCoord.x));
      target.SetValue(prefix + "y",          std::to_string(pt.pixelCoord.y));
      target.SetValue(prefix + "mm_x",       std::to_string(pt.mmCoord.x));
      target.SetValue(prefix + "mm_y",       std::to_string(pt.mmCoord.y));
      target.SetValue(prefix + "confidence", std::to_string(pt.confidence));
    }
    target.SetValue("cv.circle.count", std::to_string(result.circles.size()));
    for (size_t i = 0; i < result.circles.size(); ++i) {
      std::string prefix = "cv.circle." + std::to_string(i) + ".";
      const auto& c = result.circles[i];
      target.SetValue(prefix + "x",         std::to_string(c.centerPx.x));
      target.SetValue(prefix + "y",         std::to_string(c.centerPx.y));
      target.SetValue(prefix + "radius",    std::to_string(c.radiusPx));
      target.SetValue(prefix + "mm_x",      std::to_string(c.centerMm.x));
      target.SetValue(prefix + "mm_y",      std::to_string(c.centerMm.y));
      target.SetValue(prefix + "radius_mm", std::to_string(c.radiusMm));
    }
    target.SetValue("cv.line.count", std::to_string(result.lines.size()));
    for (size_t i = 0; i < result.lines.size(); ++i) {
      std::string prefix = "cv.line." + std::to_string(i) + ".";
      const auto& l = result.lines[i];
      target.SetValue(prefix + "start_x", std::to_string(l.startPx.x));
      target.SetValue(prefix + "start_y", std::to_string(l.startPx.y));
      target.SetValue(prefix + "end_x",   std::to_string(l.endPx.x));
      target.SetValue(prefix + "end_y",   std::to_string(l.endPx.y));
    }
  }
#endif

  auto injectBeamStream = [&](GdiCameraWindowManager* mgr, const std::string& bsKey) {
    if (!mgr) return;
    nlohmann::json bsResult = mgr->getLatestBeamStreamResult();
    target.SetValue(bsKey + ".active", "true");
    target.SetValue(bsKey + ".fps", std::to_string(mgr->getBeamStreamFps()));
    if (bsResult.contains("process_ms")) {
      target.SetValue(bsKey + ".process_ms", std::to_string(bsResult["process_ms"].get<double>()));
    }
    if (bsResult.contains("beams") && bsResult["beams"].is_array()) {
      const auto& beams = bsResult["beams"];
      target.SetValue(bsKey + ".beam.count", std::to_string(beams.size()));
      for (size_t i = 0; i < beams.size(); ++i) {
        std::string prefix = bsKey + ".beam." + std::to_string(i) + ".";
        const auto& beam = beams[i];
        if (beam.contains("side")) {
          target.SetValue(prefix + "label", beam["side"].get<std::string>());
        }
        target.SetValue(prefix + "centroid_x_px", std::to_string(beam.value("centroid_x_px", 0.0)));
        target.SetValue(prefix + "centroid_y_px", std::to_string(beam.value("centroid_y_px", 0.0)));
        if (beam.contains("d4sigma_x_px"))   target.SetValue(prefix + "d4sigma_x_px",   std::to_string(beam["d4sigma_x_px"].get<double>()));
        if (beam.contains("d4sigma_y_px"))   target.SetValue(prefix + "d4sigma_y_px",   std::to_string(beam["d4sigma_y_px"].get<double>()));
        if (beam.contains("d4sigma_x_um"))   target.SetValue(prefix + "d4sigma_x_um",   std::to_string(beam["d4sigma_x_um"].get<double>()));
        if (beam.contains("d4sigma_y_um"))   target.SetValue(prefix + "d4sigma_y_um",   std::to_string(beam["d4sigma_y_um"].get<double>()));
        if (beam.contains("peak_intensity")) target.SetValue(prefix + "peak_intensity", std::to_string(beam["peak_intensity"].get<double>()));
        if (beam.contains("roi_x"))          target.SetValue(prefix + "roi_x",          std::to_string(beam["roi_x"].get<int>()));
        if (beam.contains("roi_y"))          target.SetValue(prefix + "roi_y",          std::to_string(beam["roi_y"].get<int>()));
        if (beam.contains("roi_w"))          target.SetValue(prefix + "roi_w",          std::to_string(beam["roi_w"].get<int>()));
        if (beam.contains("roi_h"))          target.SetValue(prefix + "roi_h",          std::to_string(beam["roi_h"].get<int>()));
      }
      if (!beams.empty()) {
        const auto& first = beams[0];
        target.SetValue(bsKey + ".beam.first.centroid_x_px", std::to_string(first.value("centroid_x_px", 0.0)));
        target.SetValue(bsKey + ".beam.first.centroid_y_px", std::to_string(first.value("centroid_y_px", 0.0)));
        if (first.contains("d4sigma_x_px")) target.SetValue(bsKey + ".beam.first.d4sigma_x_px", std::to_string(first["d4sigma_x_px"].get<double>()));
        if (first.contains("d4sigma_y_px")) target.SetValue(bsKey + ".beam.first.d4sigma_y_px", std::to_string(first["d4sigma_y_px"].get<double>()));
        if (first.contains("side"))         target.SetValue(bsKey + ".beam.first.label",        first["side"].get<std::string>());
      }
    } else {
      target.SetValue(bsKey + ".beam.count", "0");
    }
  };

  if (hasBeamStream)  injectBeamStream(m_context->gdiCameraWindowManager,  "bs");
  if (hasBeamStream2) injectBeamStream(m_context->gdiCameraWindowManager2, "bs2");

  return true;
}

void RunProgramPage::applyTheme()
{
  applyProgramStatusLabel();

  m_executionStatus->currentRecipeLabel()->setStyleSheet(
    QString("font-size: 16px; font-weight: bold; color: %1;")
      .arg(QLatin1String(Theme::Color::primary)));

  m_executionStatus->actionDetailLabel()->setStyleSheet(
    QString("QLabel { "
            "  font-size: 12px; "
            "  color: %1; "
            "  background-color: %2; "
            "  border: 1px solid %3; "
            "  border-radius: 4px; "
            "  padding: 6px 10px; "
            "}")
      .arg(QLatin1String(Theme::Color::textSecondary),
           QLatin1String(Theme::Color::bgSurface),
           QLatin1String(Theme::Color::borderSubtle)));

  m_executionStatus->progressBar()->setStyleSheet(
    QString("QProgressBar { "
            "  border: 1px solid %1; "
            "  border-radius: 3px; "
            "  background-color: %2; "
            "  text-align: center; "
            "  color: %3; "
            "  font-weight: bold; "
            "} "
            "QProgressBar::chunk { "
            "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
            "      stop:0 #58d68d, stop:1 #27ae60); "
            "  border-radius: 2px; "
            "}")
      .arg(QLatin1String(Theme::Color::borderSubtle),
           QLatin1String(Theme::Color::bgDeep),
           QLatin1String(Theme::Color::textPrimary)));

  m_leftPanel->backButton()->setStyleSheet(Theme::Buttons::bezelPrimary());
  m_leftPanel->executeButton()->setStyleSheet(m_isExecuting ? Theme::Buttons::bezelDanger() : Theme::Buttons::bezelSuccess());
}