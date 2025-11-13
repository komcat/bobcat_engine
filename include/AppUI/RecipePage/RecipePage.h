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
#include <map>

struct ApplicationContext;
class RecipeManager;
class SequenceEngine;
class ProcessSequenceFactory;
class SequenceBuilder;
class ProcessConfigWidget;
class RecipeWorker;
class RecipeProgressWidget;  // ✅ NEW
class ManualAdjustWidget;    // ✅ Manual adjust widget
class TransformationMatrixLoader;

class RecipePage : public QWidget
{
  Q_OBJECT

public:
  explicit RecipePage(ApplicationContext* context, TransformationMatrixLoader* matrixLoader, QWidget* parent = nullptr);
  ~RecipePage();

public slots:
  void onProductChanged();  // Called when product is loaded/created in MainMenu

signals:
  void backToMainMenu();

  // Manual adjust signal
  void showManualAdjust(QString message, QString deviceName, double stepSizeUm,
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
  void onRemoveStep();
  void onRunRecipe();
  void onRunSelectedStep();  // ✅ NEW: Run single selected step
  void onLoadProductClicked();
  void onSelectAllProcesses();
  void onDeselectAllProcesses();
  void onToggleSelectedProcesses();
  void onUpdateProductFile();  // ✅ NEW: Update product file in recipe

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

  // Manual adjust handling
  void onShowManualAdjust(QString message, QString deviceName, double stepSizeUm,
                          bool enableX, bool enableY, bool enableZ);
  void onManualAdjustResponse(bool completed);

  // User prompt handling
  void onShowUserPrompt(QString title, QString message, bool allowCancel);
  void onUserPromptResponse(bool confirmed);



private:
  void setupUI();
  void createLeftPanel(QWidget* leftPanel);
  void createMiddlePanel(QWidget* middlePanel);
  void createRightPanel(QWidget* rightPanel);

  // UI refresh
  void refreshRecipeList();
  void updateConfigurationForm();
  void clearConfigurationForm();
  void selectStepByIndex(int index);
  void updateRecipeFileNameDisplay();  // ✅ NEW: Update recipe file name label

  // ✅ NEW: Panel switching
  void showProgressPanel();
  void showConfigPanel();

  // Recipe execution
  void setupSequenceEngine();
  bool executeRecipe();
  void buildSequenceFromRecipe(SequenceBuilder* sequence);

  // Settings persistence
  void saveLastRecipeToSettings(const QString& recipePath);
  void loadLastRecipeFromSettings();

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
  QPushButton* m_selectAllButton;
  QPushButton* m_deselectAllButton;
  QPushButton* m_toggleSelectedButton;
  QPushButton* m_updateProductFileButton;  // ✅ NEW: Update product file reference
  QLabel* m_loadedProductLabel;

  // Middle panel widgets
  QTreeWidget* m_processTemplateList;
  QPushButton* m_addButton;

  // Right panel widgets
  QScrollArea* m_configScrollArea;
  ProcessConfigWidget* m_currentConfigWidget;  // Config widget
  QPointer<RecipeProgressWidget> m_progressWidget;  // ✅ Safe pointer that auto-nulls when deleted
  ManualAdjustWidget* m_manualAdjustWidget;         // ✅ Manual adjust widget

  QPushButton* m_backButton;
  RecipeWorker* m_recipeWorker;

  QPushButton* m_loadProductButton;
  QLabel* m_productStatusLabel;
  QLabel* m_recipeFileLabel;  // ✅ NEW: Shows current recipe file name

  QEventLoop* m_promptEventLoop;     // For blocking until response
  bool m_manualAdjustResponse;       // Store manual adjust response
  bool m_userPromptResponse;         // Store user prompt response
};