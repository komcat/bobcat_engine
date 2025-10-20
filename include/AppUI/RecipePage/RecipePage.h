// RecipePage.h
#pragma once

#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QScrollArea>
#include <QLabel>
#include <map>

struct ApplicationContext;
class RecipeManager;
class SequenceEngine;
class ProcessSequenceFactory;
class SequenceBuilder;
class ProcessConfigWidget;
class RecipeWorker;


class RecipePage : public QWidget
{
  Q_OBJECT

public:
  explicit RecipePage(ApplicationContext* context, QWidget* parent = nullptr);
  ~RecipePage();

signals:
  void backToMainMenu();

private slots:
  void onRecipeProgress(const QString& message);
  void onRecipeFinished(bool success, const QString& message);

  // Left panel actions
  void onNewRecipe();
  void onLoadRecipe();
  void onSaveRecipe();
  void onRecipeStepSelected(int index);
  void onMoveStepUp();
  void onMoveStepDown();
  void onRemoveStep();
  void onRunRecipe();
  void onLoadProductClicked();  // ✅ ADD THIS
  // Middle panel actions
  void onAddProcess();
  void onProcessTemplateSelected(QListWidgetItem* item);

  // Right panel actions - now from custom widgets
  void onParameterChanged();
  void onNicknameChanged(const QString& nickname);
  void onEnabledChanged(bool enabled);
  void onRecipeNameChanged(const QString& name);

private:
  void setupUI();
  void createLeftPanel(QWidget* leftPanel);
  void createMiddlePanel(QWidget* middlePanel);
  void createRightPanel(QWidget* rightPanel);  


  // UI refresh
  void refreshRecipeList();
  void updateConfigurationForm();  // ✅ UPDATED: Now creates custom widget
  void clearConfigurationForm();

  // Recipe execution
  void setupSequenceEngine();
  bool executeRecipe();
  void buildSequenceFromRecipe(SequenceBuilder* sequence);

  ApplicationContext* m_context;
  RecipeManager* m_recipeManager;
  SequenceEngine* m_sequenceEngine;
  ProcessSequenceFactory* m_processFactory;

  int m_selectedStepIndex;

  // Left panel widgets
  QListWidget* m_recipeList;
  QLineEdit* m_recipeNameEdit;
  QPushButton* m_newButton;
  QPushButton* m_loadButton;
  QPushButton* m_saveButton;
  QPushButton* m_moveUpButton;
  QPushButton* m_moveDownButton;
  QPushButton* m_removeButton;
  QPushButton* m_runRecipeButton;
  QLabel* m_loadedProductLabel;  // ✅ Add this

  // Middle panel widgets
  QListWidget* m_processTemplateList;
  QPushButton* m_addButton;

  // Right panel widgets - SIMPLIFIED!
  QScrollArea* m_configScrollArea;
  ProcessConfigWidget* m_currentConfigWidget;  // ✅ NEW: Single widget pointer

  QPushButton* m_backButton;

  RecipeWorker* m_recipeWorker;


  QPushButton* m_loadProductButton;  // ✅ ADD THIS
  QLabel* m_productStatusLabel;      // ✅ ADD THIS
};