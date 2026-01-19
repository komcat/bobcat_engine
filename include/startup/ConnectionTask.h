// ConnectionTask.h
#pragma once

#include <QObject>
#include <QString>
#include <functional>
#include <chrono>
#include <atomic>
#include <future>
#include "ConnectionResult.h"

namespace Startup {

/**
 * @brief A single connection task that wraps a manager's connect method with timeout
 *
 * Pattern follows SequenceWorker (QObject with signals).
 * Uses std::async + std::future::wait_for() for timeout support.
 */
class ConnectionTask : public QObject
{
  Q_OBJECT

public:
  /**
   * @brief Connect function signature
   * @return Number of devices connected (>=0) or -1 on error
   */
  using ConnectFunction = std::function<int()>;

  /**
   * @brief Create a connection task
   * @param managerName Internal manager name for identification
   * @param displayName Display name for splash screen
   * @param connectFunc The blocking connect function to execute
   * @param timeout Timeout duration for the connection attempt
   * @param phase Connection phase (for ordering)
   * @param parent QObject parent
   */
  ConnectionTask(
    const QString& managerName,
    const QString& displayName,
    ConnectFunction connectFunc,
    std::chrono::milliseconds timeout,
    int phase,
    QObject* parent = nullptr
  );

  ~ConnectionTask();

  // Accessors
  QString managerName() const { return m_managerName; }
  QString displayName() const { return m_displayName; }
  int phase() const { return m_phase; }
  std::chrono::milliseconds timeout() const { return m_timeout; }
  ConnectionResult result() const { return m_result; }
  bool isComplete() const { return m_result.isComplete(); }

  /**
   * @brief Request cancellation of the task
   * Note: The underlying connect function cannot be interrupted,
   * but we'll mark the result as cancelled if it completes after cancellation.
   */
  void requestCancel();

  /**
   * @brief Execute the connection task with timeout
   *
   * This method blocks until the connection completes or times out.
   * It will emit started() at the beginning and completed() at the end.
   */
  void execute();

signals:
  /**
   * @brief Emitted when the task starts execution
   * @param displayName The display name for splash screen
   */
  void started(const QString& displayName);

  /**
   * @brief Emitted when the task completes (success, failure, or timeout)
   * @param result The connection result
   */
  void completed(const Startup::ConnectionResult& result);

  /**
   * @brief Emitted periodically during connection to show progress
   * @param displayName The display name
   * @param message Progress message
   */
  void progress(const QString& displayName, const QString& message);

private:
  QString m_managerName;
  QString m_displayName;
  ConnectFunction m_connectFunc;
  std::chrono::milliseconds m_timeout;
  int m_phase;
  ConnectionResult m_result;
  std::atomic<bool> m_cancelRequested{false};
};

} // namespace Startup

// Register metatype for signal/slot connections
Q_DECLARE_METATYPE(Startup::ConnectionResult)
