// RecipeWorker.cpp
#include "RecipeWorker.h"
#include "SequenceEngine.h"
#include "RecipeManager.h"
#include "ProcessSequenceFactory.h"
#include "SequenceBuilder.h"
#include <iostream>

RecipeWorker::RecipeWorker(
  SequenceEngine* engine,
  RecipeManager* recipeManager,
  ProcessSequenceFactory* factory,
  QObject* parent)
  : QThread(parent)
  , m_engine(engine)
  , m_recipeManager(recipeManager)
  , m_factory(factory)
  , m_stopRequested(false)
{
}

void RecipeWorker::requestStop()
{
  m_stopRequested.store(true);
  emit logMessage("Stop requested");
}

// RecipeWorker.cpp
void RecipeWorker::run()
{
  try {
    // Count total enabled steps (raw data)
    int totalEnabledSteps = 0;
    for (const auto& step : m_recipeManager->getAllSteps()) {
      if (step->enabled) {
        totalEnabledSteps++;
      }
    }

    // Emit raw execution data
    emit executionStarted(totalEnabledSteps);
    emit logMessage(QString("Recipe has %1 enabled step(s)").arg(totalEnabledSteps));

    std::string recipeName = m_recipeManager->getRecipeName();
    SequenceBuilder* sequence = m_engine->CreateSequence(recipeName);

    // BUILD PHASE: Fast, just building the sequence
    emit logMessage("Building sequence...");
    buildSequenceFromRecipe(sequence);

    // Check if stopped during build
    if (m_stopRequested.load()) {
      delete sequence;
      emit executionStopped();
      emit executionCompleted(false, "Execution stopped by user");
      return;
    }

    // EXECUTION PHASE
    emit sequenceExecutionStarted();
    emit logMessage("Executing sequence...");

    // ❌ REMOVE THIS ENTIRE BLOCK - It's wrong!
    /*
    int currentStepIndex = 0;
    for (const auto& step : m_recipeManager->getAllSteps()) {
      if (!step->enabled) continue;

      emit stepExecutionStarted(
        currentStepIndex,
        QString::fromStdString(step->userNickname)
      );

      currentStepIndex++;
    }
    */

    // ✅ Just execute - no per-step progress (for now)
    bool success = m_engine->ExecuteSequence(sequence);

    delete sequence;

    QString resultMsg = success
      ? QString("Recipe '%1' completed successfully!").arg(QString::fromStdString(recipeName))
      : "Recipe execution failed! Check logs for details.";

    emit executionCompleted(success, resultMsg);

  }
  catch (const std::exception& e) {
    emit executionCompleted(false, QString("Exception: %1").arg(e.what()));
  }
}


void RecipeWorker::buildSequenceFromRecipe(SequenceBuilder* sequence)
{
  int enabledStepIndex = 0;
  int totalStepIndex = 0;

  for (const auto& step : m_recipeManager->getAllSteps()) {
    totalStepIndex++;

    // Check for stop request
    if (m_stopRequested.load()) {
      emit logMessage("Build stopped by user");
      throw std::runtime_error("Build stopped by user");
    }

    if (!step->enabled) {
      emit logMessage(QString("Skipping disabled step: %1")
        .arg(QString::fromStdString(step->userNickname)));
      continue;
    }

    enabledStepIndex++;

    // ✅ BUILD PHASE: Quick, just adding to sequence
    emit stepBuildStarted(
      enabledStepIndex - 1,  // 0-based index
      QString::fromStdString(step->userNickname),
      QString::fromStdString(step->processType)
    );

    std::cout << "[RecipeWorker] Building step " << enabledStepIndex << ": "
      << step->userNickname << " (Type: " << step->processType << ")" << std::endl;

    std::string errorMsg;
    if (!m_factory->BuildStep(sequence, step.get(), errorMsg)) {
      std::cerr << "[RecipeWorker] " << errorMsg << std::endl;
      throw std::runtime_error(errorMsg);
    }

    emit stepBuildCompleted(enabledStepIndex - 1);
  }

  emit logMessage(QString("Sequence built with %1 enabled step(s)")
    .arg(enabledStepIndex));
}