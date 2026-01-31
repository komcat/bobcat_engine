// PIAlignExecutor.cpp
#include "PIAlignExecutor.h"
#include "unified_motion_manager.h"
#include "pi_controller.h"
#include <QMutexLocker>
#include <iostream>

// ============================================================================
// PIAlignExecutor - Main thread coordinator
// ============================================================================

PIAlignExecutor::PIAlignExecutor(UnifiedMotionManager* motionManager, QObject* parent)
    : QObject(parent)
    , m_motionManager(motionManager)
    , m_workerThread(nullptr)
    , m_worker(nullptr)
    , m_running(false)
{
    std::cout << "[PIAlignExecutor] Created" << std::endl;
}

PIAlignExecutor::~PIAlignExecutor()
{
    cleanup();
    std::cout << "[PIAlignExecutor] Destroyed" << std::endl;
}

std::string PIAlignExecutor::currentDevice() const
{
    QMutexLocker locker(const_cast<QMutex*>(&m_mutex));
    return m_deviceName;
}

void PIAlignExecutor::startAlignment(const QString& deviceName, const FSMParameters& params)
{
    if (m_running.load()) {
        std::cerr << "[PIAlignExecutor] Alignment already in progress!" << std::endl;
        emit alignmentFailed(deviceName, params.analogChannel, "Another alignment is already running");
        return;
    }

    // Store device name
    {
        QMutexLocker locker(&m_mutex);
        m_deviceName = deviceName.toStdString();
    }

    m_running.store(true);
    emit busyStateChanged(true);

    // Create worker thread
    m_workerThread = new QThread();

    // Create worker (will be moved to thread)
    m_worker = new PIAlignWorker(m_motionManager);
    m_worker->setParameters(deviceName, params);

    // Move worker to thread
    m_worker->moveToThread(m_workerThread);

    // Connect worker signals to executor slots (QueuedConnection for cross-thread)
    connect(m_worker, &PIAlignWorker::started,
            this, &PIAlignExecutor::onWorkerStarted, Qt::QueuedConnection);
    connect(m_worker, &PIAlignWorker::completed,
            this, &PIAlignExecutor::onWorkerCompleted, Qt::QueuedConnection);
    connect(m_worker, &PIAlignWorker::failed,
            this, &PIAlignExecutor::onWorkerFailed, Qt::QueuedConnection);
    connect(m_worker, &PIAlignWorker::progress,
            this, &PIAlignExecutor::onWorkerProgress, Qt::QueuedConnection);
    connect(m_worker, &PIAlignWorker::zScanProgress,
            this, &PIAlignExecutor::onWorkerZScanProgress, Qt::QueuedConnection);
    connect(m_worker, &PIAlignWorker::finished,
            this, &PIAlignExecutor::onWorkerFinished, Qt::QueuedConnection);

    // Start work when thread starts
    connect(m_workerThread, &QThread::started, m_worker, &PIAlignWorker::doWork);

    // Cleanup when thread finishes
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_workerThread, &QThread::finished, m_workerThread, &QObject::deleteLater);

    // Start the thread
    m_workerThread->start();

    std::cout << "[PIAlignExecutor] Worker thread started for " << deviceName.toStdString() << std::endl;
}

void PIAlignExecutor::requestStop()
{
    std::cout << "[PIAlignExecutor] Stop requested" << std::endl;
    if (m_worker) {
        m_worker->requestStop();
    }
}

void PIAlignExecutor::onWorkerStarted(QString deviceName, int channel)
{
    std::cout << "[PIAlignExecutor] Worker started signal received" << std::endl;
    emit alignmentStarted(deviceName, channel);
}

void PIAlignExecutor::onWorkerCompleted(QString deviceName, int channel)
{
    std::cout << "[PIAlignExecutor] Worker completed signal received" << std::endl;
    emit alignmentCompleted(deviceName, channel);
}

void PIAlignExecutor::onWorkerFailed(QString deviceName, int channel, QString errorMessage)
{
    std::cout << "[PIAlignExecutor] Worker failed signal received: " << errorMessage.toStdString() << std::endl;
    emit alignmentFailed(deviceName, channel, errorMessage);
}

void PIAlignExecutor::onWorkerProgress(QString deviceName, int status)
{
    emit alignmentProgress(deviceName, status);
}

void PIAlignExecutor::onWorkerZScanProgress(QString deviceName, int iteration, int total, double currentZ, double peakZ, double peakVoltage)
{
    emit zScanProgress(deviceName, iteration, total, currentZ, peakZ, peakVoltage);
}

void PIAlignExecutor::onWorkerFinished()
{
    std::cout << "[PIAlignExecutor] Worker finished, cleaning up" << std::endl;
    cleanup();
}

void PIAlignExecutor::cleanup()
{
    m_running.store(false);
    emit busyStateChanged(false);

    // Quit thread if running
    if (m_workerThread && m_workerThread->isRunning()) {
        m_workerThread->quit();
        if (!m_workerThread->wait(2000)) {
            std::cerr << "[PIAlignExecutor] Thread did not stop gracefully" << std::endl;
            m_workerThread->terminate();
            m_workerThread->wait();
        }
    }

    // Pointers will be cleaned up by deleteLater
    m_workerThread = nullptr;
    m_worker = nullptr;
}

// ============================================================================
// PIAlignWorker - Runs in background thread
// ============================================================================

PIAlignWorker::PIAlignWorker(UnifiedMotionManager* motionManager)
    : QObject(nullptr)
    , m_motionManager(motionManager)
    , m_stopRequested(false)
{
    std::cout << "[PIAlignWorker] Created" << std::endl;
}

PIAlignWorker::~PIAlignWorker()
{
    std::cout << "[PIAlignWorker] Destroyed" << std::endl;
}

void PIAlignWorker::setParameters(const QString& deviceName, const FSMParameters& params)
{
    m_deviceName = deviceName;
    m_params = params;
    m_stopRequested.store(false);
}

void PIAlignWorker::doWork()
{
    std::cout << "[PIAlignWorker] doWork() started" << std::endl;

    emit started(m_deviceName, m_params.analogChannel);

    // Get PI controller
    if (!m_motionManager) {
        std::cerr << "[PIAlignWorker] No motion manager!" << std::endl;
        emit failed(m_deviceName, m_params.analogChannel, "Motion manager not available");
        emit finished();
        return;
    }

    PIController* piController = m_motionManager->GetPIController(m_deviceName.toStdString());
    if (!piController || !piController->IsConnected()) {
        std::cerr << "[PIAlignWorker] PI controller not connected" << std::endl;
        emit failed(m_deviceName, m_params.analogChannel, "Device not connected");
        emit finished();
        return;
    }

    // Check if Z-scan mode is enabled
    if (m_params.zScanEnabled) {
        doZScanWork(piController);
        return;
    }

    // Normal single FSM mode
    std::cout << "[PIAlignWorker] Executing FSM on " << m_deviceName.toStdString() << std::endl;
    std::cout << "  Axis1: " << m_params.axis1 << " Length: " << m_params.length1 << " mm" << std::endl;
    std::cout << "  Axis2: " << m_params.axis2 << " Length: " << m_params.length2 << " mm" << std::endl;
    std::cout << "  Threshold: " << m_params.threshold << " V" << std::endl;
    std::cout << "  Distance: " << m_params.distance << " mm" << std::endl;
    std::cout << "  Channel: " << m_params.analogChannel << std::endl;

    // Execute FSM command
    bool success = piController->FSM(
        m_params.axis1, m_params.length1,
        m_params.axis2, m_params.length2,
        m_params.threshold, m_params.distance,
        m_params.analogChannel);

    if (!success) {
        std::cerr << "[PIAlignWorker] FSM command failed!" << std::endl;
        emit failed(m_deviceName, m_params.analogChannel, "FSM command failed - check controller error");
        emit finished();
        return;
    }

    std::cout << "[PIAlignWorker] FSM command sent, starting status polling loop..." << std::endl;

    // Timeout settings
    const int TIMEOUT_MS = 60000;  // 60 second timeout
    const int NOT_MOVING_THRESHOLD = 5;  // Consider done if not moving for 5 consecutive polls
    int elapsedMs = 0;
    int lastStatus = -999;
    int notMovingCount = 0;

    // Use direct polling loop instead of timer (more reliable across threads)
    while (!m_stopRequested.load()) {
        // Sleep for poll interval
        QThread::msleep(STATUS_POLL_INTERVAL_MS);
        elapsedMs += STATUS_POLL_INTERVAL_MS;

        if (m_stopRequested.load()) {
            std::cout << "[PIAlignWorker] Stop requested during polling" << std::endl;
            emit failed(m_deviceName, m_params.analogChannel, "Alignment cancelled by user");
            break;
        }

        // Check timeout
        if (elapsedMs >= TIMEOUT_MS) {
            std::cerr << "[PIAlignWorker] Timeout waiting for FSM completion (" << TIMEOUT_MS/1000 << "s)" << std::endl;
            emit failed(m_deviceName, m_params.analogChannel,
                       QString("Timeout after %1 seconds - PI may have finished but status not updated").arg(TIMEOUT_MS/1000));
            break;
        }

        // Query scan status
        PIController* piController = m_motionManager->GetPIController(m_deviceName.toStdString());
        if (!piController || !piController->IsConnected()) {
            std::cerr << "[PIAlignWorker] Device disconnected during alignment" << std::endl;
            emit failed(m_deviceName, m_params.analogChannel, "Device disconnected during alignment");
            break;
        }

        int status = 0;
        if (!piController->GetFastScanStatus(status)) {
            std::cerr << "[PIAlignWorker] Failed to get scan status" << std::endl;
            // Don't fail immediately - might be temporary
            continue;
        }

        // Only log when status changes to reduce spam
        if (status != lastStatus) {
            std::cout << "[PIAlignWorker] Scan status: " << status
                      << " (elapsed: " << elapsedMs/1000.0 << "s)" << std::endl;
            lastStatus = status;
        }

        emit progress(m_deviceName, status);

        // Status interpretation:
        // 0 = idle (scan completed)
        // 1 = running
        // negative = error
        if (status == 0) {
            std::cout << "[PIAlignWorker] FSM completed successfully! (took " << elapsedMs/1000.0 << "s)" << std::endl;
            emit completed(m_deviceName, m_params.analogChannel);
            break;
        }
        else if (status < 0) {
            std::cerr << "[PIAlignWorker] FSM error, status: " << status << std::endl;
            emit failed(m_deviceName, m_params.analogChannel,
                       QString("Scan error (status: %1)").arg(status));
            break;
        }
        else if (status == 1) {
            // Status says running - but check if hexapod is actually moving
            // PI sometimes doesn't update FSS status after completion
            bool anyAxisMoving = piController->IsMoving("X") ||
                                 piController->IsMoving("Y") ||
                                 piController->IsMoving("Z");

            if (!anyAxisMoving) {
                notMovingCount++;
                if (notMovingCount >= NOT_MOVING_THRESHOLD) {
                    std::cout << "[PIAlignWorker] FSM completed (detected via motion stop, FSS status stuck at 1)" << std::endl;
                    std::cout << "[PIAlignWorker] Hexapod not moving for " << notMovingCount << " consecutive polls" << std::endl;
                    emit completed(m_deviceName, m_params.analogChannel);
                    break;
                }
            } else {
                notMovingCount = 0;  // Reset counter if still moving
            }
        }
        // status == 1 and still moving: continue polling
    }

    std::cout << "[PIAlignWorker] Polling loop ended, emitting finished" << std::endl;
    emit finished();
}

bool PIAlignWorker::runSingleFSM(PIController* piController)
{
    // Execute FSM command
    bool success = piController->FSM(
        m_params.axis1, m_params.length1,
        m_params.axis2, m_params.length2,
        m_params.threshold, m_params.distance,
        m_params.analogChannel);

    if (!success) {
        std::cerr << "[PIAlignWorker] FSM command failed!" << std::endl;
        return false;
    }

    // Timeout settings
    const int TIMEOUT_MS = 60000;  // 60 second timeout
    const int NOT_MOVING_THRESHOLD = 5;
    int elapsedMs = 0;
    int lastStatus = -999;
    int notMovingCount = 0;

    // Polling loop to wait for FSM completion
    while (!m_stopRequested.load()) {
        QThread::msleep(STATUS_POLL_INTERVAL_MS);
        elapsedMs += STATUS_POLL_INTERVAL_MS;

        if (m_stopRequested.load()) {
            return false;
        }

        if (elapsedMs >= TIMEOUT_MS) {
            std::cerr << "[PIAlignWorker] FSM timeout" << std::endl;
            return false;
        }

        int status = 0;
        if (!piController->GetFastScanStatus(status)) {
            continue;
        }

        if (status != lastStatus) {
            std::cout << "[PIAlignWorker] FSM status: " << status << std::endl;
            lastStatus = status;
        }

        emit progress(m_deviceName, status);

        if (status == 0) {
            return true;  // Completed successfully
        }
        else if (status < 0) {
            return false;  // Error
        }
        else if (status == 1) {
            // Check if hexapod actually stopped moving
            bool anyAxisMoving = piController->IsMoving("X") ||
                                 piController->IsMoving("Y") ||
                                 piController->IsMoving("Z");

            if (!anyAxisMoving) {
                notMovingCount++;
                if (notMovingCount >= NOT_MOVING_THRESHOLD) {
                    return true;  // Motion stopped, consider done
                }
            } else {
                notMovingCount = 0;
            }
        }
    }

    return false;
}

void PIAlignWorker::doZScanWork(PIController* piController)
{
    std::cout << "[PIAlignWorker] Starting Z-iteration FSM scan" << std::endl;
    std::cout << "  Direction: " << (m_params.zDirection > 0 ? "Z+" : "Z-") << std::endl;
    std::cout << "  Range: " << m_params.zRange << " mm" << std::endl;
    std::cout << "  Step: " << m_params.zStep << " mm" << std::endl;

    // Get starting Z position
    double startZ = 0.0;
    if (!piController->GetPosition("Z", startZ)) {
        std::cerr << "[PIAlignWorker] Failed to get starting Z position" << std::endl;
        emit failed(m_deviceName, m_params.analogChannel, "Failed to read Z position");
        emit finished();
        return;
    }

    std::cout << "[PIAlignWorker] Starting Z position: " << startZ << " mm" << std::endl;

    // Calculate number of iterations
    int numIterations = static_cast<int>(m_params.zRange / m_params.zStep);
    if (numIterations < 1) numIterations = 1;

    std::cout << "[PIAlignWorker] Will perform " << numIterations << " Z iterations" << std::endl;

    // Track best position
    double bestZ = startZ;
    double bestVoltage = -9999.0;
    bool foundAnyValid = false;

    // Iterate through Z positions
    for (int i = 0; i <= numIterations && !m_stopRequested.load(); i++) {
        double currentZ = startZ + (i * m_params.zStep * m_params.zDirection);

        std::cout << "[PIAlignWorker] === Z Iteration " << i << "/" << numIterations
                  << " at Z=" << currentZ << " mm ===" << std::endl;

        // Emit progress signal for UI update
        emit zScanProgress(m_deviceName, i, numIterations, currentZ, bestZ, bestVoltage);

        // Move to this Z position (skip for first iteration - already there)
        if (i > 0) {
            if (!piController->MoveToPosition("Z", currentZ, true)) {
                std::cerr << "[PIAlignWorker] Failed to move to Z=" << currentZ << std::endl;
                continue;
            }
            // Wait for motion to settle
            QThread::msleep(100);
        }

        // Run FSM at this Z level
        if (!runSingleFSM(piController)) {
            std::cerr << "[PIAlignWorker] FSM failed at Z=" << currentZ << std::endl;
            if (m_stopRequested.load()) {
                emit failed(m_deviceName, m_params.analogChannel, "Z-scan cancelled by user");
                emit finished();
                return;
            }
            // Continue to next Z level even if this one failed
            continue;
        }

        // Read the analog voltage after FSM completes
        double voltage = 0.0;
        if (piController->GetAnalogVoltage(m_params.analogChannel, voltage)) {
            std::cout << "[PIAlignWorker] Z=" << currentZ << " mm, Voltage=" << voltage << " V" << std::endl;

            // Track best position (highest voltage = best alignment)
            if (voltage > bestVoltage) {
                bestVoltage = voltage;
                bestZ = currentZ;
                foundAnyValid = true;
                std::cout << "[PIAlignWorker] New best! Z=" << bestZ << " mm, V=" << bestVoltage << " V" << std::endl;
            }

            // Emit progress with updated peak value
            emit zScanProgress(m_deviceName, i, numIterations, currentZ, bestZ, bestVoltage);
        } else {
            std::cerr << "[PIAlignWorker] Failed to read analog voltage at Z=" << currentZ << std::endl;
        }
    }

    if (m_stopRequested.load()) {
        emit failed(m_deviceName, m_params.analogChannel, "Z-scan cancelled by user");
        emit finished();
        return;
    }

    if (!foundAnyValid) {
        std::cerr << "[PIAlignWorker] No valid readings found during Z-scan" << std::endl;
        emit failed(m_deviceName, m_params.analogChannel, "No valid readings during Z-scan");
        emit finished();
        return;
    }

    // Move to best Z position
    std::cout << "[PIAlignWorker] Moving to best Z position: " << bestZ << " mm (V=" << bestVoltage << " V)" << std::endl;
    if (!piController->MoveToPosition("Z", bestZ, true)) {
        std::cerr << "[PIAlignWorker] Failed to move to best Z position" << std::endl;
        emit failed(m_deviceName, m_params.analogChannel, "Failed to move to best Z position");
        emit finished();
        return;
    }

    // Wait for motion to settle
    QThread::msleep(200);

    // Run final FSM at the best Z for optimal X-Y alignment
    std::cout << "[PIAlignWorker] Running final FSM at best Z position" << std::endl;
    if (!runSingleFSM(piController)) {
        std::cerr << "[PIAlignWorker] Final FSM failed" << std::endl;
        // Still report success since we found the best Z
    }

    std::cout << "[PIAlignWorker] Z-scan completed! Best Z=" << bestZ
              << " mm, Best V=" << bestVoltage << " V" << std::endl;

    emit completed(m_deviceName, m_params.analogChannel);
    emit finished();
}
