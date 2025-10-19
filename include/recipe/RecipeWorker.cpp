// RecipeWorker.cpp
#include "RecipeWorker.h"
#include "SequenceEngine.h"
#include "RecipeManager.h"
#include "ProcessSequenceFactory.h"
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
{
}

void RecipeWorker::run()
{
  try {
    emit progressUpdate("Building sequence...");

    std::string recipeName = m_recipeManager->getRecipeName();
    SequenceBuilder* sequence = m_engine->CreateSequence(recipeName);

    buildSequenceFromRecipe(sequence);

    emit progressUpdate("Executing sequence...");

    bool success = m_engine->ExecuteSequence(sequence);

    delete sequence;

    if (success) {
      emit finished(true, "Recipe completed successfully!");
    }
    else {
      emit finished(false, "Recipe execution failed! Check logs for details.");
    }
  }
  catch (const std::exception& e) {
    emit finished(false, QString("Exception: %1").arg(e.what()));
  }
}

void RecipeWorker::buildSequenceFromRecipe(SequenceBuilder* sequence)
{
  int enabledCount = 0;
  int totalCount = 0;

  for (const auto& step : m_recipeManager->getAllSteps()) {
    totalCount++;

    if (!step->enabled) {
      std::cout << "[RecipeWorker] Skipping disabled step: "
        << step->userNickname << std::endl;
      continue;
    }

    enabledCount++;

    emit progressUpdate(QString("Building step %1: %2")
      .arg(enabledCount)
      .arg(QString::fromStdString(step->userNickname)));

    std::cout << "[RecipeWorker] Building step " << enabledCount << ": "
      << step->userNickname << " (Type: " << step->processType << ")" << std::endl;

    std::string errorMsg;
    if (!m_factory->BuildStep(sequence, step.get(), errorMsg)) {
      std::cerr << "[RecipeWorker] " << errorMsg << std::endl;
      throw std::runtime_error(errorMsg);
    }
  }

  std::cout << "[RecipeWorker] Sequence built successfully: "
    << enabledCount << " enabled steps out of "
    << totalCount << " total steps" << std::endl;
}