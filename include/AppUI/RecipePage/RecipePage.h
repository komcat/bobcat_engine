// RecipePage.h
#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QScrollArea>
#include <QLabel>
#include <QPointer>
#include <QListWidget>
#include <map>

struct ApplicationContext;
class RecipeManager;
class SequenceEngine;
class ProcessSequenceFactory;
class SequenceBuilder;
class ProcessConfigWidget;
class RecipeWorker;
class RecipeProgressWidget;  // ✅ NEW

class RecipePage : public QWidget
{
  Q_OBJECT

public:
  explicit RecipePage(ApplicationContext* context, QWidget* parent = nullptr);
  ~RecipePage();

signals:
  void backToMainMenu();

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
  void onTableCellChanged(int row, int column);
  void onMoveStepUp();
  void onMoveStepDown();
  void onRemoveStep();
  void onRunRecipe();
  void onLoadProductClicked();
  void onSelectAllProcesses();
  void onDeselectAllProcesses();
  void onToggleSelectedProcesses();
  void onUpdateProductFile();  // ✅ NEW: Update product file in recipe

  // Middle panel actions
  void onAddProcess();
  void onProcessTemplateSelected(QListWidgetItem* item);

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
  void onStopRecipeRequested();



private:
  void setupUI();
  void createLeftPanel(QWidget* leftPanel);
  void createMiddlePanel(QWidget* middlePanel);
  void createRightPanel(QWidget* rightPanel);

  // UI refresh
  void refreshRecipeList();
  void updateConfigurationForm();
  void clearConfigurationForm();

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
  RecipeManager* m_recipeManager;
  SequenceEngine* m_sequenceEngine;
  ProcessSequenceFactory* m_processFactory;

  int m_selectedStepIndex;

  // Left panel widgets
  QTableWidget* m_recipeTable;
  QLineEdit* m_recipeNameEdit;
  QPushButton* m_newButton;
  QPushButton* m_loadButton;
  QPushButton* m_saveButton;
  QPushButton* m_saveAsButton;  // ✅ NEW: Save As button
  QPushButton* m_moveUpButton;
  QPushButton* m_moveDownButton;
  QPushButton* m_removeButton;
  QPushButton* m_runRecipeButton;
  QPushButton* m_selectAllButton;
  QPushButton* m_deselectAllButton;
  QPushButton* m_toggleSelectedButton;
  QPushButton* m_updateProductFileButton;  // ✅ NEW: Update product file reference
  QLabel* m_loadedProductLabel;

  // Middle panel widgets
  QListWidget* m_processTemplateList;
  QPushButton* m_addButton;

  // Right panel widgets
  QScrollArea* m_configScrollArea;
  ProcessConfigWidget* m_currentConfigWidget;  // Config widget
  QPointer<RecipeProgressWidget> m_progressWidget;  // ✅ Safe pointer that auto-nulls when deleted

  QPushButton* m_backButton;
  RecipeWorker* m_recipeWorker;

  QPushButton* m_loadProductButton;
  QLabel* m_productStatusLabel;
};