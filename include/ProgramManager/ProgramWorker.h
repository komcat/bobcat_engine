// ProgramWorker.h
#pragma once

#include <QThread>
#include <QString>
#include <atomic>
#include <functional>

class SequenceEngine;
class ProgramManager;
class ProcessSequenceFactory;
class ProductPositionManager;
class UnifiedMotionManager;
class RecipeWorker;

/**
 * @brief Worker thread for program execution
 *
 * Executes recipes sequentially by using RecipeWorker for each recipe.
 * This is a simple orchestrator - all recipe logic is in RecipeWorker.
 */
class ProgramWorker : public QThread
{
  Q_OBJECT

public:
  ProgramWorker(
    SequenceEngine* engine,
    ProgramManager* programManager,
    ProcessSequenceFactory* factory,
    ProductPositionManager* positionManager,
    UnifiedMotionManager* motionManager,
    QObject* parent = nullptr
  );

  void requestStop();  // Request graceful stop

  // Set callback for user prompts (optional)
  using UserPromptCallback = std::function<bool(const std::string& title, const std::string& message)>;
  void setUserPromptCallback(UserPromptCallback callback);

signals:
  // Program-level signals
  void programExecutionStarted(int totalEnabledRecipes);
  void programExecutionCompleted(bool success, QString message);
  void programStopped();

  // Recipe-level signals (within program)
  void recipeStarted(int recipeIndex, QString recipeName);
  void recipeCompleted(int recipeIndex, bool success, QString message);
  void recipeSkipped(int recipeIndex, QString recipeName);

  // Step-level signals (within current recipe)
  void stepBuildStarted(int recipeIndex, int stepIndex, QString stepName, QString stepType);
  void stepBuildCompleted(int recipeIndex, int stepIndex);
  void sequenceExecutionStarted(int recipeIndex);

  // Logging
  void logMessage(QString message);

protected:
  void run() override;

private:
  bool executeRecipe(int recipeIndex, const std::string& recipeFilePath);

  SequenceEngine* m_engine;
  ProgramManager* m_programManager;
  ProcessSequenceFactory* m_factory;
  ProductPositionManager* m_positionManager;
  UnifiedMotionManager* m_motionManager;
  std::atomic<bool> m_stopRequested;
  UserPromptCallback m_userPromptCallback;
};