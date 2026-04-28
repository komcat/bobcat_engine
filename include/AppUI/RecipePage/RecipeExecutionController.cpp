// RecipeExecutionController.cpp
#include "RecipeExecutionController.h"

#include "RecipeWorker.h"
#include "safety/SafetyWatchManager.h"

RecipeExecutionController::RecipeExecutionController(QObject* parent)
  : QObject(parent)
{
}

RecipeExecutionController::~RecipeExecutionController()
{
  ShutdownSync();
}

bool RecipeExecutionController::Start(
    SequenceEngine* sequenceEngine,
    ProcessSequenceFactory* processFactory,
    ProductPositionManager* productPositionManager,
    UnifiedMotionManager* motionManager,
    SafetyWatchManager* safetyWatchManager,
    const std::string& recipeFilePath,
    SequenceContext* externalContext,
    const std::vector<bool>& breakpoints,
    WorkerConfigurator configurator)
{
  if (IsRunning()) {
    return false;  // Already running — caller should have checked.
  }

  m_worker = new RecipeWorker(
    sequenceEngine,
    processFactory,
    productPositionManager,
    motionManager,
    recipeFilePath,
    this  // Parent for ownership safety; deleteLater runs on QObject queue.
  );

  m_worker->setExternalContext(externalContext);
  m_worker->setBreakpoints(breakpoints);

  // Forward worker output signals through this controller. Page connects
  // to controller's signals (so subscriptions survive worker re-creation).
  connect(m_worker, &RecipeWorker::executionStarted,
          this, &RecipeExecutionController::executionStarted);
  connect(m_worker, &RecipeWorker::stepBuildStarted,
          this, &RecipeExecutionController::stepBuildStarted);
  connect(m_worker, &RecipeWorker::stepBuildCompleted,
          this, &RecipeExecutionController::stepBuildCompleted);
  connect(m_worker, &RecipeWorker::sequenceExecutionStarted,
          this, &RecipeExecutionController::sequenceExecutionStarted);
  connect(m_worker, &RecipeWorker::executionCompleted,
          this, &RecipeExecutionController::executionCompleted);
  connect(m_worker, &RecipeWorker::executionStopped,
          this, &RecipeExecutionController::executionStopped);
  connect(m_worker, &RecipeWorker::logMessage,
          this, &RecipeExecutionController::logMessage);
  connect(m_worker, &RecipeWorker::waitProgress,
          this, &RecipeExecutionController::waitProgress);
  connect(m_worker, &RecipeWorker::stepExecutionStarted,
          this, &RecipeExecutionController::stepExecutionStarted);
  connect(m_worker, &RecipeWorker::stepExecutionCompleted,
          this, &RecipeExecutionController::stepExecutionCompleted);
  connect(m_worker, &RecipeWorker::executionPaused,
          this, &RecipeExecutionController::executionPaused);

  // Clear the worker pointer when the thread finishes so IsRunning()
  // returns false post-completion. deleteLater frees the QObject on
  // the event loop after.
  connect(m_worker, &RecipeWorker::finished, this, [this]() {
    m_worker = nullptr;
  });
  connect(m_worker, &RecipeWorker::finished,
          m_worker, &QObject::deleteLater);

  // Safety-critical: DirectConnection ensures immediate stop on the
  // signaling thread without event-loop delay — required for E-stop
  // responsiveness.
  if (safetyWatchManager) {
    connect(safetyWatchManager, &SafetyWatchManager::safetyTriggered,
            m_worker, [this](QString /*title*/, QString /*message*/, bool critical) {
      if (critical && m_worker) {
        m_worker->requestStop();
      }
    }, Qt::DirectConnection);
  }

  // Page installs callbacks (user prompt / manual adjust / user input)
  // BEFORE the worker thread starts.
  if (configurator) {
    configurator(m_worker);
  }

  m_worker->start();
  return true;
}

void RecipeExecutionController::RequestStop()
{
  if (m_worker && m_worker->isRunning()) {
    m_worker->requestStop();
  }
}

bool RecipeExecutionController::IsRunning() const
{
  return m_worker && m_worker->isRunning();
}

void RecipeExecutionController::ShutdownSync()
{
  if (m_worker) {
    m_worker->quit();
    m_worker->wait();
    delete m_worker;
    m_worker = nullptr;
  }
}
