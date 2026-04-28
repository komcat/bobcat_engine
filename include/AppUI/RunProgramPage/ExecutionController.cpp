// ExecutionController.cpp
#include "ExecutionController.h"

#include "ApplicationContext.h"
#include "ProcessSequenceFactory.h"
#include "ProgramManager.h"
#include "ProgramWorker.h"
#include "UserInteractionOverlay.h"
#include "motions/unified_motion_manager.h"
#include "safety/SafetyWatchManager.h"
#include "settings/ApplicationSettings.h"

#include <QApplication>
#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <QTimer>

#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

ExecutionController::ExecutionController(Deps deps, QObject* parent)
    : QObject(parent)
    , m_deps(std::move(deps))
{
}

ExecutionController::~ExecutionController()
{
    if (m_worker && m_worker->isRunning()) {
        m_worker->requestStop();
        m_worker->wait(3000);
    }
}

void ExecutionController::ResetCompletedRecipes()
{
    if (!m_deps.programManager) return;
    m_completedRecipes.clear();
    m_completedRecipes.resize(m_deps.programManager->getRecipeCount(), false);
}

void ExecutionController::EnsureCompletedRecipesSize()
{
    if (!m_deps.programManager) return;
    int count = m_deps.programManager->getRecipeCount();
    if (m_completedRecipes.empty() ||
        m_completedRecipes.size() != static_cast<size_t>(count)) {
        m_completedRecipes.clear();
        m_completedRecipes.resize(count, false);
    }
}

void ExecutionController::Start(const StartParams& params, PreStartHook preStartHook)
{
    if (m_isExecuting) return;
    if (!m_deps.context || !m_deps.programManager || !m_deps.overlay) return;

    EnsureCompletedRecipesSize();
    if (params.autoRunMode) {
        // Auto-run resets all completion state for a fresh whole-program run.
        std::fill(m_completedRecipes.begin(), m_completedRecipes.end(), false);
    }

    m_isExecuting       = true;
    m_isLoopExecution   = params.loopEnabled;
    m_isRetraceEditMode = false;
    m_retraceEditRecipeIndex = -1;

    // ManualAlignPoint is a SequenceEngine-level callback (not on the worker).
    // Reset it on every Start so a re-run picks up the live overlay pointer.
    m_deps.context->sequenceEngine->SetManualAlignPointCallback(
        [overlay = m_deps.overlay](const std::string& pointName,
                                   const std::string& message,
                                   bool enableX, bool enableY, bool enableZ) -> bool {
            return overlay->RequestManualAlignPoint(
                QString::fromStdString(pointName),
                QString::fromStdString(message),
                enableX, enableY, enableZ);
        });

    m_worker = new ProgramWorker(
        m_deps.context->sequenceEngine,
        m_deps.programManager,
        m_deps.processFactory,
        m_deps.context->productPositionManager,
        m_deps.context->motionManager,
        this);
    std::cout << "[ExecutionController] Created worker=" << m_worker << std::endl;
    std::cout.flush();

    if (m_deps.context->scriptEngine) {
        m_worker->setScriptEngine(m_deps.context->scriptEngine);
    }

    m_worker->setAutoRunMode(params.autoRunMode);
    if (!params.autoRunMode) {
        m_worker->setStartRecipeIndex(params.selectedRecipeIndex);
    }
    m_worker->setUseDependencyGraph(params.useDependencies);

    // Loop execution: respect the page-supplied flag plus the program's
    // own configuration of which recipes are part of the loop.
    bool loopEnabledEffective = params.loopEnabled
                                && m_deps.programManager->hasLoopRecipes();
    m_worker->setLoopMode(loopEnabledEffective);
    m_isLoopExecution = loopEnabledEffective;
    if (loopEnabledEffective) {
        m_worker->setLoopRecipeIndices(m_deps.programManager->getLoopRecipeIndices());
        m_worker->setIgnoreDependencies(params.ignoreDependencies);
    }

    wireWorkerCallbacks();
    wireWorkerSignals();
    wireSafetyStop();

    // Page hook for callbacks that need page-side state (placement,
    // edge transfer retrace). Runs before the worker thread starts so
    // those callbacks are in place when the worker first reaches them.
    if (preStartHook) preStartHook(m_worker);

    m_worker->start();
    std::cout << "[ExecutionController] Worker started" << std::endl;
    std::cout.flush();

    schedulePollingChain();

    // Force event processing so the timer chain gets scheduled promptly.
    QApplication::processEvents();
}

void ExecutionController::RequestStop()
{
    if (m_worker && m_worker->isRunning()) {
        m_worker->requestStop();
    }
}

void ExecutionController::StartRetraceEdit(int recipeIndex)
{
    if (m_isExecuting) return;
    if (!m_deps.context || !m_deps.programManager || !m_deps.overlay) return;

    m_isExecuting           = true;
    m_isRetraceEditMode     = true;
    m_retraceEditRecipeIndex = recipeIndex;
    m_isLoopExecution       = false;

    m_worker = new ProgramWorker(
        m_deps.context->sequenceEngine,
        m_deps.programManager,
        m_deps.processFactory,
        m_deps.context->productPositionManager,
        m_deps.context->motionManager,
        this);

    m_worker->setAutoRunMode(false);
    m_worker->setStartRecipeIndex(recipeIndex);
    m_worker->setRetraceEditMode(true);

    // Retrace-edit pause callback. The shared counter survives across
    // worker callback invocations within this single retrace run.
    auto positionCounter = std::make_shared<int>(0);
    m_worker->setRetraceEditCallback(
        [overlay = m_deps.overlay, positionCounter, this](
            const std::string& deviceName,
            const std::string& positionName,
            double origX, double origY, double origZ,
            double origU, double origV, double origW,
            double currX, double currY, double currZ,
            double currU, double currV, double currW,
            bool& saveRequested) -> bool {

            (*positionCounter)++;

            auto result = overlay->RequestRetraceEdit(
                QString::fromStdString(deviceName),
                QString::fromStdString(positionName),
                origX, origY, origZ, origU, origV, origW,
                currX, currY, currZ, currU, currV, currW,
                *positionCounter, 0);

            saveRequested = result.save;

            QString logLine = result.save
                ? "  [SAVE] Position saved"
                : (result.continueExecution
                      ? "  [SKIP] Position skipped"
                      : "  [STOP] Retrace stopped by user");
            QMetaObject::invokeMethod(this, [this, logLine]() {
                emit logMessage(logLine);
            }, Qt::QueuedConnection);

            return result.continueExecution;
        });

    // Retrace-edit needs a narrower signal set than full execution.
    connect(m_worker, &ProgramWorker::logMessage,
        this, &ExecutionController::onWorkerLogMessageInternal);
    connect(m_worker, &ProgramWorker::recipeStarted,
        this, &ExecutionController::recipeStarted);
    connect(m_worker, &ProgramWorker::recipeCompleted,
        this, &ExecutionController::onWorkerRecipeCompleted);
    connect(m_worker, &ProgramWorker::programExecutionCompleted,
        this, &ExecutionController::onWorkerProgramCompleted);
    connect(m_worker, &ProgramWorker::programStopped,
        this, &ExecutionController::onWorkerProgramStopped);
    connect(m_worker, &QThread::finished,
        this, &ExecutionController::onWorkerFinished);

    m_worker->start();
}

void ExecutionController::wireWorkerCallbacks()
{
    auto* overlay = m_deps.overlay;

    m_worker->setUserPromptCallback(
        [overlay](const std::string& title, const std::string& message) -> bool {
            return overlay->RequestUserPrompt(
                QString::fromStdString(title),
                QString::fromStdString(message));
        });

    m_worker->setManualAdjustCallback(
        [overlay](const std::string& message, const std::string& deviceName,
                  double stepSizeUm, bool enableX, bool enableY, bool enableZ) -> bool {
            return overlay->RequestManualAdjust(
                QString::fromStdString(message),
                QString::fromStdString(deviceName),
                stepSizeUm, enableX, enableY, enableZ);
        });

    m_worker->setUserInputCallback(
        [overlay](const std::string& title, const std::string& prompt,
                  const std::string& defaultValue) -> std::pair<bool, std::string> {
            auto [accepted, text] = overlay->RequestUserInput(
                QString::fromStdString(title),
                QString::fromStdString(prompt),
                QString::fromStdString(defaultValue));
            return {accepted, text.toStdString()};
        });
}

void ExecutionController::wireWorkerSignals()
{
    // Forwards (page subscribes to these on the controller, not on
    // the worker — so it doesn't need to re-subscribe per execution).
    connect(m_worker, &ProgramWorker::programExecutionStarted,
        this, &ExecutionController::programExecutionStarted, Qt::QueuedConnection);
    connect(m_worker, &ProgramWorker::recipeStarted,
        this, &ExecutionController::recipeStarted, Qt::QueuedConnection);
    connect(m_worker, &ProgramWorker::recipeCompleted,
        this, &ExecutionController::onWorkerRecipeCompleted, Qt::QueuedConnection);
    connect(m_worker, &ProgramWorker::recipeSkipped,
        this, &ExecutionController::recipeSkipped, Qt::QueuedConnection);
    connect(m_worker, &ProgramWorker::programExecutionCompleted,
        this, &ExecutionController::onWorkerProgramCompleted, Qt::QueuedConnection);
    connect(m_worker, &ProgramWorker::programStopped,
        this, &ExecutionController::onWorkerProgramStopped, Qt::QueuedConnection);
    connect(m_worker, &ProgramWorker::logMessage,
        this, &ExecutionController::onWorkerLogMessageInternal, Qt::QueuedConnection);

    connect(m_worker, &ProgramWorker::finished,
        this, &ExecutionController::onWorkerFinished, Qt::QueuedConnection);

    connect(m_worker, &ProgramWorker::stepBuildStarted,
        this, &ExecutionController::stepBuildStarted, Qt::QueuedConnection);
    connect(m_worker, &ProgramWorker::stepBuildCompleted,
        this, &ExecutionController::stepBuildCompleted, Qt::QueuedConnection);
    connect(m_worker, &ProgramWorker::sequenceExecutionStarted,
        this, &ExecutionController::sequenceExecutionStarted, Qt::QueuedConnection);
    connect(m_worker, &ProgramWorker::stepExecutionCompleted,
        this, &ExecutionController::stepExecutionCompleted, Qt::QueuedConnection);
    connect(m_worker, &ProgramWorker::waitProgress,
        this, &ExecutionController::waitProgress, Qt::QueuedConnection);

    connect(m_worker, &ProgramWorker::actionStarted,
        this, &ExecutionController::actionStarted, Qt::QueuedConnection);

    connect(m_worker, &ProgramWorker::loopIterationStarted,
        this, &ExecutionController::loopIterationStarted, Qt::QueuedConnection);
    connect(m_worker, &ProgramWorker::loopIterationCompleted,
        this, &ExecutionController::loopIterationCompleted, Qt::QueuedConnection);

    connect(m_worker, &ProgramWorker::visionResultReady,
        this, &ExecutionController::visionResultReady, Qt::QueuedConnection);
}

void ExecutionController::wireSafetyStop()
{
    // DirectConnection — must trigger immediately on the safety thread,
    // not after event-loop scheduling, for E-stop responsiveness.
    if (!m_deps.context || !m_deps.context->safetyWatchManager) return;
    connect(m_deps.context->safetyWatchManager, &SafetyWatchManager::safetyTriggered,
            m_worker, [this](QString /*title*/, QString /*message*/, bool critical) {
        if (critical && m_worker) {
            std::cout << "[ExecutionController] Critical safety flag - stopping worker" << std::endl;
            m_worker->requestStop();
        }
    }, Qt::DirectConnection);
}

void ExecutionController::schedulePollingChain()
{
    // Single-shot chain that fires every 100ms to detect worker
    // completion in case programExecutionCompleted didn't deliver
    // via the queued connection (cross-thread race fallback).
    auto checkCountPtr     = std::make_shared<int>(0);
    auto checkCompletionPtr = std::make_shared<std::function<void()>>();
    QPointer<ExecutionController> self(this);

    *checkCompletionPtr = [this, self, checkCountPtr, checkCompletionPtr]() {
        if (!self) return;
        (*checkCountPtr)++;

        if (m_worker && m_worker->isFinished()) {
            std::cout << "[ExecutionController] Polling detected worker finished after "
                      << *checkCountPtr << " checks" << std::endl;
            std::cout.flush();
            forceCompletionIfStillExecuting("polling-fallback");
            return;
        }
        if (m_isExecuting && *checkCountPtr < 6000) {  // ~10 minute cap
            QTimer::singleShot(100, self.data(), [checkCompletionPtr]() {
                if (*checkCompletionPtr) {
                    (*checkCompletionPtr)();
                }
            });
        }
    };

    QTimer::singleShot(100, self.data(), [checkCompletionPtr]() {
        if (*checkCompletionPtr) {
            (*checkCompletionPtr)();
        }
    });
}

void ExecutionController::forceCompletionIfStillExecuting(const QString& reason)
{
    if (!m_isExecuting) return;
    if (!m_worker) return;

    bool success    = m_worker->getExecutionSuccess();
    QString message = m_worker->getExecutionMessage();
    if (message.isEmpty()) {
        message = "Execution completed";
    }
    std::cout << "[ExecutionController] Forcing completion via " << reason.toStdString()
              << std::endl;
    std::cout.flush();
    onWorkerProgramCompleted(success, message);
    // Worker deletion is deferred to onWorkerFinished so any pending
    // signals (recipeCompleted with chain logic) drain first.
}

void ExecutionController::onWorkerProgramCompleted(bool success, QString message)
{
    std::cout << "[ExecutionController] programExecutionCompleted: success=" << success
              << ", message=" << message.toStdString() << std::endl;
    std::cout.flush();

    if (m_isRetraceEditMode && m_worker) {
        emit retraceEditSummaryRequested(
            m_worker->getRetracePositionsVisited(),
            m_worker->getRetracePositionsSaved());
    }

    m_isExecuting       = false;
    m_isLoopExecution   = false;
    m_isRetraceEditMode = false;
    m_retraceEditRecipeIndex = -1;

    emit programExecutionCompleted(success, message);
}

void ExecutionController::onWorkerProgramStopped()
{
    m_isExecuting       = false;
    m_isLoopExecution   = false;
    m_isRetraceEditMode = false;
    m_retraceEditRecipeIndex = -1;
    emit programStopped();
}

void ExecutionController::onWorkerRecipeCompleted(int idx, bool success, QString message)
{
    if (success && idx >= 0 && idx < static_cast<int>(m_completedRecipes.size())) {
        m_completedRecipes[idx] = true;
    }
    emit recipeCompleted(idx, success, message);
}

void ExecutionController::onWorkerLogMessageInternal(QString message)
{
    emit logMessage(message);

    // Loop mode uses the loopIterationCompleted signal for completion;
    // skip the heavier completion-detection workarounds here.
    if (m_isLoopExecution) return;

    // Worker may finish before the queued programExecutionCompleted
    // signal arrives — detect terminal markers and force completion.
    bool shouldComplete = false;

    if (message.startsWith("\xE2\x9C\x85 PROGRAM COMPLETED:") && m_isExecuting) {
        std::cout << "[ExecutionController] Detected PROGRAM completion marker" << std::endl;
        std::cout.flush();
        shouldComplete = true;
    } else if (message.startsWith("\xE2\x9C\x85 RECIPE EXECUTION COMPLETED") && m_isExecuting) {
        std::cout << "[ExecutionController] Detected RECIPE completion marker" << std::endl;
        std::cout.flush();
        if (m_worker) {
            if (m_worker->isRunning()) {
                m_worker->wait(100);
            }
            if (m_worker && m_worker->isFinished()) {
                forceCompletionIfStillExecuting("recipe-completion-marker");
            }
        }
        return;
    } else if (message.contains("Context saved to:") && m_isExecuting) {
        std::cout << "[ExecutionController] Detected context save - execution finishing" << std::endl;
        std::cout.flush();
        QApplication::processEvents();
        if (m_worker) {
            if (m_worker->isRunning()) {
                m_worker->wait(500);
            }
            if (m_worker && m_worker->isFinished()) {
                forceCompletionIfStillExecuting("context-save-marker");
            }
        }
        return;
    }

    if (shouldComplete) {
        forceCompletionIfStillExecuting("program-completion-marker");
    }
}

void ExecutionController::onWorkerFinished()
{
    std::cout << "[ExecutionController] Worker finished" << std::endl;
    std::cout.flush();
    forceCompletionIfStillExecuting("worker-finished");
    finalizeExecution();
}

void ExecutionController::finalizeExecution()
{
    if (!m_worker) return;
    emit workerAboutToFinalize();
    std::cout << "[ExecutionController] Scheduling worker deletion" << std::endl;
    std::cout.flush();
    m_worker->deleteLater();
    m_worker = nullptr;
}
