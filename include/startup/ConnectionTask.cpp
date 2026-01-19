// ConnectionTask.cpp
#include "ConnectionTask.h"
#include <iostream>
#include <future>

namespace Startup {

ConnectionTask::ConnectionTask(
  const QString& managerName,
  const QString& displayName,
  ConnectFunction connectFunc,
  std::chrono::milliseconds timeout,
  int phase,
  QObject* parent
)
  : QObject(parent)
  , m_managerName(managerName)
  , m_displayName(displayName)
  , m_connectFunc(connectFunc)
  , m_timeout(timeout)
  , m_phase(phase)
{
  m_result.managerName = managerName;
  m_result.displayName = displayName;
  m_result.status = ConnectionStatus::Pending;

  // Register metatype for cross-thread signal/slot connections
  static bool registered = false;
  if (!registered) {
    qRegisterMetaType<Startup::ConnectionResult>("Startup::ConnectionResult");
    registered = true;
  }
}

ConnectionTask::~ConnectionTask()
{
  // Nothing to clean up - future will have completed or we're being destroyed
}

void ConnectionTask::requestCancel()
{
  m_cancelRequested.store(true);
}

void ConnectionTask::execute()
{
  auto startTime = std::chrono::steady_clock::now();

  // Check for early cancellation
  if (m_cancelRequested.load()) {
    m_result.status = ConnectionStatus::Cancelled;
    m_result.errorMessage = "Cancelled before execution";
    m_result.duration = std::chrono::milliseconds(0);
    emit completed(m_result);
    return;
  }

  // Update status
  m_result.status = ConnectionStatus::Connecting;
  emit started(m_displayName);

  std::cout << "[ConnectionTask] Starting " << m_managerName.toStdString()
            << " (timeout: " << m_timeout.count() << "ms)" << std::endl;

  // Launch the connection function asynchronously
  std::future<int> future = std::async(std::launch::async, [this]() {
    try {
      return m_connectFunc();
    }
    catch (const std::exception& e) {
      std::cerr << "[ConnectionTask] Exception in " << m_managerName.toStdString()
                << ": " << e.what() << std::endl;
      return -1;
    }
    catch (...) {
      std::cerr << "[ConnectionTask] Unknown exception in " << m_managerName.toStdString()
                << std::endl;
      return -1;
    }
  });

  // Wait with timeout
  auto waitStatus = future.wait_for(m_timeout);

  auto endTime = std::chrono::steady_clock::now();
  m_result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

  // Check for cancellation during execution
  if (m_cancelRequested.load()) {
    m_result.status = ConnectionStatus::Cancelled;
    m_result.errorMessage = "Cancelled during execution";
    std::cout << "[ConnectionTask] " << m_managerName.toStdString()
              << " was cancelled" << std::endl;
    emit completed(m_result);
    // Note: The future will complete in the background but we ignore the result
    return;
  }

  if (waitStatus == std::future_status::timeout) {
    // Timed out - the connection continues in background but we move on
    m_result.status = ConnectionStatus::TimedOut;
    m_result.errorMessage = QString("Connection timed out after %1ms").arg(m_timeout.count());
    std::cout << "[ConnectionTask] " << m_managerName.toStdString()
              << " TIMED OUT after " << m_timeout.count() << "ms" << std::endl;
    emit completed(m_result);
    // Note: The future continues running but we abandon waiting for it
    return;
  }

  // Get the result
  int result = future.get();

  if (result >= 0) {
    m_result.status = ConnectionStatus::Connected;
    m_result.devicesConnected = result;
    std::cout << "[ConnectionTask] " << m_managerName.toStdString()
              << " connected successfully (" << result << " devices, "
              << m_result.duration.count() << "ms)" << std::endl;
  }
  else {
    m_result.status = ConnectionStatus::Failed;
    m_result.errorMessage = "Connection function returned error";
    std::cout << "[ConnectionTask] " << m_managerName.toStdString()
              << " FAILED" << std::endl;
  }

  emit completed(m_result);
}

} // namespace Startup
