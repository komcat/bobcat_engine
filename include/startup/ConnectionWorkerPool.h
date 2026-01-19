// ConnectionWorkerPool.h
#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <map>
#include <vector>
#include <memory>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>
#include "ConnectionTask.h"
#include "ConnectionResult.h"

namespace Startup {

/**
 * @brief Manages parallel connection of hardware managers in phases
 *
 * Tasks within a phase execute in parallel using std::thread.
 * Phases execute sequentially (phase 0 completes before phase 1 starts).
 *
 * Example phases:
 *   Phase 0: [TCP, Cameras]     - run in parallel
 *   Phase 1: [Motion, IO]       - run in parallel (after phase 0)
 *   Phase 2: [TEC, Lighting]    - run in parallel (after phase 1)
 */
class ConnectionWorkerPool : public QObject
{
  Q_OBJECT

public:
  explicit ConnectionWorkerPool(QObject* parent = nullptr);
  ~ConnectionWorkerPool();

  /**
   * @brief Add a connection task to the pool
   * @param task The task to add (takes ownership)
   */
  void addTask(std::unique_ptr<ConnectionTask> task);

  /**
   * @brief Add a task with all parameters
   * @param managerName Internal manager name
   * @param displayName Display name for UI
   * @param connectFunc The connection function
   * @param timeoutMs Timeout in milliseconds
   * @param phase The phase number
   */
  void addTask(
    const QString& managerName,
    const QString& displayName,
    ConnectionTask::ConnectFunction connectFunc,
    int timeoutMs,
    int phase
  );

  /**
   * @brief Execute all tasks phase by phase
   *
   * This method blocks until all phases complete.
   * Within each phase, tasks run in parallel.
   */
  void executeAll();

  /**
   * @brief Execute all tasks asynchronously
   *
   * Returns immediately. Use signals to track progress.
   * Calls executeAll() in a background thread.
   */
  void executeAllAsync();

  /**
   * @brief Request cancellation of remaining tasks
   */
  void requestCancel();

  /**
   * @brief Get the connection summary
   */
  ConnectionSummary getSummary() const;

  /**
   * @brief Get all results
   */
  QVector<ConnectionResult> getResults() const;

  /**
   * @brief Get the number of phases
   */
  int phaseCount() const;

  /**
   * @brief Check if execution is complete
   */
  bool isComplete() const { return m_complete.load(); }

  /**
   * @brief Check if execution is in progress
   */
  bool isRunning() const { return m_running.load(); }

signals:
  /**
   * @brief Emitted when a phase starts
   * @param phase Phase number
   * @param taskCount Number of tasks in this phase
   */
  void phaseStarted(int phase, int taskCount);

  /**
   * @brief Emitted when a phase completes
   * @param phase Phase number
   */
  void phaseCompleted(int phase);

  /**
   * @brief Emitted when a task starts
   * @param displayName Task display name
   */
  void taskStarted(const QString& displayName);

  /**
   * @brief Emitted when a task completes
   * @param result Task result
   */
  void taskCompleted(const Startup::ConnectionResult& result);

  /**
   * @brief Emitted when all tasks are complete
   * @param summary Summary of all results
   */
  void allTasksCompleted(const Startup::ConnectionSummary& summary);

  /**
   * @brief Emitted on error
   * @param message Error message
   */
  void error(const QString& message);

private:
  /**
   * @brief Execute all tasks in a specific phase in parallel
   * @param phase Phase number to execute
   */
  void executePhase(int phase);

  /**
   * @brief Called when a task completes (thread-safe)
   * @param result The task result
   */
  void onTaskCompleted(const ConnectionResult& result);

  // Phase-organized tasks: phase -> list of tasks
  // Using std::map/vector because Qt containers don't support move-only types well
  std::map<int, std::vector<std::unique_ptr<ConnectionTask>>> m_phases;

  // Results storage (thread-safe access via mutex)
  mutable std::mutex m_resultsMutex;
  QVector<ConnectionResult> m_results;

  // State tracking
  std::atomic<bool> m_running{false};
  std::atomic<bool> m_complete{false};
  std::atomic<bool> m_cancelRequested{false};

  // Async execution thread
  std::unique_ptr<std::thread> m_executionThread;
};

} // namespace Startup

// Register metatype for signal/slot connections
Q_DECLARE_METATYPE(Startup::ConnectionSummary)
