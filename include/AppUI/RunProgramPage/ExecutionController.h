// ExecutionController.h
// Owns the ProgramWorker, the worker callback wiring, and the
// execution lifecycle state for a single Run Program page. The page
// drives execution through Start() / RequestStop() / StartRetraceEdit()
// and consumes execution events via the forwarded signals below.
//
// Why a controller rather than letting the page own the worker
// directly: the worker is recreated on every execution, signals must
// be re-wired each time, recipe-thread callbacks have to marshal back
// to the UI thread through the overlay, and there's a polling-timer
// fallback that detects worker completion when programExecutionCompleted
// fails to deliver. Encapsulating that lets the page consume a single
// stable signal surface that doesn't change across re-executions.
//
// What stays on the page:
//   - All UI updates (status badge, progress bar, recipe-list states,
//     command tracker, execution log)
//   - Pre-execute validation (prereq checks, QMessageBox flows)
//   - The chain-execution dialog in response to recipeCompleted
//   - The retrace-edit summary dialog (controller emits a request signal)
#pragma once

#include <QObject>
#include <QString>
#include <functional>
#include <vector>

class ApplicationContext;
class ProcessSequenceFactory;
class ProgramManager;
class ProgramWorker;
class UserInteractionOverlay;

class ExecutionController : public QObject {
    Q_OBJECT
public:
    struct Deps {
        ApplicationContext*     context        = nullptr;
        ProgramManager*         programManager = nullptr;
        ProcessSequenceFactory* processFactory = nullptr;
        UserInteractionOverlay* overlay        = nullptr;
    };

    struct StartParams {
        bool autoRunMode         = false;
        int  selectedRecipeIndex = -1;  // ignored if autoRunMode == true
        bool useDependencies     = false;
        bool loopEnabled         = false;
        bool ignoreDependencies  = false;
    };

    explicit ExecutionController(Deps deps, QObject* parent = nullptr);
    ~ExecutionController() override;

    // Optional hook run after the worker is created and the
    // controller's own callbacks are wired, but BEFORE the worker
    // thread starts. Used by the page to attach callbacks that need
    // page-side state (placement / edge transfer retrace).
    using PreStartHook = std::function<void(ProgramWorker*)>;

    // Build a fresh worker, wire callbacks/signals, start it, and arm
    // the completion polling fallback.
    void Start(const StartParams& params, PreStartHook preStartHook = {});

    // Ask the active worker to stop. No-op if nothing is running.
    void RequestStop();

    // Start a single-recipe retrace edit run.
    void StartRetraceEdit(int recipeIndex);

    bool IsExecuting()      const { return m_isExecuting; }
    bool IsRetraceEditMode() const { return m_isRetraceEditMode; }
    bool IsLoopExecution()  const { return m_isLoopExecution; }

    // Read access to the live worker. Used by chain-execution logic on
    // the page to read the live SequenceContext.
    ProgramWorker* Worker() const { return m_worker; }

    // Recipe completion tracker. The controller resizes/clears it on
    // Start() and writes it on recipeCompleted; the page reads it for
    // prerequisite validation and chain logic.
    const std::vector<bool>& CompletedRecipes() const { return m_completedRecipes; }

    // Called by the page when the active slot changes (full reset).
    void ResetCompletedRecipes();

    // Called by the page after the recipe count changes (e.g. add/remove).
    // Resizes the tracking vector to match the current recipe count.
    void EnsureCompletedRecipesSize();

signals:
    // ---- Forwarded ProgramWorker signals ----
    void programExecutionStarted(int totalRecipes);
    void recipeStarted(int recipeIndex, QString recipeName);
    void recipeCompleted(int recipeIndex, bool success, QString message);
    void recipeSkipped(int recipeIndex, QString recipeName);
    void programExecutionCompleted(bool success, QString message);
    void programStopped();
    void logMessage(QString message);
    void stepBuildStarted(int recipeIndex, int stepIndex,
                          QString stepName, QString stepType);
    void stepBuildCompleted(int recipeIndex, int stepIndex);
    void sequenceExecutionStarted(int recipeIndex);
    void stepExecutionCompleted(int recipeIndex, int stepIndex, bool success);
    void waitProgress(int remainingMs, int totalMs);
    void actionStarted(int recipeIndex, QString current,
                       QString next, QString afterNext);
    void loopIterationStarted(int iteration);
    void loopIterationCompleted(int iteration);
    void visionResultReady(QString imagePath);

    // Retrace edit run finished — page should show the summary dialog.
    void retraceEditSummaryRequested(int positionsVisited, int positionsSaved);

    // Final hook before the worker is deleted. Page should snapshot
    // anything that needs live worker access (final context table).
    void workerAboutToFinalize();

private slots:
    // Internal interceptors that update controller state before
    // forwarding the signal to subscribers.
    void onWorkerProgramCompleted(bool success, QString message);
    void onWorkerProgramStopped();
    void onWorkerRecipeCompleted(int idx, bool success, QString message);
    void onWorkerLogMessageInternal(QString message);
    void onWorkerFinished();

private:
    void wireWorkerSignals();
    void wireWorkerCallbacks();
    void wireSafetyStop();
    void schedulePollingChain();
    void forceCompletionIfStillExecuting(const QString& reason);
    void finalizeExecution();

    Deps m_deps;

    ProgramWorker* m_worker = nullptr;

    bool m_isExecuting           = false;
    bool m_isLoopExecution       = false;
    bool m_isRetraceEditMode     = false;
    int  m_retraceEditRecipeIndex = -1;

    std::vector<bool> m_completedRecipes;
};
