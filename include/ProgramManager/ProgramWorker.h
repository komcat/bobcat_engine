// ProgramWorker.h
#pragma once

#include <QThread>
#include <QString>
#include <QMutex>
#include <atomic>
#include <functional>

class SequenceEngine;
class ProgramManager;
class ProcessSequenceFactory;
class ProductPositionManager;
class UnifiedMotionManager;
class RecipeWorker;
class RecipeExecutionStateMachine;
class SequenceContext;  // ✅ Forward declaration for context sharing

/**
 * @brief Worker thread for program execution
 *
 * Executes recipes sequentially by using RecipeWorker for each recipe.
 * Supports two modes:
 * - Run all enabled recipes (auto-run mode)
 * - Run single selected recipe (single-shot mode)
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

  // Set callback for manual adjust (optional)
  using ManualAdjustCallback = std::function<bool(
    const std::string& message,
    const std::string& deviceName,
    double stepSizeUm,
    bool enableX,
    bool enableY,
    bool enableZ
  )>;
  void setManualAdjustCallback(ManualAdjustCallback callback);

  // Set callback for user input (optional)
  using UserInputCallback = std::function<std::pair<bool, std::string>(
    const std::string& title,
    const std::string& prompt,
    const std::string& defaultValue
  )>;
  void setUserInputCallback(UserInputCallback callback);

  // ✅ NEW: Configure execution mode
  void setAutoRunMode(bool autoRun);  // true = all recipes, false = single recipe
  void setStartRecipeIndex(int index);  // Which recipe to start from (for single mode)
  void setUseDependencyGraph(bool useDependencies);  // true = use state machine, false = sequential

  // ✅ Reset completion tracking (call when loading new program)
  void resetCompletionTracking();

  // ✅ Get current execution context (for vision result display)
  SequenceContext* getCurrentContext() const;

  // ✅ Get execution result (for polling fallback)
  bool getExecutionSuccess() const { return m_executionSuccess; }
  QString getExecutionMessage() const { return m_executionMessage; }

signals:
  // Program-level signals
  void programExecutionStarted(int totalRecipes);
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

  // Action-level signal with look-ahead (granular command tracking)
  void actionStarted(int recipeIndex, QString current, QString next, QString afterNext);

  // Wait progress (from current recipe)
  void waitProgress(int remainingMs, int totalMs);

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
  ManualAdjustCallback m_manualAdjustCallback;
  UserInputCallback m_userInputCallback;

  // ✅ NEW: Execution mode
  bool m_autoRunMode;        // true = all recipes, false = single recipe
  int m_startRecipeIndex;    // Starting recipe index (for single mode)
  bool m_useDependencyGraph; // true = use state machine for dependencies, false = sequential

  // ✅ Track active RecipeWorker for stop forwarding
  RecipeWorker* m_activeRecipeWorker;
  QMutex m_activeWorkerMutex;  // Protect access to m_activeRecipeWorker

  // ✅ Track completed recipes in current session (for dependency validation)
  std::vector<bool> m_completedRecipes;

  // ✅ Current execution context (shared across recipes in a program run)
  SequenceContext* m_currentContext;

  // ✅ Store execution result for finished() signal handler
  bool m_executionSuccess = false;
  QString m_executionMessage;

  // Helper methods for state machine execution
  void runWithDependencies();
  void runSequential();
  bool checkPrerequisites(int recipeIndex, QString& errorMessage);
};