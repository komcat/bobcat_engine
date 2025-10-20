// RecipeWorker.h
#pragma once

#include <QThread>
#include <QString>
#include <atomic>

class SequenceBuilder;
class SequenceEngine;
class RecipeManager;
class ProcessSequenceFactory;

/**
 * @brief Worker thread for recipe execution (Business Logic Only)
 *
 * Emits raw execution data - no UI knowledge
 */
class RecipeWorker : public QThread
{
  Q_OBJECT

public:
  RecipeWorker(
    SequenceEngine* engine,
    RecipeManager* recipeManager,
    ProcessSequenceFactory* factory,
    QObject* parent = nullptr
  );

  void requestStop();  // Request graceful stop

signals:
  // ✅ RAW DATA ONLY - No UI concepts
  void executionStarted(int totalEnabledSteps);
  void stepBuildStarted(int stepIndex, QString stepName, QString stepType);
  void stepBuildCompleted(int stepIndex);
  void allStepsBuilt();  // ✅ NEW: All steps built, now executing
  void stepExecutionStarted(int stepIndex, QString stepName);  // ✅ NEW
  void stepExecutionCompleted(int stepIndex, QString stepName);  // ✅ NEW
  void sequenceExecutionStarted();
  void executionCompleted(bool success, QString message);
  void executionStopped();
  void logMessage(QString message);  // For debugging/logging only

protected:
  void run() override;

private:
  void buildSequenceFromRecipe(SequenceBuilder* sequence);

  SequenceEngine* m_engine;
  RecipeManager* m_recipeManager;
  ProcessSequenceFactory* m_factory;
  std::atomic<bool> m_stopRequested;
};