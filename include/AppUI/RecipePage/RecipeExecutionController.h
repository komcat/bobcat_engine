// RecipeExecutionController.h
// Owns the RecipeWorker lifetime and forwards its output signals so the
// RecipePage can connect to a single stable signal source instead of
// re-wiring on every execution.
//
// Page responsibilities (kept on the page):
//   * Pre-execute validation (validateRecipe, "is anything running?",
//     save-before-run prompt) — happens before Start() is called.
//   * Installing user-interaction callbacks (setUserPromptCallback,
//     setManualAdjustCallback, setUserInputCallback). These reach into
//     page-side widgets / event-loop state, so the page provides them
//     via the WorkerConfigurator passed to Start().
//   * UI updates in response to forwarded signals (progress widget,
//     recipe-tree highlighting, page-state machine).
//
// Controller responsibilities:
//   * Creating the RecipeWorker.
//   * Wiring its 11 output signals so they emit through this controller.
//   * Installing the safety-stop DirectConnection (E-stop critical).
//   * Setting breakpoints on the worker.
//   * Connecting `finished -> deleteLater` so the worker is freed on the
//     event loop after the thread exits, AND clearing m_worker so
//     IsRunning() correctly returns false post-completion.
//   * ShutdownSync() for synchronous teardown from the page destructor.
#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <string>
#include <vector>

class RecipeWorker;
class SequenceEngine;
class ProcessSequenceFactory;
class ProductPositionManager;
class UnifiedMotionManager;
class SafetyWatchManager;
class SequenceContext;

class RecipeExecutionController : public QObject
{
  Q_OBJECT

public:
  explicit RecipeExecutionController(QObject* parent = nullptr);
  ~RecipeExecutionController() override;

  // The configurator runs after the worker is created and signal-wired,
  // BEFORE the worker thread starts. This is where the page installs
  // user-prompt / manual-adjust / user-input callbacks.
  using WorkerConfigurator = std::function<void(RecipeWorker*)>;

  // Build the worker, wire signals, install safety stop, run configurator,
  // start the worker thread. Returns true on success.
  // The recipe is loaded from the JSON file at `recipeFilePath` by the
  // worker itself.
  bool Start(SequenceEngine* sequenceEngine,
             ProcessSequenceFactory* processFactory,
             ProductPositionManager* productPositionManager,
             UnifiedMotionManager* motionManager,
             SafetyWatchManager* safetyWatchManager,
             const std::string& recipeFilePath,
             SequenceContext* externalContext,
             const std::vector<bool>& breakpoints,
             WorkerConfigurator configurator);

  // Ask the active worker to stop. No-op if nothing is running.
  void RequestStop();

  bool IsRunning() const;

  // Synchronous shutdown (call from owner's destructor): quits the
  // worker thread, joins it, deletes the worker.
  void ShutdownSync();

signals:
  // Forwarded RecipeWorker output signals. Same payload as
  // RecipeWorker's own signals — page connects to these.
  void executionStarted(int totalEnabledSteps);
  void stepBuildStarted(int stepIndex, QString stepName, QString stepType);
  void stepBuildCompleted(int stepIndex);
  void sequenceExecutionStarted();
  void executionCompleted(bool success, QString message);
  void executionStopped();
  void logMessage(QString message);
  void waitProgress(int remainingMs, int totalMs);
  void stepExecutionStarted(int stepIndex, QString stepName, QString stepType);
  void stepExecutionCompleted(int stepIndex, bool success);
  void executionPaused(int stepIndex, QString stepName, bool isBreakpoint);

private:
  RecipeWorker* m_worker = nullptr;
};
