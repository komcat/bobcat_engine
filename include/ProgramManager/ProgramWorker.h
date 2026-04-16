// ProgramWorker.h
#pragma once

#include <QThread>
#include <QString>
#include <QMutex>
#include <atomic>
#include <functional>
#include <memory>

class SequenceEngine;
class ProgramManager;
class ProcessSequenceFactory;
class ProductPositionManager;
class UnifiedMotionManager;
class RecipeWorker;
class RecipeExecutionStateMachine;
class SequenceContext;  // ✅ Forward declaration for context sharing
struct PlacementRetraceParams;  // Defined in SystemCommandHandler.h

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

  // Set callback for placement correction retrace (optional)
  using PlacementRetraceCallback = std::function<bool(const PlacementRetraceParams& params)>;
  void setPlacementRetraceCallback(PlacementRetraceCallback callback);

  // ✅ NEW: Configure execution mode
  void setAutoRunMode(bool autoRun);  // true = all recipes, false = single recipe
  void setStartRecipeIndex(int index);  // Which recipe to start from (for single mode)
  void setUseDependencyGraph(bool useDependencies);  // true = use state machine, false = sequential

  // ✅ Loop execution mode
  void setLoopMode(bool enabled);  // Enable infinite loop execution
  void setLoopRecipeIndices(const std::vector<int>& indices);  // Which recipes to loop
  void setIgnoreDependencies(bool ignore);  // Force sequential without checking dependencies
  int getLoopIteration() const { return m_loopIteration; }  // Current loop iteration

  // ✅ Reset completion tracking (call when loading new program)
  void resetCompletionTracking();

  // ============================================================================
  // RETRACE EDIT MODE
  // ============================================================================

  /**
   * @brief Enable/disable retrace edit mode for the next recipe execution
   *
   * When enabled, recipes will pause after each motion command for position adjustment.
   */
  void setRetraceEditMode(bool enabled) { m_retraceEditMode = enabled; }
  bool isRetraceEditMode() const { return m_retraceEditMode; }

  /**
   * @brief Set the callback for retrace edit pause
   */
  using RetraceEditCallback = std::function<bool(
    const std::string& deviceName,
    const std::string& positionName,
    double originalX, double originalY, double originalZ,
    double originalU, double originalV, double originalW,
    double currentX, double currentY, double currentZ,
    double currentU, double currentV, double currentW,
    bool& saveRequested
  )>;
  void setRetraceEditCallback(RetraceEditCallback callback) { m_retraceEditCallback = callback; }

  /**
   * @brief Get retrace edit statistics
   */
  int getRetracePositionsVisited() const { return m_retracePositionsVisited; }
  int getRetracePositionsSaved() const { return m_retracePositionsSaved; }

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

  // Step execution signals (actual execution progress, not build phase)
  void stepExecutionStarted(int recipeIndex, int stepIndex, QString stepName, QString stepType);
  void stepExecutionCompleted(int recipeIndex, int stepIndex, bool success);

  // Action-level signal with look-ahead (granular command tracking)
  void actionStarted(int recipeIndex, QString current, QString next, QString afterNext);

  // Vision result ready (for real-time UI updates)
  void visionResultReady(QString imagePath);

  // Wait progress (from current recipe)
  void waitProgress(int remainingMs, int totalMs);

  // Loop execution signals
  void loopIterationStarted(int iteration);  // Emitted when a new loop iteration begins
  void loopIterationCompleted(int iteration);  // Emitted when a loop iteration finishes

  // ✅ Retrace edit signals
  void retraceEditPause(QString deviceName, QString positionName,
                        double origX, double origY, double origZ,
                        double origU, double origV, double origW,
                        double currX, double currY, double currZ,
                        double currU, double currV, double currW);
  void retraceEditCompleted(int positionsVisited, int positionsSaved);

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
  PlacementRetraceCallback m_placementRetraceCallback;

  // ✅ NEW: Execution mode
  bool m_autoRunMode;        // true = all recipes, false = single recipe
  int m_startRecipeIndex;    // Starting recipe index (for single mode)
  bool m_useDependencyGraph; // true = use state machine for dependencies, false = sequential

  // ✅ Loop execution mode
  bool m_loopEnabled;                     // true = loop the loop recipes indefinitely
  bool m_ignoreDependencies;              // true = skip dependency checks, force sequential
  std::vector<int> m_loopRecipeIndices;   // Indices of recipes to loop
  int m_loopIteration;                    // Current loop iteration (1-based)

  // ✅ Track active RecipeWorker for stop forwarding
  RecipeWorker* m_activeRecipeWorker;
  QMutex m_activeWorkerMutex;  // Protect access to m_activeRecipeWorker

  // ✅ Track completed recipes in current session (for dependency validation)
  std::vector<bool> m_completedRecipes;

  // ✅ Current execution context (shared across recipes in a program run)
  // Using unique_ptr to ensure context persists after runSequential()/runWithDependencies() returns
  // This is needed for chain execution to evaluate conditions after recipe completion
  std::unique_ptr<SequenceContext> m_ownedContext;

  // ✅ Store execution result for finished() signal handler
  bool m_executionSuccess = false;
  QString m_executionMessage;

  // ✅ Retrace edit mode
  bool m_retraceEditMode = false;
  RetraceEditCallback m_retraceEditCallback;
  int m_retracePositionsVisited = 0;
  int m_retracePositionsSaved = 0;

  // Helper methods for state machine execution
  void runWithDependencies();
  void runSequential();
  bool checkPrerequisites(int recipeIndex, QString& errorMessage);

  // Helper methods for loop execution
  void runWithLoop();               // Main loop execution with pre-loop and loop phases
  void runNonLoopRecipes();         // Run non-loop recipes once (Phase 1)
  void runLoopRecipesOnce();        // Run loop recipes once (single iteration)
  void resetLoopRecipeCompletion(); // Reset completion tracking for loop recipes only
};