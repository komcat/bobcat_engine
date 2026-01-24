// RecipePage.h
#pragma once

#include <QWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QScrollArea>
#include <QLabel>
#include <QPointer>
#include <QListWidget>
#include <QEventLoop>
#include <QTreeWidget>
#include <QGroupBox>
#include <QKeyEvent>
#include <QShowEvent>
#include <QTabWidget>
#include <map>
#include <memory>

// Forward declarations
struct ApplicationContext;
class RecipeManager;
class SequenceEngine;
class ProcessSequenceFactory;
class SequenceBuilder;
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

signals:
  void backToMainMenu();

  // Manual adjust signal
  void showManualAdjust(QString message, QString deviceName, double stepSizeUm,
                        bool enableX, bool enableY, bool enableZ);

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

  // Middle panel actions
  void onAddProcess();
  void onProcessTemplateSelected(QTreeWidgetItem* item, int column);

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
                          bool enableX, bool enableY, bool enableZ);
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

  // Recipes tab slots
  void onRecipeListItemDoubleClicked(QListWidgetItem* item);
  void onRecipeListContextMenu(const QPoint& pos);
  void onRefreshRecipeList();
  void onLoadSelectedRecipe();
  void onDeleteSelectedRecipe();
  void onRenameSelectedRecipe();

private:
  void setupUI();
  void createLeftPanel(QWidget* leftPanel);
  void createMiddlePanel(QWidget* middlePanel);
  void createRightPanel(QWidget* rightPanel);

  // Middle panel tab creation
  void createRecipesTab(QWidget* recipesTab);
  void createStepsTab(QWidget* stepsTab);
  void populateRecipeListWidget();
  QString getRecipeFolderPath();

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
  bool executeRecipe();
  void buildSequenceFromRecipe(SequenceBuilder* sequence);

  // Settings persistence
  void saveLastRecipeToSettings(const QString& recipePath);
  void loadLastRecipeFromSettings();

  // Quick add mode (Ctrl+B shortcut)
  void activateQuickAddMode();
  void deactivateQuickAddMode();
  void handleQuickAddCategoryKey(int key);
  void handleQuickAddProcessKey(int key);
  void updateQuickAddDisplay();

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
  QTreeWidget* m_processTemplateList;
  QPushButton* m_addButton;
  QLabel* m_selectedProcessLabel;  // Shows currently selected process type

  // Recipes tab widgets
  QListWidget* m_recipeListWidget;
  QPushButton* m_refreshRecipeListButton;
  QLabel* m_recipeCountLabel;

  // Right panel widgets
  QScrollArea* m_configScrollArea;
  ProcessConfigWidget* m_currentConfigWidget;  // Config widget
  QPointer<RecipeProgressWidget> m_progressWidget;  // ✅ Safe pointer that auto-nulls when deleted
  ManualAdjustWidget* m_manualAdjustWidget;         // ✅ Manual adjust widget
  ManualAlignPointWidget* m_manualAlignPointWidget; // ✅ Manual align point widget
  UserInputWidget* m_userInputWidget;               // ✅ User input widget

  QPushButton* m_backButton;
  RecipeWorker* m_recipeWorker;

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

  // Quick add mode state (Ctrl+B shortcut)
  bool m_quickAddMode;                    // Whether quick add mode is active
  int m_quickAddSelectedCategory;         // Selected category index (-1 = none)
  QLabel* m_quickAddOverlay;              // Overlay showing quick add instructions

  // Page state management
  PageState m_pageState;                  // Current page state (Editing/Executing)
  void setPageState(PageState state);     // Switch between editing and executing modes

protected:
  void keyPressEvent(QKeyEvent* event) override;
  void showEvent(QShowEvent* event) override;  // Hot reload when page becomes visible
};