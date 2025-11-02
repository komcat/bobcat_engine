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

struct ApplicationContext;
class ProgramManager;
class ProgramWorker;
class ProcessSequenceFactory;
class UserPromptWidget;
class ManualAdjustWidget;
class ScanControlWidget;
class ScanExecutor;
class TransformationMatrixLoader;
class QVBoxLayout;
class QHBoxLayout;
class QEventLoop;

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
  void onProgramExecutionCompleted(bool success, QString message);
  void onProgramStopped();
  void onWorkerLogMessage(QString message);

  // User prompt handling
  void onShowUserPrompt(QString title, QString message);
  void onPromptResponse(bool accepted);

  // Manual adjust handling
  void onShowManualAdjust(QString message, QString deviceName, double stepSizeUm,
                          bool enableX, bool enableY, bool enableZ);
  void onManualAdjustResponse(bool completed);

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
  QLabel* m_currentProductLabel;  // Product display
  QPushButton* m_loadButton;
  QPushButton* m_saveButton;
  QPushButton* m_editButton;
  QPushButton* m_executeButton;
  QProgressBar* m_progressBar;
  QLabel* m_programNameLabel;
  QListWidget* m_recipeListWidget;
  QPushButton* m_backButton;
  QCheckBox* m_autoRunNextCheckBox;

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
  QStackedWidget* m_executionStack;  // Switch between log and prompt/adjust
  UserPromptWidget* m_promptWidget;
  ManualAdjustWidget* m_manualAdjustWidget;
  QEventLoop* m_promptEventLoop;     // For blocking until response
  bool m_promptResponse;             // Store user's response
  bool m_manualAdjustResponse;       // Store manual adjust response

  // State
  QString m_currentProgramPath;
  bool m_isExecuting;
  int m_totalRecipes;
  int m_currentRecipeIndex;

  void createEditWidget();
  void createExecutionWidget();

  // Edit widget slots
  void onAddRecipe();
  void onRemoveRecipe();
  void onMoveRecipeUp();
  void onMoveRecipeDown();
  void onToggleRecipeEnabled();  // ✅ NEW: Toggle enable/disable for selected recipe
  void onSaveEdit();
  void onEditRecipeSelectionChanged();
  void loadRecipePreview(const std::string& recipeFilePath);

  DataStore* m_dataStore;  // Add this
  QString m_currentSubscribedChannel;  // Track subscription
};