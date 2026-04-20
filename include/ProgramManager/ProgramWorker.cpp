// ProgramWorker.cpp
#include "ProgramWorker.h"
#include "ProgramManager.h"
#include "RecipeWorker.h"
#include "RecipeExecutionStateMachine.h"
#include "SequenceContext.h"
#include "ScriptEngine.h"
#include "SequenceEngine.h"
#include <set>
#include <QEventLoop>
#include <QMutexLocker>
#include <QStringList>
#include <QMetaObject>  // ✅ For invokeMethod
#include <memory>  // ✅ For std::make_unique

ProgramWorker::ProgramWorker(
  SequenceEngine* engine,
  ProgramManager* programManager,
  ProcessSequenceFactory* factory,
  ProductPositionManager* positionManager,
  UnifiedMotionManager* motionManager,
  QObject* parent)
  : QThread(parent)
  , m_engine(engine)
  , m_programManager(programManager)
  , m_factory(factory)
  , m_positionManager(positionManager)
  , m_motionManager(motionManager)
  , m_stopRequested(false)
  , m_userPromptCallback(nullptr)
  , m_autoRunMode(true)      // ✅ Default: run all recipes
  , m_startRecipeIndex(0)     // ✅ Default: start from first
  , m_useDependencyGraph(false)  // ✅ Default: sequential execution
  , m_loopEnabled(false)         // ✅ Default: no loop
  , m_ignoreDependencies(false)  // ✅ Default: respect dependencies
  , m_loopIteration(0)           // ✅ No iterations yet
  , m_activeRecipeWorker(nullptr)  // ✅ No active worker initially
  , m_ownedContext(nullptr)  // ✅ Context created on first execution
{
}

void ProgramWorker::requestStop()
{
  m_stopRequested.store(true);
  emit logMessage("Program stop requested");

  // Forward stop to active RecipeWorker
  QMutexLocker locker(&m_activeWorkerMutex);
  if (m_activeRecipeWorker) {
    m_activeRecipeWorker->requestStop();
    emit logMessage("Stop forwarded to active recipe");
  }

  // Forward stop to ScriptEngine if running
  if (m_scriptEngine && m_scriptEngine->IsRunning()) {
    m_scriptEngine->RequestStop();
    emit logMessage("Stop forwarded to script engine");
  }
}

void ProgramWorker::setUserPromptCallback(UserPromptCallback callback)
{
  m_userPromptCallback = callback;
}

void ProgramWorker::setManualAdjustCallback(ManualAdjustCallback callback)
{
  m_manualAdjustCallback = callback;
}

void ProgramWorker::setUserInputCallback(UserInputCallback callback)
{
  m_userInputCallback = callback;
}

void ProgramWorker::setPlacementRetraceCallback(PlacementRetraceCallback callback)
{
  m_placementRetraceCallback = callback;
}

// ✅ NEW: Set execution mode
void ProgramWorker::setAutoRunMode(bool autoRun)
{
  m_autoRunMode = autoRun;
}

// ✅ NEW: Set starting recipe index
void ProgramWorker::setStartRecipeIndex(int index)
{
  m_startRecipeIndex = index;
}

// ✅ NEW: Enable/disable dependency graph execution
void ProgramWorker::setUseDependencyGraph(bool useDependencies)
{
  m_useDependencyGraph = useDependencies;
}

// ✅ Loop mode configuration
void ProgramWorker::setLoopMode(bool enabled)
{
  m_loopEnabled = enabled;
  emit logMessage(QString("Loop mode: %1").arg(enabled ? "enabled" : "disabled"));
}

void ProgramWorker::setLoopRecipeIndices(const std::vector<int>& indices)
{
  m_loopRecipeIndices = indices;
  emit logMessage(QString("Loop recipe indices set: %1 recipes").arg(indices.size()));
}

void ProgramWorker::setIgnoreDependencies(bool ignore)
{
  m_ignoreDependencies = ignore;
  emit logMessage(QString("Ignore dependencies: %1").arg(ignore ? "yes" : "no"));
}

// ✅ NEW: Reset completion tracking
void ProgramWorker::resetCompletionTracking()
{
  m_completedRecipes.clear();
}

void ProgramWorker::run()
{
  emit logMessage("ProgramWorker run() started");

  // Log active slot info
  int activeSlotIndex = m_programManager->getActiveSlotIndex();
  QString slotName = QString::fromStdString(m_programManager->getSlotName(activeSlotIndex));
  emit logMessage(QString("Executing slot: %1 (index %2)").arg(slotName).arg(activeSlotIndex));

  try {
    // Choose execution mode
    if (m_loopEnabled && !m_loopRecipeIndices.empty()) {
      // Loop mode: run non-loop recipes once, then loop the loop recipes
      runWithLoop();
    } else if (m_useDependencyGraph) {
      runWithDependencies();
    } else {
      runSequential();
    }
  }
  catch (const std::exception& e) {
    QString failMsg = QString("Program exception: %1").arg(e.what());
    emit logMessage(QString("Top-level exception: %1").arg(e.what()));
    m_executionSuccess = false;
    m_executionMessage = failMsg;
    emit programExecutionCompleted(false, failMsg);
  }
  emit logMessage("ProgramWorker run() finished");
}

void ProgramWorker::runSequential()
{
  try {
    const auto& allRecipes = m_programManager->getAllRecipes();

    // ✅ Create shared context for all recipes in this program run
    // Context is owned by m_ownedContext to persist after this function returns
    // This is critical for chain execution to evaluate conditions
    m_ownedContext = std::make_unique<SequenceContext>();
    emit logMessage("Created shared context for program run");

    // ✅ Initialize completion tracking (only if not already initialized or in auto-run mode)
    if (m_completedRecipes.empty() || m_completedRecipes.size() != allRecipes.size() || m_autoRunMode) {
      m_completedRecipes.clear();
      m_completedRecipes.resize(allRecipes.size(), false);
    }

    // ✅ DETERMINE WHICH RECIPES TO RUN
    int startIndex = 0;
    int endIndex = static_cast<int>(allRecipes.size());

    if (!m_autoRunMode) {
      // Single recipe mode - only run the selected recipe
      startIndex = m_startRecipeIndex;
      endIndex = m_startRecipeIndex + 1;

      // Validate index
      if (startIndex < 0 || startIndex >= static_cast<int>(allRecipes.size())) {
        m_executionSuccess = false;
        m_executionMessage = "Invalid recipe index";
        emit programExecutionCompleted(false, "Invalid recipe index");
        return;
      }

      // ✅ Check prerequisites if using dependencies
      if (m_useDependencyGraph) {
        QString errorMessage;
        if (!checkPrerequisites(startIndex, errorMessage)) {
          m_executionSuccess = false;
          m_executionMessage = errorMessage;
          emit programExecutionCompleted(false, errorMessage);
          emit logMessage(QString("Cannot run recipe: %1").arg(errorMessage));
          return;
        }
      }

      emit logMessage(QString("Single recipe mode: running recipe at index %1").arg(startIndex));
    }
    else {
      // Auto-run mode - run all enabled recipes
      emit logMessage("Auto-run mode: running all enabled recipes");
    }

    // Count enabled recipes in range
    int totalEnabledRecipes = 0;
    for (int i = startIndex; i < endIndex; i++) {
      if (allRecipes[i]->enabled) {
        totalEnabledRecipes++;
      }
    }

    emit programExecutionStarted(totalEnabledRecipes);
    emit logMessage(QString("Program has %1 enabled recipe(s) to execute").arg(totalEnabledRecipes));

    // Execute recipes in range
    int executedCount = 0;

    for (int recipeIndex = startIndex; recipeIndex < endIndex; recipeIndex++) {
      const auto& recipeRef = allRecipes[recipeIndex];

      // Check for stop request
      if (m_stopRequested.load()) {
        emit logMessage("Program execution stopped by user");
        emit programStopped();
        m_executionSuccess = false;
        m_executionMessage = "Program stopped by user";
        emit programExecutionCompleted(false, "Program stopped by user");
        return;
      }

      if (!recipeRef->enabled) {
        emit recipeSkipped(recipeIndex, QString::fromStdString(recipeRef->displayName));
        emit logMessage(QString("Skipping disabled recipe: %1")
          .arg(QString::fromStdString(recipeRef->displayName)));
        continue;
      }

      // Execute this recipe using RecipeWorker (or script via ScriptEngine)
      emit recipeStarted(recipeIndex, QString::fromStdString(recipeRef->displayName));
      emit logMessage(QString("Starting recipe %1 of %2: %3")
        .arg(executedCount + 1)
        .arg(totalEnabledRecipes)
        .arg(QString::fromStdString(recipeRef->displayName)));

      bool success = false;
      if (recipeRef->type == RecipeReference::SCRIPT) {
        success = executeScript(recipeIndex, recipeRef->recipeFilePath);
      } else {
        success = executeRecipe(recipeIndex, recipeRef->recipeFilePath);
      }

      if (success) {
        // ✅ Mark recipe as completed
        if (recipeIndex >= 0 && recipeIndex < static_cast<int>(m_completedRecipes.size())) {
          m_completedRecipes[recipeIndex] = true;
        }

        emit recipeCompleted(recipeIndex, true, "Recipe completed successfully");
        emit logMessage(QString("Recipe '%1' completed successfully")
          .arg(QString::fromStdString(recipeRef->displayName)));
      }
      else {
        QString errorMsg = QString("Recipe '%1' failed")
          .arg(QString::fromStdString(recipeRef->displayName));
        emit recipeCompleted(recipeIndex, false, errorMsg);
        emit logMessage(errorMsg);

        // Stop program if a recipe fails
        QString failMsg = QString("Program stopped: Recipe '%1' failed")
          .arg(QString::fromStdString(recipeRef->displayName));
        m_executionSuccess = false;
        m_executionMessage = failMsg;
        emit programExecutionCompleted(false, failMsg);
        return;
      }

      executedCount++;
    }

    // All recipes completed successfully
    QString completionMsg = m_autoRunMode
      ? QString("Program '%1' completed successfully! Executed %2 recipe(s)")
      .arg(QString::fromStdString(m_programManager->getProgramName()))
      .arg(executedCount)
      : QString("Recipe '%1' completed successfully!")
      .arg(QString::fromStdString(allRecipes[m_startRecipeIndex]->displayName));

    // ✅ Store result for polling fallback
    m_executionSuccess = true;
    m_executionMessage = completionMsg;

    // ✅ Emit log message first to trigger event processing (this works!)
    emit logMessage(QString("PROGRAM COMPLETED: %1").arg(completionMsg));

    emit programExecutionCompleted(true, completionMsg);
  }
  catch (const std::exception& e) {
    QString failMsg = QString("Program exception: %1").arg(e.what());
    emit logMessage(QString("Exception in runSequential(): %1").arg(e.what()));
    m_executionSuccess = false;
    m_executionMessage = failMsg;
    emit programExecutionCompleted(false, failMsg);
  }
}

void ProgramWorker::runWithDependencies()
{
  try {
    const auto& allRecipes = m_programManager->getAllRecipes();

    // ✅ Create shared context for all recipes in this program run
    // Context is owned by m_ownedContext to persist after this function returns
    m_ownedContext = std::make_unique<SequenceContext>();

    if (allRecipes.empty()) {
      m_executionSuccess = false;
      m_executionMessage = "No recipes in program";
      emit programExecutionCompleted(false, "No recipes in program");
      return;
    }

    // ✅ Initialize completion tracking (only if not already initialized or in auto-run mode)
    if (m_completedRecipes.empty() || m_completedRecipes.size() != allRecipes.size() || m_autoRunMode) {
      m_completedRecipes.clear();
      m_completedRecipes.resize(allRecipes.size(), false);
    }

    // ============================================================================
    // STEP 1: CREATE AND INITIALIZE STATE MACHINE
    // ============================================================================
    RecipeExecutionStateMachine stateMachine;

    // Build recipe names list
    std::vector<std::string> recipeNames;
    for (const auto& recipe : allRecipes) {
      recipeNames.push_back(recipe->displayName);
    }

    stateMachine.initialize(static_cast<int>(allRecipes.size()), recipeNames);
    emit logMessage("Initialized dependency state machine");

    // ============================================================================
    // STEP 2: IMPORT DEPENDENCIES FROM PROGRAM (with conditions)
    // Use the active POR (Plan of Record) dependency set
    // ============================================================================
    int activeSetIndex = m_programManager->getActiveDependencySetIndex();
    emit logMessage(QString("Using dependency set: %1 (POR)")
      .arg(QString::fromStdString(m_programManager->getDependencySetName(activeSetIndex))));

    for (int i = 0; i < static_cast<int>(allRecipes.size()); ++i) {
      const auto& deps = m_programManager->getRecipeDependencies(activeSetIndex, i);

      for (const auto& dep : deps) {
        if (!stateMachine.addDependency(i, dep.predecessorIndex, dep.condition)) {
          QString failMsg = QString("Failed to add dependency: Recipe %1 depends on Recipe %2")
            .arg(i).arg(dep.predecessorIndex);
          m_executionSuccess = false;
          m_executionMessage = failMsg;
          emit programExecutionCompleted(false, failMsg);
          return;
        }
      }
    }

    // Set execution context for condition evaluation
    stateMachine.setExecutionContext(m_ownedContext.get());
    emit logMessage("Execution context set for condition evaluation");

    // ============================================================================
    // STEP 3: VALIDATE DEPENDENCY GRAPH
    // ============================================================================
    std::vector<std::string> errors;
    if (!stateMachine.validateGraph(errors)) {
      QString errorMsg = "Dependency graph validation failed:\n";
      for (const auto& err : errors) {
        errorMsg += "• " + QString::fromStdString(err) + "\n";
      }
      m_executionSuccess = false;
      m_executionMessage = errorMsg;
      emit programExecutionCompleted(false, errorMsg);
      return;
    }

    emit logMessage("Dependency graph validated successfully");

    // ============================================================================
    // STEP 4: MARK DISABLED RECIPES
    // ============================================================================
    for (int i = 0; i < static_cast<int>(allRecipes.size()); ++i) {
      if (!allRecipes[i]->enabled) {
        stateMachine.setRecipeDisabled(i);
        emit logMessage(QString("Recipe %1 is disabled")
          .arg(QString::fromStdString(allRecipes[i]->displayName)));
      }
    }

    // ============================================================================
    // STEP 5: RESET EXECUTION STATE
    // ============================================================================
    stateMachine.resetExecution();

    // Count total recipes to execute (enabled only)
    int totalToExecute = 0;
    for (const auto& recipe : allRecipes) {
      if (recipe->enabled) totalToExecute++;
    }

    emit programExecutionStarted(totalToExecute);
    emit logMessage(QString("Starting dependency-based execution: %1 enabled recipes")
      .arg(totalToExecute));
    emit logMessage(stateMachine.getExecutionSummary().c_str());

    // ============================================================================
    // STEP 6: MAIN EXECUTION LOOP
    // ============================================================================
    int executedCount = 0;

    while (!stateMachine.isExecutionComplete()) {
      // Check for stop request
      if (m_stopRequested.load()) {
        emit logMessage("Program execution stopped by user");
        emit programStopped();
        m_executionSuccess = false;
        m_executionMessage = "Program stopped by user";
        emit programExecutionCompleted(false, "Program stopped by user");
        return;
      }

      // Get all recipes that are ready to execute
      std::vector<int> readyRecipes = stateMachine.getReadyRecipes();

      if (readyRecipes.empty()) {
        // No recipes ready but execution not complete
        // This could mean all remaining recipes are pending with no way to proceed
        int pending, ready, running, completed, failed, skipped;
        stateMachine.getStateCounts(pending, ready, running, completed, failed, skipped);

        if (pending > 0 && ready == 0 && running == 0) {
          // Deadlock - recipes are waiting but nothing can proceed
          QString failMsg = QString("Execution deadlock: %1 recipes pending but none ready").arg(pending);
          m_executionSuccess = false;
          m_executionMessage = failMsg;
          emit programExecutionCompleted(false, failMsg);
          return;
        }

        // All remaining recipes are disabled or skipped
        break;
      }

      // Execute ready recipes (currently one at a time - could be parallelized in future)
      for (int recipeIndex : readyRecipes) {
        const auto& recipeRef = allRecipes[recipeIndex];

        // Check stop again before starting recipe
        if (m_stopRequested.load()) {
          emit logMessage("Program execution stopped by user");
          emit programStopped();
          m_executionSuccess = false;
          m_executionMessage = "Program stopped by user";
          emit programExecutionCompleted(false, "Program stopped by user");
          return;
        }

        // Mark recipe as started
        stateMachine.markRecipeStarted(recipeIndex);

        emit recipeStarted(recipeIndex, QString::fromStdString(recipeRef->displayName));
        emit logMessage(QString("[%1/%2] Starting: %3")
          .arg(executedCount + 1)
          .arg(totalToExecute)
          .arg(QString::fromStdString(recipeRef->displayName)));

        // Show dependencies info
        auto predecessors = stateMachine.getPredecessors(recipeIndex);
        if (!predecessors.empty()) {
          QString depStr = "  Dependencies: ";
          for (size_t i = 0; i < predecessors.size(); ++i) {
            if (i > 0) depStr += ", ";
            depStr += QString("Recipe %1").arg(predecessors[i] + 1);
          }
          emit logMessage(depStr);
        }

        // Execute the recipe or script
        bool success = false;
        if (recipeRef->type == RecipeReference::SCRIPT) {
          success = executeScript(recipeIndex, recipeRef->recipeFilePath);
        } else {
          success = executeRecipe(recipeIndex, recipeRef->recipeFilePath);
        }

        // Mark recipe as completed
        if (success) {
          // ✅ Mark in tracking
          if (recipeIndex >= 0 && recipeIndex < static_cast<int>(m_completedRecipes.size())) {
            m_completedRecipes[recipeIndex] = true;
          }

          stateMachine.markRecipeCompleted(recipeIndex, true);
          emit recipeCompleted(recipeIndex, true, "Recipe completed successfully");
          emit logMessage(QString("✓ Completed: %1")
            .arg(QString::fromStdString(recipeRef->displayName)));
        }
        else {
          stateMachine.markRecipeCompleted(recipeIndex, false, "Recipe execution failed");
          emit recipeCompleted(recipeIndex, false, "Recipe failed");
          emit logMessage(QString("✗ Failed: %1")
            .arg(QString::fromStdString(recipeRef->displayName)));

          // Get list of recipes that will be skipped
          auto successors = stateMachine.getSuccessors(recipeIndex);
          if (!successors.empty()) {
            emit logMessage(QString("  %1 dependent recipe(s) will be skipped")
              .arg(successors.size()));
          }
        }

        emit logMessage(stateMachine.getExecutionSummary().c_str());
        executedCount++;
      }
    }

    // ============================================================================
    // STEP 7: REPORT FINAL STATUS
    // ============================================================================
    int pending, ready, running, completed, failed, skipped;
    stateMachine.getStateCounts(pending, ready, running, completed, failed, skipped);

    emit logMessage(QString("\nExecution Summary:"));
    emit logMessage(QString("  Completed: %1").arg(completed));
    emit logMessage(QString("  Failed: %1").arg(failed));
    emit logMessage(QString("  Skipped: %1").arg(skipped));

    if (failed > 0) {
      QString failMsg = QString("Program '%1' completed with %2 failure(s)")
        .arg(QString::fromStdString(m_programManager->getProgramName()))
        .arg(failed);
      m_executionSuccess = false;
      m_executionMessage = failMsg;
      emit programExecutionCompleted(false, failMsg);
    }
    else {
      QString successMsg = QString("Program '%1' completed successfully! Executed %2 recipe(s)")
        .arg(QString::fromStdString(m_programManager->getProgramName()))
        .arg(completed);
      m_executionSuccess = true;
      m_executionMessage = successMsg;
      emit programExecutionCompleted(true, successMsg);
    }
  }
  catch (const std::exception& e) {
    QString failMsg = QString("Program exception: %1").arg(e.what());
    m_executionSuccess = false;
    m_executionMessage = failMsg;
    emit programExecutionCompleted(false, failMsg);
  }
}

bool ProgramWorker::executeRecipe(int recipeIndex, const std::string& recipeFilePath)
{
  // ✅ Create RecipeWorker for this recipe
  RecipeWorker* recipeWorker = new RecipeWorker(
    m_engine,
    m_factory,
    m_positionManager,
    m_motionManager,
    recipeFilePath,
    nullptr  // No parent - we'll delete it manually
  );

  // ✅ Register as active worker (thread-safe)
  {
    QMutexLocker locker(&m_activeWorkerMutex);
    m_activeRecipeWorker = recipeWorker;
  }

  // ✅ Set UserPrompt callback if available
  if (m_userPromptCallback) {
    recipeWorker->setUserPromptCallback(m_userPromptCallback);
  }

  // ✅ Set ManualAdjust callback if available
  if (m_manualAdjustCallback) {
    recipeWorker->setManualAdjustCallback(m_manualAdjustCallback);
  }

  // ✅ Set UserInput callback if available
  if (m_userInputCallback) {
    recipeWorker->setUserInputCallback(m_userInputCallback);
  }

  if (m_placementRetraceCallback) {
    recipeWorker->setPlacementRetraceCallback(m_placementRetraceCallback);
  }

  // ✅ Set shared context for vision result storage
  if (m_ownedContext) {
    recipeWorker->setExternalContext(m_ownedContext.get());
  }

  // ✅ Set loop mode to reduce verbose logging during rapid iterations
  if (m_loopEnabled) {
    recipeWorker->setLoopMode(true);
  }

  // ✅ Set retrace edit mode if enabled
  if (m_retraceEditMode) {
    recipeWorker->setRetraceEditMode(true);
    if (m_retraceEditCallback) {
      recipeWorker->setRetraceEditCallback(m_retraceEditCallback);
    }
  }

  // ✅ Connect RecipeWorker signals to ProgramWorker signals
  // Forward step-level signals with recipeIndex prefix
  connect(recipeWorker, &RecipeWorker::stepBuildStarted,
    this, [this, recipeIndex](int stepIndex, QString stepName, QString stepType) {
    emit stepBuildStarted(recipeIndex, stepIndex, stepName, stepType);
  });

  connect(recipeWorker, &RecipeWorker::stepBuildCompleted,
    this, [this, recipeIndex](int stepIndex) {
    emit stepBuildCompleted(recipeIndex, stepIndex);
  });

  connect(recipeWorker, &RecipeWorker::sequenceExecutionStarted,
    this, [this, recipeIndex]() {
    emit sequenceExecutionStarted(recipeIndex);
  });

  // Forward step execution signals (actual execution progress)
  connect(recipeWorker, &RecipeWorker::stepExecutionStarted,
    this, [this, recipeIndex](int stepIndex, QString stepName, QString stepType) {
    emit stepExecutionStarted(recipeIndex, stepIndex, stepName, stepType);
  });
  connect(recipeWorker, &RecipeWorker::stepExecutionCompleted,
    this, [this, recipeIndex](int stepIndex, bool success) {
    emit stepExecutionCompleted(recipeIndex, stepIndex, success);
  });

  // Forward action started signals with look-ahead
  connect(recipeWorker, &RecipeWorker::actionStarted,
    this, [this, recipeIndex](QString current, QString next, QString afterNext) {
    emit actionStarted(recipeIndex, current, next, afterNext);
  });

  // Forward vision result ready for real-time UI updates
  connect(recipeWorker, &RecipeWorker::visionResultReady,
    this, &ProgramWorker::visionResultReady);

  // Forward log messages
  connect(recipeWorker, &RecipeWorker::logMessage,
    this, &ProgramWorker::logMessage);

  // Forward wait progress updates
  connect(recipeWorker, &RecipeWorker::waitProgress,
    this, &ProgramWorker::waitProgress);

  // ✅ Forward retrace edit signals
  connect(recipeWorker, &RecipeWorker::retraceEditPause,
    this, &ProgramWorker::retraceEditPause);
  connect(recipeWorker, &RecipeWorker::retraceEditCompleted,
    this, [this](int visited, int saved) {
      m_retracePositionsVisited += visited;
      m_retracePositionsSaved += saved;
      emit retraceEditCompleted(visited, saved);
    });

  // ✅ Use QEventLoop to properly wait for completion
  bool success = false;
  QString errorMessage;
  QEventLoop loop;

  // Connect completion signal to event loop (use Qt::QueuedConnection for cross-thread safety)
  connect(recipeWorker, &RecipeWorker::executionCompleted,
    &loop, [&loop, &success, &errorMessage](bool s, QString msg) {
    success = s;
    errorMessage = msg;
    loop.quit();  // Exit event loop
  }, Qt::QueuedConnection);

  // Also quit on stopped signal
  connect(recipeWorker, &RecipeWorker::executionStopped,
    &loop, [&loop, &success]() {
    success = false;
    loop.quit();
  }, Qt::QueuedConnection);

  // Check for stop request before starting
  if (m_stopRequested.load()) {
    QMutexLocker locker(&m_activeWorkerMutex);
    m_activeRecipeWorker = nullptr;
    delete recipeWorker;
    return false;
  }

  // Start worker
  recipeWorker->start();

  // Wait using event loop (processes signals properly)
  loop.exec();

  // Wait for thread to actually finish
  recipeWorker->wait();

  // ✅ Clear active worker (thread-safe)
  {
    QMutexLocker locker(&m_activeWorkerMutex);
    m_activeRecipeWorker = nullptr;
  }

  // ✅ Emit completion marker BEFORE deleting worker (signal forwarding still active)
  if (success) {
    emit logMessage(QString("RECIPE EXECUTION COMPLETED"));
  }

  // Cleanup
  delete recipeWorker;

  return success;
}

bool ProgramWorker::executeScript(int /*scriptIndex*/, const std::string& scriptFilePath)
{
  if (!m_scriptEngine) {
    emit logMessage("Script engine not available");
    return false;
  }

  if (m_scriptEngine->IsRunning()) {
    emit logMessage("Script engine is busy");
    return false;
  }

  emit logMessage(QString("Executing script: %1").arg(QString::fromStdString(scriptFilePath)));

  // Wire up user prompt callback so bobcat.prompt() works from scripts
  if (m_userPromptCallback && m_engine) {
    m_engine->SetUserPromptCallback(m_userPromptCallback);
  }

  // Set output callback to forward script output to log
  m_scriptEngine->SetOutputCallback([this](const std::string& output) {
    emit logMessage(QString("[Script] %1").arg(QString::fromStdString(output)));
  });

  std::string errorMessage;
  bool success = m_scriptEngine->ExecuteFile(scriptFilePath, errorMessage);

  // Clear output callback
  m_scriptEngine->SetOutputCallback(nullptr);

  if (!success) {
    emit logMessage(QString("Script error: %1").arg(QString::fromStdString(errorMessage)));
  }

  return success;
}

bool ProgramWorker::checkPrerequisites(int recipeIndex, QString& errorMessage)
{
  // Get dependencies for this recipe from the active POR set
  int activeSetIndex = m_programManager->getActiveDependencySetIndex();
  auto dependencies = m_programManager->getRecipeDependencies(activeSetIndex, recipeIndex);

  if (dependencies.empty()) {
    // No prerequisites, can run
    return true;
  }

  // Check if all prerequisites have been completed
  QStringList missingPrereqs;
  for (const auto& dep : dependencies) {
    if (dep.predecessorIndex < 0 || dep.predecessorIndex >= static_cast<int>(m_completedRecipes.size())) {
      // Invalid dependency index
      missingPrereqs << QString("Recipe %1 (invalid index)").arg(dep.predecessorIndex + 1);
      continue;
    }

    if (!m_completedRecipes[dep.predecessorIndex]) {
      // Prerequisite not completed
      const RecipeReference* depRecipe = m_programManager->getRecipe(dep.predecessorIndex);
      if (depRecipe) {
        missingPrereqs << QString("Recipe %1: %2")
          .arg(dep.predecessorIndex + 1)
          .arg(QString::fromStdString(depRecipe->displayName));
      } else {
        missingPrereqs << QString("Recipe %1").arg(dep.predecessorIndex + 1);
      }
    }
  }

  if (!missingPrereqs.isEmpty()) {
    const RecipeReference* recipe = m_programManager->getRecipe(recipeIndex);
    QString recipeName = recipe ? QString::fromStdString(recipe->displayName) : QString("Recipe %1").arg(recipeIndex + 1);

    errorMessage = QString("Cannot run '%1'.\n\nPrerequisites not completed:\n• %2\n\nPlease run the prerequisite recipe(s) first.")
      .arg(recipeName)
      .arg(missingPrereqs.join("\n• "));
    return false;
  }

  return true;
}

SequenceContext* ProgramWorker::getCurrentContext() const
{
  // ✅ Returns the shared context used across all recipes in this program run
  // Context is created at start of runSequential()/runWithDependencies()
  // and passed to each RecipeWorker via setExternalContext()
  // Context persists after run functions return to support chain execution
  return m_ownedContext.get();
}

// ============================================================================
// LOOP EXECUTION METHODS
// ============================================================================

void ProgramWorker::runWithLoop()
{
  emit logMessage("runWithLoop() started");

  const auto& allRecipes = m_programManager->getAllRecipes();

  // Create shared context for all recipes in this program run
  m_ownedContext = std::make_unique<SequenceContext>();

  // Initialize completion tracking
  m_completedRecipes.clear();
  m_completedRecipes.resize(allRecipes.size(), false);

  // Build sets of loop and non-loop recipe indices
  std::set<int> loopRecipeSet(m_loopRecipeIndices.begin(), m_loopRecipeIndices.end());

  std::vector<int> nonLoopIndices;
  for (size_t i = 0; i < allRecipes.size(); i++) {
    if (loopRecipeSet.find(static_cast<int>(i)) == loopRecipeSet.end() && allRecipes[i]->enabled) {
      nonLoopIndices.push_back(static_cast<int>(i));
    }
  }

  int totalNonLoop = static_cast<int>(nonLoopIndices.size());
  int totalLoop = static_cast<int>(m_loopRecipeIndices.size());

  emit logMessage(QString("=== LOOP EXECUTION MODE %1 ===")
    .arg(m_ignoreDependencies ? "(Sequential)" : "(with Dependencies)"));
  emit logMessage(QString("Loop recipes: %1 (selected for loop)").arg(totalLoop));
  emit logMessage(QString("Skipped recipes: %1 (not selected)").arg(totalNonLoop));
  if (m_ignoreDependencies) {
    emit logMessage("Dependencies ignored - running in sequential order");
  }

  // =========================================================================
  // CREATE STATE MACHINE FOR ALL RECIPES
  // =========================================================================
  RecipeExecutionStateMachine stateMachine;

  std::vector<std::string> recipeNames;
  for (const auto& recipe : allRecipes) {
    recipeNames.push_back(recipe->displayName);
  }
  stateMachine.initialize(static_cast<int>(allRecipes.size()), recipeNames);

  // Import all dependencies from the active POR set
  int activeSetIndex = m_programManager->getActiveDependencySetIndex();
  emit logMessage(QString("Using dependency set: %1 (POR)")
    .arg(QString::fromStdString(m_programManager->getDependencySetName(activeSetIndex))));

  for (int i = 0; i < static_cast<int>(allRecipes.size()); ++i) {
    const auto& deps = m_programManager->getRecipeDependencies(activeSetIndex, i);
    for (const auto& dep : deps) {
      if (!stateMachine.addDependency(i, dep.predecessorIndex, dep.condition)) {
        QString failMsg = QString("Failed to add dependency: Recipe %1 depends on Recipe %2")
          .arg(i + 1).arg(dep.predecessorIndex + 1);
        m_executionSuccess = false;
        m_executionMessage = failMsg;
        emit programExecutionCompleted(false, failMsg);
        return;
      }
    }
  }

  stateMachine.setExecutionContext(m_ownedContext.get());

  // Validate dependency graph
  std::vector<std::string> errors;
  if (!stateMachine.validateGraph(errors)) {
    QString errorMsg = "Dependency graph validation failed:\n";
    for (const auto& err : errors) {
      errorMsg += QString::fromStdString(err) + "\n";
    }
    m_executionSuccess = false;
    m_executionMessage = errorMsg;
    emit programExecutionCompleted(false, errorMsg);
    return;
  }

  // Mark disabled recipes
  for (int i = 0; i < static_cast<int>(allRecipes.size()); ++i) {
    if (!allRecipes[i]->enabled) {
      stateMachine.markDisabled(i);
    }
  }

  // =========================================================================
  // LOOP EXECUTION: Only run selected loop recipes (non-selected are skipped)
  // =========================================================================
  if (m_loopRecipeIndices.empty()) {
    emit logMessage("No loop recipes configured - execution complete");
    m_executionSuccess = true;
    m_executionMessage = "Program completed (no loop recipes)";
    emit programExecutionCompleted(true, "Program completed (no loop recipes)");
    return;
  }

  emit logMessage(QString("\n--- Phase 2: Starting infinite loop with %1 recipe(s) ---").arg(totalLoop));
  emit programExecutionStarted(totalLoop);

  // ✅ Enable loop execution mode - auto-skip user interactions (prompts, manual adjust, etc.)
  if (m_ownedContext) {
    m_ownedContext->SetLoopExecutionMode(true);
    emit logMessage("Loop mode enabled: User interactions will be auto-skipped");
  }

  m_loopIteration = 0;

  while (!m_stopRequested.load()) {
    m_loopIteration++;
    emit loopIterationStarted(m_loopIteration);

    // ✅ PERFORMANCE: Reduce log verbosity during rapid loop execution
    // Only emit detailed iteration headers every 10 iterations to reduce signal flood
    bool verboseLogging = (m_loopIteration <= 5) || (m_loopIteration % 10 == 0);
    if (verboseLogging) {
      emit logMessage(QString("\n========== LOOP ITERATION %1 ==========").arg(m_loopIteration));
    }

    // Reset only loop recipes in the state machine for this iteration
    for (int idx : m_loopRecipeIndices) {
      if (allRecipes[idx]->enabled) {
        stateMachine.resetRecipeState(idx);
      }
    }

    // Reset completion tracking for loop recipes
    resetLoopRecipeCompletion();

    // ✅ CRITICAL FIX: Clear context captured values between iterations to prevent memory exhaustion
    // This releases frame data, image buffers, and accumulated values from previous iteration
    if (m_ownedContext) {
      m_ownedContext->ClearCapturedValues();  // Clear values but keep settings like loop mode
    }

    // Run loop recipes
    bool iterationSuccess = true;

    if (m_ignoreDependencies) {
      // ✅ SEQUENTIAL MODE: Run loop recipes in order, ignore dependencies
      for (int recipeIndex : m_loopRecipeIndices) {
        if (m_stopRequested.load()) break;

        const auto& recipeRef = allRecipes[recipeIndex];
        if (!recipeRef->enabled) continue;

        emit recipeStarted(recipeIndex, QString::fromStdString(recipeRef->displayName));
        // Reduce log verbosity during rapid iterations
        if (verboseLogging) {
          emit logMessage(QString("[Iter %1] Starting: %2")
            .arg(m_loopIteration)
            .arg(QString::fromStdString(recipeRef->displayName)));
        }

        bool success = false;
        if (recipeRef->type == RecipeReference::SCRIPT) {
          success = executeScript(recipeIndex, recipeRef->recipeFilePath);
        } else {
          success = executeRecipe(recipeIndex, recipeRef->recipeFilePath);
        }

        if (success) {
          m_completedRecipes[recipeIndex] = true;
          emit recipeCompleted(recipeIndex, true, "Recipe completed successfully");
        } else {
          emit recipeCompleted(recipeIndex, false, "Recipe failed");
          // Always log failures
          emit logMessage(QString("[Iter %1] Recipe '%2' failed")
            .arg(m_loopIteration)
            .arg(QString::fromStdString(recipeRef->displayName)));
          iterationSuccess = false;
          break;
        }
      }
    } else {
      // ✅ DEPENDENCY MODE: Use state machine for dependency-based execution
      bool iterationComplete = false;

      while (!iterationComplete && !m_stopRequested.load()) {
        // Get recipes that are ready to run
        std::vector<int> readyRecipes = stateMachine.getReadyRecipes();

        // Filter to only loop recipes
        std::vector<int> readyLoop;
        for (int idx : readyRecipes) {
          if (loopRecipeSet.find(idx) != loopRecipeSet.end()) {
            readyLoop.push_back(idx);
          }
        }

        if (readyLoop.empty()) {
          // Check if all loop recipes are done for this iteration
          bool allDone = true;
          for (int idx : m_loopRecipeIndices) {
            auto state = stateMachine.getRecipeState(idx);
            if (state != RecipeExecutionState::Completed &&
                state != RecipeExecutionState::Skipped &&
                state != RecipeExecutionState::Failed &&
                state != RecipeExecutionState::Disabled) {
              allDone = false;
              break;
            }
          }
          iterationComplete = allDone;
          break;
        }

        // Execute ready loop recipes
        for (int recipeIndex : readyLoop) {
          if (m_stopRequested.load()) break;

          const auto& recipeRef = allRecipes[recipeIndex];
          stateMachine.markRunning(recipeIndex);

          emit recipeStarted(recipeIndex, QString::fromStdString(recipeRef->displayName));
          // Reduce log verbosity during rapid iterations
          if (verboseLogging) {
            emit logMessage(QString("[Iter %1] Starting: %2")
              .arg(m_loopIteration)
              .arg(QString::fromStdString(recipeRef->displayName)));
          }

          bool success = false;
          if (recipeRef->type == RecipeReference::SCRIPT) {
            success = executeScript(recipeIndex, recipeRef->recipeFilePath);
          } else {
            success = executeRecipe(recipeIndex, recipeRef->recipeFilePath);
          }

          if (success) {
            m_completedRecipes[recipeIndex] = true;
            stateMachine.markCompleted(recipeIndex);
            emit recipeCompleted(recipeIndex, true, "Recipe completed successfully");
          } else {
            stateMachine.markFailed(recipeIndex);
            emit recipeCompleted(recipeIndex, false, "Recipe failed");
            // Always log failures
            emit logMessage(QString("[Iter %1] Recipe '%2' failed")
              .arg(m_loopIteration)
              .arg(QString::fromStdString(recipeRef->displayName)));
            iterationSuccess = false;
            iterationComplete = true;
            break;
          }
        }
      }
    }

    emit loopIterationCompleted(m_loopIteration);

    if (!iterationSuccess) {
      QString failMsg = QString("Loop stopped: recipe failed at iteration %1").arg(m_loopIteration);
      emit logMessage(QString("Loop stopped due to recipe failure at iteration %1").arg(m_loopIteration));
      m_executionSuccess = false;
      m_executionMessage = failMsg;
      emit programExecutionCompleted(false, failMsg);
      return;
    }

    if (m_stopRequested.load()) {
      break;
    }

    // Reduce log verbosity during rapid iterations
    if (verboseLogging) {
      emit logMessage(QString("Iteration %1 complete").arg(m_loopIteration));
    }
  }

  // Loop was stopped by user
  emit logMessage(QString("\n=== LOOP STOPPED ==="));
  emit logMessage(QString("Completed %1 full iteration(s)").arg(m_loopIteration));
  emit programStopped();
  QString completionMsg = QString("Loop execution stopped after %1 iteration(s)").arg(m_loopIteration);
  m_executionSuccess = true;
  m_executionMessage = completionMsg;
  emit programExecutionCompleted(true, completionMsg);
}

void ProgramWorker::runNonLoopRecipes()
{
  // This is a placeholder - the actual implementation is inline in runWithLoop()
  // to avoid code duplication
}

void ProgramWorker::runLoopRecipesOnce()
{
  // This is a placeholder - the actual implementation is inline in runWithLoop()
  // to avoid code duplication
}

void ProgramWorker::resetLoopRecipeCompletion()
{
  // Reset completion tracking only for recipes that are in the loop
  for (int recipeIndex : m_loopRecipeIndices) {
    if (recipeIndex >= 0 && recipeIndex < static_cast<int>(m_completedRecipes.size())) {
      m_completedRecipes[recipeIndex] = false;
    }
  }
}