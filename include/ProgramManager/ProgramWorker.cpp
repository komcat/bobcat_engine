// ProgramWorker.cpp
#include "ProgramWorker.h"
#include "ProgramManager.h"
#include "RecipeWorker.h"
#include "RecipeExecutionStateMachine.h"
#include <QEventLoop>
#include <QMutexLocker>
#include <QStringList>
#include <iostream>

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
  , m_activeRecipeWorker(nullptr)  // ✅ No active worker initially
{
}

void ProgramWorker::requestStop()
{
  m_stopRequested.store(true);
  emit logMessage("Program stop requested");

  // ✅ CRITICAL FIX: Forward stop to active RecipeWorker
  QMutexLocker locker(&m_activeWorkerMutex);
  if (m_activeRecipeWorker) {
    m_activeRecipeWorker->requestStop();
    emit logMessage("Stop forwarded to active recipe");
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

// ✅ NEW: Reset completion tracking
void ProgramWorker::resetCompletionTracking()
{
  m_completedRecipes.clear();
}

void ProgramWorker::run()
{
  try {
    // Choose execution mode based on dependency graph setting
    if (m_useDependencyGraph) {
      runWithDependencies();
    } else {
      runSequential();
    }
  }
  catch (const std::exception& e) {
    emit programExecutionCompleted(false, QString("Program exception: %1").arg(e.what()));
  }
}

void ProgramWorker::runSequential()
{
  try {
    const auto& allRecipes = m_programManager->getAllRecipes();

    // ✅ Initialize completion tracking (only if not already initialized or in auto-run mode)
    if (m_completedRecipes.empty() || m_completedRecipes.size() != allRecipes.size() || m_autoRunMode) {
      m_completedRecipes.clear();
      m_completedRecipes.resize(allRecipes.size(), false);
    }

    // ✅ DETERMINE WHICH RECIPES TO RUN
    int startIndex = 0;
    int endIndex = allRecipes.size();

    if (!m_autoRunMode) {
      // Single recipe mode - only run the selected recipe
      startIndex = m_startRecipeIndex;
      endIndex = m_startRecipeIndex + 1;

      // Validate index
      if (startIndex < 0 || startIndex >= static_cast<int>(allRecipes.size())) {
        emit programExecutionCompleted(false, "Invalid recipe index");
        return;
      }

      // ✅ Check prerequisites if using dependencies
      if (m_useDependencyGraph) {
        QString errorMessage;
        if (!checkPrerequisites(startIndex, errorMessage)) {
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
        emit programExecutionCompleted(false, "Program stopped by user");
        return;
      }

      if (!recipeRef->enabled) {
        emit recipeSkipped(recipeIndex, QString::fromStdString(recipeRef->displayName));
        emit logMessage(QString("Skipping disabled recipe: %1")
          .arg(QString::fromStdString(recipeRef->displayName)));
        continue;
      }

      // Execute this recipe using RecipeWorker
      emit recipeStarted(recipeIndex, QString::fromStdString(recipeRef->displayName));
      emit logMessage(QString("Starting recipe %1 of %2: %3")
        .arg(executedCount + 1)
        .arg(totalEnabledRecipes)
        .arg(QString::fromStdString(recipeRef->displayName)));

      bool success = executeRecipe(recipeIndex, recipeRef->recipeFilePath);

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
        emit programExecutionCompleted(false,
          QString("Program stopped: Recipe '%1' failed")
          .arg(QString::fromStdString(recipeRef->displayName)));
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

    emit programExecutionCompleted(true, completionMsg);
  }
  catch (const std::exception& e) {
    emit programExecutionCompleted(false, QString("Program exception: %1").arg(e.what()));
  }
}

void ProgramWorker::runWithDependencies()
{
  try {
    const auto& allRecipes = m_programManager->getAllRecipes();

    if (allRecipes.empty()) {
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

    stateMachine.initialize(allRecipes.size(), recipeNames);
    emit logMessage("Initialized dependency state machine");

    // ============================================================================
    // STEP 2: IMPORT DEPENDENCIES FROM PROGRAM
    // ============================================================================
    for (int i = 0; i < static_cast<int>(allRecipes.size()); ++i) {
      const auto& deps = m_programManager->getRecipeDependencies(i);

      for (int predIndex : deps) {
        if (!stateMachine.addDependency(i, predIndex)) {
          emit programExecutionCompleted(false,
            QString("Failed to add dependency: Recipe %1 depends on Recipe %2")
            .arg(i).arg(predIndex));
          return;
        }
      }
    }

    // ============================================================================
    // STEP 3: VALIDATE DEPENDENCY GRAPH
    // ============================================================================
    std::vector<std::string> errors;
    if (!stateMachine.validateGraph(errors)) {
      QString errorMsg = "Dependency graph validation failed:\n";
      for (const auto& err : errors) {
        errorMsg += "• " + QString::fromStdString(err) + "\n";
      }
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
          emit programExecutionCompleted(false,
            QString("Execution deadlock: %1 recipes pending but none ready").arg(pending));
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

        // Execute the recipe
        bool success = executeRecipe(recipeIndex, recipeRef->recipeFilePath);

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
      emit programExecutionCompleted(false,
        QString("Program '%1' completed with %2 failure(s)")
        .arg(QString::fromStdString(m_programManager->getProgramName()))
        .arg(failed));
    }
    else {
      emit programExecutionCompleted(true,
        QString("Program '%1' completed successfully! Executed %2 recipe(s)")
        .arg(QString::fromStdString(m_programManager->getProgramName()))
        .arg(completed));
    }
  }
  catch (const std::exception& e) {
    emit programExecutionCompleted(false, QString("Program exception: %1").arg(e.what()));
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

  // Forward log messages
  connect(recipeWorker, &RecipeWorker::logMessage,
    this, &ProgramWorker::logMessage);

  // ✅ Use QEventLoop to properly wait for completion
  bool success = false;
  QString errorMessage;
  QEventLoop loop;

  // Connect completion signal to event loop
  connect(recipeWorker, &RecipeWorker::executionCompleted,
    &loop, [&loop, &success, &errorMessage](bool s, QString msg) {
    success = s;
    errorMessage = msg;
    loop.quit();  // Exit event loop
  });

  // Also quit on stopped signal
  connect(recipeWorker, &RecipeWorker::executionStopped,
    &loop, [&loop, &success]() {
    success = false;
    loop.quit();
  });

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

  // Cleanup
  delete recipeWorker;

  return success;
}

bool ProgramWorker::checkPrerequisites(int recipeIndex, QString& errorMessage)
{
  // Get dependencies for this recipe
  std::vector<int> dependencies = m_programManager->getRecipeDependencies(recipeIndex);

  if (dependencies.empty()) {
    // No prerequisites, can run
    return true;
  }

  // Check if all prerequisites have been completed
  QStringList missingPrereqs;
  for (int depIndex : dependencies) {
    if (depIndex < 0 || depIndex >= static_cast<int>(m_completedRecipes.size())) {
      // Invalid dependency index
      missingPrereqs << QString("Recipe %1 (invalid index)").arg(depIndex + 1);
      continue;
    }

    if (!m_completedRecipes[depIndex]) {
      // Prerequisite not completed
      const RecipeReference* depRecipe = m_programManager->getRecipe(depIndex);
      if (depRecipe) {
        missingPrereqs << QString("Recipe %1: %2")
          .arg(depIndex + 1)
          .arg(QString::fromStdString(depRecipe->displayName));
      } else {
        missingPrereqs << QString("Recipe %1").arg(depIndex + 1);
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