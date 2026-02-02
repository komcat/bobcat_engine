// JogWorker.cpp
#include "JogWorker.h"

#include "unified_motion_manager.h"
#include "TransformationMatrixLoader.h"
#include "CoordinateTransformer.h"

#include <cmath>
#include <iostream>

JogWorker::JogWorker(
  UnifiedMotionManager* motionManager,
  TransformationMatrixLoader* matrixLoader,
  QObject* parent)
  : QThread(parent)
  , m_motionManager(motionManager)
  , m_matrixLoader(matrixLoader)
{
}

JogWorker::~JogWorker()
{
  requestStop();
  wait(5000);  // Wait up to 5 seconds for thread to finish
}

void JogWorker::queueJogCommand(
  const std::string& deviceName,
  const std::string& axis,
  double direction,
  double stepSizeMicrons)
{
  JogCommand cmd;
  cmd.deviceName = deviceName;
  cmd.axis = axis;
  cmd.direction = direction;
  cmd.stepSizeMicrons = stepSizeMicrons;

  {
    QMutexLocker locker(&m_queueMutex);

    // Clear any pending commands for the same device - new command overrides old ones
    // This ensures responsive jogging: rapid clicks don't queue up multiple moves
    std::queue<JogCommand> filteredQueue;
    while (!m_commandQueue.empty()) {
      JogCommand pending = m_commandQueue.front();
      m_commandQueue.pop();
      // Keep commands for OTHER devices
      if (pending.deviceName != deviceName) {
        filteredQueue.push(pending);
      }
    }
    m_commandQueue = std::move(filteredQueue);

    // Add the new command
    m_commandQueue.push(cmd);
  }

  // Wake up worker thread
  m_queueCondition.wakeOne();
}

void JogWorker::requestStop()
{
  m_stopRequested.store(true);

  // Clear queue
  {
    QMutexLocker locker(&m_queueMutex);
    while (!m_commandQueue.empty()) {
      m_commandQueue.pop();
    }
  }

  // Wake up worker to check stop flag
  m_queueCondition.wakeAll();
}

void JogWorker::clearPendingCommands()
{
  QMutexLocker locker(&m_queueMutex);
  while (!m_commandQueue.empty()) {
    m_commandQueue.pop();
  }
}

int JogWorker::getPendingCount()
{
  QMutexLocker locker(&m_queueMutex);
  return static_cast<int>(m_commandQueue.size());
}

void JogWorker::run()
{
  emit logMessage("[JogWorker] Started");

  while (!m_stopRequested.load()) {
    JogCommand cmd;
    bool hasCommand = false;

    // Wait for command or stop signal
    {
      QMutexLocker locker(&m_queueMutex);

      // Wait if queue is empty
      while (m_commandQueue.empty() && !m_stopRequested.load()) {
        m_queueCondition.wait(&m_queueMutex, 100);  // 100ms timeout for periodic stop check
      }

      // Get command if available
      if (!m_commandQueue.empty() && !m_stopRequested.load()) {
        cmd = m_commandQueue.front();
        m_commandQueue.pop();
        hasCommand = true;
      }
    }

    // Process command outside of lock
    if (hasCommand) {
      m_busy.store(true);
      processCommand(cmd);
      m_busy.store(false);
    }
  }

  emit logMessage("[JogWorker] Stopped");
}

void JogWorker::processCommand(const JogCommand& cmd)
{
  emit jogStarted(
    QString::fromStdString(cmd.deviceName),
    QString::fromStdString(cmd.axis),
    cmd.direction
  );

  // Validate managers
  if (!m_motionManager || !m_matrixLoader) {
    emit jogCompleted(
      QString::fromStdString(cmd.deviceName),
      QString::fromStdString(cmd.axis),
      false,
      "Invalid motion manager or matrix loader"
    );
    return;
  }

  // Get device transformation matrix
  auto matrix = m_matrixLoader->GetMatrix(cmd.deviceName);
  if (!matrix) {
    emit jogCompleted(
      QString::fromStdString(cmd.deviceName),
      QString::fromStdString(cmd.axis),
      false,
      QString("No transformation matrix for device: %1").arg(QString::fromStdString(cmd.deviceName))
    );
    return;
  }

  // Create global movement vector (in microns)
  CoordinateTransformer::Vector6D globalMove(0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
  double stepInMicrons = cmd.stepSizeMicrons * cmd.direction;

  if (cmd.axis == "X") {
    globalMove.x = stepInMicrons;
  }
  else if (cmd.axis == "Y") {
    globalMove.y = stepInMicrons;
  }
  else if (cmd.axis == "Z") {
    globalMove.z = stepInMicrons;
  }
  else if (cmd.axis == "U") {
    globalMove.u = stepInMicrons;
  }
  else if (cmd.axis == "V") {
    globalMove.v = stepInMicrons;
  }
  else if (cmd.axis == "W") {
    globalMove.w = stepInMicrons;
  }

  // Transform to local coordinates
  auto localMove = CoordinateTransformer::TransformGlobalToLocal(globalMove, *matrix);

  // Convert microns to millimeters
  double xMM = localMove.x / 1000.0;
  double yMM = localMove.y / 1000.0;
  double zMM = localMove.z / 1000.0;
  double uMM = localMove.u / 1000.0;
  double vMM = localMove.v / 1000.0;
  double wMM = localMove.w / 1000.0;

  // Send commands for each non-zero axis
  // Use blocking=false so we don't wait for motion completion
  // The SDK call itself may still take time, but that's why we're in a worker thread
  bool success = true;
  if (std::abs(xMM) > 0.0001) {
    success &= m_motionManager->MoveRelative(cmd.deviceName, "X", xMM, false);
  }
  if (std::abs(yMM) > 0.0001) {
    success &= m_motionManager->MoveRelative(cmd.deviceName, "Y", yMM, false);
  }
  if (std::abs(zMM) > 0.0001) {
    success &= m_motionManager->MoveRelative(cmd.deviceName, "Z", zMM, false);
  }
  if (std::abs(uMM) > 0.0001) {
    success &= m_motionManager->MoveRelative(cmd.deviceName, "U", uMM, false);
  }
  if (std::abs(vMM) > 0.0001) {
    success &= m_motionManager->MoveRelative(cmd.deviceName, "V", vMM, false);
  }
  if (std::abs(wMM) > 0.0001) {
    success &= m_motionManager->MoveRelative(cmd.deviceName, "W", wMM, false);
  }

  // Emit completion
  QString dirStr = (cmd.direction > 0) ? "+" : "-";
  if (success) {
    emit jogCompleted(
      QString::fromStdString(cmd.deviceName),
      QString::fromStdString(cmd.axis),
      true,
      QString("Jog %1%2 %3um").arg(QString::fromStdString(cmd.axis)).arg(dirStr).arg(cmd.stepSizeMicrons)
    );
  }
  else {
    emit jogCompleted(
      QString::fromStdString(cmd.deviceName),
      QString::fromStdString(cmd.axis),
      false,
      "Jog command failed"
    );
  }
}
