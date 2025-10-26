// RecipeWorker.h
#pragma once

#include <QThread>
#include <QString>
#include <atomic>
#include <functional>

class SequenceBuilder;
class SequenceEngine;
class RecipeManager;
class ProcessSequenceFactory;
class ProductPositionManager;
class UnifiedMotionManager;

/**
 * @brief Worker thread for recipe execution (Self-contained)
 *
 * Handles EVERYTHING for executing a single recipe:
 * - Loading recipe file
 * - Loading product positions
 * - Building device registry
 * - UserPrompt callback setup
 * - Building and executing sequence
 *
 * This is the CORE executor - ProgramWorker uses this internally
 */
class RecipeWorker : public QThread
{
  Q_OBJECT

public:
  RecipeWorker(
    SequenceEngine* engine,
    ProcessSequenceFactory* factory,
    ProductPositionManager* positionManager,
    UnifiedMotionManager* motionManager,
    const std::string& recipeFilePath,
    QObject* parent = nullptr
  );

  void requestStop();  // Request graceful stop

  // Set callback for user prompts (optional - if not set, auto-accepts all)
  using UserPromptCallback = std::function<bool(const std::string& title, const std::string& message)>;
  void setUserPromptCallback(UserPromptCallback callback);

  // Set callback for manual adjust (optional - if not set, auto-accepts all)
  using ManualAdjustCallback = std::function<bool(
    const std::string& message,
    const std::string& deviceName,
    double stepSizeUm,
    bool enableX,
    bool enableY,
    bool enableZ
  )>;
  void setManualAdjustCallback(ManualAdjustCallback callback);

signals:
  // ✅ RAW DATA ONLY - No UI concepts
  void executionStarted(int totalEnabledSteps);
  void stepBuildStarted(int stepIndex, QString stepName, QString stepType);
  void stepBuildCompleted(int stepIndex);
  void sequenceExecutionStarted();
  void executionCompleted(bool success, QString message);
  void executionStopped();
  void logMessage(QString message);

protected:
  void run() override;

private:
  void buildSequenceFromRecipe(SequenceBuilder* sequence, RecipeManager* recipeManager);

  SequenceEngine* m_engine;
  ProcessSequenceFactory* m_factory;
  ProductPositionManager* m_positionManager;
  UnifiedMotionManager* m_motionManager;
  std::string m_recipeFilePath;
  std::atomic<bool> m_stopRequested;
  UserPromptCallback m_userPromptCallback;
  ManualAdjustCallback m_manualAdjustCallback;
};