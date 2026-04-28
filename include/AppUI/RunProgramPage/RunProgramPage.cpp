// RunProgramPage.cpp
#include "RunProgramPage.h"
#include "ProgramSettingsStore.h"
#include "VisionHistoryWidget.h"
#include "SequenceContextViewer.h"
#include "ProgramEditPanel.h"
#include "SlotManagerWidget.h"
#include "ApplicationContext.h"
#include "settings/ApplicationSettings.h"
#include "ProgramManager.h"
#include "ConditionExpressionEvaluator.h"
#include "safety/SafetyWatchManager.h"
#include "ProgramWorker.h"
#include "ProcessSequenceFactory.h"
#include "UserPromptWidget.h"
#include "ManualAdjustWidget.h"
#include "ManualAlignPointWidget.h"
#include "UserInputWidget.h"
#include "ScanControlWidget.h"
#include "ScanController.h"
#include "RollCorrectionWidget.h"
#include "MotionInspectorWidget.h"
#include "Theme.h"
#include "ThemeButtonStyles.h"
#include "common/ScanActivityTracker.h"
#include "motions/unified_motion_manager.h"
#include "SequenceEngine.h"
#include "MotionCommandHandler.h"
#include "CameraClickToMoveWidget.h"
#include <future>
#include "RetraceEditWidget.h"
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
  , m_programWorker(nullptr)
  , m_processFactory(nullptr)
  , m_isExecuting(false)
  , m_totalRecipes(0)
  , m_currentRecipeIndex(-1)
  , m_pendingChainRecipeIndex(-1)
  , m_currentLoopIteration(0)
  , m_isLoopExecution(false)
  , m_executionStack(nullptr)
  , m_promptWidget(nullptr)
  , m_manualAdjustWidget(nullptr)
  , m_manualAlignPointWidget(nullptr)
  , m_userInputWidget(nullptr)
  , m_promptEventLoop(nullptr)
  , m_promptResponse(false)
  , m_manualAdjustResponse(false)
  , m_manualAlignPointResponse(false)
  , m_userInputAccepted(false)
  , m_userInputText("")
  , m_scanWidget(nullptr)
  , m_dataStore(context->dataStore)
  , m_currentSubscribedChannel("")
  , m_currentRecipeTotalSteps(0)      // ✅ Initialize step tracking
  , m_currentRecipeCompletedSteps(0)  // ✅ Initialize step tracking
  , m_currentContext(nullptr)          // Execution context
  , m_retraceEditWidget(nullptr)       // ✅ Retrace edit widget
  , m_retraceEditContinue(false)       // ✅ Retrace edit continue status
  , m_retraceEditSave(false)           // ✅ Retrace edit save status
  , m_isRetraceEditMode(false)         // ✅ Retrace edit mode flag
  , m_retraceEditRecipeIndex(-1)       // ✅ Retrace edit recipe index
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
    m_currentProductLabel->setText(QString("Product: %1").arg(QString::fromStdString(productName)));
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

  if (m_programWorker && m_programWorker->isRunning()) {
    m_programWorker->requestStop();
    m_programWorker->wait(3000);
  }

  // ScanController + ScanExecutor cleanup is automatic via QObject parenting.

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

  // Left panel (20%) - Execution controls
  m_leftPanel = new QWidget();
  m_leftPanel->setMinimumWidth(500);
  m_leftPanel->setMaximumWidth(700);
  QVBoxLayout* leftLayout = new QVBoxLayout(m_leftPanel);
  createLeftPanel(leftLayout);

  // Center panel - Execution view (scanning is now a tab inside)
  createRightPanel();

  // Add to main layout with stretch factors (25/75 split - no right panel)
  mainLayout->addWidget(m_leftPanel, 1);     // Left: 25%
  mainLayout->addWidget(m_rightStack, 3);    // Center: 75%

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
  m_backButton->setStyleSheet(Theme::Buttons::bezelPrimary());
  leftLayout->addWidget(m_backButton);

  // === Product Display with Chain Indicator ===
  QWidget* productRow = new QWidget();
  QHBoxLayout* productRowLayout = new QHBoxLayout(productRow);
  productRowLayout->setContentsMargins(0, 0, 0, 0);
  productRowLayout->setSpacing(8);

  m_currentProductLabel = new QLabel("Product: None");
  m_currentProductLabel->setStyleSheet(
    "QLabel {"
    "  color: #cccccc;"
    "  font-weight: bold;"
    "  padding: 5px;"
    "}"
  );
  m_currentProductLabel->setWordWrap(true);
  productRowLayout->addWidget(m_currentProductLabel);

  // Chain execution indicator (orange circle when enabled)
  m_chainIndicator = new QLabel();
  m_chainIndicator->setFixedSize(16, 16);
  m_chainIndicator->setToolTip("Chain execution enabled");
  m_chainIndicator->setVisible(false);  // Hidden by default
  m_chainIndicator->setStyleSheet(
    "QLabel {"
    "  background-color: #e69500;"
    "  border-radius: 8px;"
    "  border: 2px solid #ffaa00;"
    "}"
  );
  productRowLayout->addWidget(m_chainIndicator);

  // Loop execution indicator (teal circle when enabled)
  m_loopIndicator = new QLabel();
  m_loopIndicator->setFixedSize(16, 16);
  m_loopIndicator->setToolTip("Loop execution enabled");
  m_loopIndicator->setVisible(false);  // Hidden by default
  m_loopIndicator->setStyleSheet(
    "QLabel {"
    "  background-color: #4ec9b0;"
    "  border-radius: 8px;"
    "  border: 2px solid #5fd9c0;"
    "}"
  );
  productRowLayout->addWidget(m_loopIndicator);
  productRowLayout->addStretch();

  leftLayout->addWidget(productRow);

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

  QVBoxLayout* fileButtonLayout = new QVBoxLayout();
  fileButtonLayout->setSpacing(5);
  m_newButton = new QPushButton("New");
  m_newButton->setStyleSheet(
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
  m_saveAsButton = new QPushButton("Save As");
  m_saveAsButton->setStyleSheet(
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

  fileButtonLayout->addWidget(m_newButton);
  fileButtonLayout->addWidget(m_loadButton);
  fileButtonLayout->addWidget(m_saveButton);
  fileButtonLayout->addWidget(m_saveAsButton);
  fileButtonLayout->addWidget(m_editButton);
  fileLayout->addLayout(fileButtonLayout);

  fileTabLayout->addWidget(fileGroup);

  // === RECENT PROGRAMS LIST ===
  QGroupBox* recentGroup = new QGroupBox("Recent Programs");
  recentGroup->setStyleSheet(
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
  QVBoxLayout* recentLayout = new QVBoxLayout(recentGroup);
  recentLayout->setContentsMargins(5, 5, 5, 5);
  recentLayout->setSpacing(0);

  m_recentProgramsList = new QListWidget();
  m_recentProgramsList->setStyleSheet(
    "QListWidget { "
    "  background-color: #2d2d30; "
    "  border: 1px solid #3f3f46; "
    "  color: #d4d4d4; "
    "  font-size: 10px; "
    "} "
    "QListWidget::item { "
    "  padding: 4px 6px; "
    "  border-bottom: 1px solid #3f3f46; "
    "} "
    "QListWidget::item:hover { "
    "  background-color: #3c3c3c; "
    "} "
    "QListWidget::item:selected { "
    "  background-color: #0e639c; "
    "  color: white; "
    "}"
  );
  m_recentProgramsList->setMaximumHeight(200);
  m_recentProgramsList->setToolTip("Double-click to load a recent program");
  connect(m_recentProgramsList, &QListWidget::itemDoubleClicked,
          this, &RunProgramPage::onRecentProgramClicked);
  recentLayout->addWidget(m_recentProgramsList);

  fileTabLayout->addWidget(recentGroup);
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
  m_executeButton->setStyleSheet(Theme::Buttons::bezelSuccess());
  executeLayout->addWidget(m_executeButton);

  // Progress bar moved to right panel (execution widget) for better visibility

  executeTabLayout->addWidget(executeGroup, 0);

  // Run-side Active Slot widget (combo + recipe count label).
  {
    SlotManagerWidget::Deps deps;
    deps.manager      = m_programManager;
    deps.dialogParent = this;
    m_runSlotWidget = new SlotManagerWidget(SlotManagerWidget::Mode::Run, deps);
    connect(m_runSlotWidget, &SlotManagerWidget::activeSlotChanged,
            this, &RunProgramPage::onSlotChangedFromRun);
    executeTabLayout->addWidget(m_runSlotWidget, 0);
  }

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

  // Enable context menu for retrace edit
  m_recipeListWidget->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_recipeListWidget, &QListWidget::customContextMenuRequested,
          this, &RunProgramPage::onRecipeListContextMenu);

  recipeLayout->addWidget(m_recipeListWidget);

  executeTabLayout->addWidget(recipeGroup, 1);

  m_tabWidget->addTab(executeTab, "Execute");

  // === OPTION TAB ===
  QWidget* optionTab = new QWidget();
  QVBoxLayout* optionTabLayout = new QVBoxLayout(optionTab);
  optionTabLayout->setContentsMargins(5, 10, 5, 5);
  optionTabLayout->setSpacing(10);

  QGroupBox* optionGroup = new QGroupBox("Execution Options");
  optionGroup->setStyleSheet(
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
  QVBoxLayout* optionLayout = new QVBoxLayout(optionGroup);

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
  optionLayout->addWidget(m_autoRunNextCheckBox);

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
  optionLayout->addWidget(m_useDependenciesCheckBox);

  // Chain Execution checkbox - auto-trigger dependent recipes after manual execution
  m_chainExecutionCheckBox = new QCheckBox("Enable chain execution");
  m_chainExecutionCheckBox->setChecked(false);
  m_chainExecutionCheckBox->setToolTip("When enabled, completing a recipe will automatically trigger dependent recipes if conditions are met");
  m_chainExecutionCheckBox->setStyleSheet(
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
    "  border-color: #e67e22; "
    "} "
    "QCheckBox::indicator:checked { "
    "  background-color: #e67e22; "
    "  border-color: #e67e22; "
    "} "
    "QCheckBox::indicator:checked:hover { "
    "  background-color: #f39c12; "
    "  border-color: #f39c12; "
    "} "
    "QCheckBox::indicator:unchecked { "
    "  background-color: #2d2d30; "
    "  border-color: #555555; "
    "}"
  );
  optionLayout->addWidget(m_chainExecutionCheckBox);

  // Connect checkbox to show/hide the chain indicator
  connect(m_chainExecutionCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
    m_chainIndicator->setVisible(checked);
  });

  // Loop Execution section
  QGroupBox* loopGroup = new QGroupBox("Loop Execution");
  loopGroup->setStyleSheet(
    "QGroupBox { "
    "  color: #4ec9b0; "
    "  font-weight: bold; "
    "  border: 1px solid #3c3c3c; "
    "  border-radius: 5px; "
    "  margin-top: 10px; "
    "  padding-top: 10px; "
    "} "
    "QGroupBox::title { "
    "  subcontrol-origin: margin; "
    "  left: 10px; "
    "  padding: 0 5px; "
    "}"
  );
  QVBoxLayout* loopLayout = new QVBoxLayout(loopGroup);
  loopLayout->setContentsMargins(10, 15, 10, 10);
  loopLayout->setSpacing(8);

  // Loop enabled checkbox
  m_loopEnabledCheckBox = new QCheckBox("Enable infinite loop");
  m_loopEnabledCheckBox->setChecked(false);
  m_loopEnabledCheckBox->setToolTip("Run selected recipes in an infinite loop until stopped");
  m_loopEnabledCheckBox->setStyleSheet(
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
    "  border-color: #4ec9b0; "
    "} "
    "QCheckBox::indicator:checked { "
    "  background-color: #4ec9b0; "
    "  border-color: #4ec9b0; "
    "} "
    "QCheckBox::indicator:checked:hover { "
    "  background-color: #5fd9c0; "
    "  border-color: #5fd9c0; "
    "}"
  );
  loopLayout->addWidget(m_loopEnabledCheckBox);

  // Connect checkbox to show/hide the loop indicator
  connect(m_loopEnabledCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
    // Only show indicator if loop is enabled AND there are loop recipes configured
    m_loopIndicator->setVisible(checked && m_programManager->hasLoopRecipes());
  });

  // Configure loop button
  m_configureLoopButton = new QPushButton("Configure Loop Recipes...");
  m_configureLoopButton->setToolTip("Select which recipes to include in the loop");
  m_configureLoopButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #3c3c3c; "
    "  color: #d4d4d4; "
    "  border: 1px solid #555555; "
    "  padding: 6px 12px; "
    "  border-radius: 4px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #505050; "
    "  border-color: #4ec9b0; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #4ec9b0; "
    "}"
  );
  connect(m_configureLoopButton, &QPushButton::clicked, this, &RunProgramPage::onConfigureLoop);
  loopLayout->addWidget(m_configureLoopButton);

  // Loop status label
  m_loopStatusLabel = new QLabel("No recipes configured for loop");
  m_loopStatusLabel->setStyleSheet("color: #888888; font-size: 11px; padding: 2px 5px;");
  loopLayout->addWidget(m_loopStatusLabel);

  // Ignore dependencies checkbox
  m_ignoreDependenciesCheckBox = new QCheckBox("Ignore dependencies");
  m_ignoreDependenciesCheckBox->setChecked(false);
  m_ignoreDependenciesCheckBox->setToolTip("Force sequential execution without checking dependency conditions");
  m_ignoreDependenciesCheckBox->setStyleSheet(
    "QCheckBox { "
    "  spacing: 8px; "
    "  padding: 5px; "
    "  color: #d4d4d4; "
    "} "
    "QCheckBox::indicator { "
    "  width: 18px; "
    "  height: 18px; "
    "  border: 2px solid #555555; "
    "  border-radius: 3px; "
    "  background-color: #2d2d30; "
    "} "
    "QCheckBox::indicator:hover { "
    "  border-color: #e8a838; "
    "} "
    "QCheckBox::indicator:checked { "
    "  background-color: #e8a838; "
    "  border-color: #e8a838; "
    "} "
    "QCheckBox::indicator:checked:hover { "
    "  background-color: #f0b848; "
    "  border-color: #f0b848; "
    "}"
  );
  loopLayout->addWidget(m_ignoreDependenciesCheckBox);

  optionLayout->addWidget(loopGroup);

  optionLayout->addStretch();
  optionTabLayout->addWidget(optionGroup);
  optionTabLayout->addStretch();

  m_tabWidget->addTab(optionTab, "Option");

  // Set Execute tab as default
  m_tabWidget->setCurrentIndex(1);

  // Add tab widget to left layout
  leftLayout->addWidget(m_tabWidget, 1);

  // Connect signals
  connect(m_newButton, &QPushButton::clicked, this, &RunProgramPage::onNewProgram);
  connect(m_loadButton, &QPushButton::clicked, this, &RunProgramPage::onLoadProgram);
  connect(m_saveButton, &QPushButton::clicked, this, &RunProgramPage::onSaveProgram);
  connect(m_saveAsButton, &QPushButton::clicked, this, &RunProgramPage::onSaveAsProgram);
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
  QGroupBox* programNameGroup = new QGroupBox("Program Name");
  programNameGroup->setStyleSheet(Theme::Boxes::groupBoxDark());
  QVBoxLayout* programNameLayout = new QVBoxLayout(programNameGroup);

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
  layout->addWidget(programNameGroup);

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
          this, [this]() { updateRecipeList(); updateSlotInfo(); });
  connect(m_editPanel, &ProgramEditPanel::inspectMotionRequested,
          this, [this](int row) {
            const RecipeReference* recipe = m_programManager->getRecipe(row);
            if (recipe && m_motionInspectorWidget && m_motionInspectorTabIndex >= 0) {
              m_motionInspectorWidget->loadRecipe(
                  QString::fromStdString(recipe->recipeFilePath),
                  QString::fromStdString(recipe->displayName));
              showExecutionWidget();
              m_executionStack->setCurrentIndex(0);
              m_executionTabWidget->setCurrentIndex(m_motionInspectorTabIndex);
            }
          });
  connect(m_editPanel, &ProgramEditPanel::retraceEditRequested,
          this, [this](int row) { onStartRetraceEdit(row); });

  connect(m_saveEditButton, &QPushButton::clicked, this, &RunProgramPage::onSaveEdit);
}

// Stores the requested base state and re-applies the badge. Use this instead
// of touching m_programStatusLabel directly so that an active scan overlay
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
  if (!m_programStatusLabel) {
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

  m_programStatusLabel->setText(text);
  m_programStatusLabel->setStyleSheet(
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

  // === HEADER SECTION ===
  // Header: large status badge on the left (250x150), program info on the right
  QHBoxLayout* headerRow = new QHBoxLayout();
  headerRow->setContentsMargins(0, 0, 0, 0);
  headerRow->setSpacing(15);

  // Program status indicator (Idle/Running/Stopped/Scanning) — 250x150 badge on the left
  m_programStatusLabel = new QLabel("Idle");
  m_programStatusLabel->setFixedSize(250, 150);
  m_programStatusLabel->setAlignment(Qt::AlignCenter);
  // Initial state mirrors the "Idle" defaults stored in m_baseStatus*; the
  // helper paints from those members so a scan overlay can swap in/out
  // without losing the underlying state.
  applyProgramStatusLabel();
  headerRow->addWidget(m_programStatusLabel, 0, Qt::AlignTop);

  // Right side: program title + status line + current recipe (stacked)
  QVBoxLayout* headerTextLayout = new QVBoxLayout();
  headerTextLayout->setContentsMargins(0, 0, 0, 0);
  headerTextLayout->setSpacing(4);

  m_executionTitleLabel = new QLabel("Program Execution");
  m_executionTitleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #ffffff;");
  headerTextLayout->addWidget(m_executionTitleLabel);

  // Status row
  m_executionStatusLabel = new QLabel("Ready to execute");
  m_executionStatusLabel->setStyleSheet("font-size: 14px; color: #cccccc;");
  headerTextLayout->addWidget(m_executionStatusLabel);

  // Current recipe
  m_currentRecipeLabel = new QLabel("Current Recipe: None");
  m_currentRecipeLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #0e639c;");
  headerTextLayout->addWidget(m_currentRecipeLabel);

  headerTextLayout->addStretch();

  headerRow->addLayout(headerTextLayout, 1);

  layout->addLayout(headerRow);

  layout->addSpacing(10);

  // === COMMAND TRACKER ROW (4 cells: Past, Current, Next1, Next2) ===
  m_commandTrackerRow = new QWidget();
  QHBoxLayout* trackerLayout = new QHBoxLayout(m_commandTrackerRow);
  trackerLayout->setContentsMargins(0, 0, 0, 0);
  trackerLayout->setSpacing(4);

  auto createTrackerCell = [](const QString& headerText, QLabel*& label) -> QWidget* {
    QWidget* cell = new QWidget();
    QVBoxLayout* cellLayout = new QVBoxLayout(cell);
    cellLayout->setContentsMargins(8, 4, 8, 4);
    cellLayout->setSpacing(2);

    QLabel* header = new QLabel(headerText);
    header->setStyleSheet("font-size: 10px; color: #888888; font-weight: bold;");
    header->setAlignment(Qt::AlignCenter);
    cellLayout->addWidget(header);

    label = new QLabel("-");
    label->setStyleSheet(
      "QLabel { "
      "  font-size: 11px; "
      "  color: #cccccc; "
      "  padding: 4px; "
      "}"
    );
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    label->setMinimumHeight(40);
    cellLayout->addWidget(label);

    cell->setStyleSheet(
      "QWidget { "
      "  background-color: #2d2d2d; "
      "  border: 1px solid #3f3f46; "
      "  border-radius: 4px; "
      "}"
    );
    return cell;
  };

  // Create the 4 cells
  QWidget* pastCell = createTrackerCell("PREVIOUS", m_cmdPastLabel);
  QWidget* currentCell = createTrackerCell("CURRENT", m_cmdCurrentLabel);
  QWidget* next1Cell = createTrackerCell("NEXT", m_cmdNext1Label);
  QWidget* next2Cell = createTrackerCell("AFTER", m_cmdNext2Label);

  // Style the current cell differently (highlighted)
  currentCell->setStyleSheet(
    "QWidget { "
    "  background-color: #0e4f7c; "
    "  border: 1px solid #0078d4; "
    "  border-radius: 4px; "
    "}"
  );
  m_cmdCurrentLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 11px; "
    "  color: #ffffff; "
    "  font-weight: bold; "
    "  padding: 4px; "
    "}"
  );

  trackerLayout->addWidget(pastCell, 1);
  trackerLayout->addWidget(currentCell, 1);
  trackerLayout->addWidget(next1Cell, 1);
  trackerLayout->addWidget(next2Cell, 1);

  layout->addWidget(m_commandTrackerRow);
  // Hidden from the UI by request; underlying labels remain wired so
  // m_cmdPastLabel / m_cmdCurrentLabel / m_cmdNext1Label / m_cmdNext2Label
  // continue to receive updates and can be re-enabled by removing this line.
  m_commandTrackerRow->hide();

  // === ACTION DETAIL LINE (human-readable description of current action) ===
  m_actionDetailLabel = new QLabel("-");
  m_actionDetailLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 12px; "
    "  color: #cccccc; "
    "  background-color: #2d2d2d; "
    "  border: 1px solid #3f3f46; "
    "  border-radius: 4px; "
    "  padding: 6px 10px; "
    "}"
  );
  m_actionDetailLabel->setWordWrap(false);
  m_actionDetailLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_actionDetailLabel->setToolTip("Current sequence action");
  layout->addWidget(m_actionDetailLabel);

  // === PROGRESS BAR (tracks step execution progress) ===
  m_progressBar = new QProgressBar();
  m_progressBar->setRange(0, 100);
  m_progressBar->setValue(0);
  m_progressBar->setTextVisible(true);
  m_progressBar->setFixedHeight(18);
  m_progressBar->setStyleSheet(
    "QProgressBar { "
    "  border: 1px solid #555555; "
    "  border-radius: 3px; "
    "  background-color: #2d2d30; "
    "  text-align: center; "
    "  color: #ffffff; "
    "  font-weight: bold; "
    "} "
    "QProgressBar::chunk { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
    "      stop:0 #58d68d, stop:1 #27ae60); "
    "  border-radius: 2px; "
    "}"
  );
  layout->addWidget(m_progressBar);
  layout->addSpacing(4);

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

  // Create stacked widget to switch between tabs and prompt/input views
  m_executionStack = new QStackedWidget();

  // === PROMPT VIEW (Index 1) ===
  m_promptWidget = new UserPromptWidget();
  m_promptWidget->setStyleSheet(
    "UserPromptWidget { "
    "  background-color: #1e1e1e; "
    "}"
  );

  // Apply prompt alignment from application settings
  if (m_context && m_context->applicationSettings) {
    std::string alignStr = m_context->applicationSettings->GetString("UI", "PromptAlignment", "Center");
    if (alignStr == "TopLeft") {
      m_promptWidget->setAlignment(UserPromptWidget::Alignment::TopLeft);
    }
  }

  // Connect prompt response
  connect(m_promptWidget, &UserPromptWidget::responseReceived,
    this, &RunProgramPage::onPromptResponse);

  // === MANUAL ADJUST VIEW (Index 2) ===
  m_manualAdjustWidget = new ManualAdjustWidget(
    m_context,
    m_matrixLoader);
  m_manualAdjustWidget->setStyleSheet(
    "ManualAdjustWidget { "
    "  background-color: #1e1e1e; "
    "}"
  );

  // Connect manual adjust response
  connect(m_manualAdjustWidget, &ManualAdjustWidget::responseReceived,
    this, &RunProgramPage::onManualAdjustResponse);

  // === MANUAL ALIGN POINT VIEW (Index 3) ===
  m_manualAlignPointWidget = new ManualAlignPointWidget(
    m_context->motionManager,
    m_context
  );
  m_manualAlignPointWidget->setStyleSheet(
    "ManualAlignPointWidget { "
    "  background-color: #1e1e1e; "
    "}"
  );

  // Connect manual align point response
  connect(m_manualAlignPointWidget, &ManualAlignPointWidget::responseReceived,
    this, &RunProgramPage::onManualAlignPointResponse);

  // === USER INPUT VIEW (Index 4) ===
  m_userInputWidget = new UserInputWidget();
  m_userInputWidget->setStyleSheet(
    "UserInputWidget { "
    "  background-color: #1e1e1e; "
    "}"
  );

  // Apply input alignment from application settings (same as prompt)
  if (m_context && m_context->applicationSettings) {
    std::string alignStr = m_context->applicationSettings->GetString("UI", "PromptAlignment", "Center");
    if (alignStr == "TopLeft") {
      m_userInputWidget->setAlignment(UserInputWidget::Alignment::TopLeft);
    }
  }

  // Connect user input response
  connect(m_userInputWidget, &UserInputWidget::responseReceived,
    this, &RunProgramPage::onUserInputResponse);

  // === RETRACE EDIT VIEW (Index 5) ===
  m_retraceEditWidget = new RetraceEditWidget(
    m_context->motionManager,
    m_matrixLoader);
  m_retraceEditWidget->setStyleSheet(
    "RetraceEditWidget { "
    "  background-color: #1e1e1e; "
    "}"
  );

  // Connect retrace edit response
  connect(m_retraceEditWidget, &RetraceEditWidget::responseReceived,
    this, &RunProgramPage::onRetraceEditResponse);

  // Add all views to stack
  m_executionStack->addWidget(m_executionTabWidget);      // Index 0: Tabs (Log + Live View)
  m_executionStack->addWidget(m_promptWidget);            // Index 1: Prompt
  m_executionStack->addWidget(m_manualAdjustWidget);      // Index 2: Manual Adjust
  m_executionStack->addWidget(m_manualAlignPointWidget);  // Index 3: Manual Align Point
  m_executionStack->addWidget(m_userInputWidget);         // Index 4: User Input
  m_executionStack->addWidget(m_retraceEditWidget);       // Index 5: Retrace Edit

  // Placement Retrace Widget (reuses PlacementRetraceWidget from trainer module)
  m_placementRetraceWidget = new PlacementRetraceWidget(m_context);
  connect(m_placementRetraceWidget, &PlacementRetraceWidget::retraceCompleted,
    this, &RunProgramPage::onPlacementRetraceCompleted);
  connect(m_placementRetraceWidget, &PlacementRetraceWidget::retraceSkipped,
    this, &RunProgramPage::onPlacementRetraceSkipped);
  connect(m_placementRetraceWidget, &PlacementRetraceWidget::retraceEnded,
    this, &RunProgramPage::onPlacementRetraceEnded);
  // Manual tune offsets edited during a retrace — persist the active set
  // back to its source JSON so the offset is remembered next run.
  //
  // Guard: if a recipe is actively executing (m_isExecuting), skip the disk
  // write to avoid overwriting the set mid-recipe. The offset is still
  // applied in-memory for the current retrace; it just won't be persisted
  // until the recipe finishes. This prevents a later recipe step that uses
  // the same set from silently picking up a freshly-edited offset.
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
  m_executionStack->addWidget(m_placementRetraceWidget);  // Index 6: Placement Retrace

  // Edge Transfer retrace widget (embedded, index 7)
  m_edgeRetraceWidget = new EdgeRetraceWidget(m_context);
  m_edgeRetraceWidget->SetRecipeMode(true);
  connect(m_edgeRetraceWidget, &EdgeRetraceWidget::recipeOutcomeReady,
    this, &RunProgramPage::onEdgeTransferRetraceOutcome);
  m_executionStack->addWidget(m_edgeRetraceWidget);       // Index 7: Edge Transfer Retrace

  // Cross-thread signal bridges for recipe callbacks (this → this connections).
  // Must be connected once here, NOT in onExecuteProgram(), to avoid accumulating
  // duplicate connections on each execution.
  connect(this, &RunProgramPage::showUserPrompt,
    this, &RunProgramPage::onShowUserPrompt);
  connect(this, &RunProgramPage::showManualAdjust,
    this, &RunProgramPage::onShowManualAdjust);
  connect(this, &RunProgramPage::showPlacementRetrace,
    this, &RunProgramPage::onShowPlacementRetrace);
  connect(this, &RunProgramPage::showEdgeTransferRetrace,
    this, &RunProgramPage::onShowEdgeTransferRetrace);

  m_executionStack->setCurrentIndex(0);                   // Start with tabs

  layout->addWidget(m_executionStack, 1);

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
    m_currentProductLabel->setText(QString("Product: %1").arg(QString::fromStdString(productName)));

    std::cout << "[RunProgramPage] Product changed to: " << productName << std::endl;

    // ✅ Load recent programs list
    loadRecentProgramsList();

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

  // ✅ Reset completion tracking when loading new program
  m_completedRecipes.clear();
  m_completedRecipes.resize(m_programManager->getRecipeCount(), false);

  // ✅ Update the program name edit field (for when user switches to edit mode)
  m_programNameEdit->setText(QString::fromStdString(m_programManager->getProgramName()));

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
  m_completedRecipes.clear();
  m_completedRecipes.resize(m_programManager->getRecipeCount(), false);
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
    m_pendingChainRecipeIndex = -1;  // Clear chain execution in auto-run mode
  }

  // ✅ Check prerequisites in single-recipe mode with dependencies enabled
  if (!autoRunMode && m_useDependenciesCheckBox->isChecked()) {
    std::vector<DependencyLink> dependencies = m_programManager->getRecipeDependencies(selectedRecipeIndex);

    if (!dependencies.empty()) {
      QStringList missingPrereqs;
      for (const auto& dep : dependencies) {
        int depIndex = dep.predecessorIndex;
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
  m_executeButton->setStyleSheet(Theme::Buttons::bezelDanger());

  // Update program status indicator
  setProgramStatus("Running", Theme::Color::success, Theme::Color::textPrimary);

  m_autoRunNextCheckBox->setEnabled(false);

  // ✅ Set ManualAlignPoint callback on SequenceEngine (thread-aware)
  m_context->sequenceEngine->SetManualAlignPointCallback(
    [this](const std::string& pointName, const std::string& message,
           bool enableX, bool enableY, bool enableZ) -> bool {
    bool result = false;

    // Must be called on UI thread
    QMetaObject::invokeMethod(this, [this, pointName, message, enableX, enableY, enableZ, &result]() {
      // Switch to manual align point view (Index 3)
      m_executionStack->setCurrentIndex(3);

      // Show the manual align point widget
      m_manualAlignPointWidget->showAlignment(
        QString::fromStdString(pointName),
        QString::fromStdString(message),
        enableX, enableY, enableZ
      );

      // Block until user responds (event loop pattern)
      QEventLoop loop;
      m_promptEventLoop = &loop;
      loop.exec();
      m_promptEventLoop = nullptr;

      result = m_manualAlignPointResponse;
    }, Qt::BlockingQueuedConnection);

    return result;
  });

  // Create and start worker
  m_programWorker = new ProgramWorker(
    m_context->sequenceEngine,
    m_programManager,
    m_processFactory,
    m_context->productPositionManager,
    m_context->motionManager,
    this
  );
  std::cout << "[RunProgramPage] Created m_programWorker=" << m_programWorker << std::endl;
  std::cout.flush();

  if (m_context->scriptEngine) {
    m_programWorker->setScriptEngine(m_context->scriptEngine);
  }

  m_programWorker->setAutoRunMode(autoRunMode);
  if (!autoRunMode) {
    m_programWorker->setStartRecipeIndex(selectedRecipeIndex);
  }

  // ✅ Set dependency execution mode
  m_programWorker->setUseDependencyGraph(m_useDependenciesCheckBox->isChecked());

  // ✅ Set loop execution mode
  bool loopEnabled = m_loopEnabledCheckBox->isChecked() && m_programManager->hasLoopRecipes();
  m_programWorker->setLoopMode(loopEnabled);
  m_isLoopExecution = loopEnabled;  // Track loop mode for UI optimization
  if (loopEnabled) {
    m_programWorker->setLoopRecipeIndices(m_programManager->getLoopRecipeIndices());
    m_programWorker->setIgnoreDependencies(m_ignoreDependenciesCheckBox->isChecked());
    m_currentLoopIteration = 0;
  }

  // Set UserPrompt callback (signal connection is in constructor)
  m_programWorker->setUserPromptCallback(
    [this](const std::string& title, const std::string& message) -> bool {
    emit showUserPrompt(QString::fromStdString(title), QString::fromStdString(message));

    QEventLoop loop;
    m_promptEventLoop = &loop;
    loop.exec();
    m_promptEventLoop = nullptr;

    return m_promptResponse;
  });

  // Set ManualAdjust callback (signal connections are in constructor)
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
      // Switch to user input view (Index 4)
      m_executionStack->setCurrentIndex(4);

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

  // PlacementRetrace callback — bridges the recipe thread to the UI.
  // Called from SystemCommandHandler on the recipe worker thread.
  // Emits showPlacementRetrace (cross-thread signal) to trigger
  // onShowPlacementRetrace on the UI thread, then blocks here in a
  // QEventLoop until the retrace widget signals completion/skip/end.
  // The event loop is quit by onPlacementRetraceSkipped or onPlacementRetraceEnded.
  // NOTE: onPlacementRetraceCompleted does NOT quit the loop — it just records
  // the save. The user must click "Continue" or "End" to advance the recipe.
  m_programWorker->setPlacementRetraceCallback(
    [this](const PlacementRetraceParams& params) -> bool {
    emit showPlacementRetrace(QString::fromStdString(params.trainingSetName),
                              params.autoStart, params.autoDetect, params.autoSaveContinue);

    QEventLoop loop;
    m_promptEventLoop = &loop;
    loop.exec();
    m_promptEventLoop = nullptr;

    return m_placementRetraceResponse;
  });

  // Edge Transfer retrace callback — mirrors the placement pattern: emit a
  // cross-thread signal to switch to the embedded widget, then block on a
  // QEventLoop until onEdgeTransferRetraceOutcome stores the response and
  // quits the loop.
  m_programWorker->setEdgeTransferRetraceCallback(
    [this](const EdgeTransferRetraceParams& params) -> EdgeTransferRetraceOutcome {
    emit showEdgeTransferRetrace(QString::fromStdString(params.trainingSetName));

    QEventLoop loop;
    m_promptEventLoop = &loop;
    loop.exec();
    m_promptEventLoop = nullptr;

    return m_edgeRetraceResponse;
  });

  // Connect signals (use Qt::QueuedConnection for cross-thread safety)
  std::cout << "[RunProgramPage] Setting up signal connections..." << std::endl;
  connect(m_programWorker, &ProgramWorker::programExecutionStarted,
    this, &RunProgramPage::onProgramExecutionStarted, Qt::QueuedConnection);
  connect(m_programWorker, &ProgramWorker::recipeStarted,
    this, &RunProgramPage::onRecipeStarted, Qt::QueuedConnection);
  connect(m_programWorker, &ProgramWorker::recipeCompleted,
    this, &RunProgramPage::onRecipeCompleted, Qt::QueuedConnection);
  connect(m_programWorker, &ProgramWorker::recipeSkipped,
    this, &RunProgramPage::onRecipeSkipped, Qt::QueuedConnection);
  connect(m_programWorker, &ProgramWorker::programExecutionCompleted,
    this, &RunProgramPage::onProgramExecutionCompleted, Qt::QueuedConnection);
  connect(m_programWorker, &ProgramWorker::programStopped,
    this, &RunProgramPage::onProgramStopped, Qt::QueuedConnection);
  connect(m_programWorker, &ProgramWorker::logMessage,
    this, &RunProgramPage::onWorkerLogMessage, Qt::QueuedConnection);

  // ✅ Connect finished() to trigger completion check AND cleanup
  connect(m_programWorker, &ProgramWorker::finished,
    this, &RunProgramPage::onWorkerFinished, Qt::QueuedConnection);

  // ✅ Connect step-level signals for progress tracking
  connect(m_programWorker, &ProgramWorker::stepBuildStarted,
    this, &RunProgramPage::onStepBuildStarted, Qt::QueuedConnection);
  connect(m_programWorker, &ProgramWorker::stepBuildCompleted,
    this, &RunProgramPage::onStepBuildCompleted, Qt::QueuedConnection);
  connect(m_programWorker, &ProgramWorker::sequenceExecutionStarted,
    this, &RunProgramPage::onSequenceExecutionStarted, Qt::QueuedConnection);
  connect(m_programWorker, &ProgramWorker::stepExecutionCompleted,
    this, &RunProgramPage::onStepExecutionCompleted, Qt::QueuedConnection);
  connect(m_programWorker, &ProgramWorker::waitProgress,
    this, &RunProgramPage::onWaitProgress, Qt::QueuedConnection);

  // ✅ Connect action-level signal for granular command tracking
  connect(m_programWorker, &ProgramWorker::actionStarted,
    this, &RunProgramPage::onActionStarted, Qt::QueuedConnection);

  // ✅ Connect loop iteration signals
  connect(m_programWorker, &ProgramWorker::loopIterationStarted,
    this, &RunProgramPage::onLoopIterationStarted, Qt::QueuedConnection);
  connect(m_programWorker, &ProgramWorker::loopIterationCompleted,
    this, &RunProgramPage::onLoopIterationCompleted, Qt::QueuedConnection);

  // ✅ Connect vision result ready for real-time display updates
  connect(m_programWorker, &ProgramWorker::visionResultReady,
    this, &RunProgramPage::onVisionResultReady, Qt::QueuedConnection);

  // Safety-critical: DirectConnection ensures immediate stop on the signaling
  // thread without event loop delay — required for E-stop responsiveness.
  if (m_context->safetyWatchManager) {
    connect(m_context->safetyWatchManager, &SafetyWatchManager::safetyTriggered,
            m_programWorker, [this](QString /*title*/, QString /*message*/, bool critical) {
      if (critical && m_programWorker) {
        std::cout << "[RunProgramPage] Critical safety flag - stopping program worker" << std::endl;
        m_programWorker->requestStop();
      }
    }, Qt::DirectConnection);
  }

  std::cout << "[RunProgramPage] All signals connected, main thread ID: " << QThread::currentThreadId() << std::endl;
  std::cout.flush();

  // Start execution
  m_programWorker->start();
  std::cout << "[RunProgramPage] ProgramWorker started" << std::endl;
  std::cout.flush();

  // Start context refresh timer for real-time context tab updates
  startContextRefreshTimer();

  // ✅ Workaround: Poll for thread completion using QTimer::singleShot chain
  // This is more reliable than regular timer for cross-thread scenarios
  auto checkCountPtr = std::make_shared<int>(0);
  auto selfPtr = QPointer<RunProgramPage>(this);  // Safe reference to this

  // Use shared_ptr to hold the function so it can capture itself
  auto checkCompletionPtr = std::make_shared<std::function<void()>>();
  *checkCompletionPtr = [this, selfPtr, checkCountPtr, checkCompletionPtr]() {
    if (!selfPtr) return;  // Widget destroyed

    (*checkCountPtr)++;

    if (m_programWorker && m_programWorker->isFinished()) {
      std::cout << "[RunProgramPage] Timer detected thread finished after " << *checkCountPtr << " checks" << std::endl;
      std::cout.flush();
      if (m_isExecuting) {
        std::cout << "[RunProgramPage] Forcing completion via timer fallback" << std::endl;
        std::cout.flush();
        // ✅ FIX: Check if worker still exists before accessing it
        if (m_programWorker) {
          // Get stored result from worker
          bool success = m_programWorker->getExecutionSuccess();
          QString message = m_programWorker->getExecutionMessage();
          if (message.isEmpty()) {
            message = "Execution completed";
          }
          onProgramExecutionCompleted(success, message);
          // ✅ FIX: DON'T delete worker here - let onWorkerFinished handle cleanup
          // This allows pending signals (like recipeCompleted with chain execution) to be processed first
        }
      }
    }
    else if (m_isExecuting && *checkCountPtr < 6000) {  // Max 10 minutes
      // Schedule next check - only if widget still exists
      if (selfPtr) {
        QTimer::singleShot(100, selfPtr.data(), [checkCompletionPtr]() {
          if (*checkCompletionPtr) {
            (*checkCompletionPtr)();
          }
        });
      }
    }
  };

  // Start the polling chain - use selfPtr for safety
  if (selfPtr) {
    QTimer::singleShot(100, selfPtr.data(), [checkCompletionPtr]() {
      if (*checkCompletionPtr) {
        (*checkCompletionPtr)();
      }
    });
  }
  std::cout << "[RunProgramPage] Completion checker timer started (singleShot chain)" << std::endl;
  std::cout.flush();

  // ✅ Force event processing to ensure timer gets scheduled
  QApplication::processEvents();
  std::cout << "[RunProgramPage] Initial processEvents() called" << std::endl;
  std::cout.flush();
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

  // ✅ Reset command tracker
  m_recipeStepNames.clear();
  m_cmdPastLabel->setText("-");
  m_cmdCurrentLabel->setText("Starting...");
  m_cmdNext1Label->setText("-");
  m_cmdNext2Label->setText("-");
  if (m_actionDetailLabel) m_actionDetailLabel->setText("Starting...");

  // Update list item to show running state
  QListWidgetItem* item = m_recipeListWidget->item(recipeIndex);
  if (item) {
    QString baseText = QString("%1    %2")
      .arg(recipeIndex + 1, 2, 10, QChar('0'))
      .arg(recipeName);

    // Show dependencies if using dependency mode
    if (m_useDependenciesCheckBox->isChecked()) {
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
  QListWidgetItem* item = m_recipeListWidget->item(recipeIndex);
  if (item) {
    const RecipeReference* recipe = m_programManager->getRecipe(recipeIndex);
    if (recipe) {
      QString baseText = QString("%1    %2")
        .arg(recipeIndex + 1, 2, 10, QChar('0'))
        .arg(QString::fromStdString(recipe->displayName));

      // Show dependencies if using dependency mode
      if (m_useDependenciesCheckBox->isChecked()) {
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

  // ✅ Mark recipe as completed in tracking vector (for prerequisite validation)
  if (success && recipeIndex >= 0 && recipeIndex < static_cast<int>(m_completedRecipes.size())) {
    m_completedRecipes[recipeIndex] = true;
  }

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
    m_executionStatusLabel->setText(QString("Recipe failed: %1").arg(message));
  }

  // ✅ Chain Execution: Find and queue dependent recipes
  // Skip verbose logging during loop execution
  if (!m_isLoopExecution) {
    std::cout << "[RunProgramPage] Chain execution check: success=" << success
              << ", chainEnabled=" << m_chainExecutionCheckBox->isChecked()
              << ", autoRunNext=" << m_autoRunNextCheckBox->isChecked() << std::endl;
    std::cout.flush();
  }

  if (success && m_chainExecutionCheckBox->isChecked() && !m_autoRunNextCheckBox->isChecked()) {
    // Get the current context from the worker for condition evaluation
    std::cout << "[RunProgramPage] Chain execution: m_programWorker=" << m_programWorker << std::endl;
    std::cout.flush();

    SequenceContext* execContext = m_programWorker ? m_programWorker->getCurrentContext() : nullptr;

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

      for (const auto& dep : deps) {
        int predIndex = dep.predecessorIndex;

        // Check if predecessor is completed
        if (predIndex < 0 || predIndex >= static_cast<int>(m_completedRecipes.size()) ||
            !m_completedRecipes[predIndex]) {
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
            if (!m_isExecuting && m_chainExecutionCheckBox->isChecked()) {
              const RecipeReference* nextRecipe = m_programManager->getRecipe(chainRecipeIndex);
              std::cout << "[RunProgramPage] Chain execution: Starting Recipe " << (chainRecipeIndex + 1)
                        << " (" << (nextRecipe ? nextRecipe->displayName : "unknown") << ")" << std::endl;
              std::cout.flush();

              // Select the recipe in the list
              m_recipeListWidget->setCurrentRow(chainRecipeIndex);

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
  QListWidgetItem* item = m_recipeListWidget->item(recipeIndex);
  if (item) {
    QString baseText = QString("%1    %2")
      .arg(recipeIndex + 1, 2, 10, QChar('0'))
      .arg(recipeName);

    // Show dependencies if using dependency mode
    if (m_useDependenciesCheckBox->isChecked()) {
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
  m_isExecuting = false;
  m_isLoopExecution = false;  // Reset loop mode flag
  m_isRetraceEditMode = false;  // ✅ Reset retrace edit mode flag
  m_retraceEditRecipeIndex = -1;

  // ✅ Reset command tracker
  m_cmdPastLabel->setText("-");
  m_cmdCurrentLabel->setText(success ? "Completed" : "Failed");
  m_cmdNext1Label->setText("-");
  m_cmdNext2Label->setText("-");
  if (m_actionDetailLabel) m_actionDetailLabel->setText(success ? "Completed" : "Failed");
  m_recipeStepNames.clear();

  std::cout << "[RunProgramPage] Updating execute button..." << std::endl;
  std::cout.flush();

  // Re-enable execute button
  updateExecuteButton();  // This sets setEnabled based on m_isExecuting flag
  m_executeButton->setText("▶ Execute Program");
  m_executeButton->setStyleSheet(Theme::Buttons::bezelSuccess());

  // Update program status indicator
  setProgramStatus("Idle", Theme::Color::bgSurface, Theme::Color::textDisabled);

  std::cout << "[RunProgramPage] Enabling auto-run checkbox..." << std::endl;
  std::cout.flush();
  m_autoRunNextCheckBox->setEnabled(true);

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
  m_executeButton->update();
  m_executeButton->repaint();
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

  m_isExecuting = false;
  m_isLoopExecution = false;  // Reset loop mode flag
  m_isRetraceEditMode = false;  // ✅ Reset retrace edit mode flag
  m_retraceEditRecipeIndex = -1;
  m_pendingChainRecipeIndex = -1;  // ✅ Clear pending chain execution

  // ✅ Reset command tracker
  m_cmdPastLabel->setText("-");
  m_cmdCurrentLabel->setText("Stopped");
  m_cmdNext1Label->setText("-");
  m_cmdNext2Label->setText("-");
  if (m_actionDetailLabel) m_actionDetailLabel->setText("Stopped");
  m_recipeStepNames.clear();

  // ✅ Update status label to "Stopped"
  setProgramStatus("Stopped", Theme::Color::danger, Theme::Color::textPrimary);

  // Re-enable execute button
  updateExecuteButton();  // This sets setEnabled based on m_isExecuting flag
  m_executeButton->setText("▶ Execute Program");
  m_executeButton->setStyleSheet(Theme::Buttons::bezelSuccess());

  m_autoRunNextCheckBox->setEnabled(true);

  // No confirmation dialog needed - stop button itself is the confirmation
}

void RunProgramPage::onWorkerFinished()
{
  std::cout << "[RunProgramPage] onWorkerFinished() called" << std::endl;
  std::cout.flush();

  // If we haven't received programExecutionCompleted yet, trigger it now
  if (m_isExecuting) {
    std::cout << "[RunProgramPage] Still executing, forcing completion from finished handler" << std::endl;
    std::cout.flush();

    // ✅ FIX: Use actual execution result from worker instead of always assuming success
    bool success = true;
    QString message = "Execution completed (via finished signal)";
    if (m_programWorker) {
      success = m_programWorker->getExecutionSuccess();
      QString workerMessage = m_programWorker->getExecutionMessage();
      if (!workerMessage.isEmpty()) {
        message = workerMessage;
      }
    }
    onProgramExecutionCompleted(success, message);
  }

  // Clean up worker after signals are processed
  if (m_programWorker) {
    // ✅ FIX: Force final context table update before deleting worker
    // This ensures the context view shows the final captured values
    std::cout << "[RunProgramPage] Final context table update before worker deletion" << std::endl;
    std::cout.flush();
    updateContextTable(true);  // Force update even though worker isn't running

    std::cout << "[RunProgramPage] Scheduling worker deletion" << std::endl;
    std::cout.flush();
    m_programWorker->deleteLater();
    m_programWorker = nullptr;
  }
}

void RunProgramPage::onWorkerLogMessage(QString message)
{
  // Display in execution log
  // Note: QPlainTextEdit with setMaximumBlockCount(2000) automatically trims oldest lines
  m_logTextEdit->appendPlainText(message);

  // Skip verbose console logging during loop execution to reduce I/O overhead
  if (!m_isLoopExecution) {
    std::cout << "[RunProgramPage] " << message.toStdString() << std::endl;
  }

  // ✅ WORKAROUND: Detect completion markers since normal signals aren't being delivered
  // Skip this heavy processing during loop execution - loop uses different completion path
  if (m_isLoopExecution) {
    return;  // Loop mode handles completion differently via loopIterationCompleted signal
  }

  // Check for both program-level and recipe-level completion markers
  bool shouldComplete = false;

  if (message.startsWith("✅ PROGRAM COMPLETED:") && m_isExecuting) {
    std::cout << "[RunProgramPage] Detected PROGRAM completion marker!" << std::endl;
    std::cout.flush();
    shouldComplete = true;
  }
  else if (message.startsWith("✅ RECIPE EXECUTION COMPLETED") && m_isExecuting) {
    std::cout << "[RunProgramPage] Detected RECIPE completion marker!" << std::endl;
    std::cout.flush();
    // Check immediately if worker is done (it should be by now)
    if (m_programWorker) {
      std::cout << "[RunProgramPage] Checking worker state: running=" << m_programWorker->isRunning()
                << ", finished=" << m_programWorker->isFinished() << std::endl;
      std::cout.flush();
      // Wait a tiny bit for thread to fully finish
      if (m_programWorker->isRunning()) {
        std::cout << "[RunProgramPage] Worker still running, waiting up to 100ms..." << std::endl;
        std::cout.flush();
        m_programWorker->wait(100);  // Wait up to 100ms
      }
      std::cout << "[RunProgramPage] After wait: running=" << m_programWorker->isRunning()
                << ", finished=" << m_programWorker->isFinished() << std::endl;
      std::cout.flush();
      if (m_programWorker && m_programWorker->isFinished()) {
        std::cout << "[RunProgramPage] Worker finished after recipe completion, forcing UI update" << std::endl;
        std::cout.flush();
        // ✅ FIX: Check worker still exists before accessing
        if (m_programWorker) {
          bool success = m_programWorker->getExecutionSuccess();
          QString actualMessage = m_programWorker->getExecutionMessage();
          if (actualMessage.isEmpty()) {
            actualMessage = "Execution completed";
          }
          onProgramExecutionCompleted(success, actualMessage);
          // ✅ FIX: DON'T delete worker here - let onWorkerFinished handle cleanup
          // This allows pending signals (like recipeCompleted with chain execution) to be processed first
          // The worker will be cleaned up when the thread finishes via onWorkerFinished
        }
      }
    } else {
      std::cout << "[RunProgramPage] m_programWorker is null!" << std::endl;
      std::cout.flush();
    }
  }
  else if (message.contains("Context saved to:") && m_isExecuting) {
    // Fallback: detect end of context summary (this is the last log from RecipeWorker)
    std::cout << "[RunProgramPage] Detected context save - execution finishing" << std::endl;
    std::cout.flush();
    // Process events and check immediately
    QApplication::processEvents();
    if (m_programWorker) {
      if (m_programWorker->isRunning()) {
        m_programWorker->wait(500);  // Wait up to 500ms
      }
      if (m_programWorker && m_programWorker->isFinished()) {
        std::cout << "[RunProgramPage] Worker finished after context save, forcing UI update" << std::endl;
        std::cout.flush();
        // ✅ FIX: Check worker still exists before accessing
        if (m_programWorker) {
          bool success = m_programWorker->getExecutionSuccess();
          QString actualMessage = m_programWorker->getExecutionMessage();
          if (actualMessage.isEmpty()) {
            actualMessage = "Execution completed";
          }
          onProgramExecutionCompleted(success, actualMessage);
          // ✅ FIX: DON'T delete worker here - let onWorkerFinished handle cleanup
        }
      }
    }
  }

  if (shouldComplete) {
    // Get result from worker if available
    bool success = true;
    QString actualMessage = "Execution completed";
    if (m_programWorker) {
      success = m_programWorker->getExecutionSuccess();
      actualMessage = m_programWorker->getExecutionMessage();
    }

    // Force completion handler
    std::cout << "[RunProgramPage] Forcing completion via log message workaround" << std::endl;
    std::cout.flush();
    onProgramExecutionCompleted(success, actualMessage);
    // ✅ FIX: DON'T delete worker here - let onWorkerFinished handle cleanup
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
    m_progressBar->setValue(progress);
  }
}

void RunProgramPage::onActionStarted(int /*recipeIndex*/, QString current, QString next, QString afterNext)
{
  // Update the command tracker with the currently executing action and look-ahead
  // The current step (from onStepBuildStarted) becomes the previous
  QString prevCurrent = m_cmdCurrentLabel->text();
  if (!prevCurrent.isEmpty() && prevCurrent != "Starting..." && prevCurrent != "-" && prevCurrent != "Completed") {
    m_cmdPastLabel->setText(prevCurrent);
  }

  // Update current to show the executing action
  m_cmdCurrentLabel->setText(current);

  // Show next and after-next from look-ahead
  m_cmdNext1Label->setText(next.isEmpty() ? "-" : next);
  m_cmdNext2Label->setText(afterNext.isEmpty() ? "-" : afterNext);

  // Mirror the current action into the visible single-row description.
  // `current` here is already a human-readable string built by
  // ActionInfo::toHumanReadable() in RecipeWorker.
  if (m_actionDetailLabel) {
    m_actionDetailLabel->setText(current.isEmpty() ? "-" : current);
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

void RunProgramPage::onManualAlignPointResponse(bool completed)
{
  // Store the response
  m_manualAlignPointResponse = completed;

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

  // Subscribe to new channel
  std::string channelStd = channel.toStdString();
  m_dataStore->Subscribe(channelStd,
    [this, channel](const std::string& /*channelId*/, float value) {
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
    m_loopIndicator->setVisible(m_loopEnabledCheckBox->isChecked() && m_programManager->hasLoopRecipes());

    // Refresh the recipe list to show loop indicators
    updateRecipeList();
  }
}

void RunProgramPage::updateLoopStatusLabel()
{
  if (!m_programManager) {
    m_loopStatusLabel->setText("No program loaded");
    m_loopStatusLabel->setStyleSheet("color: #888888; font-size: 11px; padding: 2px 5px;");
    return;
  }

  std::vector<int> loopRecipes = m_programManager->getLoopRecipeIndices();

  if (loopRecipes.empty()) {
    m_loopStatusLabel->setText("No recipes configured for loop");
    m_loopStatusLabel->setStyleSheet("color: #888888; font-size: 11px; padding: 2px 5px;");
  } else if (loopRecipes.size() == 1) {
    m_loopStatusLabel->setText("1 recipe in loop");
    m_loopStatusLabel->setStyleSheet("color: #4ec9b0; font-size: 11px; padding: 2px 5px;");
  } else {
    m_loopStatusLabel->setText(QString("%1 recipes in loop").arg(loopRecipes.size()));
    m_loopStatusLabel->setStyleSheet("color: #4ec9b0; font-size: 11px; padding: 2px 5px;");
  }
}

void RunProgramPage::onLoopIterationStarted(int iteration)
{
  m_currentLoopIteration = iteration;

  // Update the execution status to show loop iteration
  m_executionStatusLabel->setText(QString("Loop Iteration %1").arg(iteration));

  // Update program status indicator
  setProgramStatus(QString("Loop #%1").arg(iteration),
                   Theme::Color::accentSoft, Theme::Color::bgDeep);

  // Reset progress bar for new iteration
  m_progressBar->setValue(0);
}

void RunProgramPage::onLoopIterationCompleted(int iteration)
{
  m_currentLoopIteration = iteration;

  // Log iteration completion
  std::cout << "[RunProgramPage] Loop iteration " << iteration << " completed" << std::endl;
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
  QListWidgetItem* item = m_recipeListWidget->itemAt(pos);
  if (!item) return;

  int row = m_recipeListWidget->row(item);
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

  QAction* selectedAction = contextMenu.exec(m_recipeListWidget->viewport()->mapToGlobal(pos));

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
      m_executionStack->setCurrentIndex(0);  // Ensure tabs (not prompt) are visible
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

  // Confirm with user
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

  if (result != QMessageBox::Yes) {
    return;
  }

  // Switch to execution view
  showExecutionWidget();

  // Clear log
  m_logTextEdit->clear();
  m_logTextEdit->appendPlainText(QString("=== Retrace Edit: %1 ===")
    .arg(QString::fromStdString(recipe->displayName)));

  // Set up retrace edit state
  m_isRetraceEditMode = true;
  m_retraceEditRecipeIndex = recipeIndex;
  m_isExecuting = true;
  m_currentRecipeIndex = recipeIndex;
  updateExecuteButton();

  // Create worker
  m_programWorker = new ProgramWorker(
    m_context->sequenceEngine,
    m_programManager,
    m_processFactory,
    m_context->productPositionManager,
    m_context->motionManager,
    this
  );

  // Configure for retrace edit mode - single recipe only
  m_programWorker->setAutoRunMode(false);
  m_programWorker->setStartRecipeIndex(recipeIndex);
  m_programWorker->setRetraceEditMode(true);

  // Set up retrace edit callback - this runs in worker thread, so we use signals
  // ✅ FIX: Use shared_ptr to avoid dangling reference when function returns
  auto positionCounter = std::make_shared<int>(0);
  m_programWorker->setRetraceEditCallback(
    [this, positionCounter](
      const std::string& deviceName,
      const std::string& positionName,
      double origX, double origY, double origZ,
      double origU, double origV, double origW,
      double currX, double currY, double currZ,
      double currU, double currV, double currW,
      bool& saveRequested) -> bool {

      (*positionCounter)++;
      int currentPosition = *positionCounter;

      // Show the retrace edit widget (must be done on GUI thread)
      QMetaObject::invokeMethod(this, [this, deviceName, positionName,
        origX, origY, origZ, origU, origV, origW,
        currX, currY, currZ, currU, currV, currW, currentPosition]() {

        m_executionStack->setCurrentIndex(5);  // Index 5 = retrace edit widget
        m_retraceEditWidget->showRetraceEdit(
          QString::fromStdString(deviceName),
          QString::fromStdString(positionName),
          origX, origY, origZ, origU, origV, origW,
          currX, currY, currZ, currU, currV, currW,
          currentPosition, 0);  // 0 = unknown total

      }, Qt::BlockingQueuedConnection);

      // Wait for user response using event loop
      QEventLoop loop;
      m_retraceEditContinue = false;
      m_retraceEditSave = false;

      connect(m_retraceEditWidget, &RetraceEditWidget::responseReceived, &loop,
        [&loop, this](bool continueExec, bool save) {
          m_retraceEditContinue = continueExec;
          m_retraceEditSave = save;
          loop.quit();
        });

      loop.exec();

      // Return results
      saveRequested = m_retraceEditSave;

      // Switch back to log view
      QMetaObject::invokeMethod(this, [this]() {
        m_executionStack->setCurrentIndex(0);
      }, Qt::QueuedConnection);

      return m_retraceEditContinue;
    }
  );

  // Connect signals
  connect(m_programWorker, &ProgramWorker::logMessage,
    this, &RunProgramPage::onWorkerLogMessage);
  connect(m_programWorker, &ProgramWorker::recipeStarted,
    this, &RunProgramPage::onRecipeStarted);
  connect(m_programWorker, &ProgramWorker::recipeCompleted,
    this, &RunProgramPage::onRecipeCompleted);
  connect(m_programWorker, &ProgramWorker::programExecutionCompleted,
    this, [this](bool success, QString message) {
      if (m_isRetraceEditMode) {
        // Show retrace summary
        showRetraceEditSummary(
          m_programWorker->getRetracePositionsVisited(),
          m_programWorker->getRetracePositionsSaved());
      }
      onProgramExecutionCompleted(success, message);
    });
  connect(m_programWorker, &ProgramWorker::programStopped,
    this, &RunProgramPage::onProgramStopped);
  connect(m_programWorker, &QThread::finished,
    this, &RunProgramPage::onWorkerFinished);

  // Start execution
  m_programWorker->start();
}

void RunProgramPage::onRetraceEditResponse(bool continueExecution, bool saveRequested)
{
  m_retraceEditContinue = continueExecution;
  m_retraceEditSave = saveRequested;

  // Log the action
  if (saveRequested) {
    m_logTextEdit->appendPlainText("  [SAVE] Position saved");
  } else if (continueExecution) {
    m_logTextEdit->appendPlainText("  [SKIP] Position skipped");
  } else {
    m_logTextEdit->appendPlainText("  [STOP] Retrace stopped by user");
  }
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

  m_isRetraceEditMode = false;
  m_retraceEditRecipeIndex = -1;
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
  m_executionStack->setCurrentIndex(6);

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

  m_executionStack->setCurrentIndex(0);

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
  m_executionStack->setCurrentIndex(0);

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
  m_executionStack->setCurrentIndex(7);

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
  m_executionStack->setCurrentIndex(0);
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
  m_programNameLabel->setText(programName);

  // Update execution title label with program file name
  if (!programName.isEmpty() && programName != "Untitled Program") {
    m_executionTitleLabel->setText("Program file: " + programName);
  } else {
    m_executionTitleLabel->setText("Program Execution");
  }

  // Display program file path
  if (!m_currentProgramPath.isEmpty()) {
    m_programPathLabel->setText("Path: " + m_currentProgramPath);
  } else {
    m_programPathLabel->setText("");
  }

  // Update slot system UI (program load brought in a new slot list).
  if (m_runSlotWidget)  m_runSlotWidget->Reload();
  if (m_editSlotWidget) m_editSlotWidget->Reload();
  updateSlotInfo();
}

void RunProgramPage::updateRecipeList()
{
  // Run-side recipe list (the QListWidget on the left).
  m_recipeListWidget->clear();

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
      m_recipeListWidget->addItem(item);
      recipeNumber++;
      continue;
    }

    QListWidgetItem* item = new QListWidgetItem(displayText);
    if (recipe->includeInLoop) {
      item->setForeground(QColor(78, 201, 176));  // Teal color for loop recipes
    }
    m_recipeListWidget->addItem(item);
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
  m_executeButton->setEnabled(hasRecipes && !m_isExecuting);
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
  if (!m_recentProgramsList) return;

  m_recentProgramsList->clear();

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

    m_recentProgramsList->addItem(item);
  }

  std::cout << "[RunProgramPage] Loaded " << m_recentProgramsList->count()
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
  // Make sure the viewer has the latest worker pointer before it starts
  // polling getCurrentContext() on it.
  m_contextViewer->SetWorker(m_programWorker);
  m_contextViewer->StartLivePolling(m_isLoopExecution ? 1000 : 500);
}

void RunProgramPage::stopContextRefreshTimer()
{
  if (m_contextViewer) m_contextViewer->StopLivePolling();
}

void RunProgramPage::updateContextTable(bool forceUpdate)
{
  if (!m_contextViewer) return;
  // Hand the worker pointer to the viewer in case it changed since the
  // last refresh; SetWorker is cheap (just stores a pointer).
  m_contextViewer->SetWorker(m_programWorker);
  if (forceUpdate) m_contextViewer->ForceRefresh();
  // Live polling timer fires its own refresh; we don't need to do it here.
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

  m_currentRecipeLabel->setStyleSheet(
    QString("font-size: 16px; font-weight: bold; color: %1;")
      .arg(QLatin1String(Theme::Color::primary)));

  m_actionDetailLabel->setStyleSheet(
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

  m_progressBar->setStyleSheet(
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

  m_backButton->setStyleSheet(Theme::Buttons::bezelPrimary());
  m_executeButton->setStyleSheet(m_isExecuting ? Theme::Buttons::bezelDanger() : Theme::Buttons::bezelSuccess());
}