// RunProgramPage.cpp
#include "RunProgramPage.h"
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
#include "RollCorrectionWidget.h"
#include "ScanExecutor.h"
#include "motions/unified_motion_manager.h"
#include "CameraClickToMoveWidget.h"
#include "RetraceEditWidget.h"
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
#include <QJsonDocument>
#include <QJsonObject>
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
  , m_scanExecutor(nullptr)
  , m_dataStore(context->dataStore)
  , m_currentSubscribedChannel("")
  , m_currentRecipeTotalSteps(0)      // ✅ Initialize step tracking
  , m_currentRecipeCompletedSteps(0)  // ✅ Initialize step tracking
  , m_visionResultGroup(nullptr)       // ✅ Vision result group
  , m_visionResultImageLabel(nullptr)  // ✅ Vision result image
  , m_currentContext(nullptr)          // ✅ Execution context
  , m_visionNavWidget(nullptr)         // Vision nav container
  , m_visionPrevButton(nullptr)        // Vision prev button
  , m_visionNextButton(nullptr)        // Vision next button
  , m_visionIndexLabel(nullptr)        // Vision index label
  , m_visionHistoryViewIndex(-1)       // -1 = show latest
  , m_contextTabWidget(nullptr)        // Context tab container
  , m_contextTree(nullptr)             // Context variables tree
  , m_contextFilterEdit(nullptr)       // Context filter textbox
  , m_contextStatusLabel(nullptr)      // Context status label
  , m_contextRefreshTimer(nullptr)     // Context refresh timer
  , m_contextCopyButton(nullptr)       // Copy to clipboard button
  , m_contextExportButton(nullptr)     // Export to CSV button
  , m_retraceEditWidget(nullptr)       // ✅ Retrace edit widget
  , m_retraceEditContinue(false)       // ✅ Retrace edit continue status
  , m_retraceEditSave(false)           // ✅ Retrace edit save status
  , m_isRetraceEditMode(false)         // ✅ Retrace edit mode flag
  , m_retraceEditRecipeIndex(-1)       // ✅ Retrace edit recipe index
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
  if (m_context->safetyWatchManager) {
    m_scanExecutor->setSafetyWatchManager(m_context->safetyWatchManager);
  }

  setupUI();

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
  // Stop CV auto-refresh timer
  if (m_cvAutoRefreshTimer) {
    m_cvAutoRefreshTimer->stop();
  }

  // Stop context refresh timer
  stopContextRefreshTimer();

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

  // Progress bar moved to right panel (execution widget) for better visibility

  executeTabLayout->addWidget(executeGroup, 0);

  // === SLOT SELECTOR (above recipe list) ===
  QGroupBox* slotGroup = new QGroupBox("Active Slot");
  slotGroup->setStyleSheet(
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
  QVBoxLayout* slotLayout = new QVBoxLayout(slotGroup);
  slotLayout->setSpacing(5);

  m_slotSelector = new QComboBox();
  m_slotSelector->setMinimumHeight(30);
  m_slotSelector->setStyleSheet(
    "QComboBox { "
    "  background-color: #3c3c3c; "
    "  color: #d4d4d4; "
    "  border: 1px solid #555555; "
    "  border-radius: 3px; "
    "  padding: 5px 10px; "
    "  font-size: 13px; "
    "} "
    "QComboBox:hover { "
    "  border-color: #0e639c; "
    "} "
    "QComboBox::drop-down { "
    "  border: none; "
    "  width: 25px; "
    "} "
    "QComboBox::down-arrow { "
    "  image: none; "
    "  border-left: 5px solid transparent; "
    "  border-right: 5px solid transparent; "
    "  border-top: 5px solid #d4d4d4; "
    "} "
    "QComboBox QAbstractItemView { "
    "  background-color: #2d2d30; "
    "  color: #d4d4d4; "
    "  selection-background-color: #0e639c; "
    "  selection-color: white; "
    "  border: 1px solid #555555; "
    "}"
  );
  connect(m_slotSelector, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &RunProgramPage::onSlotSelectionChanged);
  slotLayout->addWidget(m_slotSelector);

  m_slotInfoLabel = new QLabel("0 recipes in this slot");
  m_slotInfoLabel->setStyleSheet("color: #888888; font-size: 11px; font-style: italic;");
  slotLayout->addWidget(m_slotInfoLabel);

  executeTabLayout->addWidget(slotGroup, 0);

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

  // === SLOT MANAGEMENT BAR ===
  QGroupBox* slotManageGroup = new QGroupBox("Recipe Slot");
  slotManageGroup->setStyleSheet(
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
  QHBoxLayout* slotManageLayout = new QHBoxLayout(slotManageGroup);
  slotManageLayout->setSpacing(5);

  m_editSlotSelector = new QComboBox();
  m_editSlotSelector->setMinimumHeight(30);
  m_editSlotSelector->setMinimumWidth(150);
  m_editSlotSelector->setStyleSheet(
    "QComboBox { "
    "  background-color: #3c3c3c; "
    "  color: #d4d4d4; "
    "  border: 1px solid #555555; "
    "  border-radius: 3px; "
    "  padding: 5px 10px; "
    "  font-size: 13px; "
    "} "
    "QComboBox:hover { "
    "  border-color: #0e639c; "
    "} "
    "QComboBox::drop-down { "
    "  border: none; "
    "  width: 25px; "
    "} "
    "QComboBox::down-arrow { "
    "  image: none; "
    "  border-left: 5px solid transparent; "
    "  border-right: 5px solid transparent; "
    "  border-top: 5px solid #d4d4d4; "
    "} "
    "QComboBox QAbstractItemView { "
    "  background-color: #2d2d30; "
    "  color: #d4d4d4; "
    "  selection-background-color: #0e639c; "
    "  selection-color: white; "
    "  border: 1px solid #555555; "
    "}"
  );
  connect(m_editSlotSelector, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &RunProgramPage::onEditSlotSelectionChanged);
  slotManageLayout->addWidget(m_editSlotSelector, 1);

  // Slot management button style
  QString slotButtonStyle =
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5a5a5a, stop:1 #3c3c3c); "
    "  color: #d4d4d4; "
    "  padding: 5px 10px; "
    "  font-size: 12px; "
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
    "} "
    "QPushButton:disabled { "
    "  background-color: #3c3c3c; "
    "  color: #666666; "
    "}";

  m_renameSlotButton = new QPushButton("Rename");
  m_renameSlotButton->setMinimumHeight(30);
  m_renameSlotButton->setStyleSheet(slotButtonStyle);
  m_renameSlotButton->setToolTip("Rename the current slot");
  connect(m_renameSlotButton, &QPushButton::clicked, this, &RunProgramPage::onRenameSlot);
  slotManageLayout->addWidget(m_renameSlotButton);

  m_addSlotButton = new QPushButton("+ Add");
  m_addSlotButton->setMinimumHeight(30);
  m_addSlotButton->setStyleSheet(slotButtonStyle);
  m_addSlotButton->setToolTip("Add a new slot (max 5)");
  connect(m_addSlotButton, &QPushButton::clicked, this, &RunProgramPage::onAddSlot);
  slotManageLayout->addWidget(m_addSlotButton);

  m_duplicateSlotButton = new QPushButton("Copy");
  m_duplicateSlotButton->setMinimumHeight(30);
  m_duplicateSlotButton->setStyleSheet(slotButtonStyle);
  m_duplicateSlotButton->setToolTip("Duplicate the current slot");
  connect(m_duplicateSlotButton, &QPushButton::clicked, this, &RunProgramPage::onDuplicateSlot);
  slotManageLayout->addWidget(m_duplicateSlotButton);

  m_deleteSlotButton = new QPushButton("Delete");
  m_deleteSlotButton->setMinimumHeight(30);
  m_deleteSlotButton->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #c0392b, stop:1 #a93226); "
    "  color: white; "
    "  padding: 5px 10px; "
    "  font-size: 12px; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.4); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.4); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #d0493b, stop:1 #c0392b); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #8e2820; "
    "} "
    "QPushButton:disabled { "
    "  background-color: #5a5a5a; "
    "  color: #888888; "
    "}"
  );
  m_deleteSlotButton->setToolTip("Delete the current slot (cannot delete last slot)");
  connect(m_deleteSlotButton, &QPushButton::clicked, this, &RunProgramPage::onDeleteSlot);
  slotManageLayout->addWidget(m_deleteSlotButton);

  layout->addWidget(slotManageGroup);

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

  // Enable context menu for right-click rename
  m_editRecipeTable->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_editRecipeTable, &QTableWidget::customContextMenuRequested,
          this, &RunProgramPage::onRecipeTableContextMenu);

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

  // === HEADER SECTION ===
  // Title row: Program title on left, status on right
  QHBoxLayout* titleRow = new QHBoxLayout();

  m_executionTitleLabel = new QLabel("Program Execution");
  m_executionTitleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #ffffff;");
  titleRow->addWidget(m_executionTitleLabel);

  titleRow->addStretch();

  // Program status indicator (Idle/Running/Stopped)
  m_programStatusLabel = new QLabel("Idle");
  m_programStatusLabel->setStyleSheet(
    "QLabel { "
    "  background-color: #3f3f46; "
    "  color: #888888; "
    "  padding: 4px 12px; "
    "  border-radius: 4px; "
    "  font-weight: bold; "
    "  font-size: 12px; "
    "}"
  );
  titleRow->addWidget(m_programStatusLabel);

  layout->addLayout(titleRow);

  // Status row
  m_executionStatusLabel = new QLabel("Ready to execute");
  m_executionStatusLabel->setStyleSheet("font-size: 14px; color: #cccccc;");
  layout->addWidget(m_executionStatusLabel);

  // Current recipe
  m_currentRecipeLabel = new QLabel("Current Recipe: None");
  m_currentRecipeLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #0e639c;");
  layout->addWidget(m_currentRecipeLabel);

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
    "  color: #d4d4d4; "
    "} "
    "QProgressBar::chunk { "
    "  background-color: #4CAF50; "
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
  createContextTab();
  m_executionTabWidget->addTab(m_contextTabWidget, "Context");

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

  // Vision result image display
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
  m_visionResultImageLabel->setMinimumHeight(300);
  m_visionResultImageLabel->setStyleSheet(
    "QLabel { "
    "  background-color: #0f0f0f; "
    "  border: 1px solid #3f3f46; "
    "  color: #808080; "
    "  font-style: italic; "
    "}"
  );
  visionLayout->addWidget(m_visionResultImageLabel, 1);  // Give image area stretch

  // Vision history navigation controls
  m_visionNavWidget = new QWidget();
  QHBoxLayout* navLayout = new QHBoxLayout(m_visionNavWidget);
  navLayout->setContentsMargins(0, 5, 0, 0);
  navLayout->setSpacing(10);

  m_visionPrevButton = new QPushButton("◀ Prev");
  m_visionPrevButton->setFixedWidth(70);
  m_visionPrevButton->setEnabled(false);
  m_visionPrevButton->setStyleSheet(
    "QPushButton { background-color: #2d2d30; color: #cccccc; border: 1px solid #3f3f46; padding: 4px 8px; }"
    "QPushButton:hover { background-color: #3d3d40; }"
    "QPushButton:disabled { color: #606060; background-color: #1e1e1e; }"
  );
  connect(m_visionPrevButton, &QPushButton::clicked, this, &RunProgramPage::onVisionPrevious);

  m_visionIndexLabel = new QLabel("- / -");
  m_visionIndexLabel->setAlignment(Qt::AlignCenter);
  m_visionIndexLabel->setMinimumWidth(60);
  m_visionIndexLabel->setStyleSheet("QLabel { color: #808080; }");

  m_visionNextButton = new QPushButton("Next ▶");
  m_visionNextButton->setFixedWidth(70);
  m_visionNextButton->setEnabled(false);
  m_visionNextButton->setStyleSheet(
    "QPushButton { background-color: #2d2d30; color: #cccccc; border: 1px solid #3f3f46; padding: 4px 8px; }"
    "QPushButton:hover { background-color: #3d3d40; }"
    "QPushButton:disabled { color: #606060; background-color: #1e1e1e; }"
  );
  connect(m_visionNextButton, &QPushButton::clicked, this, &RunProgramPage::onVisionNext);

  navLayout->addStretch();
  navLayout->addWidget(m_visionPrevButton);
  navLayout->addWidget(m_visionIndexLabel);
  navLayout->addWidget(m_visionNextButton);
  navLayout->addStretch();

  visionLayout->addWidget(m_visionNavWidget);

  visionTabLayout->addWidget(m_visionResultGroup, 1);  // Give vision group stretch

  m_executionTabWidget->addTab(visionTab, "Vision");

  // === ROLL CORRECTION TAB (Index 5) ===
  QWidget* rollCorrectionTab = new QWidget();
  QVBoxLayout* rollTabLayout = new QVBoxLayout(rollCorrectionTab);
  rollTabLayout->setContentsMargins(10, 10, 10, 10);
  rollTabLayout->setSpacing(10);

  m_rollCorrectionWidget = new RollCorrectionWidget();
  rollTabLayout->addWidget(m_rollCorrectionWidget);
  rollTabLayout->addStretch();

  m_executionTabWidget->addTab(rollCorrectionTab, "Roll Correction");

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
  connect(m_rollCorrectionWidget, &RollCorrectionWidget::clearRequested,
    this, &RunProgramPage::onRollCorrectionClearRequested);

  // Connect scan widget to this page
  connect(m_scanWidget, &ScanControlWidget::scanRequested,
    this, &RunProgramPage::onScanRequested);
  connect(m_scanWidget, &ScanControlWidget::rotationScanRequested,
    this, &RunProgramPage::onRotationScanRequested);
  connect(m_scanWidget, &ScanControlWidget::stopRequested,
    this, &RunProgramPage::onStopScanRequested);
  connect(m_scanWidget, &ScanControlWidget::channelChanged,
    this, &RunProgramPage::onChannelChanged);

  // Trigger subscription for initial channel
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
// SLOT SYSTEM HANDLERS
// ============================================================================

void RunProgramPage::onSlotSelectionChanged(int index)
{
  if (index < 0 || !m_programManager) return;

  m_programManager->setActiveSlot(index);
  updateRecipeList();
  updateSlotInfo();

  // Sync edit slot selector if it exists
  if (m_editSlotSelector) {
    m_editSlotSelector->blockSignals(true);
    m_editSlotSelector->setCurrentIndex(index);
    m_editSlotSelector->blockSignals(false);
  }

  // Reset completion tracking when slot changes
  m_completedRecipes.clear();
  m_completedRecipes.resize(m_programManager->getRecipeCount(), false);

  std::cout << "[RunProgramPage] Switched to slot: "
            << m_programManager->getSlotName(index) << std::endl;
}

void RunProgramPage::onEditSlotSelectionChanged(int index)
{
  if (index < 0 || !m_programManager) return;

  m_programManager->setActiveSlot(index);
  updateRecipeList();
  updateSlotInfo();

  // Sync execute tab slot selector
  if (m_slotSelector) {
    m_slotSelector->blockSignals(true);
    m_slotSelector->setCurrentIndex(index);
    m_slotSelector->blockSignals(false);
  }

  // Refresh the edit table
  if (m_editRecipeTable) {
    onEditRecipeSelectionChanged();  // This will refresh the table content
  }

  std::cout << "[RunProgramPage] Edit slot switched to: "
            << m_programManager->getSlotName(index) << std::endl;
}

void RunProgramPage::onRenameSlot()
{
  if (!m_programManager) return;

  int currentIndex = m_programManager->getActiveSlotIndex();
  QString currentName = QString::fromStdString(m_programManager->getSlotName(currentIndex));

  bool ok;
  QString newName = QInputDialog::getText(this, "Rename Slot",
    "Enter new name for slot:", QLineEdit::Normal, currentName, &ok);

  if (ok && !newName.isEmpty() && newName != currentName) {
    m_programManager->renameSlot(currentIndex, newName.toStdString());
    updateSlotSelectors();
  }
}

void RunProgramPage::onAddSlot()
{
  if (!m_programManager) return;

  if (m_programManager->getSlotCount() >= MAX_RECIPE_SLOTS) {
    QMessageBox::warning(this, "Maximum Slots Reached",
      QString("Cannot add more than %1 slots.").arg(MAX_RECIPE_SLOTS));
    return;
  }

  bool ok;
  QString name = QInputDialog::getText(this, "Add Slot",
    "Enter name for new slot:", QLineEdit::Normal,
    QString("Slot %1").arg(m_programManager->getSlotCount() + 1), &ok);

  if (ok && !name.isEmpty()) {
    int newIndex = m_programManager->createSlot(name.toStdString());
    if (newIndex >= 0) {
      updateSlotSelectors();
      // Switch to the new slot
      m_programManager->setActiveSlot(newIndex);
      if (m_slotSelector) m_slotSelector->setCurrentIndex(newIndex);
      if (m_editSlotSelector) m_editSlotSelector->setCurrentIndex(newIndex);
      updateRecipeList();
      updateSlotInfo();
    }
  }
}

void RunProgramPage::onDeleteSlot()
{
  if (!m_programManager) return;

  if (m_programManager->getSlotCount() <= 1) {
    QMessageBox::warning(this, "Cannot Delete",
      "Cannot delete the last slot. A program must have at least one slot.");
    return;
  }

  int currentIndex = m_programManager->getActiveSlotIndex();
  QString slotName = QString::fromStdString(m_programManager->getSlotName(currentIndex));

  QMessageBox::StandardButton reply = QMessageBox::question(this, "Delete Slot",
    QString("Delete slot '%1' and all its recipes?\n\nThis cannot be undone.").arg(slotName),
    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

  if (reply == QMessageBox::Yes) {
    if (m_programManager->deleteSlot(currentIndex)) {
      updateSlotSelectors();
      // Update active slot index in selectors
      int newIndex = m_programManager->getActiveSlotIndex();
      if (m_slotSelector) m_slotSelector->setCurrentIndex(newIndex);
      if (m_editSlotSelector) m_editSlotSelector->setCurrentIndex(newIndex);
      updateRecipeList();
      updateSlotInfo();
    }
  }
}

void RunProgramPage::onDuplicateSlot()
{
  if (!m_programManager) return;

  if (m_programManager->getSlotCount() >= MAX_RECIPE_SLOTS) {
    QMessageBox::warning(this, "Maximum Slots Reached",
      QString("Cannot add more than %1 slots.").arg(MAX_RECIPE_SLOTS));
    return;
  }

  int currentIndex = m_programManager->getActiveSlotIndex();
  QString currentName = QString::fromStdString(m_programManager->getSlotName(currentIndex));

  bool ok;
  QString name = QInputDialog::getText(this, "Duplicate Slot",
    "Enter name for the copy:", QLineEdit::Normal,
    currentName + " (Copy)", &ok);

  if (ok && !name.isEmpty()) {
    int newIndex = m_programManager->duplicateSlot(currentIndex, name.toStdString());
    if (newIndex >= 0) {
      updateSlotSelectors();
      // Switch to the new slot
      m_programManager->setActiveSlot(newIndex);
      if (m_slotSelector) m_slotSelector->setCurrentIndex(newIndex);
      if (m_editSlotSelector) m_editSlotSelector->setCurrentIndex(newIndex);
      updateRecipeList();
      updateSlotInfo();
    }
  }
}

void RunProgramPage::updateSlotSelectors()
{
  if (!m_programManager) return;

  auto names = m_programManager->getSlotNames();
  int activeIndex = m_programManager->getActiveSlotIndex();

  // Update both selectors
  for (QComboBox* selector : {m_slotSelector, m_editSlotSelector}) {
    if (!selector) continue;
    selector->blockSignals(true);
    selector->clear();
    for (const auto& name : names) {
      selector->addItem(QString::fromStdString(name));
    }
    selector->setCurrentIndex(activeIndex);
    selector->blockSignals(false);
  }

  // Update button states
  if (m_deleteSlotButton) {
    m_deleteSlotButton->setEnabled(names.size() > 1);
  }
  if (m_addSlotButton) {
    m_addSlotButton->setEnabled(static_cast<int>(names.size()) < MAX_RECIPE_SLOTS);
  }
  if (m_duplicateSlotButton) {
    m_duplicateSlotButton->setEnabled(static_cast<int>(names.size()) < MAX_RECIPE_SLOTS);
  }
}

void RunProgramPage::updateSlotInfo()
{
  if (!m_slotInfoLabel || !m_programManager) return;

  int recipeCount = m_programManager->getRecipeCount();
  QString text = QString("%1 recipe%2 in this slot")
    .arg(recipeCount)
    .arg(recipeCount == 1 ? "" : "s");
  m_slotInfoLabel->setText(text);
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
  m_executeButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #f44336; "
    "  color: white; "
    "  font-size: 16px; "
    "  font-weight: bold; "
    "}");

  // Update program status indicator
  m_programStatusLabel->setText("Running");
  m_programStatusLabel->setStyleSheet(
    "QLabel { "
    "  background-color: #4caf50; "
    "  color: white; "
    "  padding: 4px 12px; "
    "  border-radius: 4px; "
    "  font-weight: bold; "
    "  font-size: 12px; "
    "}"
  );

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
  m_recipeStepNames.clear();

  std::cout << "[RunProgramPage] Updating execute button..." << std::endl;
  std::cout.flush();

  // Re-enable execute button
  updateExecuteButton();  // This sets setEnabled based on m_isExecuting flag
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

  // Update program status indicator
  m_programStatusLabel->setText("Idle");
  m_programStatusLabel->setStyleSheet(
    "QLabel { "
    "  background-color: #3f3f46; "
    "  color: #888888; "
    "  padding: 4px 12px; "
    "  border-radius: 4px; "
    "  font-weight: bold; "
    "  font-size: 12px; "
    "}"
  );

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
  m_recipeStepNames.clear();

  // ✅ Update status label to "Stopped"
  m_programStatusLabel->setText("Stopped");
  m_programStatusLabel->setStyleSheet(
    "QLabel { "
    "  background-color: #ef4444; "
    "  color: white; "
    "  padding: 4px 12px; "
    "  border-radius: 4px; "
    "  font-weight: bold; "
    "  font-size: 12px; "
    "}"
  );

  // Re-enable execute button
  updateExecuteButton();  // This sets setEnabled based on m_isExecuting flag
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

// SCANNING INTEGRATION (Refactored)
// ============================================================================

void RunProgramPage::onScanRequested(QString deviceName, ScanControlWidget::ScanMode mode, QString channel)
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

  // Map widget ScanMode to executor ScanMode
  ScanMode execMode;
  switch (mode) {
    case ScanControlWidget::ScanMode::Coarse:    execMode = ScanMode::Coarse;    break;
    case ScanControlWidget::ScanMode::Fine:       execMode = ScanMode::Fine;      break;
    case ScanControlWidget::ScanMode::UltraFine:  execMode = ScanMode::UltraFine; break;
  }

  // Execute scan - all business logic handled by ScanExecutor
  bool success = m_scanExecutor->executeScan(deviceName, execMode, channel);

  if (!success) {
    // Re-enable controls if scan failed to start
    m_scanWidget->setScanButtonsEnabled(true);
    m_scanWidget->setStopButtonEnabled(false);
  }
}

void RunProgramPage::onRotationScanRequested(QString deviceName, QString rotationAxis,
                                              double rotationStep, double threshold,
                                              int maxNoImprovement, QString channel)
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

  // Execute rotation scan
  bool success = m_scanExecutor->executeRotationScan(
    deviceName, rotationAxis, rotationStep, threshold, maxNoImprovement, channel);

  if (!success) {
    // Re-enable controls if scan failed to start
    m_scanWidget->setScanButtonsEnabled(true);
    m_scanWidget->setStopButtonEnabled(false);
  }
}

void RunProgramPage::onScanLogMessage(QString message)
{
  // Forward log messages to execution log
  m_logTextEdit->appendPlainText(message);
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
  // Log the stop action - note: device will return to global peak position
  m_logTextEdit->appendPlainText("Stopping scan - returning to global peak position...");

  // Stop any active scan (this will block until return to peak is complete)
  m_scanExecutor->stopScan();

  // Re-enable scan controls, disable stop button
  m_scanWidget->setScanButtonsEnabled(true);
  m_scanWidget->setStopButtonEnabled(false);

  // Log completion
  m_logTextEdit->appendPlainText("Scan stopped - device returned to peak position");
}

// === ROLL CORRECTION SLOTS ===

void RunProgramPage::onRegisterCH1Requested(QString deviceName)
{
  if (!m_context || !m_context->motionManager) return;

  auto pos = m_context->motionManager->GetCurrentPosition(deviceName.toStdString());
  m_rollCorrectionWidget->setCH1Position(deviceName, pos.x, pos.y, pos.z, pos.u, pos.v, pos.w);

  QString msg = QString("[RollCorrection] Registered CH1 peak for %1: X=%2 um")
    .arg(deviceName).arg(pos.x, 0, 'f', 3);
  m_logTextEdit->appendPlainText(msg);
}

void RunProgramPage::onRegisterCH2Requested(QString deviceName)
{
  if (!m_context || !m_context->motionManager) return;

  auto pos = m_context->motionManager->GetCurrentPosition(deviceName.toStdString());
  m_rollCorrectionWidget->setCH2Position(deviceName, pos.x, pos.y, pos.z, pos.u, pos.v, pos.w);

  QString msg = QString("[RollCorrection] Registered CH2 peak for %1: X=%2 um")
    .arg(deviceName).arg(pos.x, 0, 'f', 3);
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

void RunProgramPage::onCorrectWRequested(QString deviceName, double angleDeg, bool positiveW, double halfwayXmm)
{
  if (!m_context || !m_context->motionManager) return;

  std::string dev = deviceName.toStdString();

  // Step 1: Verify CH1 position is available (we move to CH1 peak first)
  auto ch1Pos = m_rollCorrectionWidget->getCH1Position(deviceName);
  if (!ch1Pos.valid) {
    m_logTextEdit->appendPlainText(QString("[RollCorrection] ERROR: CH1 peak not registered for %1").arg(deviceName));
    return;
  }

  // Save current velocity, set to 1 mm/s for all correction moves
  double originalVelocity = 0.0;
  m_context->motionManager->GetSystemVelocity(dev, originalVelocity);
  m_context->motionManager->SetSystemVelocity(dev, 1.0);
  m_logTextEdit->appendPlainText(QString("[RollCorrection] %1 velocity: %2 -> 1.0 mm/s").arg(deviceName).arg(originalVelocity, 0, 'f', 2));

  // Step 2: Move to CH1 peak position first
  m_logTextEdit->appendPlainText(QString("[RollCorrection] Step 1: Moving %1 to CH1 peak position...").arg(deviceName));
  PositionStruct ch1Target;
  ch1Target.x = ch1Pos.x; ch1Target.y = ch1Pos.y; ch1Target.z = ch1Pos.z;
  ch1Target.u = ch1Pos.u; ch1Target.v = ch1Pos.v; ch1Target.w = ch1Pos.w;
  bool ok = m_context->motionManager->MoveToAbs6(dev, ch1Target, true);
  if (!ok) {
    m_logTextEdit->appendPlainText(QString("[RollCorrection] FAILED to move %1 to CH1 peak - aborting correction").arg(deviceName));
    m_context->motionManager->SetSystemVelocity(dev, originalVelocity);
    return;
  }
  m_logTextEdit->appendPlainText(QString("[RollCorrection] %1 at CH1 peak").arg(deviceName));

  // Step 3: Move halfway X offset (direction per case table)
  auto currentPos = m_context->motionManager->GetCurrentPosition(dev);
  // halfwayXmm is signed: negative = move UP (case 1,3), positive = move DOWN (case 2,4)
  // The action is opposite: we negate halfway to get the correction direction
  double newX = currentPos.x - halfwayXmm;  // subtract because halfway is (ch2-ch1)/2, we move opposite
  m_logTextEdit->appendPlainText(QString("[RollCorrection] Step 2: Moving %1 X halfway: %2 -> %3 mm (offset %4 mm)")
    .arg(deviceName)
    .arg(currentPos.x, 0, 'f', 4)
    .arg(newX, 0, 'f', 4)
    .arg(-halfwayXmm, 0, 'f', 4));
  ok = m_context->motionManager->MoveToPosition(dev, "X", newX, true);
  if (!ok) {
    m_logTextEdit->appendPlainText(QString("[RollCorrection] FAILED X halfway move for %1 - aborting correction").arg(deviceName));
    m_context->motionManager->SetSystemVelocity(dev, originalVelocity);
    return;
  }

  // Step 4: Apply W rotation correction
  currentPos = m_context->motionManager->GetCurrentPosition(dev);
  double wCorrection = positiveW ? angleDeg : -angleDeg;
  double newW = currentPos.w + wCorrection;
  m_logTextEdit->appendPlainText(QString("[RollCorrection] Step 3: Correcting W for %1: %2 -> %3 (%4%5 deg)")
    .arg(deviceName)
    .arg(currentPos.w, 0, 'f', 4)
    .arg(newW, 0, 'f', 4)
    .arg(positiveW ? "+" : "-")
    .arg(angleDeg, 0, 'f', 4));
  ok = m_context->motionManager->MoveToPosition(dev, "W", newW, true);

  // Restore original velocity
  m_context->motionManager->SetSystemVelocity(dev, originalVelocity);
  m_logTextEdit->appendPlainText(QString("[RollCorrection] %1 velocity restored to %2 mm/s").arg(deviceName).arg(originalVelocity, 0, 'f', 2));

  m_logTextEdit->appendPlainText(ok ?
    QString("[RollCorrection] Correction complete for %1").arg(deviceName) :
    QString("[RollCorrection] FAILED W correction for %1").arg(deviceName));
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

  // Open the graphical dependency window
  DependencyGraphWindow* graphWindow = new DependencyGraphWindow(m_programManager, this);
  graphWindow->setAttribute(Qt::WA_DeleteOnClose);
  graphWindow->exec();

  // Auto-save program when dialog closes (if we have a save path)
  if (!m_currentProgramPath.isEmpty()) {
    if (m_programManager->saveProgram(m_currentProgramPath.toStdString())) {
      std::cout << "[RunProgramPage] Auto-saved program after dependency changes" << std::endl;
    }
  }

  // Refresh the recipe table to show updated dependencies
  updateRecipeList();
}

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
  m_programStatusLabel->setText(QString("Loop #%1").arg(iteration));
  m_programStatusLabel->setStyleSheet(
    "QLabel { "
    "  background-color: #4ec9b0; "
    "  color: #1e1e1e; "
    "  padding: 4px 12px; "
    "  border-radius: 4px; "
    "  font-weight: bold; "
    "  font-size: 12px; "
    "}"
  );

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

void RunProgramPage::onRecipeTableContextMenu(const QPoint& pos)
{
  QTableWidgetItem* item = m_editRecipeTable->itemAt(pos);
  if (!item) return;

  int row = item->row();
  if (row < 0 || row >= m_programManager->getRecipeCount()) return;

  // Create context menu
  QMenu contextMenu(this);
  QString menuStyle =
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
    "} "
    "QMenu::item:disabled { "
    "  color: #666666; "
    "}";
  contextMenu.setStyleSheet(menuStyle);

  QAction* renameAction = contextMenu.addAction("Rename Recipe...");

  // Add "Move to Slot" submenu if there are multiple slots
  QMenu* moveToSlotMenu = nullptr;
  std::vector<QAction*> slotActions;
  if (m_programManager->getSlotCount() > 1) {
    contextMenu.addSeparator();
    moveToSlotMenu = contextMenu.addMenu("Move to Slot");
    moveToSlotMenu->setStyleSheet(menuStyle);

    int currentSlot = m_programManager->getActiveSlotIndex();
    auto slotNames = m_programManager->getSlotNames();

    for (int i = 0; i < static_cast<int>(slotNames.size()); ++i) {
      if (i != currentSlot) {
        QAction* slotAction = moveToSlotMenu->addAction(QString::fromStdString(slotNames[i]));
        slotAction->setData(i);  // Store slot index in action data
        slotActions.push_back(slotAction);
      }
    }
  }

  // Add retrace edit option (only if not currently running)
  QAction* retraceAction = nullptr;
  if (!m_isExecuting) {
    contextMenu.addSeparator();
    retraceAction = contextMenu.addAction("Retrace Edit This Recipe...");
    retraceAction->setToolTip("Run through recipe and pause at each motion for position adjustment");
  }

  QAction* selectedAction = contextMenu.exec(m_editRecipeTable->viewport()->mapToGlobal(pos));

  if (selectedAction == renameAction) {
    renameRecipe(row);
  }
  else if (retraceAction && selectedAction == retraceAction) {
    onStartRetraceEdit(row);
  }
  else if (moveToSlotMenu) {
    // Check if a slot action was selected
    for (QAction* slotAction : slotActions) {
      if (selectedAction == slotAction) {
        int targetSlotIndex = slotAction->data().toInt();
        QString recipeName = QString::fromStdString(m_programManager->getRecipe(row)->displayName);
        QString targetSlotName = QString::fromStdString(m_programManager->getSlotName(targetSlotIndex));

        if (m_programManager->moveRecipeToSlot(row, targetSlotIndex)) {
          updateRecipeList();
          updateSlotInfo();
          std::cout << "[RunProgramPage] Moved recipe '" << recipeName.toStdString()
                    << "' to slot '" << targetSlotName.toStdString() << "'" << std::endl;
        } else {
          QMessageBox::warning(this, "Move Failed",
            QString("Failed to move recipe '%1' to slot '%2'.")
              .arg(recipeName).arg(targetSlotName));
        }
        break;
      }
    }
  }
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

  if (retraceAction && selectedAction == retraceAction) {
    onStartRetraceEdit(row);
  }
}

void RunProgramPage::renameRecipe(int recipeIndex)
{
  const RecipeReference* recipe = m_programManager->getRecipe(recipeIndex);
  if (!recipe) return;

  QString currentName = QString::fromStdString(recipe->displayName);
  QString recipeFilePath = QString::fromStdString(recipe->recipeFilePath);

  // Show rename dialog
  bool ok;
  QString newName = QInputDialog::getText(
    this,
    "Rename Recipe",
    "Enter new recipe name:",
    QLineEdit::Normal,
    currentName,
    &ok
  );

  if (!ok || newName.trimmed().isEmpty()) return;

  newName = newName.trimmed();
  if (newName == currentName) return;  // No change

  // Validate filename characters
  QString invalidChars = "<>:\"/\\|?*";
  for (QChar c : invalidChars) {
    if (newName.contains(c)) {
      QMessageBox::warning(this, "Invalid Name",
        "Recipe name contains invalid characters.\n\nInvalid characters: < > : \" / \\ | ? *");
      return;
    }
  }

  // Compute new file path
  QFileInfo oldFileInfo(recipeFilePath);
  QString newFilePath = oldFileInfo.absolutePath() + "/" + newName + ".json";

  // Check if target file already exists (and is a different file)
  if (newFilePath != recipeFilePath && QFile::exists(newFilePath)) {
    QMessageBox::warning(this, "Name Exists",
      QString("A recipe file named '%1.json' already exists.").arg(newName));
    return;
  }

  // Update the recipe JSON content and rename the file
  QFile file(recipeFilePath);
  if (!file.open(QIODevice::ReadOnly)) {
    QMessageBox::critical(this, "Error", "Failed to open recipe file for reading.");
    return;
  }
  QByteArray fileData = file.readAll();
  file.close();

  QJsonDocument doc = QJsonDocument::fromJson(fileData);
  if (!doc.isObject()) {
    QMessageBox::critical(this, "Error", "Recipe file is not valid JSON.");
    return;
  }

  QJsonObject obj = doc.object();
  obj["recipe_name"] = newName;

  // Write to new file path
  QFile newFile(newFilePath);
  if (!newFile.open(QIODevice::WriteOnly)) {
    QMessageBox::critical(this, "Error", "Failed to write renamed recipe file.");
    return;
  }
  newFile.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
  newFile.close();

  // Remove old file if the path changed
  if (newFilePath != recipeFilePath) {
    QFile::remove(recipeFilePath);
  }

  // Update ProgramManager with new display name and file path
  m_programManager->setRecipeDisplayName(recipeIndex, newName.toStdString());
  RecipeReference* mutableRecipe = m_programManager->getRecipe(recipeIndex);
  if (mutableRecipe) {
    mutableRecipe->recipeFilePath = newFilePath.toStdString();
  }

  // Auto-save program to persist the new file path
  if (!m_currentProgramPath.isEmpty()) {
    m_programManager->saveProgram(m_currentProgramPath.toStdString());
  }

  QMessageBox::information(this, "Recipe Renamed",
    QString("Recipe renamed to \"%1\" and file saved.").arg(newName));

  // Refresh the table to show new name
  updateRecipeList();

  // Re-select the same row
  m_editRecipeTable->selectRow(recipeIndex);
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

  // Update slot system UI
  updateSlotSelectors();
  updateSlotInfo();
}

void RunProgramPage::updateRecipeList()
{
  // Update left panel list widget
  m_recipeListWidget->clear();

  int recipeNumber = 1;
  for (const auto& recipe : m_programManager->getAllRecipes()) {
    QString displayText;
    QString loopIndicator = recipe->includeInLoop ? " [L]" : "";

    if (recipe->enabled) {
      displayText = QString("%1    %2%3")
        .arg(recipeNumber, 2, 10, QChar('0'))
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
    std::vector<DependencyLink> deps = m_programManager->getRecipeDependencies(rowIndex);
    QString depsText;
    if (deps.empty()) {
      depsText = "—";  // No dependencies
    } else {
      for (size_t i = 0; i < deps.size(); ++i) {
        if (i > 0) depsText += ", ";
        depsText += QString::number(deps[i].predecessorIndex + 1);  // Show 1-based index
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

  // Update UI list
  loadRecentProgramsList();
}

void RunProgramPage::loadRecentProgramsList()
{
  if (!m_recentProgramsList) return;

  m_recentProgramsList->clear();

  QSettings settings("Fabrinet", "BobcatEngine");
  QStringList recentPrograms = settings.value("RunProgramPage/recentPrograms").toStringList();

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

void RunProgramPage::updateVisionResultImage()
{
#ifdef HAS_OPENCV
  std::cout << "[RunProgramPage] updateVisionResultImage() started" << std::endl;
  std::cout.flush();

  if (!m_visionResultImageLabel) {
    std::cout << "[RunProgramPage] m_visionResultImageLabel is null, returning" << std::endl;
    std::cout.flush();
    return;
  }

  // ✅ Load from temporary file to avoid cross-thread context access
  std::string tempImagePath = "logs/vision_result_temp.png";
  cv::Mat resultImage = cv::imread(tempImagePath, cv::IMREAD_COLOR);

  if (resultImage.empty()) {
    std::cout << "[RunProgramPage] No vision result image file found at: " << tempImagePath << std::endl;
    std::cout.flush();
    return;
  }

  std::cout << "[RunProgramPage] Loaded vision result from file: " << resultImage.cols << "x" << resultImage.rows << std::endl;
  std::cout.flush();

  // Convert cv::Mat to QImage (BGR -> RGB)
  cv::Mat rgbImage;
  cv::cvtColor(resultImage, rgbImage, cv::COLOR_BGR2RGB);
  QImage qImage(rgbImage.data, rgbImage.cols, rgbImage.rows,
                static_cast<int>(rgbImage.step), QImage::Format_RGB888);
  qImage = qImage.copy();  // Deep copy to detach from cv::Mat memory

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

  std::cout << "[RunProgramPage] 📷 Vision result image updated: "
            << resultImage.cols << "x" << resultImage.rows << std::endl;
  std::cout.flush();
#else
  // No OpenCV support
  if (m_visionResultImageLabel) {
    m_visionResultImageLabel->setText("OpenCV not available");
  }
#endif
}

// ✅ Clear stale vision results
void RunProgramPage::clearVisionResult()
{
  std::cout << "[RunProgramPage] Clearing vision result..." << std::endl;

  // Delete temporary file if it exists
  std::string tempImagePath = "logs/vision_result_temp.png";
  if (QFile::exists(QString::fromStdString(tempImagePath))) {
    QFile::remove(QString::fromStdString(tempImagePath));
    std::cout << "[RunProgramPage] 🗑️ Deleted stale vision result file: " << tempImagePath << std::endl;
  }

  // Reset label to initial state
  if (m_visionResultImageLabel) {
    m_visionResultImageLabel->clear();  // Clear any pixmap
    m_visionResultImageLabel->setText("No vision result");
    m_visionResultImageLabel->setStyleSheet(
      "QLabel { "
      "  background-color: #0f0f0f; "
      "  border: 1px solid #3f3f46; "
      "  color: #808080; "
      "  font-style: italic; "
      "}"
    );
    std::cout << "[RunProgramPage] ✅ Vision result label reset to initial state" << std::endl;
  }

  // ✅ Also clear vision history
  clearVisionHistory();
}

// ============================================================================
// VISION HISTORY - Real-time vision result display with history navigation
// ============================================================================

void RunProgramPage::onVisionResultReady(QString imagePath)
{
  std::cout << "[RunProgramPage] 🖼️ Vision result ready: " << imagePath.toStdString() << std::endl;

  // Add to history (FIFO - oldest gets removed when at max)
  if (m_visionHistory.size() >= MAX_VISION_HISTORY) {
    m_visionHistory.erase(m_visionHistory.begin());  // Remove oldest
  }
  m_visionHistory.push_back(imagePath);

  // Reset view index to latest (-1 means show latest)
  m_visionHistoryViewIndex = -1;

  // Update navigation controls
  updateVisionNavigation();

  // Load and display the latest image
  loadVisionHistoryImage(static_cast<int>(m_visionHistory.size()) - 1);
}

void RunProgramPage::onVisionPrevious()
{
  if (m_visionHistory.empty()) return;

  int currentIndex = m_visionHistoryViewIndex;
  if (currentIndex == -1) {
    // Currently showing latest, go to second-to-last
    currentIndex = static_cast<int>(m_visionHistory.size()) - 2;
  } else if (currentIndex > 0) {
    currentIndex--;
  }

  if (currentIndex >= 0) {
    m_visionHistoryViewIndex = currentIndex;
    loadVisionHistoryImage(currentIndex);
    updateVisionNavigation();
  }
}

void RunProgramPage::onVisionNext()
{
  if (m_visionHistory.empty()) return;

  int currentIndex = m_visionHistoryViewIndex;
  if (currentIndex == -1) {
    // Already at latest, nothing to do
    return;
  }

  if (currentIndex < static_cast<int>(m_visionHistory.size()) - 1) {
    currentIndex++;
    if (currentIndex == static_cast<int>(m_visionHistory.size()) - 1) {
      // At latest, set to -1
      m_visionHistoryViewIndex = -1;
    } else {
      m_visionHistoryViewIndex = currentIndex;
    }
    loadVisionHistoryImage(currentIndex);
    updateVisionNavigation();
  }
}

void RunProgramPage::updateVisionNavigation()
{
  int total = static_cast<int>(m_visionHistory.size());
  int displayIndex = m_visionHistoryViewIndex;

  if (displayIndex == -1) {
    displayIndex = total - 1;  // Latest
  }

  // Update index label (1-based for display)
  if (total > 0) {
    m_visionIndexLabel->setText(QString("%1 / %2").arg(displayIndex + 1).arg(total));
    m_visionIndexLabel->setStyleSheet("QLabel { color: #FFA500; font-weight: bold; }");
  } else {
    m_visionIndexLabel->setText("- / -");
    m_visionIndexLabel->setStyleSheet("QLabel { color: #808080; }");
  }

  // Update button states
  bool canGoPrev = total > 1 && (m_visionHistoryViewIndex != 0);
  bool canGoNext = total > 1 && (m_visionHistoryViewIndex != -1);

  m_visionPrevButton->setEnabled(canGoPrev);
  m_visionNextButton->setEnabled(canGoNext);
}

void RunProgramPage::loadVisionHistoryImage(int index)
{
#ifdef HAS_OPENCV
  if (index < 0 || index >= static_cast<int>(m_visionHistory.size())) {
    return;
  }

  QString imagePath = m_visionHistory[index];
  cv::Mat resultImage = cv::imread(imagePath.toStdString(), cv::IMREAD_COLOR);

  if (resultImage.empty()) {
    std::cout << "[RunProgramPage] ⚠️ Failed to load history image: " << imagePath.toStdString() << std::endl;
    return;
  }

  // Convert cv::Mat to QImage (BGR -> RGB)
  cv::Mat rgbImage;
  cv::cvtColor(resultImage, rgbImage, cv::COLOR_BGR2RGB);
  QImage qImage(rgbImage.data, rgbImage.cols, rgbImage.rows,
                static_cast<int>(rgbImage.step), QImage::Format_RGB888);
  qImage = qImage.copy();  // Deep copy to detach from cv::Mat memory

  // Scale to fit the label while maintaining aspect ratio
  QPixmap pixmap = QPixmap::fromImage(qImage);
  pixmap = pixmap.scaled(m_visionResultImageLabel->size(),
                         Qt::KeepAspectRatio, Qt::SmoothTransformation);

  m_visionResultImageLabel->setPixmap(pixmap);

  // Use orange border for current view, different shade if viewing history
  QString borderColor = (m_visionHistoryViewIndex == -1) ? "#FFA500" : "#FF6B00";
  m_visionResultImageLabel->setStyleSheet(
    QString("QLabel { "
    "  background-color: #0f0f0f; "
    "  border: 2px solid %1; "
    "}").arg(borderColor)
  );

  std::cout << "[RunProgramPage] 📷 Loaded history image [" << (index + 1) << "/"
            << m_visionHistory.size() << "]: " << imagePath.toStdString() << std::endl;
#else
  (void)index;
#endif
}

void RunProgramPage::clearVisionHistory()
{
  std::cout << "[RunProgramPage] Clearing vision history..." << std::endl;

  m_visionHistory.clear();
  m_visionHistoryViewIndex = -1;

  // Update navigation controls
  updateVisionNavigation();

  std::cout << "[RunProgramPage] ✅ Vision history cleared" << std::endl;
}

// ============================================================================
// CONTEXT TAB - Real-time Sequence Context Variable Viewer
// ============================================================================

void RunProgramPage::createContextTab()
{
  m_contextTabWidget = new QWidget();
  QVBoxLayout* contextLayout = new QVBoxLayout(m_contextTabWidget);
  contextLayout->setContentsMargins(5, 5, 5, 5);
  contextLayout->setSpacing(8);

  // Header row with filter, expand/collapse, and status
  QHBoxLayout* headerLayout = new QHBoxLayout();

  QLabel* filterLabel = new QLabel("Filter:");
  filterLabel->setStyleSheet("color: #d4d4d4; font-weight: bold; font-size: 18pt;");
  headerLayout->addWidget(filterLabel);

  m_contextFilterEdit = new QLineEdit();
  m_contextFilterEdit->setPlaceholderText("Type to filter variables...");
  m_contextFilterEdit->setStyleSheet(
    "QLineEdit { "
    "  background-color: #3c3c3c; "
    "  color: #d4d4d4; "
    "  border: 1px solid #555555; "
    "  border-radius: 3px; "
    "  padding: 5px; "
    "  min-width: 200px; "
    "  font-size: 18pt; "
    "} "
    "QLineEdit:focus { "
    "  border: 1px solid #0e639c; "
    "}"
  );
  headerLayout->addWidget(m_contextFilterEdit, 1);

  // Expand All / Collapse All buttons
  QString smallButtonStyle =
    "QPushButton { "
    "  background-color: #3c3c3c; "
    "  color: #d4d4d4; "
    "  border: 1px solid #555555; "
    "  border-radius: 3px; "
    "  padding: 4px 10px; "
    "  font-size: 14pt; "
    "} "
    "QPushButton:hover { "
    "  background-color: #505050; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #606060; "
    "}";

  m_contextExpandAllButton = new QPushButton("Expand All");
  m_contextExpandAllButton->setStyleSheet(smallButtonStyle);
  headerLayout->addWidget(m_contextExpandAllButton);

  m_contextCollapseAllButton = new QPushButton("Collapse All");
  m_contextCollapseAllButton->setStyleSheet(smallButtonStyle);
  headerLayout->addWidget(m_contextCollapseAllButton);

  // Font size toggle button (small = app default, large = 18pt)
  QSettings fontSettings("Fabrinet", "BobcatEngine");
  m_contextLargeFont = fontSettings.value("ContextTree/LargeFont", false).toBool();
  m_contextFontToggleButton = new QPushButton(m_contextLargeFont ? "Aa-" : "Aa+");
  m_contextFontToggleButton->setToolTip(m_contextLargeFont ? "Switch to small font" : "Switch to large font");
  m_contextFontToggleButton->setStyleSheet(smallButtonStyle);
  headerLayout->addWidget(m_contextFontToggleButton);

  m_contextStatusLabel = new QLabel("No context data");
  m_contextStatusLabel->setStyleSheet("color: #888888; font-style: italic; font-size: 18pt;");
  headerLayout->addWidget(m_contextStatusLabel);

  contextLayout->addLayout(headerLayout);

  // Context variables tree (dot-separated hierarchy)
  m_contextTree = new QTreeWidget();
  m_contextTree->setColumnCount(3);
  m_contextTree->setHeaderLabels({"Variable", "Value", "Type"});
  m_contextTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_contextTree->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_contextTree->setAlternatingRowColors(false);
  m_contextTree->setIndentation(20);

  // Load column widths from settings (with defaults for larger font)
  QSettings settings("Fabrinet", "BobcatEngine");
  int col0Width = settings.value("ContextTree/ColumnWidth0", 300).toInt();
  int col1Width = settings.value("ContextTree/ColumnWidth1", 450).toInt();
  m_contextTree->setColumnWidth(0, col0Width);  // Variable name (tree column)
  m_contextTree->setColumnWidth(1, col1Width);  // Value
  m_contextTree->header()->setStretchLastSection(true);  // Type stretches

  // Save column widths when user resizes them
  connect(m_contextTree->header(), &QHeaderView::sectionResized,
    this, [this](int logicalIndex, int /*oldSize*/, int newSize) {
      if (logicalIndex < 2) {  // Only save Variable and Value columns (Type stretches)
        QSettings settings("Fabrinet", "BobcatEngine");
        settings.setValue(QString("ContextTree/ColumnWidth%1").arg(logicalIndex), newSize);
      }
    });

  m_contextTree->setStyleSheet(
    "QTreeWidget { "
    "  background-color: #1e1e1e; "
    "  border: 1px solid #3f3f46; "
    "  color: #d4d4d4; "
    "  font-size: 18pt; "
    "} "
    "QTreeWidget::item { "
    "  padding: 4px; "
    "} "
    "QTreeWidget::item:alternate { "
    "  background-color: #252526; "
    "} "
    "QTreeWidget::item:selected { "
    "  background-color: #0e639c; "
    "  color: white; "
    "} "
    "QTreeWidget::branch { "
    "  background: transparent; "
    "  border-image: none; "
    "  image: none; "
    "} "
    "QTreeWidget::branch:has-children:!has-siblings:closed,"
    "QTreeWidget::branch:closed:has-children:has-siblings { "
    "  border-image: none; "
    "  image: url(assets/icons/down_arrow.png); "
    "} "
    "QTreeWidget::branch:open:has-children:!has-siblings,"
    "QTreeWidget::branch:open:has-children:has-siblings { "
    "  border-image: none; "
    "  image: url(assets/icons/up_arrow.png); "
    "} "
    "QHeaderView::section { "
    "  background-color: #2d2d30; "
    "  color: #d4d4d4; "
    "  padding: 8px; "
    "  border: 1px solid #3f3f46; "
    "  font-weight: bold; "
    "  font-size: 18pt; "
    "}"
  );

  contextLayout->addWidget(m_contextTree, 1);

  // Button row for copy/export
  QHBoxLayout* buttonLayout = new QHBoxLayout();

  // Refresh CV button (left side)
  m_contextRefreshCvButton = new QPushButton("Refresh CV");
  m_contextRefreshCvButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #2d5a27; "
    "  color: #d4d4d4; "
    "  border: 1px solid #3a7d32; "
    "  border-radius: 3px; "
    "  padding: 8px 16px; "
    "  font-size: 18pt; "
    "} "
    "QPushButton:hover { "
    "  background-color: #3a7d32; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #4a9d42; "
    "} "
    "QPushButton:checked { "
    "  background-color: #4a9d42; "
    "  border: 2px solid #6abf62; "
    "}"
  );
  m_contextRefreshCvButton->setCheckable(true);
  m_contextRefreshCvButton->setToolTip("Click to inject cv.* values from continuous vision.\nToggle ON for auto-refresh every 500ms.");
  buttonLayout->addWidget(m_contextRefreshCvButton);

  buttonLayout->addStretch();

  m_contextCopyButton = new QPushButton("Copy to Clipboard");
  m_contextCopyButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #3c3c3c; "
    "  color: #d4d4d4; "
    "  border: 1px solid #555555; "
    "  border-radius: 3px; "
    "  padding: 8px 16px; "
    "  font-size: 18pt; "
    "} "
    "QPushButton:hover { "
    "  background-color: #505050; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #606060; "
    "}"
  );
  buttonLayout->addWidget(m_contextCopyButton);

  m_contextExportButton = new QPushButton("Export to CSV");
  m_contextExportButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #3c3c3c; "
    "  color: #d4d4d4; "
    "  border: 1px solid #555555; "
    "  border-radius: 3px; "
    "  padding: 8px 16px; "
    "  font-size: 18pt; "
    "} "
    "QPushButton:hover { "
    "  background-color: #505050; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #606060; "
    "}"
  );
  buttonLayout->addWidget(m_contextExportButton);

  contextLayout->addLayout(buttonLayout);

  // Connect filter textbox
  connect(m_contextFilterEdit, &QLineEdit::textChanged,
    this, &RunProgramPage::onContextFilterChanged);

  // Connect buttons
  connect(m_contextCopyButton, &QPushButton::clicked,
    this, &RunProgramPage::onContextCopyToClipboard);
  connect(m_contextExportButton, &QPushButton::clicked,
    this, &RunProgramPage::onContextExportToCsv);

  // Connect Expand All / Collapse All
  connect(m_contextExpandAllButton, &QPushButton::clicked,
    this, [this]() { m_contextTree->expandAll(); });
  connect(m_contextCollapseAllButton, &QPushButton::clicked,
    this, [this]() { m_contextTree->collapseAll(); });

  // Connect Font Toggle
  connect(m_contextFontToggleButton, &QPushButton::clicked,
    this, [this]() {
      m_contextLargeFont = !m_contextLargeFont;
      QSettings s("Fabrinet", "BobcatEngine");
      s.setValue("ContextTree/LargeFont", m_contextLargeFont);
      m_contextFontToggleButton->setText(m_contextLargeFont ? "Aa-" : "Aa+");
      m_contextFontToggleButton->setToolTip(m_contextLargeFont ? "Switch to small font" : "Switch to large font");
      applyContextFontSize();
    });

  // Connect Refresh CV button (toggle auto-refresh)
  connect(m_contextRefreshCvButton, &QPushButton::clicked,
    this, [this](bool checked) {
      if (checked) {
        // Toggle ON: do immediate refresh + start auto-refresh timer
        onContextRefreshCv();
        if (!m_cvAutoRefreshTimer) {
          m_cvAutoRefreshTimer = new QTimer(this);
          m_cvAutoRefreshTimer->setInterval(500);
          connect(m_cvAutoRefreshTimer, &QTimer::timeout, this, &RunProgramPage::onContextRefreshCv);
        }
        m_cvAutoRefreshTimer->start();
        m_contextRefreshCvButton->setText("Refresh CV (Auto)");
      } else {
        // Toggle OFF: stop auto-refresh
        if (m_cvAutoRefreshTimer) {
          m_cvAutoRefreshTimer->stop();
        }
        m_contextRefreshCvButton->setText("Refresh CV");
      }
    });

  // Create refresh timer (but don't start yet)
  m_contextRefreshTimer = new QTimer(this);
  m_contextRefreshTimer->setInterval(500);  // 500ms update interval
  connect(m_contextRefreshTimer, &QTimer::timeout,
    this, [this]() { updateContextTable(false); });

  // Apply the persisted font size preference
  applyContextFontSize();

  std::cout << "[RunProgramPage] Context tab created (tree view)" << std::endl;
}

void RunProgramPage::applyContextFontSize()
{
  QString contentSize = m_contextLargeFont ? "18pt" : "9pt";
  QString buttonSize = m_contextLargeFont ? "14pt" : "9pt";
  QString headerPad = m_contextLargeFont ? "8px" : "4px";
  QString btnPad = m_contextLargeFont ? "8px 16px" : "4px 10px";

  // Filter label
  auto* filterLabel = m_contextTabWidget->findChild<QLabel*>();
  if (filterLabel && filterLabel->text() == "Filter:") {
    filterLabel->setStyleSheet(QString("color: #d4d4d4; font-weight: bold; font-size: %1;").arg(contentSize));
  }

  // Filter edit
  m_contextFilterEdit->setStyleSheet(
    QString(
      "QLineEdit { "
      "  background-color: #3c3c3c; "
      "  color: #d4d4d4; "
      "  border: 1px solid #555555; "
      "  border-radius: 3px; "
      "  padding: 5px; "
      "  min-width: 200px; "
      "  font-size: %1; "
      "} "
      "QLineEdit:focus { "
      "  border: 1px solid #0e639c; "
      "}"
    ).arg(contentSize)
  );

  // Small header buttons (Expand All, Collapse All, Font Toggle)
  QString smallBtnStyle = QString(
    "QPushButton { "
    "  background-color: #3c3c3c; "
    "  color: #d4d4d4; "
    "  border: 1px solid #555555; "
    "  border-radius: 3px; "
    "  padding: 4px 10px; "
    "  font-size: %1; "
    "} "
    "QPushButton:hover { "
    "  background-color: #505050; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #606060; "
    "}"
  ).arg(buttonSize);
  m_contextExpandAllButton->setStyleSheet(smallBtnStyle);
  m_contextCollapseAllButton->setStyleSheet(smallBtnStyle);
  m_contextFontToggleButton->setStyleSheet(smallBtnStyle);

  // Status label
  m_contextStatusLabel->setStyleSheet(QString("color: #888888; font-style: italic; font-size: %1;").arg(contentSize));

  // Tree widget
  m_contextTree->setStyleSheet(
    QString(
      "QTreeWidget { "
      "  background-color: #1e1e1e; "
      "  border: 1px solid #3f3f46; "
      "  color: #d4d4d4; "
      "  font-size: %1; "
      "} "
      "QTreeWidget::item { "
      "  padding: 4px; "
      "} "
      "QTreeWidget::item:alternate { "
      "  background-color: #252526; "
      "} "
      "QTreeWidget::item:selected { "
      "  background-color: #0e639c; "
      "  color: white; "
      "} "
      "QTreeWidget::branch { "
      "  background: transparent; "
      "} "
      "QTreeWidget::branch:has-children:!has-siblings:closed,"
      "QTreeWidget::branch:closed:has-children:has-siblings { "
      "  border-image: none; "
      "  image: url(assets/icons/down_arrow.png); "
      "} "
      "QTreeWidget::branch:open:has-children:!has-siblings,"
      "QTreeWidget::branch:open:has-children:has-siblings { "
      "  border-image: none; "
      "  image: url(assets/icons/up_arrow.png); "
      "} "
      "QHeaderView::section { "
      "  background-color: #2d2d30; "
      "  color: #d4d4d4; "
      "  padding: %2; "
      "  border: 1px solid #3f3f46; "
      "  font-weight: bold; "
      "  font-size: %1; "
      "}"
    ).arg(contentSize, headerPad)
  );

  // Bottom action buttons (Refresh CV, Copy, Export)
  QString actionBtnStyle = QString(
    "QPushButton { "
    "  background-color: #3c3c3c; "
    "  color: #d4d4d4; "
    "  border: 1px solid #555555; "
    "  border-radius: 3px; "
    "  padding: %1; "
    "  font-size: %2; "
    "} "
    "QPushButton:hover { "
    "  background-color: #505050; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #606060; "
    "}"
  ).arg(btnPad, contentSize);
  m_contextCopyButton->setStyleSheet(actionBtnStyle);
  m_contextExportButton->setStyleSheet(actionBtnStyle);

  // Refresh CV button (green variant)
  m_contextRefreshCvButton->setStyleSheet(
    QString(
      "QPushButton { "
      "  background-color: #2d5a27; "
      "  color: #d4d4d4; "
      "  border: 1px solid #3a7d32; "
      "  border-radius: 3px; "
      "  padding: %1; "
      "  font-size: %2; "
      "} "
      "QPushButton:hover { "
      "  background-color: #3a7d32; "
      "} "
      "QPushButton:pressed { "
      "  background-color: #4a9d42; "
      "} "
      "QPushButton:checked { "
      "  background-color: #4a9d42; "
      "  border: 2px solid #6abf62; "
      "}"
    ).arg(btnPad, contentSize)
  );
}

void RunProgramPage::updateContextTable(bool forceUpdate)
{
  // Safety check: Don't access context if worker is not actively running
  if (!forceUpdate && (!m_programWorker || !m_programWorker->isRunning())) {
    if (m_contextTree->topLevelItemCount() > 0) {
      m_contextStatusLabel->setText("variables (last run)");
      m_contextStatusLabel->setStyleSheet("color: #888888; font-style: italic;");
    } else {
      m_contextStatusLabel->setText("No context data");
      m_contextStatusLabel->setStyleSheet("color: #888888; font-style: italic;");
    }
    return;
  }

  // Get context from ProgramWorker (thread-safe)
  SequenceContext* context = m_programWorker ? m_programWorker->getCurrentContext() : nullptr;

  if (!context) {
    if (m_contextTree->topLevelItemCount() > 0) {
      m_contextStatusLabel->setText("variables (last run)");
      m_contextStatusLabel->setStyleSheet("color: #888888; font-style: italic;");
    } else {
      m_contextStatusLabel->setText("No context data");
      m_contextStatusLabel->setStyleSheet("color: #888888; font-style: italic;");
    }
    return;
  }

  refreshContextTreeDisplay(context, "", "");
}

void RunProgramPage::onContextFilterChanged(const QString& /*filter*/)
{
  // If CV auto-refresh is active, refresh the standalone context display
  if (m_cvAutoRefreshTimer && m_cvAutoRefreshTimer->isActive() && m_standaloneCvContext) {
    displayStandaloneContext();
  } else {
    // Immediately update the table with new filter
    updateContextTable();
  }
}

void RunProgramPage::startContextRefreshTimer()
{
  // Stop CV auto-refresh when recipe execution starts
  if (m_cvAutoRefreshTimer && m_cvAutoRefreshTimer->isActive()) {
    m_cvAutoRefreshTimer->stop();
    m_contextRefreshCvButton->setChecked(false);
    m_contextRefreshCvButton->setText("Refresh CV");
  }

  if (m_contextRefreshTimer && !m_contextRefreshTimer->isActive()) {
    // Use longer interval during loop execution to reduce UI overhead
    int interval = m_isLoopExecution ? 1000 : 500;
    m_contextRefreshTimer->setInterval(interval);
    std::cout << "[RunProgramPage] Starting context refresh timer (" << interval << "ms)" << std::endl;
    updateContextTable();  // Immediate update
    m_contextRefreshTimer->start();
  }
}

void RunProgramPage::stopContextRefreshTimer()
{
  if (m_contextRefreshTimer && m_contextRefreshTimer->isActive()) {
    std::cout << "[RunProgramPage] Stopping context refresh timer" << std::endl;
    m_contextRefreshTimer->stop();
    // Final update to show end state
    updateContextTable();
  }
}

void RunProgramPage::onContextCopyToClipboard()
{
  if (!m_contextTree || m_contextTree->topLevelItemCount() == 0) {
    std::cout << "[RunProgramPage] No context data to copy" << std::endl;
    return;
  }

  // Recursive helper to collect leaf entries with full dot-separated paths
  struct Entry { QString variable; QString value; QString type; };
  std::vector<Entry> entries;

  std::function<void(QTreeWidgetItem*, const QString&)> collectLeaves;
  collectLeaves = [&](QTreeWidgetItem* item, const QString& prefix) {
    QString fullPath = prefix.isEmpty() ? item->text(0) : prefix + "." + item->text(0);
    if (item->childCount() == 0) {
      // Leaf node
      entries.push_back({fullPath, item->text(1), item->text(2)});
    } else {
      for (int i = 0; i < item->childCount(); ++i) {
        collectLeaves(item->child(i), fullPath);
      }
    }
  };

  for (int i = 0; i < m_contextTree->topLevelItemCount(); ++i) {
    collectLeaves(m_contextTree->topLevelItem(i), "");
  }

  // Build tab-separated text
  QString clipboardText;
  clipboardText += "Variable\tValue\tType\n";
  for (const auto& e : entries) {
    clipboardText += QString("%1\t%2\t%3\n").arg(e.variable, e.value, e.type);
  }

  QClipboard* clipboard = QApplication::clipboard();
  clipboard->setText(clipboardText);

  std::cout << "[RunProgramPage] Copied " << entries.size() << " context variables to clipboard" << std::endl;

  m_contextCopyButton->setText("Copied!");
  QTimer::singleShot(1500, this, [this]() {
    m_contextCopyButton->setText("Copy to Clipboard");
  });
}

void RunProgramPage::onContextExportToCsv()
{
  if (!m_contextTree || m_contextTree->topLevelItemCount() == 0) {
    std::cout << "[RunProgramPage] No context data to export" << std::endl;
    return;
  }

  // Collect leaf entries from tree
  struct Entry { QString variable; QString value; QString type; };
  std::vector<Entry> entries;

  std::function<void(QTreeWidgetItem*, const QString&)> collectLeaves;
  collectLeaves = [&](QTreeWidgetItem* item, const QString& prefix) {
    QString fullPath = prefix.isEmpty() ? item->text(0) : prefix + "." + item->text(0);
    if (item->childCount() == 0) {
      entries.push_back({fullPath, item->text(1), item->text(2)});
    } else {
      for (int i = 0; i < item->childCount(); ++i) {
        collectLeaves(item->child(i), fullPath);
      }
    }
  };

  for (int i = 0; i < m_contextTree->topLevelItemCount(); ++i) {
    collectLeaves(m_contextTree->topLevelItem(i), "");
  }

  // Generate default filename with timestamp
  QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
  QString defaultFileName = QString("context_export_%1.csv").arg(timestamp);

  QString filePath = QFileDialog::getSaveFileName(
    this,
    "Export Context to CSV",
    defaultFileName,
    "CSV Files (*.csv);;All Files (*)"
  );

  if (filePath.isEmpty()) {
    return;  // User cancelled
  }

  QFile file(filePath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    std::cerr << "[RunProgramPage] Failed to open file for writing: " << filePath.toStdString() << std::endl;
    return;
  }

  QTextStream stream(&file);
  stream << "Variable,Value,Type\n";

  auto escapeForCsv = [](const QString& str) -> QString {
    QString escaped = str;
    escaped.replace("\"", "\"\"");
    if (escaped.contains(',') || escaped.contains('"') || escaped.contains('\n')) {
      escaped = QString("\"%1\"").arg(escaped);
    }
    return escaped;
  };

  for (const auto& e : entries) {
    stream << escapeForCsv(e.variable) << "," << escapeForCsv(e.value) << "," << escapeForCsv(e.type) << "\n";
  }

  file.close();

  std::cout << "[RunProgramPage] Exported " << entries.size() << " context variables to: " << filePath.toStdString() << std::endl;

  m_contextExportButton->setText("Exported!");
  QTimer::singleShot(1500, this, [this]() {
    m_contextExportButton->setText("Export to CSV");
  });
}

void RunProgramPage::onContextRefreshCv()
{
  if (!m_context || !m_context->gdiCameraWindowManager) {
    m_contextStatusLabel->setText("GDI Camera Window not available");
    m_contextStatusLabel->setStyleSheet(QString("color: #f44747; font-style: italic; font-size: %1;").arg(m_contextLargeFont ? "18pt" : "9pt"));
    return;
  }

  bool hasVision = false;
  bool hasBeamStream = m_context->gdiCameraWindowManager->isBeamStreamActive();
  bool hasBeamStream2 = m_context->gdiCameraWindowManager2 &&
                        m_context->gdiCameraWindowManager2->isBeamStreamActive();

#ifdef HAS_OPENCV
  hasVision = m_context->gdiCameraWindowManager->isVisionProcessingActive();
#endif

  if (!hasVision && !hasBeamStream && !hasBeamStream2) {
    m_contextStatusLabel->setText("No live data source active (vision / beam stream)");
    m_contextStatusLabel->setStyleSheet(QString("color: #cca700; font-style: italic; font-size: %1;").arg(m_contextLargeFont ? "18pt" : "9pt"));
    return;
  }

  // Create standalone context if needed
  if (!m_standaloneCvContext) {
    m_standaloneCvContext = std::make_unique<SequenceContext>();
  }

#ifdef HAS_OPENCV
  if (hasVision) {
    // Read latest vision result from GdiCameraWindowManager
    VisionResult result = m_context->gdiCameraWindowManager->getLatestVisionResult();

    // Populate the standalone context with cv.* keys (mirrors InjectContinuousVisionResult)
    m_standaloneCvContext->SetValue("cv.result.success", result.success ? "true" : "false");
    m_standaloneCvContext->SetValue("cv.result.plugin", result.pluginName);
    m_standaloneCvContext->SetValue("cv.result.processing_time_ms", std::to_string(result.processingTimeMs));

    // Measurements
    for (const auto& [key, val] : result.measurements) {
      m_standaloneCvContext->SetValue("cv.result." + key, std::to_string(val));
    }

    // Points
    m_standaloneCvContext->SetValue("cv.point.count", std::to_string(result.points.size()));
    for (size_t i = 0; i < result.points.size(); ++i) {
      std::string prefix = "cv.point." + std::to_string(i) + ".";
      const auto& pt = result.points[i];
      m_standaloneCvContext->SetValue(prefix + "x", std::to_string(pt.pixelCoord.x));
      m_standaloneCvContext->SetValue(prefix + "y", std::to_string(pt.pixelCoord.y));
      m_standaloneCvContext->SetValue(prefix + "mm_x", std::to_string(pt.mmCoord.x));
      m_standaloneCvContext->SetValue(prefix + "mm_y", std::to_string(pt.mmCoord.y));
      m_standaloneCvContext->SetValue(prefix + "confidence", std::to_string(pt.confidence));
    }

    // Circles
    m_standaloneCvContext->SetValue("cv.circle.count", std::to_string(result.circles.size()));
    for (size_t i = 0; i < result.circles.size(); ++i) {
      std::string prefix = "cv.circle." + std::to_string(i) + ".";
      const auto& c = result.circles[i];
      m_standaloneCvContext->SetValue(prefix + "x", std::to_string(c.centerPx.x));
      m_standaloneCvContext->SetValue(prefix + "y", std::to_string(c.centerPx.y));
      m_standaloneCvContext->SetValue(prefix + "radius", std::to_string(c.radiusPx));
      m_standaloneCvContext->SetValue(prefix + "mm_x", std::to_string(c.centerMm.x));
      m_standaloneCvContext->SetValue(prefix + "mm_y", std::to_string(c.centerMm.y));
      m_standaloneCvContext->SetValue(prefix + "radius_mm", std::to_string(c.radiusMm));
    }

    // Lines
    m_standaloneCvContext->SetValue("cv.line.count", std::to_string(result.lines.size()));
    for (size_t i = 0; i < result.lines.size(); ++i) {
      std::string prefix = "cv.line." + std::to_string(i) + ".";
      const auto& l = result.lines[i];
      m_standaloneCvContext->SetValue(prefix + "start_x", std::to_string(l.startPx.x));
      m_standaloneCvContext->SetValue(prefix + "start_y", std::to_string(l.startPx.y));
      m_standaloneCvContext->SetValue(prefix + "end_x", std::to_string(l.endPx.x));
      m_standaloneCvContext->SetValue(prefix + "end_y", std::to_string(l.endPx.y));
    }
  }
#endif

  // Inject beam stream data (always available, no HAS_OPENCV dependency)
  if (hasBeamStream) {
    nlohmann::json bsResult = m_context->gdiCameraWindowManager->getLatestBeamStreamResult();

    m_standaloneCvContext->SetValue("bs.active", "true");
    m_standaloneCvContext->SetValue("bs.fps", std::to_string(m_context->gdiCameraWindowManager->getBeamStreamFps()));

    if (bsResult.contains("process_ms")) {
      m_standaloneCvContext->SetValue("bs.process_ms", std::to_string(bsResult["process_ms"].get<double>()));
    }

    if (bsResult.contains("beams") && bsResult["beams"].is_array()) {
      const auto& beams = bsResult["beams"];
      m_standaloneCvContext->SetValue("bs.beam.count", std::to_string(beams.size()));

      for (size_t i = 0; i < beams.size(); ++i) {
        std::string prefix = "bs.beam." + std::to_string(i) + ".";
        const auto& beam = beams[i];

        if (beam.contains("side")) {
          m_standaloneCvContext->SetValue(prefix + "label", beam["side"].get<std::string>());
        }
        m_standaloneCvContext->SetValue(prefix + "centroid_x_px", std::to_string(beam.value("centroid_x_px", 0.0)));
        m_standaloneCvContext->SetValue(prefix + "centroid_y_px", std::to_string(beam.value("centroid_y_px", 0.0)));
        if (beam.contains("d4sigma_x_px")) {
          m_standaloneCvContext->SetValue(prefix + "d4sigma_x_px", std::to_string(beam["d4sigma_x_px"].get<double>()));
        }
        if (beam.contains("d4sigma_y_px")) {
          m_standaloneCvContext->SetValue(prefix + "d4sigma_y_px", std::to_string(beam["d4sigma_y_px"].get<double>()));
        }
        if (beam.contains("d4sigma_x_um")) {
          m_standaloneCvContext->SetValue(prefix + "d4sigma_x_um", std::to_string(beam["d4sigma_x_um"].get<double>()));
        }
        if (beam.contains("d4sigma_y_um")) {
          m_standaloneCvContext->SetValue(prefix + "d4sigma_y_um", std::to_string(beam["d4sigma_y_um"].get<double>()));
        }
        if (beam.contains("peak_intensity")) {
          m_standaloneCvContext->SetValue(prefix + "peak_intensity", std::to_string(beam["peak_intensity"].get<double>()));
        }
        if (beam.contains("roi_x")) {
          m_standaloneCvContext->SetValue(prefix + "roi_x", std::to_string(beam["roi_x"].get<int>()));
        }
        if (beam.contains("roi_y")) {
          m_standaloneCvContext->SetValue(prefix + "roi_y", std::to_string(beam["roi_y"].get<int>()));
        }
        if (beam.contains("roi_w")) {
          m_standaloneCvContext->SetValue(prefix + "roi_w", std::to_string(beam["roi_w"].get<int>()));
        }
        if (beam.contains("roi_h")) {
          m_standaloneCvContext->SetValue(prefix + "roi_h", std::to_string(beam["roi_h"].get<int>()));
        }
      }

      // Convenience aliases for first beam
      if (!beams.empty()) {
        m_standaloneCvContext->SetValue("bs.beam.first.centroid_x_px", std::to_string(beams[0].value("centroid_x_px", 0.0)));
        m_standaloneCvContext->SetValue("bs.beam.first.centroid_y_px", std::to_string(beams[0].value("centroid_y_px", 0.0)));
        if (beams[0].contains("d4sigma_x_px")) {
          m_standaloneCvContext->SetValue("bs.beam.first.d4sigma_x_px", std::to_string(beams[0]["d4sigma_x_px"].get<double>()));
        }
        if (beams[0].contains("d4sigma_y_px")) {
          m_standaloneCvContext->SetValue("bs.beam.first.d4sigma_y_px", std::to_string(beams[0]["d4sigma_y_px"].get<double>()));
        }
        if (beams[0].contains("side")) {
          m_standaloneCvContext->SetValue("bs.beam.first.label", beams[0]["side"].get<std::string>());
        }
      }
    } else {
      m_standaloneCvContext->SetValue("bs.beam.count", "0");
    }
  }

  // Inject beam stream data from second GDI camera window (bs2.* keys)
  if (hasBeamStream2) {
    nlohmann::json bsResult = m_context->gdiCameraWindowManager2->getLatestBeamStreamResult();

    m_standaloneCvContext->SetValue("bs2.active", "true");
    m_standaloneCvContext->SetValue("bs2.fps", std::to_string(m_context->gdiCameraWindowManager2->getBeamStreamFps()));

    if (bsResult.contains("process_ms")) {
      m_standaloneCvContext->SetValue("bs2.process_ms", std::to_string(bsResult["process_ms"].get<double>()));
    }

    if (bsResult.contains("beams") && bsResult["beams"].is_array()) {
      const auto& beams = bsResult["beams"];
      m_standaloneCvContext->SetValue("bs2.beam.count", std::to_string(beams.size()));

      for (size_t i = 0; i < beams.size(); ++i) {
        std::string prefix = "bs2.beam." + std::to_string(i) + ".";
        const auto& beam = beams[i];

        if (beam.contains("side")) {
          m_standaloneCvContext->SetValue(prefix + "label", beam["side"].get<std::string>());
        }
        m_standaloneCvContext->SetValue(prefix + "centroid_x_px", std::to_string(beam.value("centroid_x_px", 0.0)));
        m_standaloneCvContext->SetValue(prefix + "centroid_y_px", std::to_string(beam.value("centroid_y_px", 0.0)));
        if (beam.contains("d4sigma_x_px")) {
          m_standaloneCvContext->SetValue(prefix + "d4sigma_x_px", std::to_string(beam["d4sigma_x_px"].get<double>()));
        }
        if (beam.contains("d4sigma_y_px")) {
          m_standaloneCvContext->SetValue(prefix + "d4sigma_y_px", std::to_string(beam["d4sigma_y_px"].get<double>()));
        }
        if (beam.contains("d4sigma_x_um")) {
          m_standaloneCvContext->SetValue(prefix + "d4sigma_x_um", std::to_string(beam["d4sigma_x_um"].get<double>()));
        }
        if (beam.contains("d4sigma_y_um")) {
          m_standaloneCvContext->SetValue(prefix + "d4sigma_y_um", std::to_string(beam["d4sigma_y_um"].get<double>()));
        }
        if (beam.contains("peak_intensity")) {
          m_standaloneCvContext->SetValue(prefix + "peak_intensity", std::to_string(beam["peak_intensity"].get<double>()));
        }
        if (beam.contains("roi_x")) {
          m_standaloneCvContext->SetValue(prefix + "roi_x", std::to_string(beam["roi_x"].get<int>()));
        }
        if (beam.contains("roi_y")) {
          m_standaloneCvContext->SetValue(prefix + "roi_y", std::to_string(beam["roi_y"].get<int>()));
        }
        if (beam.contains("roi_w")) {
          m_standaloneCvContext->SetValue(prefix + "roi_w", std::to_string(beam["roi_w"].get<int>()));
        }
        if (beam.contains("roi_h")) {
          m_standaloneCvContext->SetValue(prefix + "roi_h", std::to_string(beam["roi_h"].get<int>()));
        }
      }

      // Convenience aliases for first beam
      if (!beams.empty()) {
        m_standaloneCvContext->SetValue("bs2.beam.first.centroid_x_px", std::to_string(beams[0].value("centroid_x_px", 0.0)));
        m_standaloneCvContext->SetValue("bs2.beam.first.centroid_y_px", std::to_string(beams[0].value("centroid_y_px", 0.0)));
        if (beams[0].contains("d4sigma_x_px")) {
          m_standaloneCvContext->SetValue("bs2.beam.first.d4sigma_x_px", std::to_string(beams[0]["d4sigma_x_px"].get<double>()));
        }
        if (beams[0].contains("d4sigma_y_px")) {
          m_standaloneCvContext->SetValue("bs2.beam.first.d4sigma_y_px", std::to_string(beams[0]["d4sigma_y_px"].get<double>()));
        }
        if (beams[0].contains("side")) {
          m_standaloneCvContext->SetValue("bs2.beam.first.label", beams[0]["side"].get<std::string>());
        }
      }
    } else {
      m_standaloneCvContext->SetValue("bs2.beam.count", "0");
    }
  }

  // Display the standalone context in the table
  displayStandaloneContext();
}

void RunProgramPage::displayStandaloneContext()
{
  if (!m_standaloneCvContext) return;
  refreshContextTreeDisplay(m_standaloneCvContext.get(), "", " (live)");
}

void RunProgramPage::refreshContextTreeDisplay(SequenceContext* context, const QString& varLabelPrefix, const QString& varLabelSuffix)
{
  if (!context) return;

  QString filterText = m_contextFilterEdit->text().toLower().trimmed();
  auto summary = context->GetSummary();

  // Save expansion and selection state before rebuilding
  QSet<QString> expandedPaths;
  QSet<QString> selectedPaths;
  QSet<QString> allPreviousPaths;  // Track all known nodes to detect new ones
  bool wasEmpty = (m_contextTree->topLevelItemCount() == 0);

  if (!wasEmpty) {
    std::function<void(QTreeWidgetItem*, const QString&)> collectState;
    collectState = [&](QTreeWidgetItem* item, const QString& path) {
      allPreviousPaths.insert(path);
      if (item->isExpanded()) {
        expandedPaths.insert(path);
      }
      if (item->isSelected()) {
        selectedPaths.insert(path);
      }
      for (int i = 0; i < item->childCount(); ++i) {
        QString childPath = path + "." + item->child(i)->text(0);
        collectState(item->child(i), childPath);
      }
    };
    for (int i = 0; i < m_contextTree->topLevelItemCount(); ++i) {
      auto* item = m_contextTree->topLevelItem(i);
      collectState(item, item->text(0));
    }
  }

  m_contextTree->setUpdatesEnabled(false);
  m_contextTree->clear();

  int totalCount = 0;
  int shownCount = 0;

  // Map to track created parent nodes by their full dot-path
  std::map<QString, QTreeWidgetItem*> nodeMap;

  for (const auto& [key, value] : summary.capturedValues) {
    totalCount++;

    // Apply filter on full key
    QString keyStr = QString::fromStdString(key);
    if (!filterText.isEmpty() && !keyStr.toLower().contains(filterText)) {
      continue;
    }
    shownCount++;

    // Split key by '.' to build hierarchy
    QStringList parts = keyStr.split('.');

    // Navigate/create parent nodes for all segments except the last
    QTreeWidgetItem* parent = nullptr;
    QString currentPath;

    for (int i = 0; i < parts.size() - 1; ++i) {
      currentPath = currentPath.isEmpty() ? parts[i] : currentPath + "." + parts[i];

      auto it = nodeMap.find(currentPath);
      if (it != nodeMap.end()) {
        parent = it->second;
      } else {
        QTreeWidgetItem* node;
        if (parent) {
          node = new QTreeWidgetItem(parent);
        } else {
          node = new QTreeWidgetItem(m_contextTree);
        }
        node->setText(0, parts[i]);
        node->setForeground(0, QColor(78, 201, 176));  // Teal for group names
        QFont font = node->font(0);
        font.setBold(true);
        node->setFont(0, font);
        nodeMap[currentPath] = node;
        parent = node;
      }
    }

    // Create the leaf node (last segment)
    QTreeWidgetItem* leaf;
    if (parent) {
      leaf = new QTreeWidgetItem(parent);
    } else {
      leaf = new QTreeWidgetItem(m_contextTree);
    }
    leaf->setText(0, parts.last());
    leaf->setForeground(0, QColor(86, 156, 214));  // Blue for variable names

    // Format value and type
    QString valueStr;
    QString typeStr;
    QColor valueColor;

    switch (value.type) {
      case SequenceContext::ValueType::NUMERIC:
        valueStr = QString::number(value.numericValue, 'g', 8);
        typeStr = "Numeric";
        valueColor = QColor(181, 206, 168);  // Green
        break;
      case SequenceContext::ValueType::STRING:
        valueStr = QString::fromStdString(value.stringValue);
        typeStr = "String";
        valueColor = QColor(206, 145, 120);  // Orange
        break;
      case SequenceContext::ValueType::BOOLEAN:
        valueStr = value.booleanValue ? "true" : "false";
        typeStr = "Boolean";
        valueColor = QColor(86, 156, 214);  // Blue
        break;
      case SequenceContext::ValueType::IMAGE_FRAME:
        valueStr = "[Image Frame]";
        if (!value.tempFilePath.empty()) {
          valueStr += QString(" (%1)").arg(QString::fromStdString(value.tempFilePath));
        }
        typeStr = "Image";
        valueColor = QColor(220, 220, 170);  // Yellow
        break;
      default:
        valueStr = "(none)";
        typeStr = "None";
        valueColor = QColor(128, 128, 128);
        break;
    }

    leaf->setText(1, valueStr);
    leaf->setForeground(1, valueColor);
    leaf->setText(2, typeStr);
    leaf->setForeground(2, QColor(128, 128, 128));
  }

  // Restore expansion and selection state, or expand all on first population
  // New nodes (not in previous state) are expanded by default
  if (wasEmpty) {
    m_contextTree->expandAll();
  } else {
    std::function<void(QTreeWidgetItem*, const QString&)> restoreState;
    restoreState = [&](QTreeWidgetItem* item, const QString& path) {
      if (allPreviousPaths.contains(path)) {
        // Existing node - restore previous expand/collapse state
        item->setExpanded(expandedPaths.contains(path));
      } else {
        // New node - expand by default so user can see new data
        item->setExpanded(true);
      }
      // Restore selection state
      if (selectedPaths.contains(path)) {
        item->setSelected(true);
      }
      for (int i = 0; i < item->childCount(); ++i) {
        QString childPath = path + "." + item->child(i)->text(0);
        restoreState(item->child(i), childPath);
      }
    };
    for (int i = 0; i < m_contextTree->topLevelItemCount(); ++i) {
      auto* item = m_contextTree->topLevelItem(i);
      restoreState(item, item->text(0));
    }
  }

  m_contextTree->setUpdatesEnabled(true);

  // Update status label
  m_contextStatusLabel->setStyleSheet(QString("color: #4ec9b0; font-size: %1;").arg(m_contextLargeFont ? "18pt" : "9pt"));
  if (filterText.isEmpty()) {
    m_contextStatusLabel->setText(QString("%1 %2variables%3").arg(totalCount).arg(varLabelPrefix).arg(varLabelSuffix));
  } else {
    m_contextStatusLabel->setText(QString("%1 of %2 %3variables%4").arg(shownCount).arg(totalCount).arg(varLabelPrefix).arg(varLabelSuffix));
  }
}