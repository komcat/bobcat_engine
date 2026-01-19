// ConnectionWorkerPool.cpp
#include "ConnectionWorkerPool.h"
#include <iostream>
#include <chrono>
#include <algorithm>

namespace Startup {

ConnectionWorkerPool::ConnectionWorkerPool(QObject* parent)
  : QObject(parent)
{
  // Register metatypes for cross-thread signal/slot connections
  static bool registered = false;
  if (!registered) {
    qRegisterMetaType<Startup::ConnectionResult>("Startup::ConnectionResult");
    qRegisterMetaType<Startup::ConnectionSummary>("Startup::ConnectionSummary");
    registered = true;
  }
}

ConnectionWorkerPool::~ConnectionWorkerPool()
{
  // Request cancellation if still running
  if (m_running.load()) {
    requestCancel();
  }

  // Wait for execution thread to complete
  if (m_executionThread && m_executionThread->joinable()) {
    m_executionThread->join();
  }
}

void ConnectionWorkerPool::addTask(std::unique_ptr<ConnectionTask> task)
{
  if (!task) return;

  int phase = task->phase();
  m_phases[phase].push_back(std::move(task));
}

void ConnectionWorkerPool::addTask(
  const QString& managerName,
  const QString& displayName,
  ConnectionTask::ConnectFunction connectFunc,
  int timeoutMs,
  int phase
)
{
  auto task = std::make_unique<ConnectionTask>(
    managerName,
    displayName,
    connectFunc,
    std::chrono::milliseconds(timeoutMs),
    phase,
    nullptr  // No parent - we manage lifetime
  );
  addTask(std::move(task));
}

void ConnectionWorkerPool::executeAll()
{
  if (m_running.load()) {
    emit error("Execution already in progress");
    return;
  }

  m_running.store(true);
  m_complete.store(false);
  m_cancelRequested.store(false);

  auto totalStartTime = std::chrono::steady_clock::now();

  std::cout << "\n[ConnectionWorkerPool] ========================================" << std::endl;
  std::cout << "[ConnectionWorkerPool] Starting parallel hardware connection" << std::endl;
  std::cout << "[ConnectionWorkerPool] Phases: " << phaseCount() << std::endl;
  std::cout << "[ConnectionWorkerPool] ========================================\n" << std::endl;

  // Execute phases sequentially
  for (int phase = 0; phase < phaseCount() && !m_cancelRequested.load(); ++phase) {
    executePhase(phase);
  }

  auto totalEndTime = std::chrono::steady_clock::now();
  auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
    totalEndTime - totalStartTime);

  // Build summary
  ConnectionSummary summary = getSummary();
  summary.totalDuration = totalDuration;

  m_running.store(false);
  m_complete.store(true);

  std::cout << "\n[ConnectionWorkerPool] ========================================" << std::endl;
  std::cout << "[ConnectionWorkerPool] All connections complete" << std::endl;
  std::cout << "[ConnectionWorkerPool] Total: " << summary.totalTasks << " tasks" << std::endl;
  std::cout << "[ConnectionWorkerPool] Success: " << summary.successfulTasks << std::endl;
  std::cout << "[ConnectionWorkerPool] Failed: " << summary.failedTasks << std::endl;
  std::cout << "[ConnectionWorkerPool] Timed Out: " << summary.timedOutTasks << std::endl;
  std::cout << "[ConnectionWorkerPool] Duration: " << totalDuration.count() << "ms" << std::endl;
  std::cout << "[ConnectionWorkerPool] ========================================\n" << std::endl;

  emit allTasksCompleted(summary);
}

void ConnectionWorkerPool::executeAllAsync()
{
  if (m_running.load()) {
    emit error("Execution already in progress");
    return;
  }

  // Clean up previous thread if any
  if (m_executionThread && m_executionThread->joinable()) {
    m_executionThread->join();
  }

  // Start execution in a new thread
  m_executionThread = std::make_unique<std::thread>([this]() {
    executeAll();
  });
}

void ConnectionWorkerPool::requestCancel()
{
  m_cancelRequested.store(true);

  // Request cancellation on all tasks
  for (auto& [phase, tasks] : m_phases) {
    for (auto& task : tasks) {
      task->requestCancel();
    }
  }
}

void ConnectionWorkerPool::executePhase(int phase)
{
  auto it = m_phases.find(phase);
  if (it == m_phases.end() || it->second.empty()) {
    std::cout << "[ConnectionWorkerPool] Phase " << phase << ": No tasks" << std::endl;
    return;
  }

  auto& tasks = it->second;
  int taskCount = static_cast<int>(tasks.size());

  std::cout << "[ConnectionWorkerPool] Phase " << phase
            << ": Starting " << taskCount << " tasks in parallel" << std::endl;

  emit phaseStarted(phase, taskCount);

  // Create threads for parallel execution
  std::vector<std::thread> threads;
  threads.reserve(taskCount);

  for (auto& task : tasks) {
    if (m_cancelRequested.load()) {
      break;
    }

    // Connect task signals before execution
    // Note: We use direct connection since we're in the same thread context
    connect(task.get(), &ConnectionTask::started,
            this, &ConnectionWorkerPool::taskStarted, Qt::DirectConnection);
    connect(task.get(), &ConnectionTask::completed,
            this, [this](const ConnectionResult& result) {
              onTaskCompleted(result);
            }, Qt::DirectConnection);

    // Start task in its own thread
    threads.emplace_back([&task]() {
      task->execute();
    });
  }

  // Wait for all tasks in this phase to complete
  for (auto& thread : threads) {
    if (thread.joinable()) {
      thread.join();
    }
  }

  std::cout << "[ConnectionWorkerPool] Phase " << phase << ": Complete" << std::endl;
  emit phaseCompleted(phase);
}

void ConnectionWorkerPool::onTaskCompleted(const ConnectionResult& result)
{
  // Thread-safe result storage
  {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    m_results.push_back(result);
  }

  emit taskCompleted(result);
}

ConnectionSummary ConnectionWorkerPool::getSummary() const
{
  ConnectionSummary summary;

  std::lock_guard<std::mutex> lock(m_resultsMutex);

  summary.totalTasks = static_cast<int>(m_results.size());
  for (const auto& result : m_results) {
    if (result.isComplete()) {
      summary.completedTasks++;
    }
    if (result.status == ConnectionStatus::Connected) {
      summary.successfulTasks++;
    }
    else if (result.status == ConnectionStatus::Failed) {
      summary.failedTasks++;
    }
    else if (result.status == ConnectionStatus::TimedOut) {
      summary.timedOutTasks++;
    }
  }

  return summary;
}

QVector<ConnectionResult> ConnectionWorkerPool::getResults() const
{
  std::lock_guard<std::mutex> lock(m_resultsMutex);
  return m_results;
}

int ConnectionWorkerPool::phaseCount() const
{
  if (m_phases.empty()) {
    return 0;
  }
  // Return max phase number + 1 (std::map is sorted, so rbegin gives the largest key)
  return m_phases.rbegin()->first + 1;
}

} // namespace Startup
