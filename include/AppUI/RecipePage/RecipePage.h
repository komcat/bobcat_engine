// RecipePage.h
#pragma once

#include <QWidget>
#include <QDateTime>
#include <QPushButton>
#include <QLineEdit>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QScrollArea>
#include <QLabel>
#include <QPointer>
#include <QEventLoop>
#include <QTreeWidget>
#include <QGroupBox>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSplitter>
#include <QStringList>
#include <QTabWidget>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>

// Forward declarations
struct ApplicationContext;
class RecipeManager;
class SequenceEngine;
class ProcessSequenceFactory;
class ProcessConfigWidget;
class RecipeWorker;
class RecipeProgressWidget;
class ManualAdjustWidget;
class ManualAlignPointWidget;
class UserInputWidget;
class TransformationMatrixLoader;
class ContextViewerWindow;
class SequenceContext;
class RecipeTreeItemFactory;  // NEW: Factory for tree items
class RecipeClipboard;        // NEW: Clipboard for copy/paste
class ContextVariablePreviewWidget;  // NEW: Shows context variables from recipe
class RecipeRunWindow;        // NEW: Floating window hosting execution-time widgets
class ProcessVisibilityDialog;  // NEW: Modal dialog to manage hidden process list
class RecipeBrowserWidget;     // NEW: Self-contained "Recipes" tab (browse / load / folder ops)
class ProcessCatalogWidget;    // NEW: Self-contained left-panel "Available Processes" catalog
class RecipeExecutionController;  // NEW: Owns RecipeWorker lifetime + signal forwarding
class QuickAddController;       // NEW: Ctrl+B two-key quick-add UX

class RecipePage : public QWidget
{
  Q_OBJECT

public:
  // Page state enum - controls what the user can interact with
  enum class PageState {
    Editing,    // Normal editing mode - all controls enabled
    Executing   // Recipe is running - editing controls disabled
  };

  explicit RecipePage(ApplicationContext* context, TransformationMatrixLoader* matrixLoader, QWidget* parent = nullptr);
  ~RecipePage();

public:
  /**
   * @brief Check if user can leave the page (handles unsaved changes)
   * @return true if user can leave, false if user cancelled navigation
   * Shows a dialog if there are unsaved changes, offering to save/discard/cancel
   */
  bool canLeavePage();

public slots:
  void onProductChanged();  // Called when product is loaded/created in MainMenu
  void loadRecipeFromFile(const QString& filePath);  // Load recipe by file path (used by RunProgramPage)
  void applyTheme();        // Re-applies all local stylesheets using current Theme::Color::* values

signals:
  void backToMainMenu();

  // Manual adjust signal
  void showManualAdjust(QString message, QString deviceName, double stepSizeUm,
                        bool enableX, bool enableY, bool enableZ,
                        QStringList imagePaths);

  // Manual align point signal
  void showManualAlignPoint(QString pointName, QString message,
                            bool enableX, bool enableY, bool enableZ);

  // User prompt signal (for non-blocking prompts in execution area)
  void showUserPrompt(QString title, QString message, bool allowCancel);

private slots:
  // Legacy progress (kept for compatibility)
  void onRecipeProgress(const QString& message);
  void onRecipeFinished(bool success, const QString& message);

  // Left panel actions
  void onNewRecipe();
  void onLoadRecipe();
  void onSaveRecipe();
  void onSaveRecipeAs();  // ✅ NEW: Save As action
  void onRecipeStepSelected();
  void onMoveStepUp();
  void onMoveStepDown();
  void onMoveStepToLine();  // Move step to specific line number
  void onRemoveStep();
  void onRunRecipe();
  void onRunSelectedStep();  // ✅ NEW: Run single selected step
  void onBreakpointButtonClicked();  // ✅ Toggle breakpoint on selected step
  void onLoadProductClicked();
  void onSelectAllProcesses();
  void onDeselectAllProcesses();
  void onToggleSelectedProcesses();
  void onUpdateProductFile();  // ✅ NEW: Update product file in recipe
  void onCopySelectedSteps();  // ✅ NEW: Copy selected process steps to clipboard
  void onPasteSteps();         // ✅ NEW: Paste copied process steps

  // Process insert (request from ProcessCatalogWidget)
  void onAddProcessRequested(const QString& processType);

  // Right panel actions - from custom widgets
  void onParameterChanged();
  void onNicknameChanged(const QString& nickname);
  void onEnabledChanged(bool enabled);
  void onRecipeNameChanged(const QString& name);

  // ✅ NEW: Raw data interpreters (UI layer)
  void onExecutionStarted(int totalEnabledSteps);
  void onStepBuildStarted(int stepIndex, QString stepName, QString stepType);
  void onStepBuildCompleted(int stepIndex);
  void onSequenceExecutionStarted();
  void onExecutionCompleted(bool success, QString message);
  void onExecutionStopped();
  void onWorkerLogMessage(QString message);
  void onWaitProgress(int remainingMs, int totalMs);
  void onStopRecipeRequested();

  // Step execution events (from pub-sub)
  void onStepExecutionStarted(int stepIndex, QString stepName, QString stepType);
  void onStepExecutionCompleted(int stepIndex, bool success);

  // Breakpoint/pause handling
  void onExecutionPaused(int stepIndex, QString stepName, bool isBreakpoint);

  // Manual adjust handling
  void onShowManualAdjust(QString message, QString deviceName, double stepSizeUm,
                          bool enableX, bool enableY, bool enableZ,
                          QStringList imagePaths);
  void onManualAdjustResponse(bool completed);

  // Manual align point handling
  void onShowManualAlignPoint(QString pointName, QString message,
                              bool enableX, bool enableY, bool enableZ);
  void onManualAlignPointResponse(bool completed);

  // User input handling
  void onUserInputResponse(bool accepted, QString inputText);

  // User prompt handling
  void onShowUserPrompt(QString title, QString message, bool allowCancel);
  void onUserPromptResponse(bool confirmed);

  // Context viewer
  void onShowContextViewer();
  void onContextRefreshRequested();

  // Recipe load (request from RecipeBrowserWidget)
  void onRecipeLoadRequested(const QString& filePath);

private:
  // Live data injection for Context Viewer
  void injectLiveBeamStreamData();

  void setupUI();
  void createMiddlePanel(QWidget* middlePanel);
  void createRightPanel(QWidget* rightPanel);

  // Middle panel tab creation
  void createStepsTab(QWidget* stepsTab);

  // UI refresh
  void refreshRecipeList();
  void updateConfigurationForm();
  void clearConfigurationForm();
  void selectStepByIndex(int index);
  void updateRecipeFileNameDisplay();  // ✅ NEW: Update recipe file name label
  QTreeWidgetItem* findTreeItemByIndex(int stepIndex);  // Find tree item by step index
  void updateTreeItemsForSwap(int index1, int index2);  // Fast update for move up/down
  void updateSingleStepItem(int stepIndex);             // Fast update for single item (checkbox toggle)

  // ✅ NEW: Panel switching
  void showProgressPanel();
  void showConfigPanel();

  // Step execution highlighting
  void highlightExecutingStep(int stepIndex);
  void clearExecutingStepHighlight();

  // Recipe execution
  void setupSequenceEngine();

  // Installs the 4 user-interaction callbacks (user prompt / manual
  // adjust / user input) on a freshly-created RecipeWorker. Called via
  // the WorkerConfigurator passed to RecipeExecutionController::Start.
  // Lambdas inside reach page-side widgets and event-loop state, so
  // this stays on the page rather than moving into the controller.
  void installRecipeWorkerCallbacks(RecipeWorker* worker);

  // Auto-load workflow: reads last-recipe path from RecipePageSettingsStore,
  // loads the recipe, and refreshes UI / loads associated product file.
  // The QSettings I/O itself lives in RecipePageSettingsStore.
  void loadLastRecipeFromSettings();

  // Unsaved changes handling
  void onBackButtonClicked();  // Handle back button with unsaved changes check

  ApplicationContext* m_context;
  TransformationMatrixLoader* m_matrixLoader;
  RecipeManager* m_recipeManager;
  SequenceEngine* m_sequenceEngine;
  ProcessSequenceFactory* m_processFactory;

  int m_selectedStepIndex;

  // Left panel widgets
  QTreeWidget* m_recipeTree;
  QLineEdit* m_recipeNameEdit;
  QPushButton* m_newButton;
  QPushButton* m_loadButton;
  QPushButton* m_saveButton;
  QPushButton* m_saveAsButton;  // ✅ NEW: Save As button
  QPushButton* m_moveUpButton;
  QPushButton* m_moveDownButton;
  QPushButton* m_removeButton;
  QPushButton* m_runRecipeButton;
  QPushButton* m_runSelectedStepButton;  // ✅ NEW: Run single selected step
  QPushButton* m_breakpointButton;       // ✅ Toggle breakpoint on selected step
  QPushButton* m_selectAllButton;
  QPushButton* m_deselectAllButton;
  QPushButton* m_toggleSelectedButton;
  QPushButton* m_updateProductFileButton;  // ✅ NEW: Update product file reference
  QPushButton* m_copyButton;               // ✅ NEW: Copy selected steps
  QPushButton* m_pasteButton;              // ✅ NEW: Paste steps
  QLabel* m_loadedProductLabel;

  // Middle panel widgets
  QTabWidget* m_middleTabWidget;

  // Left panel "Available Processes" — extracted (see ProcessCatalogWidget.h).
  ProcessCatalogWidget* m_processCatalog = nullptr;

  // Recipes tab — extracted to its own widget (see RecipeBrowserWidget.h).
  RecipeBrowserWidget* m_recipeBrowser = nullptr;

  // Right panel widgets
  QScrollArea* m_configScrollArea;
  ProcessConfigWidget* m_currentConfigWidget;  // Config widget
  QPointer<RecipeProgressWidget> m_progressWidget;  // ✅ Safe pointer that auto-nulls when deleted
  ManualAdjustWidget* m_manualAdjustWidget;         // ✅ Manual adjust widget
  ManualAlignPointWidget* m_manualAlignPointWidget; // ✅ Manual align point widget
  UserInputWidget* m_userInputWidget;               // ✅ User input widget

  // Floating window that hosts execution-time widgets (progress / manual
  // adjust / manual align / user input).  Created in setupUI(), reused
  // across runs.  See include/AppUI/RecipePage/RecipeRunWindow.h.
  RecipeRunWindow* m_runWindow = nullptr;

  QPushButton* m_backButton;
  // RecipeWorker is now owned by m_executionController (see below).
  RecipeExecutionController* m_executionController = nullptr;

  QPushButton* m_loadProductButton;
  QPushButton* m_stopButton;  // ✅ NEW: Emergency stop button
  QLabel* m_productStatusLabel;
  QLabel* m_recipeFileLabel;  // ✅ NEW: Shows current recipe file name

  QEventLoop* m_promptEventLoop;     // For blocking until response
  bool m_manualAdjustResponse;       // Store manual adjust response
  bool m_manualAlignPointResponse;   // Store manual align point response
  bool m_userInputAccepted;          // Store user input accepted status
  QString m_userInputText;           // Store user input text
  bool m_userPromptResponse;         // Store user prompt response

  // Context viewer
  ContextViewerWindow* m_contextViewer;
  SequenceContext* m_currentContext;     // Current execution context (owned)
  QPushButton* m_contextViewerButton;

  // Vision result image display
  QGroupBox* m_visionResultGroup;
  QLabel* m_visionResultImageLabel;
  void updateVisionResultImage();

  // Context variable preview (shows variables that will be available)
  ContextVariablePreviewWidget* m_contextVariablePreview;
  void updateContextVariablePreview();  // NEW: Update context variable display

  // Refactored components
  std::unique_ptr<RecipeTreeItemFactory> m_treeItemFactory;  // Factory for tree items
  std::unique_ptr<RecipeClipboard> m_clipboard;              // Clipboard for copy/paste

  // Quick add mode (Ctrl+B) — extracted to QuickAddController.
  QuickAddController* m_quickAddController = nullptr;

  // Page state management
  PageState m_pageState;                  // Current page state (Editing/Executing)
  void setPageState(PageState state);     // Switch between editing and executing modes

  // Last product file that showEvent()'s hot reload actually loaded, and its
  // modification time at that moment. Used to skip the reload when the file
  // has not changed since - see showEvent(). An invalid QDateTime means "never
  // reloaded", which forces the first activation to load.
  std::string m_lastProductReloadPath;
  QDateTime m_lastProductReloadMtime;

protected:
  void keyPressEvent(QKeyEvent* event) override;
  void showEvent(QShowEvent* event) override;  // Hot reload when page becomes visible
  void resizeEvent(QResizeEvent* event) override;  // Enforce fixed 20/55/25 panel widths

private:
  QSplitter* m_mainSplitter = nullptr;  // Three-pane splitter (left/middle/right)
  void applyPanelSizes();               // Push 20/55/25 sizes to m_mainSplitter
};