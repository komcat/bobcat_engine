// RunProgramPage.h
#pragma once

#include <QWidget>
#include <QPushButton>
#include <QProgressBar>
#include <QListWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QCheckBox>
#include <QTextEdit>
#include <QComboBox>

struct ApplicationContext;
class ProgramManager;
class ProgramWorker;
class ProcessSequenceFactory;
class UserPromptWidget;
class AdaptiveScanController;
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
  explicit RunProgramPage(ApplicationContext* context, QWidget* parent = nullptr);
  ~RunProgramPage();

signals:
  void backToMainMenu();

  // User prompt signal
  void showUserPrompt(QString title, QString message);

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

  // Scanning operations
  void onLeftCoarseScan();
  void onLeftFineScan();
  void onRightCoarseScan();
  void onRightFineScan();

private:
  void setupUI();
  void createLeftPanel(QVBoxLayout* leftLayout);
  void createRightPanel();

  void showEditWidget();
  void showExecutionWidget();

  void updateProgramInfo();
  void updateRecipeList();
  void updateExecuteButton();

  ApplicationContext* m_context;
  ProgramManager* m_programManager;
  ProgramWorker* m_programWorker;
  ProcessSequenceFactory* m_processFactory;

  // Left panel widgets
  QWidget* m_leftPanel;
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
  QPushButton* m_addRecipeButton;
  QListWidget* m_editRecipeList;
  QPushButton* m_removeRecipeButton;
  QPushButton* m_moveUpButton;
  QPushButton* m_moveDownButton;
  QPushButton* m_saveEditButton;

  // Execution widget components
  QLabel* m_executionStatusLabel;
  QLabel* m_currentRecipeLabel;
  QLabel* m_executionLogLabel;
  QTextEdit* m_logTextEdit;

  // Scanning controls
  QWidget* m_scanControlPanel;
  QComboBox* m_scanChannelCombo;
  QPushButton* m_leftCoarseButton;
  QPushButton* m_leftFineButton;
  QPushButton* m_rightCoarseButton;
  QPushButton* m_rightFineButton;
  AdaptiveScanController* m_scanController;  // Created on-demand

  // Prompt system
  QStackedWidget* m_executionStack;  // Switch between log and prompt
  UserPromptWidget* m_promptWidget;
  QEventLoop* m_promptEventLoop;     // For blocking until response
  bool m_promptResponse;             // Store user's response

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
  void onSaveEdit();
  void onEditRecipeSelectionChanged();

  // Scanning helper methods
  void createScanControlPanel();
  void startScan(const QString& deviceName, bool isCoarse);
  void cleanupScanController();
};