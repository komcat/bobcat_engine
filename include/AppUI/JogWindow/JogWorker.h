// JogWorker.h
// Worker thread for executing jog commands asynchronously to prevent UI freezing
#pragma once

#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QString>
#include <atomic>
#include <queue>
#include <string>

// Forward declarations
class UnifiedMotionManager;
class TransformationMatrixLoader;
class CoordinateTransformer;

/**
 * @brief Jog command structure for queuing
 */
struct JogCommand {
  std::string deviceName;
  std::string axis;      // X, Y, Z, U, V, W
  double direction;      // +1.0 or -1.0
  double stepSizeMicrons;
};

/**
 * @brief Worker thread for executing jog commands asynchronously
 *
 * Uses a command queue pattern - UI pushes commands, worker processes them
 * on a background thread to prevent UI freezing from slow motion SDK calls.
 *
 * Usage:
 *   auto* worker = new JogWorker(motionManager, matrixLoader);
 *   connect(worker, &JogWorker::jogCompleted, this, &MyClass::onJogCompleted);
 *   worker->start();
 *   worker->queueJogCommand("gantry-main", "X", 1.0, 10.0);
 */
class JogWorker : public QThread
{
  Q_OBJECT

public:
  /**
   * @brief Construct a jog worker
   * @param motionManager UnifiedMotionManager for motion control
   * @param matrixLoader TransformationMatrixLoader for coordinate transformation
   * @param parent QObject parent
   */
  JogWorker(
    UnifiedMotionManager* motionManager,
    TransformationMatrixLoader* matrixLoader,
    QObject* parent = nullptr
  );

  ~JogWorker();

  /**
   * @brief Queue a jog command for execution
   * @param deviceName Device to jog
   * @param axis Axis to move (X, Y, Z, U, V, W)
   * @param direction Direction (+1.0 or -1.0)
   * @param stepSizeMicrons Step size in microns
   */
  void queueJogCommand(
    const std::string& deviceName,
    const std::string& axis,
    double direction,
    double stepSizeMicrons
  );

  /**
   * @brief Request graceful stop and clear pending commands
   */
  void requestStop();

  /**
   * @brief Clear all pending jog commands without stopping the worker
   */
  void clearPendingCommands();

  /**
   * @brief Check if worker is currently executing a jog
   */
  bool isBusy() const { return m_busy.load(); }

  /**
   * @brief Get number of pending commands in queue
   */
  int getPendingCount();

signals:
  // Emitted when a jog command starts executing
  void jogStarted(QString deviceName, QString axis, double direction);

  // Emitted when a jog command completes
  void jogCompleted(QString deviceName, QString axis, bool success, QString message);

  // Emitted for logging
  void logMessage(QString message);

protected:
  void run() override;

private:
  void processCommand(const JogCommand& cmd);

  // Managers (not owned)
  UnifiedMotionManager* m_motionManager;
  TransformationMatrixLoader* m_matrixLoader;

  // Command queue with synchronization
  std::queue<JogCommand> m_commandQueue;
  QMutex m_queueMutex;
  QWaitCondition m_queueCondition;

  // Thread-safe state
  std::atomic<bool> m_stopRequested{false};
  std::atomic<bool> m_busy{false};
};
