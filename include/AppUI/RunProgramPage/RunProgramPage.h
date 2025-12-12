// RunProgramPage.h
#pragma once
#include "DataStore.h"
#include <QWidget>
#include <QPushButton>
#include <QProgressBar>
#include <QListWidget>
#include <QTableWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QCheckBox>
#include <QTextEdit>
#include <QLineEdit>
#include <QTimer>  // ✅ ADDED for periodic channel refresh
#include <QTabWidget>  // ✅ ADDED for tab layout
#include <QGroupBox>  // ✅ ADDED for vision result group

struct ApplicationContext;
class ProgramManager;
class ProgramWorker;
class ProcessSequenceFactory;
class UserPromptWidget;
class ManualAdjustWidget;
class ManualAlignPointWidget;
class UserInputWidget;
class ScanControlWidget;
class ScanExecutor;
class TransformationMatrixLoader;
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

signals:
  void backToMainMenu();

  // User prompt signal
  void showUserPrompt(QString title, QString message);

  // Manual adjust signal
  void showManualAdjust(QString message, QString deviceName, double stepSizeUm,
                        bool enableX, bool enableY, bool enableZ);

private slots:
  // File operations
  void onNewProgram();
  void onLoadProgram();
  void onSaveProgram();
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
  void onWaitProgress(int remainingMs, int totalMs);
  void onWorkerFinished();  // ✅ Handle QThread::finished()

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
  void onScanRequested(QString deviceName, bool isCoarse, QString channel);
  void onScanLogMessage(QString message);
  void onScanCompleted(QString deviceName, double peakValue, bool success);
  void onScanError(QString errorMessage);
  void onStopScanRequested();
  void onChannelChanged(QString channel);  // ✅ ADDED

  // ✅ ADDED: Periodic channel list refresh
  void refreshDataStoreChannels();

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

  ApplicationContext* m_context;
  TransformationMatrixLoader* m_matrixLoader;
  ProgramManager* m_programManager;
  ProgramWorker* m_programWorker;
  ProcessSequenceFactory* m_processFactory;

  // Left panel widgets
  QWidget* m_leftPanel;
  QWidget* m_rightPanel;  // ✅ Right panel for scanning controls
  QTabWidget* m_tabWidget;  // ✅ ADDED: Tab widget for File/Execute tabs
  QLabel* m_currentProductLabel;  // Product display
  QPushButton* m_newButton;
  QPushButton* m_loadButton;
  QPushButton* m_saveButton;
  QPushButton* m_editButton;
  QPushButton* m_executeButton;
  QProgressBar* m_progressBar;
  QLabel* m_programNameLabel;
  QLabel* m_programPathLabel;  // ✅ ADDED: Display program file path
  QListWidget* m_recipeListWidget;
  QPushButton* m_backButton;
  QCheckBox* m_autoRunNextCheckBox;
  QCheckBox* m_useDependenciesCheckBox;  // ✅ NEW: Enable dependency-based execution

  // Right panel
  QStackedWidget* m_rightStack;
  QWidget* m_editWidget;
  QWidget* m_executionWidget;

  // Edit widget components
  QLineEdit* m_programNameEdit;
  QPushButton* m_addRecipeButton;
  QTableWidget* m_editRecipeTable;
  QTextEdit* m_recipePreviewText;
  QPushButton* m_removeRecipeButton;
  QPushButton* m_moveUpButton;
  QPushButton* m_moveDownButton;
  QPushButton* m_toggleEnableButton;  // ✅ NEW: Toggle enable/disable
  QPushButton* m_saveEditButton;

  // Execution widget components
  QLabel* m_executionStatusLabel;
  QLabel* m_currentRecipeLabel;
  QLabel* m_executionLogLabel;
  QTextEdit* m_logTextEdit;

  // Scanning controls (refactored)
  ScanControlWidget* m_scanWidget;
  ScanExecutor* m_scanExecutor;
  QTimer* m_channelRefreshTimer;  // ✅ ADDED: Periodic refresh of DataStore channels

  // Prompt system
  QStackedWidget* m_executionStack;  // Switch between log and prompt/adjust/input
  UserPromptWidget* m_promptWidget;
  ManualAdjustWidget* m_manualAdjustWidget;
  ManualAlignPointWidget* m_manualAlignPointWidget;  // ✅ Manual align point widget
  UserInputWidget* m_userInputWidget;
  QEventLoop* m_promptEventLoop;     // For blocking until response
  bool m_promptResponse;             // Store user's response
  bool m_manualAdjustResponse;       // Store manual adjust response
  bool m_manualAlignPointResponse;   // ✅ Store manual align point response
  bool m_userInputAccepted;          // Store user input accepted status
  QString m_userInputText;           // Store user input text

  // State
  QString m_currentProgramPath;
  bool m_isExecuting;
  int m_totalRecipes;
  int m_currentRecipeIndex;
  std::vector<bool> m_completedRecipes;  // ✅ Track completed recipes (persists across executions)

  // ✅ Step-level progress tracking
  int m_currentRecipeTotalSteps;    // Total steps in current recipe
  int m_currentRecipeCompletedSteps; // Completed steps in current recipe

  void createEditWidget();
  void createExecutionWidget();

  // Edit widget slots
  void onAddRecipe();
  void onRemoveRecipe();
  void onMoveRecipeUp();
  void onMoveRecipeDown();
  void onToggleRecipeEnabled();  // ✅ NEW: Toggle enable/disable for selected recipe
  void onManageDependencies();    // ✅ NEW: Open dependency management dialog
  void onSaveEdit();
  void onEditRecipeSelectionChanged();
  void loadRecipePreview(const std::string& recipeFilePath);

  DataStore* m_dataStore;  // Add this
  QString m_currentSubscribedChannel;  // Track subscription

  // Vision result image display
  QGroupBox* m_visionResultGroup;        // ✅ Vision result display group
  QLabel* m_visionResultImageLabel;      // ✅ Vision result image display
  SequenceContext* m_currentContext;     // ✅ Current execution context
  void updateVisionResultImage();        // ✅ Update vision result image from context
  void clearVisionResult();              // ✅ Clear stale vision result
};