// ProgramWorker.cpp
#include "ProgramWorker.h"
#include "ProgramManager.h"
#include "RecipeWorker.h"
#include <QEventLoop>
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
{
}

void ProgramWorker::requestStop()
{
  m_stopRequested.store(true);
  emit logMessage("Program stop requested");
}

void ProgramWorker::setUserPromptCallback(UserPromptCallback callback)
{
  m_userPromptCallback = callback;
}

void ProgramWorker::run()
{
  try {
    // Count total enabled recipes
    int totalEnabledRecipes = 0;
    for (const auto& recipe : m_programManager->getAllRecipes()) {
      if (recipe->enabled) {
        totalEnabledRecipes++;
      }
    }

    emit programExecutionStarted(totalEnabledRecipes);
    emit logMessage(QString("Program has %1 enabled recipe(s)").arg(totalEnabledRecipes));

    // Execute each enabled recipe sequentially
    int executedCount = 0;
    int recipeIndex = 0;

    for (const auto& recipeRef : m_programManager->getAllRecipes()) {
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
        recipeIndex++;
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
      recipeIndex++;
    }

    // All recipes completed successfully
    emit programExecutionCompleted(true,
      QString("Program '%1' completed successfully! Executed %2 recipe(s)")
      .arg(QString::fromStdString(m_programManager->getProgramName()))
      .arg(executedCount));
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

  // ✅ Set UserPrompt callback if available
  if (m_userPromptCallback) {
    recipeWorker->setUserPromptCallback(m_userPromptCallback);
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

  // ✅ FIX: Use QEventLoop to properly wait for completion
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
    delete recipeWorker;
    return false;
  }

  // Start worker
  recipeWorker->start();

  // Wait using event loop (processes signals properly)
  loop.exec();

  // Wait for thread to actually finish
  recipeWorker->wait();

  // Cleanup
  delete recipeWorker;

  return success;
}