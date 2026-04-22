// RunProgramPage.h
#pragma once
#include "DataStore.h"
#include <QWidget>
#include <QPushButton>
#include <QProgressBar>
#include <QListWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QCheckBox>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QTimer>  // For periodic channel refresh
#include <QTabWidget>  // For tab layout
#include <QGroupBox>  // For vision result group
#include <QComboBox>  // For slot selector
#include <vector>
#include <string>
#include <memory>

struct ApplicationContext;
class SequenceContext;
class ProgramManager;
class ProgramWorker;
class ProcessSequenceFactory;
class UserPromptWidget;
class ManualAdjustWidget;
class ManualAlignPointWidget;
class UserInputWidget;
class RetraceEditWidget;
#include "ScanControlWidget.h"
class ScanExecutor;
enum class ScanMode;
class TransformationMatrixLoader;
class CameraClickToMoveWidget;
class RollCorrectionWidget;
class PlacementRetraceWidget;
class MotionInspectorWidget;
#include "PlacementCorrectionData.h"  // PlacementCorrectionSet (value member)
#include "../EdgeTransferTrainer/EdgeTransferData.h"  // EdgeTransferSet (value member)
#include "../../sequence/EdgeTransferRetrace.h"       // EdgeTransferRetraceOutcome
class QVBoxLayout;
class QHBoxLayout;
class QEventLoop;
class SequenceContext;

/**
 * @brief Run Program Page - Execute programs (collections of recipes)
 *
 * Layout: 20% left panel (controls) | 80% right panel (edit/execution view)
 */
class RunProgramPage : public QWidget
{
  Q_OBJECT

public:
  explicit RunProgramPage(ApplicationContext* context, TransformationMatrixLoader* matrixLoader, QWidget* parent = nullptr);
  ~RunProgramPage();

public slots:
  void onProductChanged();  // Called when product is loaded/created in MainMenu
  void applyTheme();

signals:
  void backToMainMenu();

  // User prompt signal
  void showUserPrompt(QString title, QString message);

  // Manual adjust signal
  void showManualAdjust(QString message, QString deviceName, double stepSizeUm,
                        bool enableX, bool enableY, bool enableZ);

  // Placement correction retrace signal
  void showPlacementRetrace(QString trainingSetName, bool autoStart,
                            bool autoDetect, bool autoSaveContinue);

  // Edge Transfer retrace signal (cross-thread bridge for the callback).
  void showEdgeTransferRetrace(QString trainingSetName);

  // Request to navigate to RecipePage and load a recipe for editing
  void requestEditRecipe(QString filePath);

private slots:
  // File operations
  void onNewProgram();
  void onLoadProgram();
  void onSaveProgram();
  void onSaveAsProgram();
  void onEditProgram();

  // Execution
  void onExecuteProgram();
  void onStopProgram();

  // Program execution signals
  void onProgramExecutionStarted(int totalRecipes);
  void onRecipeStarted(int recipeIndex, QString recipeName);
  void onRecipeCompleted(int recipeIndex, bool success, QString message);
  void onRecipeSkipped(int recipeIndex, QString recipeName);  // ✅ NEW: Handle skipped recipes
  void onProgramExecutionCompleted(bool success, QString message);
  void onProgramStopped();
  void onWorkerLogMessage(QString message);

  // ✅ Step-level progress tracking (for progress bar)
  void onStepBuildStarted(int recipeIndex, int stepIndex, QString stepName, QString stepType);
  void onStepBuildCompleted(int recipeIndex, int stepIndex);
  void onSequenceExecutionStarted(int recipeIndex);
  void onStepExecutionCompleted(int recipeIndex, int stepIndex, bool success);
  void onWaitProgress(int remainingMs, int totalMs);
  void onWorkerFinished();  // ✅ Handle QThread::finished()

  // ✅ Action-level tracking with look-ahead (granular command display)
  void onActionStarted(int recipeIndex, QString current, QString next, QString afterNext);

  // ✅ Loop execution tracking
  void onLoopIterationStarted(int iteration);
  void onLoopIterationCompleted(int iteration);
  void onConfigureLoop();            // Open loop configuration dialog
  void updateLoopStatusLabel();      // Update loop status display

  // User prompt handling
  void onShowUserPrompt(QString title, QString message);
  void onPromptResponse(bool accepted);

  // Manual adjust handling
  void onShowManualAdjust(QString message, QString deviceName, double stepSizeUm,
                          bool enableX, bool enableY, bool enableZ);
  void onManualAdjustResponse(bool completed);

  // Manual align point handling
  void onManualAlignPointResponse(bool completed);

  // User input handling
  void onUserInputResponse(bool accepted, QString inputText);

  // Scanning integration (refactored)
  void onScanRequested(QString deviceName, ScanControlWidget::ScanMode mode, QString channel);
  void onScanLogMessage(QString message);
  void onScanCompleted(QString deviceName, double peakValue, bool success);
  void onScanError(QString errorMessage);
  void onStopScanRequested();
  void onChannelChanged(QString channel);  // ✅ ADDED

  // ✅ ADDED: Periodic channel list refresh
  void refreshDataStoreChannels();

  // Placement correction retrace handling
  void onShowPlacementRetrace(QString trainingSetName, bool autoStart,
                              bool autoDetect, bool autoSaveContinue);
  void onPlacementRetraceCompleted(const PlacementCorrectionSet& updatedSet);
  void onPlacementRetraceSkipped();
  void onPlacementRetraceEnded();

  // Edge Transfer retrace handling
  void onShowEdgeTransferRetrace(QString trainingSetName);
  void onEdgeTransferRetraceOutcome(EdgeTransferRetraceOutcome outcome);

  // Roll correction handling
  void onRegisterCH1Requested(QString deviceName);
  void onRegisterCH2Requested(QString deviceName);
  void onMoveToCH1Requested(QString deviceName);
  void onMoveToCH2Requested(QString deviceName);
  void onCorrectWRequested(QString deviceName, QString channel, double angleDeg, bool positiveW, double halfwayXmm);
  void onFineGridScanRequested(QString deviceName);
  void onStopGridScanRequested();
  void onGridScanFinished(bool success, QString deviceName, QString errorMessage);
  void onRollCorrectionFinished(bool success, QString deviceName, QString message);
  void onRollCorrectionClearRequested(QString deviceName);

private:
  void setupUI();
  void createLeftPanel(QVBoxLayout* leftLayout);
  void createRightPanel();

  void showEditWidget();
  void showExecutionWidget();

  void updateProgramInfo();
  void updateRecipeList();
  void updateExecuteButton();

  // Settings persistence
  void saveLastProgramToSettings(const QString& programPath);
  void loadLastProgramFromSettings();
  void saveRecentPrograms(const QString& programPath);
  void loadRecentProgramsList();
  void onRecentProgramClicked(QListWidgetItem* item);

  ApplicationContext* m_context;
  TransformationMatrixLoader* m_matrixLoader;
  ProgramManager* m_programManager;
  ProgramWorker* m_programWorker;
  ProcessSequenceFactory* m_processFactory;

  // Left panel widgets
  QWidget* m_leftPanel;
  QTabWidget* m_tabWidget;  // ✅ ADDED: Tab widget for File/Execute tabs
  QLabel* m_currentProductLabel;  // Product display
  QLabel* m_chainIndicator;       // Chain execution indicator (orange dot)
  QLabel* m_loopIndicator;        // Loop execution indicator (teal dot)
  QPushButton* m_newButton;
  QPushButton* m_loadButton;
  QPushButton* m_saveButton;
  QPushButton* m_saveAsButton;
  QPushButton* m_editButton;
  QListWidget* m_recentProgramsList;  // Recent programs list in File tab
  QPushButton* m_executeButton;
  QProgressBar* m_progressBar;
  QLabel* m_programNameLabel;
  QLabel* m_programPathLabel;  // ✅ ADDED: Display program file path
  QListWidget* m_recipeListWidget;
  QPushButton* m_backButton;
  QCheckBox* m_autoRunNextCheckBox;
  QCheckBox* m_useDependenciesCheckBox;  // ✅ NEW: Enable dependency-based execution
  QCheckBox* m_chainExecutionCheckBox;   // ✅ NEW: Auto-trigger dependent recipes after completion
  QCheckBox* m_loopEnabledCheckBox;      // ✅ NEW: Enable infinite loop execution
  QCheckBox* m_ignoreDependenciesCheckBox; // ✅ NEW: Ignore dependencies, force sequential
  QPushButton* m_configureLoopButton;    // ✅ NEW: Open loop configuration dialog
  QLabel* m_loopStatusLabel;             // ✅ NEW: Shows loop status (e.g., "3 recipes in loop")

  // Right panel
  QStackedWidget* m_rightStack;
  QWidget* m_editWidget;
  QWidget* m_executionWidget;

  // Edit widget components
  QLineEdit* m_programNameEdit;
  QPushButton* m_addRecipeButton;
  QPushButton* m_addScriptButton;
  QTableWidget* m_editRecipeTable;
  QTextEdit* m_recipePreviewText;
  QPushButton* m_removeRecipeButton;
  QPushButton* m_moveUpButton;
  QPushButton* m_moveDownButton;
  QPushButton* m_toggleEnableButton;  // ✅ NEW: Toggle enable/disable
  QPushButton* m_saveEditButton;

  // Execution widget components
  QLabel* m_executionTitleLabel;  // Shows "Program Execution" or program file name
  QLabel* m_executionStatusLabel;
  QLabel* m_currentRecipeLabel;
  QLabel* m_executionLogLabel;
  QLabel* m_programStatusLabel;   // Shows "Idle", "Running", "Stopped"
  // Base (non-scan) status snapshot — what the badge should revert to once
  // any active scan ends. Set via setProgramStatus(); paint via applyProgramStatusLabel().
  // Defaults match the "Idle" appearance; setProgramStatus() at runtime reads
  // from Theme::Color::* so a palette swap takes effect without code changes.
  QString m_baseStatusText = "Idle";
  QString m_baseStatusBgColor;    // initialized in ctor body from Theme::Color::bgSurface
  QString m_baseStatusFgColor;    // initialized in ctor body from Theme::Color::textDisabled
  bool m_isScanActive = false;
  void setProgramStatus(const QString& text, const QString& bgColor, const QString& fgColor);
  void applyProgramStatusLabel();
  QPlainTextEdit* m_logTextEdit;  // Using QPlainTextEdit for better performance with large logs
  QTabWidget* m_executionTabWidget;  // Tab widget for Log and Live View
  CameraClickToMoveWidget* m_liveViewWidget;  // Live camera view widget

  // Command tracker row (4 cells: past, current, next1, next2)
  QWidget* m_commandTrackerRow;
  QLabel* m_cmdPastLabel;
  QLabel* m_cmdCurrentLabel;
  QLabel* m_cmdNext1Label;
  QLabel* m_cmdNext2Label;
  // Single-row human-readable description of the action currently running.
  QLabel* m_actionDetailLabel = nullptr;
  std::vector<std::string> m_recipeStepNames;  // Store step names for current recipe

  // Scanning controls (refactored)
  ScanControlWidget* m_scanWidget;
  ScanExecutor* m_scanExecutor;
  RollCorrectionWidget* m_rollCorrectionWidget = nullptr;
  PlacementRetraceWidget* m_placementRetraceWidget = nullptr;
  MotionInspectorWidget* m_motionInspectorWidget = nullptr;
  int m_motionInspectorTabIndex = -1;  // Index of the Motion Inspector tab in m_executionTabWidget
  PlacementCorrectionSet m_activePlacementSet;          // Loaded set (owned copy, pointer passed to widget)
  std::string m_activePlacementStoreBasePath;           // Base path for persisting tuned offsets
  bool m_placementRetraceResponse = false;

  // Edge Transfer retrace — embedded widget + outcome delivered via callback.
  class EdgeRetraceWidget* m_edgeRetraceWidget = nullptr;
  EdgeTransferSet m_activeEdgeTransferSet;              // Loaded set for the widget
  EdgeTransferRetraceOutcome m_edgeRetraceResponse = EdgeTransferRetraceOutcome::Cancelled;
  std::unique_ptr<SequenceContext> m_gridScanContext;  // Context for grid scan stop control
  std::unique_ptr<SequenceContext> m_rollCorrectionContext;  // Context for stepwise roll correction stop control
  bool m_rollCorrectionRunning = false;
  QTimer* m_channelRefreshTimer;  // Periodic refresh of DataStore channels

  // Prompt system
  QStackedWidget* m_executionStack;  // Switch between log and prompt/adjust/input
  UserPromptWidget* m_promptWidget;
  ManualAdjustWidget* m_manualAdjustWidget;
  ManualAlignPointWidget* m_manualAlignPointWidget;  // ✅ Manual align point widget
  UserInputWidget* m_userInputWidget;
  RetraceEditWidget* m_retraceEditWidget;  // ✅ Retrace edit widget
  QEventLoop* m_promptEventLoop;     // For blocking until response
  bool m_promptResponse;             // Store user's response
  bool m_manualAdjustResponse;       // Store manual adjust response
  bool m_manualAlignPointResponse;   // ✅ Store manual align point response
  bool m_userInputAccepted;          // Store user input accepted status
  QString m_userInputText;           // Store user input text
  bool m_retraceEditContinue;        // ✅ Store retrace edit continue status
  bool m_retraceEditSave;            // ✅ Store retrace edit save status
  bool m_isRetraceEditMode;          // ✅ Currently in retrace edit mode
  int m_retraceEditRecipeIndex;      // ✅ Recipe index being retrace-edited

  // Slot System UI - Execute Tab (left panel)
  QComboBox* m_slotSelector = nullptr;
  QLabel* m_slotInfoLabel = nullptr;  // "X recipes in this slot"

  // Slot System UI - Edit Widget
  QComboBox* m_editSlotSelector = nullptr;
  QPushButton* m_renameSlotButton = nullptr;
  QPushButton* m_addSlotButton = nullptr;
  QPushButton* m_deleteSlotButton = nullptr;
  QPushButton* m_duplicateSlotButton = nullptr;

  // State
  QString m_currentProgramPath;
  bool m_isExecuting;
  int m_totalRecipes;
  int m_currentRecipeIndex;
  std::vector<bool> m_completedRecipes;  // Track completed recipes (persists across executions)
  int m_pendingChainRecipeIndex;         // Next recipe to run in chain execution mode (-1 = none)

  // ✅ Step-level progress tracking
  int m_currentRecipeTotalSteps;    // Total steps in current recipe
  int m_currentRecipeCompletedSteps; // Completed steps in current recipe

  // ✅ Loop execution tracking
  int m_currentLoopIteration;       // Current loop iteration (0 = not looping)
  bool m_isLoopExecution;           // True when running in loop mode (skip heavy UI updates)

  void createEditWidget();
  void createExecutionWidget();

  // Slot System handlers
  void onSlotSelectionChanged(int index);      // Execute tab slot dropdown
  void onEditSlotSelectionChanged(int index);  // Edit widget slot dropdown
  void onRenameSlot();
  void onAddSlot();
  void onDeleteSlot();
  void onDuplicateSlot();
  void updateSlotSelectors();  // Refresh both slot dropdowns
  void updateSlotInfo();       // Update "X recipes" label

  // Edit widget slots
  void onAddRecipe();
  void onAddScript();
  void onRemoveRecipe();
  void onMoveRecipeUp();
  void onMoveRecipeDown();
  void onToggleRecipeEnabled();  // Toggle enable/disable for selected recipe
  void onManageDependencies();   // Open dependency management dialog
  void onSaveEdit();
  void onEditRecipeSelectionChanged();
  void loadRecipePreview(const std::string& recipeFilePath);
  void onRecipeTableContextMenu(const QPoint& pos);  // Context menu for recipe table (edit view)
  void onRecipeListContextMenu(const QPoint& pos);   // Context menu for recipe list (execution view)
  void renameRecipe(int recipeIndex);                // Rename recipe dialog

  // ✅ Retrace edit mode
  void onStartRetraceEdit(int recipeIndex);          // Start retrace edit for a recipe
  void onRetraceEditResponse(bool continueExecution, bool saveRequested);
  void showRetraceEditSummary(int positionsVisited, int positionsSaved);

  DataStore* m_dataStore;  // Add this
  QString m_currentSubscribedChannel;  // Track subscription

  // Vision result image display
  QGroupBox* m_visionResultGroup;        // ✅ Vision result display group
  QLabel* m_visionResultImageLabel;      // ✅ Vision result image display
  SequenceContext* m_currentContext;     // ✅ Current execution context
  void updateVisionResultImage();        // ✅ Update vision result image from context
  void clearVisionResult();              // ✅ Clear stale vision result

  // Vision history navigation
  QWidget* m_visionNavWidget;            // Container for navigation controls
  QPushButton* m_visionPrevButton;       // Previous result button
  QPushButton* m_visionNextButton;       // Next result button
  QLabel* m_visionIndexLabel;            // Shows "3 / 5" index
  std::vector<QString> m_visionHistory;  // List of history image paths
  int m_visionHistoryViewIndex;          // Currently viewed index (-1 = latest)
  static const int MAX_VISION_HISTORY = 10;

  // Vision history methods
  void onVisionResultReady(QString imagePath);  // Slot for new vision result
  void onVisionPrevious();                      // Show previous result
  void onVisionNext();                          // Show next result
  void updateVisionNavigation();                // Update nav button states and label
  void loadVisionHistoryImage(int index);       // Load specific history image
  void clearVisionHistory();                    // Clear history on new program run

  // Context Tab Components (real-time context variable viewer with tree hierarchy)
  QWidget* m_contextTabWidget;           // Container widget for context tab
  QTreeWidget* m_contextTree;            // Tree showing context variables (dot-separated hierarchy)
  QLineEdit* m_contextFilterEdit;        // Filter/search textbox
  QLabel* m_contextStatusLabel;          // Status label (e.g., "12 variables")
  QTimer* m_contextRefreshTimer;         // 500ms refresh timer
  QPushButton* m_contextCopyButton;      // Copy to clipboard button
  QPushButton* m_contextExportButton;    // Export to CSV button
  QPushButton* m_contextRefreshCvButton; // Refresh CV inject button
  QPushButton* m_contextExpandAllButton; // Expand all tree nodes
  QPushButton* m_contextCollapseAllButton; // Collapse all tree nodes
  QPushButton* m_contextFontToggleButton = nullptr; // Toggle small/large font
  bool m_contextLargeFont = false;        // Current font mode (false=small/default, true=large/18pt)
  std::unique_ptr<SequenceContext> m_standaloneCvContext;  // Standalone context for CV refresh
  QTimer* m_cvAutoRefreshTimer = nullptr;  // Auto-refresh timer for CV data

  // Context tab methods
  void createContextTab();                             // Creates the context tab UI
  void updateContextTable(bool forceUpdate = false);   // Refreshes tree from context
  void onContextFilterChanged(const QString& filter);  // Handles filter text changes
  void startContextRefreshTimer();                     // Starts the 500ms timer
  void stopContextRefreshTimer();                      // Stops the timer
  void onContextCopyToClipboard();                     // Copy context to clipboard
  void onContextExportToCsv();                         // Export context to CSV file
  void onContextRefreshCv();                           // Refresh CV inject into standalone context
  void displayStandaloneContext();                     // Display standalone context in tree
  void refreshContextTreeDisplay(SequenceContext* context, const QString& varLabelPrefix, const QString& varLabelSuffix);  // Shared tree builder
  void applyContextFontSize();                         // Apply current font size to all context tab widgets
};