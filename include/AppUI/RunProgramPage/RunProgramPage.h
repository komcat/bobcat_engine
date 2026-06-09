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
#include <atomic>
#include <QTabWidget>  // For tab layout
#include <QGroupBox>  // For vision result group
#include <vector>
#include <string>
#include <memory>

struct ApplicationContext;
class SequenceContext;
class ProgramManager;
class ProgramWorker;
class ProcessSequenceFactory;
#include "ScanControlWidget.h"
class TransformationMatrixLoader;
class CameraClickToMoveWidget;
class RollCorrectionWidget;
class RollCorrectionController;
class GlobalMaximizeWidget;
class GlobalMaximizeController;
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

  // Cross-thread bridges for the embedded retrace flows. The page's
  // handlers do data loading (training-set load, store-base resolution)
  // before switching the overlay's index — these signals let the
  // recipe worker thread trigger that work on the UI thread.
  void showPlacementRetrace(QString trainingSetName, bool autoStart,
                            bool autoDetect, bool autoSaveContinue);
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

  // DUT initialization — prompts for SN and dispatches dut/LoadOrCreate
  // through DUTCommandHandler. On success the SN is overlaid on the
  // status badge (see applyProgramStatusLabel).
  void onInitDutClicked();

  // Closes the active DUT via dut/Close and clears the SN overlay.
  void onCloseDutClicked();

  // "Snapshot" quick-action — prompts for a label and dispatches
  // system/CaptureSnapshot through a stack-local SystemCommandHandler.
  void onCaptureSnapshotClicked();

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

  // ✅ Action-level tracking with look-ahead (granular command display)
  void onActionStarted(int recipeIndex, QString current, QString next, QString afterNext);

  // ✅ Loop execution tracking
  void onLoopIterationStarted(int iteration);
  void onLoopIterationCompleted(int iteration);
  void onConfigureLoop();            // Open loop configuration dialog
  void updateLoopStatusLabel();      // Update loop status display

  // The 5 simple overlay slots (UserPrompt / ManualAdjust /
  // ManualAlignPoint / UserInput / RetraceEdit) live inside
  // UserInteractionOverlay; the page no longer needs onShow*/onResponse
  // handlers for them.

  // Scan tab — DataStore subscription glue (controller handles executor).
  void onChannelChanged(QString channel);

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

  // Roll correction is handled by RollCorrectionController (owns the widget
  // signal wiring, worker threads and stop flags) — see m_rollCorrectionController.

private:
  void setupUI();
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
  ProcessSequenceFactory* m_processFactory;

  // Owns the ProgramWorker, callback wiring, and execution lifecycle.
  // Page subscribes to its forwarded signals for UI updates.
  class ExecutionController* m_executionController = nullptr;

  // Left panel — owns its own widgets (back button, product banner,
  // 3-tab pane with file actions / execute button + recipe list /
  // option checkboxes). The page accesses the inner widgets via
  // typed getters when it needs to wire signals or read state.
  class RunPageLeftPanel* m_leftPanel = nullptr;


  // Right panel
  QStackedWidget* m_rightStack;
  QWidget* m_editWidget;
  QWidget* m_executionWidget;

  // Edit widget components. The recipe table, preview pane, and the
  // Add/Remove/Move/Toggle/ManageDeps buttons all live inside
  // ProgramEditPanel; the page keeps the program-name field, the slot
  // management bar, and the top-bar Save button.
  QLineEdit* m_programNameEdit;
  class ProgramEditPanel* m_editPanel = nullptr;
  QPushButton* m_saveEditButton;
  // Wraps m_programNameEdit so we can rewrite its title to "Program
  // Name *" when there are unsaved edits. setProgramDirty() drives this.
  class QGroupBox* m_programNameGroup = nullptr;
  bool m_programDirty = false;
  void setProgramDirty(bool dirty);

  // Execution view header strip — the 250x150 status badge, program
  // info labels (title / status / current recipe), the 4-cell
  // command tracker, the action detail line, and the progress bar
  // all live in ExecutionStatusWidget. The page accesses inner
  // widgets via getters when it updates UI in response to execution
  // events.
  class ExecutionStatusWidget* m_executionStatus = nullptr;

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

  QLabel* m_executionLogLabel;
  QPlainTextEdit* m_logTextEdit;  // Using QPlainTextEdit for better performance with large logs
  QTabWidget* m_executionTabWidget;  // Tab widget for Log and Live View
  CameraClickToMoveWidget* m_liveViewWidget;  // Live camera view widget

  std::vector<std::string> m_recipeStepNames;  // Store step names for current recipe

  // Scanning controls. The widget is the body of the "Scanning" tab so it
  // lives on the page; the controller owns the ScanExecutor and the
  // glue between the widget's signals and that executor.
  ScanControlWidget* m_scanWidget;
  class ScanController* m_scanController = nullptr;
  RollCorrectionWidget* m_rollCorrectionWidget = nullptr;
  RollCorrectionController* m_rollCorrectionController = nullptr;
  GlobalMaximizeWidget* m_globalMaximizeWidget = nullptr;
  GlobalMaximizeController* m_globalMaximizeController = nullptr;
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
  QTimer* m_channelRefreshTimer;  // Periodic refresh of DataStore channels

  // Interaction overlay: a QStackedWidget that holds the execution
  // tabs (index 0) plus the 5 blocking interaction widgets (UserPrompt,
  // ManualAdjust, ManualAlignPoint, UserInput, RetraceEdit) and the
  // page-owned PlacementRetrace / EdgeTransferRetrace pages added via
  // AddExtraPage(). m_promptEventLoop is the page-side QEventLoop used
  // for the two embedded retrace flows (placement, edge transfer) —
  // they involve page-side data loading and response state; the
  // overlay's built-in widgets each run their own internal loop.
  class UserInteractionOverlay* m_overlay = nullptr;
  int m_placementRetracePageIndex   = -1;
  int m_edgeTransferRetracePageIndex = -1;
  QEventLoop* m_promptEventLoop      = nullptr;  // for embedded retraces

  // Slot System UI. Run-side is a slim selector + info label; edit-side
  // is a selector + Rename/Add/Copy/Delete buttons. Both flavours live
  // inside SlotManagerWidget; the page just bridges their signals.
  class SlotManagerWidget* m_runSlotWidget  = nullptr;
  class SlotManagerWidget* m_editSlotWidget = nullptr;

  // State.
  // Note: source-of-truth flags for execution state (running, loop,
  // retrace edit, completion tracking) live in ExecutionController.
  // The page mirrors m_isExecuting / m_isLoopExecution from the
  // controller's signals so existing UI checks stay readable.
  QString m_currentProgramPath;
  bool m_isExecuting               = false;
  int m_totalRecipes               = 0;
  int m_currentRecipeIndex         = 0;
  int m_pendingChainRecipeIndex    = -1;

  // Step-level progress tracking (page-side counters for the progress bar).
  int m_currentRecipeTotalSteps    = 0;
  int m_currentRecipeCompletedSteps = 0;

  // Loop execution tracking.
  int m_currentLoopIteration       = 0;
  bool m_isLoopExecution           = false;

  void createEditWidget();
  void createExecutionWidget();

  // Slot system bridges. Slot ops happen inside SlotManagerWidget;
  // these helpers are called from its activeSlotChanged / slotsModified
  // signals to refresh page-level UI (recipe list, completion tracker,
  // edit panel, recipe-count label on the run widget).
  void onSlotChangedFromRun(int index);
  void onSlotChangedFromEdit(int index);
  void onSlotsModified();
  void updateSlotInfo();       // Pushes recipe count to m_runSlotWidget

  // Edit widget slots — most of the recipe-list operations live inside
  // ProgramEditPanel now; only the program-level Save button stays here.
  void onSaveEdit();
  void onRecipeListContextMenu(const QPoint& pos);   // Context menu for the run-side recipe list

  // Retrace edit mode entry + post-run summary dialog.
  void onStartRetraceEdit(int recipeIndex);
  void showRetraceEditSummary(int positionsVisited, int positionsSaved);

  DataStore* m_dataStore;  // Add this
  QString m_currentSubscribedChannel;  // Track subscription

  // Scan-widget display throttle: SMU polling delivers up to 20 Hz, but
  // refreshing the large styled QLabel cascades a 50-70ms MainWindow
  // UpdateRequest each time — at 20 Hz that's >1s of UI thread per second
  // and the Qt camera viewport drops to ~4 fps. The DataStore subscriber
  // (worker thread) just stashes the latest value here; a 10 Hz UI-thread
  // timer reads it and pushes it to the label.
  std::atomic<float> m_pendingScanValue{0.0f};
  std::atomic<bool>  m_pendingScanDirty{false};
  QTimer*            m_scanDisplayTimer = nullptr;

  // Vision result image display + history navigation. The widget owns
  // its own QLabel/buttons/index label/history buffer; the page just
  // forwards the worker's "vision result ready" signal into AddImage.
  class VisionHistoryWidget* m_visionHistoryWidget = nullptr;
  SequenceContext* m_currentContext;     // Current execution context
  void onVisionResultReady(QString imagePath);  // Slot for new vision result
  void updateVisionResultImage();        // Refresh from logs/vision_result_temp.png
  void clearVisionResult();              // Clear stale vision result + history

  // Context Tab Components (real-time context variable viewer with tree hierarchy)
  // Context tab — owned end-to-end by SequenceContextViewer.
  class SequenceContextViewer* m_contextViewer = nullptr;

  // Page-side helper that fills a SequenceContext with cv.* / bs.* /
  // bs2.* values from the GdiCameraWindowManager(s). Wired into the
  // viewer via SetCvPopulator() in the page constructor.
  bool populateCvContext(SequenceContext& target, QString& errorMsgIfFalse);

  // Convenience wrappers (kept so the existing call sites in execution
  // glue keep their familiar names; each forwards to the viewer).
  void startContextRefreshTimer();
  void stopContextRefreshTimer();
  void updateContextTable(bool forceUpdate = false);
};