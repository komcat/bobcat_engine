// RunProgramPage.cpp
#include "RunProgramPage.h"
#include "ApplicationContext.h"
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
#include "ScanExecutor.h"
#include "CameraClickToMoveWidget.h"
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
#include <QFile>
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
  , m_contextTabWidget(nullptr)        // Context tab container
  , m_contextTable(nullptr)            // Context variables table
  , m_contextFilterEdit(nullptr)       // Context filter textbox
  , m_contextStatusLabel(nullptr)      // Context status label
  , m_contextRefreshTimer(nullptr)     // Context refresh timer
  , m_contextCopyButton(nullptr)       // Copy to clipboard button
  , m_contextExportButton(nullptr)     // Export to CSV button
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
  m_leftPanel->setMinimumWidth(250);
  m_leftPanel->setMaximumWidth(350);
  QVBoxLayout* leftLayout = new QVBoxLayout(m_leftPanel);
  createLeftPanel(leftLayout);

  // Right panel (20%) - Scanning controls (create BEFORE createRightPanel)
  m_rightPanel = new QWidget();
  m_rightPanel->setMinimumWidth(300);
  m_rightPanel->setMaximumWidth(400);
  m_rightPanel->setStyleSheet("QWidget { background-color: #252526; }");

  // Center panel (60%) - Execution view (calls createExecutionWidget which needs m_rightPanel)
  createRightPanel();

  // Add to main layout with stretch factors (20/60/20 split)
  mainLayout->addWidget(m_leftPanel, 2);     // Left: 20%
  mainLayout->addWidget(m_rightStack, 6);    // Center: 60%
  mainLayout->addWidget(m_rightPanel, 2);    // Right: 20%

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

  executeTabLayout->addWidget(executeGroup, 0);

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
  layout->addSpacing(8);

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

  m_logTextEdit = new QTextEdit();
  m_logTextEdit->setReadOnly(true);
  m_logTextEdit->setStyleSheet(
    "QTextEdit { "
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

  // Connect user input response
  connect(m_userInputWidget, &UserInputWidget::responseReceived,
    this, &RunProgramPage::onUserInputResponse);

  // Add all views to stack
  m_executionStack->addWidget(m_executionTabWidget);      // Index 0: Tabs (Log + Live View)
  m_executionStack->addWidget(m_promptWidget);            // Index 1: Prompt
  m_executionStack->addWidget(m_manualAdjustWidget);      // Index 2: Manual Adjust
  m_executionStack->addWidget(m_manualAlignPointWidget);  // Index 3: Manual Align Point
  m_executionStack->addWidget(m_userInputWidget);         // Index 4: User Input
  m_executionStack->setCurrentIndex(0);                   // Start with tabs

  layout->addWidget(m_executionStack, 1);

  // === SCANNING CONTROLS (Moved to Right Panel) ===
  if (m_rightPanel) {
    QVBoxLayout* rightLayout = new QVBoxLayout(m_rightPanel);
    rightLayout->setContentsMargins(10, 10, 10, 10);
    rightLayout->setSpacing(10);

    // Title for right panel
    QLabel* scanTitle = new QLabel("Hexapod Scanning");
    scanTitle->setStyleSheet(
      "QLabel { "
      "  font-size: 16px; "
      "  font-weight: bold; "
      "  color: #ffffff; "
      "  padding: 5px; "
      "}");
    rightLayout->addWidget(scanTitle);

    // Scan widget
    m_scanWidget = new ScanControlWidget();

    // Populate channels from DataStore (initial load)
    refreshDataStoreChannels();

    // ✅ ADDED: Set up periodic channel refresh (every 3 seconds)
    // This ensures new channels (e.g., SMU channels added when polling starts) appear in the dropdown
    m_channelRefreshTimer = new QTimer(this);
    connect(m_channelRefreshTimer, &QTimer::timeout, this, &RunProgramPage::refreshDataStoreChannels);
    m_channelRefreshTimer->start(3000);  // Refresh every 3 seconds

    rightLayout->addWidget(m_scanWidget);

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

    rightLayout->addWidget(m_visionResultGroup);
    rightLayout->addStretch();  // Push controls to top

    // Connect scan widget to this page
    connect(m_scanWidget, &ScanControlWidget::scanRequested,
      this, &RunProgramPage::onScanRequested);
    connect(m_scanWidget, &ScanControlWidget::rotationScanRequested,
      this, &RunProgramPage::onRotationScanRequested);
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

  // Connect safety watch - auto-stop on critical safety flag
  if (m_context->safetyWatchManager) {
    connect(m_context->safetyWatchManager, &SafetyWatchManager::safetyTriggered,
            m_programWorker, [this](QString title, QString message, bool critical) {
      if (critical && m_programWorker) {
        std::cout << "[RunProgramPage] Critical safety flag - stopping program worker" << std::endl;
        m_programWorker->requestStop();
      }
    }, Qt::DirectConnection);  // Direct connection for immediate response
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
    if (*checkCountPtr % 10 == 0) {  // Log every second
      std::cout << "[RunProgramPage] Timer heartbeat #" << *checkCountPtr
                << ", m_isExecuting=" << m_isExecuting
                << ", worker running=" << (m_programWorker ? m_programWorker->isRunning() : false)
                << ", worker finished=" << (m_programWorker ? m_programWorker->isFinished() : false) << std::endl;
      std::cout.flush();
    }

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
  updateVisionResultImage();

  if (!success) {
    m_executionStatusLabel->setText(QString("Recipe failed: %1").arg(message));
  }

  // ✅ Chain Execution: Find and queue dependent recipes
  std::cout << "[RunProgramPage] Chain execution check: success=" << success
            << ", chainEnabled=" << m_chainExecutionCheckBox->isChecked()
            << ", autoRunNext=" << m_autoRunNextCheckBox->isChecked() << std::endl;
  std::cout.flush();

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

        // Countdown label
        QLabel* countdownLabel = new QLabel("Auto-continue in 10 seconds...");
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
        int* countdown = new int(10);
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
          m_logTextEdit->append(QString("<span style='color: #f39c12;'>→ Chain: Recipe %1 (%2) will run next</span>")
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

          m_logTextEdit->append(QString("<span style='color: #e74c3c;'>✗ Chain execution stopped by user</span>"));
        }

        break;  // Only process one recipe at a time
      } else if (!failedCondition.empty()) {
        // Log skipped due to condition
        std::cout << "[RunProgramPage] Chain execution: Recipe " << (i + 1)
                  << " skipped - condition not met: " << failedCondition << std::endl;
        std::cout.flush();

        m_logTextEdit->append(QString("<span style='color: #95a5a6;'>⊘ Recipe %1 skipped - condition not met</span>")
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

  // ✅ Reset command tracker
  m_cmdPastLabel->setText("-");
  m_cmdCurrentLabel->setText(success ? "Completed" : "Failed");
  m_cmdNext1Label->setText("-");
  m_cmdNext2Label->setText("-");
  m_recipeStepNames.clear();

  std::cout << "[RunProgramPage] Updating execute button..." << std::endl;
  std::cout.flush();

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
    onProgramExecutionCompleted(true, "Execution completed (via finished signal)");
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
  m_logTextEdit->append(message);

  // Also print to console for debugging
  std::cout << "[RunProgramPage] " << message.toStdString() << std::endl;

  // ✅ WORKAROUND: Detect completion markers since normal signals aren't being delivered
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

  // Note: Command tracker is now updated by onActionStarted for more granular tracking
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

void RunProgramPage::onActionStarted(int recipeIndex, QString current, QString next, QString afterNext)
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
  // Log the stop action - note: device will return to global peak position
  m_logTextEdit->append("Stopping scan - returning to global peak position...");

  // Stop any active scan (this will block until return to peak is complete)
  m_scanExecutor->stopScan();

  // Re-enable scan controls, disable stop button
  m_scanWidget->setScanButtonsEnabled(true);
  m_scanWidget->setStopButtonEnabled(false);

  // Log completion
  m_logTextEdit->append("Scan stopped - device returned to peak position");
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

  // Open the graphical dependency window
  DependencyGraphWindow* graphWindow = new DependencyGraphWindow(m_programManager, this);
  graphWindow->setAttribute(Qt::WA_DeleteOnClose);
  graphWindow->exec();

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

  QAction* renameAction = contextMenu.addAction("Rename Recipe...");
  QAction* selectedAction = contextMenu.exec(m_editRecipeTable->viewport()->mapToGlobal(pos));

  if (selectedAction == renameAction) {
    renameRecipe(row);
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

  // Update display name in ProgramManager
  m_programManager->setRecipeDisplayName(recipeIndex, newName.toStdString());

  // Also update the recipe file itself
  RecipeManager tempRecipeManager;
  if (tempRecipeManager.loadRecipe(recipeFilePath.toStdString())) {
    tempRecipeManager.setRecipeName(newName.toStdString());
    if (tempRecipeManager.saveRecipe(recipeFilePath.toStdString())) {
      QMessageBox::information(this, "Recipe Renamed",
        QString("Recipe renamed to \"%1\" and saved to file.").arg(newName));
    } else {
      QMessageBox::warning(this, "Save Failed",
        "Recipe display name updated but failed to save to recipe file.");
    }
  } else {
    QMessageBox::warning(this, "Load Failed",
      "Recipe display name updated but failed to load recipe file for saving.");
  }

  // Refresh the table to show new name
  updateRecipeList();

  // Re-select the same row
  m_editRecipeTable->selectRow(recipeIndex);
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

  // Header row with filter and status
  QHBoxLayout* headerLayout = new QHBoxLayout();

  QLabel* filterLabel = new QLabel("Filter:");
  filterLabel->setStyleSheet("color: #d4d4d4; font-weight: bold;");
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
    "} "
    "QLineEdit:focus { "
    "  border: 1px solid #0e639c; "
    "}"
  );
  headerLayout->addWidget(m_contextFilterEdit, 1);

  m_contextStatusLabel = new QLabel("No context data");
  m_contextStatusLabel->setStyleSheet("color: #888888; font-style: italic;");
  headerLayout->addWidget(m_contextStatusLabel);

  contextLayout->addLayout(headerLayout);

  // Context variables table
  m_contextTable = new QTableWidget();
  m_contextTable->setColumnCount(3);
  m_contextTable->setHorizontalHeaderLabels({"Variable", "Value", "Type"});
  m_contextTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_contextTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_contextTable->setAlternatingRowColors(true);

  // Column widths
  m_contextTable->setColumnWidth(0, 200);  // Variable name
  m_contextTable->setColumnWidth(1, 300);  // Value
  m_contextTable->horizontalHeader()->setStretchLastSection(true);  // Type stretches

  m_contextTable->verticalHeader()->setVisible(false);
  m_contextTable->setSortingEnabled(true);

  m_contextTable->setStyleSheet(
    "QTableWidget { "
    "  background-color: #1e1e1e; "
    "  border: 1px solid #3f3f46; "
    "  gridline-color: #3f3f46; "
    "  color: #d4d4d4; "
    "} "
    "QTableWidget::item { "
    "  padding: 4px; "
    "} "
    "QTableWidget::item:alternate { "
    "  background-color: #252526; "
    "} "
    "QTableWidget::item:selected { "
    "  background-color: #0e639c; "
    "  color: white; "
    "} "
    "QHeaderView::section { "
    "  background-color: #2d2d30; "
    "  color: #d4d4d4; "
    "  padding: 6px; "
    "  border: 1px solid #3f3f46; "
    "  font-weight: bold; "
    "}"
  );

  contextLayout->addWidget(m_contextTable, 1);

  // Button row for copy/export
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->addStretch();

  m_contextCopyButton = new QPushButton("Copy to Clipboard");
  m_contextCopyButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #3c3c3c; "
    "  color: #d4d4d4; "
    "  border: 1px solid #555555; "
    "  border-radius: 3px; "
    "  padding: 6px 12px; "
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
    "  padding: 6px 12px; "
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

  // Create refresh timer (but don't start yet)
  m_contextRefreshTimer = new QTimer(this);
  m_contextRefreshTimer->setInterval(500);  // 500ms update interval
  connect(m_contextRefreshTimer, &QTimer::timeout,
    this, [this]() { updateContextTable(false); });

  std::cout << "[RunProgramPage] Context tab created" << std::endl;
}

void RunProgramPage::updateContextTable(bool forceUpdate)
{
  // Safety check: Don't access context if worker is not actively running
  // This prevents race conditions during worker thread shutdown
  // But preserve the existing table contents (don't clear them)
  // UNLESS forceUpdate is true - used for final update before worker deletion
  if (!forceUpdate && (!m_programWorker || !m_programWorker->isRunning())) {
    // Keep existing table data visible, just update status to show it's from last run
    if (m_contextTable->rowCount() > 0) {
      m_contextStatusLabel->setText(QString("%1 variables (last run)").arg(m_contextTable->rowCount()));
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
    // Keep existing table data if we have any
    if (m_contextTable->rowCount() > 0) {
      m_contextStatusLabel->setText(QString("%1 variables (last run)").arg(m_contextTable->rowCount()));
      m_contextStatusLabel->setStyleSheet("color: #888888; font-style: italic;");
    } else {
      m_contextStatusLabel->setText("No context data");
      m_contextStatusLabel->setStyleSheet("color: #888888; font-style: italic;");
    }
    return;
  }

  // Get current filter text
  QString filterText = m_contextFilterEdit->text().toLower().trimmed();

  // Get summary from context (thread-safe via internal mutex)
  auto summary = context->GetSummary();

  // Temporarily disable sorting for performance
  m_contextTable->setSortingEnabled(false);
  m_contextTable->setRowCount(0);

  int totalCount = 0;
  int shownCount = 0;

  for (const auto& [key, value] : summary.capturedValues) {
    totalCount++;

    // Apply filter
    QString keyStr = QString::fromStdString(key);
    if (!filterText.isEmpty() && !keyStr.toLower().contains(filterText)) {
      continue;
    }

    shownCount++;
    int row = m_contextTable->rowCount();
    m_contextTable->insertRow(row);

    // Variable name
    QTableWidgetItem* keyItem = new QTableWidgetItem(keyStr);
    keyItem->setForeground(QColor(86, 156, 214));  // Blue for variable names
    m_contextTable->setItem(row, 0, keyItem);

    // Value and type
    QString valueStr;
    QString typeStr;
    QColor valueColor;

    switch (value.type) {
      case SequenceContext::ValueType::NUMERIC:
        valueStr = QString::number(value.numericValue, 'g', 8);
        typeStr = "Numeric";
        valueColor = QColor(181, 206, 168);  // Green for numbers
        break;
      case SequenceContext::ValueType::STRING:
        valueStr = QString::fromStdString(value.stringValue);
        typeStr = "String";
        valueColor = QColor(206, 145, 120);  // Orange for strings
        break;
      case SequenceContext::ValueType::BOOLEAN:
        valueStr = value.booleanValue ? "true" : "false";
        typeStr = "Boolean";
        valueColor = QColor(86, 156, 214);  // Blue for booleans
        break;
      case SequenceContext::ValueType::IMAGE_FRAME:
        valueStr = "[Image Frame]";
        if (!value.tempFilePath.empty()) {
          valueStr += QString(" (%1)").arg(QString::fromStdString(value.tempFilePath));
        }
        typeStr = "Image";
        valueColor = QColor(220, 220, 170);  // Yellow for images
        break;
      default:
        valueStr = "(none)";
        typeStr = "None";
        valueColor = QColor(128, 128, 128);  // Gray for none
        break;
    }

    QTableWidgetItem* valueItem = new QTableWidgetItem(valueStr);
    valueItem->setForeground(valueColor);
    m_contextTable->setItem(row, 1, valueItem);

    // Type
    QTableWidgetItem* typeItem = new QTableWidgetItem(typeStr);
    typeItem->setForeground(QColor(128, 128, 128));  // Gray for type
    m_contextTable->setItem(row, 2, typeItem);
  }

  // Re-enable sorting
  m_contextTable->setSortingEnabled(true);

  // Update status label
  m_contextStatusLabel->setStyleSheet("color: #4ec9b0;");  // Teal color
  if (filterText.isEmpty()) {
    m_contextStatusLabel->setText(QString("%1 variables").arg(totalCount));
  } else {
    m_contextStatusLabel->setText(QString("%1 of %2 variables").arg(shownCount).arg(totalCount));
  }
}

void RunProgramPage::onContextFilterChanged(const QString& filter)
{
  Q_UNUSED(filter);
  // Immediately update the table with new filter
  updateContextTable();
}

void RunProgramPage::startContextRefreshTimer()
{
  if (m_contextRefreshTimer && !m_contextRefreshTimer->isActive()) {
    std::cout << "[RunProgramPage] Starting context refresh timer (500ms)" << std::endl;
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
  if (!m_contextTable || m_contextTable->rowCount() == 0) {
    std::cout << "[RunProgramPage] No context data to copy" << std::endl;
    return;
  }

  // Build tab-separated text from table contents
  QString clipboardText;
  clipboardText += "Variable\tValue\tType\n";  // Header row

  for (int row = 0; row < m_contextTable->rowCount(); ++row) {
    QTableWidgetItem* varItem = m_contextTable->item(row, 0);
    QTableWidgetItem* valueItem = m_contextTable->item(row, 1);
    QTableWidgetItem* typeItem = m_contextTable->item(row, 2);

    QString varName = varItem ? varItem->text() : "";
    QString value = valueItem ? valueItem->text() : "";
    QString type = typeItem ? typeItem->text() : "";

    clipboardText += QString("%1\t%2\t%3\n").arg(varName, value, type);
  }

  // Copy to clipboard
  QClipboard* clipboard = QApplication::clipboard();
  clipboard->setText(clipboardText);

  std::cout << "[RunProgramPage] Copied " << m_contextTable->rowCount() << " context variables to clipboard" << std::endl;

  // Brief visual feedback - change button text temporarily
  m_contextCopyButton->setText("Copied!");
  QTimer::singleShot(1500, this, [this]() {
    m_contextCopyButton->setText("Copy to Clipboard");
  });
}

void RunProgramPage::onContextExportToCsv()
{
  if (!m_contextTable || m_contextTable->rowCount() == 0) {
    std::cout << "[RunProgramPage] No context data to export" << std::endl;
    return;
  }

  // Generate default filename with timestamp
  QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
  QString defaultFileName = QString("context_export_%1.csv").arg(timestamp);

  // Open file dialog
  QString filePath = QFileDialog::getSaveFileName(
    this,
    "Export Context to CSV",
    defaultFileName,
    "CSV Files (*.csv);;All Files (*)"
  );

  if (filePath.isEmpty()) {
    return;  // User cancelled
  }

  // Open file for writing
  QFile file(filePath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    std::cerr << "[RunProgramPage] Failed to open file for writing: " << filePath.toStdString() << std::endl;
    return;
  }

  QTextStream stream(&file);

  // Write CSV header
  stream << "Variable,Value,Type\n";

  // Write data rows
  for (int row = 0; row < m_contextTable->rowCount(); ++row) {
    QTableWidgetItem* varItem = m_contextTable->item(row, 0);
    QTableWidgetItem* valueItem = m_contextTable->item(row, 1);
    QTableWidgetItem* typeItem = m_contextTable->item(row, 2);

    QString varName = varItem ? varItem->text() : "";
    QString value = valueItem ? valueItem->text() : "";
    QString type = typeItem ? typeItem->text() : "";

    // Escape quotes in values and wrap in quotes if contains comma
    auto escapeForCsv = [](const QString& str) -> QString {
      QString escaped = str;
      escaped.replace("\"", "\"\"");  // Double up quotes
      if (escaped.contains(',') || escaped.contains('"') || escaped.contains('\n')) {
        escaped = QString("\"%1\"").arg(escaped);
      }
      return escaped;
    };

    stream << escapeForCsv(varName) << "," << escapeForCsv(value) << "," << escapeForCsv(type) << "\n";
  }

  file.close();

  std::cout << "[RunProgramPage] Exported " << m_contextTable->rowCount() << " context variables to: " << filePath.toStdString() << std::endl;

  // Brief visual feedback
  m_contextExportButton->setText("Exported!");
  QTimer::singleShot(1500, this, [this]() {
    m_contextExportButton->setText("Export to CSV");
  });
}