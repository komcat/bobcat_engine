// SMUConnectionWorker.cpp
#include "SMUConnectionWorker.h"
#include "SMUManager.h"
#include "ISMUDevice.h"
#include <QMutexLocker>
#include <algorithm>
#include <iostream>

SMUConnectionWorker::SMUConnectionWorker(SMUManager* manager, QObject* parent)
    : QObject(parent)
    , m_manager(manager)
    , m_workerThread(nullptr)
    , m_processTimer(nullptr)
    , m_running(false)
    , m_busy(false)
{
    std::cout << "[SMUConnectionWorker] Created" << std::endl;
}

SMUConnectionWorker::~SMUConnectionWorker()
{
    stop();
    std::cout << "[SMUConnectionWorker] Destroyed" << std::endl;
}

void SMUConnectionWorker::start()
{
    if (m_running.load()) {
        return;  // Already running
    }

    // Create worker thread
    m_workerThread = new QThread();

    // Create timer (will be moved to worker thread)
    m_processTimer = new QTimer();
    m_processTimer->setInterval(PROCESS_INTERVAL_MS);
    connect(m_processTimer, &QTimer::timeout, this, &SMUConnectionWorker::processQueue);

    // Move this object and timer to worker thread
    this->moveToThread(m_workerThread);
    m_processTimer->moveToThread(m_workerThread);

    // Start timer when thread starts
    connect(m_workerThread, &QThread::started, m_processTimer, QOverload<>::of(&QTimer::start));

    // Cleanup on thread finish
    connect(m_workerThread, &QThread::finished, m_processTimer, &QTimer::stop);
    connect(m_workerThread, &QThread::finished, m_workerThread, &QThread::deleteLater);

    m_running.store(true);
    m_workerThread->start();

    std::cout << "[SMUConnectionWorker] Started" << std::endl;
}

void SMUConnectionWorker::stop()
{
    if (!m_running.load()) {
        return;  // Not running
    }

    m_running.store(false);

    if (m_workerThread) {
        m_workerThread->quit();
        if (!m_workerThread->wait(2000)) {  // Wait up to 2 seconds
            std::cerr << "[SMUConnectionWorker] Thread did not stop gracefully, terminating" << std::endl;
            m_workerThread->terminate();
            m_workerThread->wait();
        }
        m_workerThread = nullptr;
    }

    // Timer is deleted by thread cleanup
    m_processTimer = nullptr;

    std::cout << "[SMUConnectionWorker] Stopped" << std::endl;
}

void SMUConnectionWorker::setCurrentDevice(const std::string& deviceName)
{
    QMutexLocker locker(&m_queueMutex);
    m_currentDevice = deviceName;
    std::cout << "[SMUConnectionWorker] Device set to: " << deviceName << std::endl;
}

std::string SMUConnectionWorker::getCurrentDevice() const
{
    QMutexLocker locker(&const_cast<QMutex&>(m_queueMutex));
    return m_currentDevice;
}

// ============================================================================
// PUBLIC SLOTS - Queue requests from UI (thread-safe)
// ============================================================================

void SMUConnectionWorker::requestConnect(const QString& deviceName)
{
    QMutexLocker locker(&m_queueMutex);

    std::cout << "[SMUConnectionWorker] requestConnect: " << deviceName.toStdString() << std::endl;

    // Clear any pending commands for this device (only one connection op at a time)
    m_pendingCommands.erase(
        std::remove_if(m_pendingCommands.begin(), m_pendingCommands.end(),
            [&deviceName](const PendingCommand& cmd) {
                return cmd.deviceName == deviceName.toStdString();
            }),
        m_pendingCommands.end());

    // Add connect command
    m_pendingCommands.push_back({PendingCommand::Connect, deviceName.toStdString()});
}

void SMUConnectionWorker::requestDisconnect(const QString& deviceName)
{
    QMutexLocker locker(&m_queueMutex);

    std::cout << "[SMUConnectionWorker] requestDisconnect: " << deviceName.toStdString() << std::endl;

    // Clear any pending commands for this device
    m_pendingCommands.erase(
        std::remove_if(m_pendingCommands.begin(), m_pendingCommands.end(),
            [&deviceName](const PendingCommand& cmd) {
                return cmd.deviceName == deviceName.toStdString();
            }),
        m_pendingCommands.end());

    // Add disconnect command
    m_pendingCommands.push_back({PendingCommand::Disconnect, deviceName.toStdString()});
}

// ============================================================================
// PRIVATE SLOTS - Process in worker thread
// ============================================================================

void SMUConnectionWorker::processQueue()
{
    if (!m_running.load() || !m_manager || m_busy.load()) {
        return;
    }

    // Get next command
    PendingCommand cmd;
    {
        QMutexLocker locker(&m_queueMutex);
        if (m_pendingCommands.empty()) {
            return;
        }
        cmd = m_pendingCommands.front();
        m_pendingCommands.erase(m_pendingCommands.begin());
    }

    m_busy.store(true);

    std::cout << "[SMUConnectionWorker] Processing command for device: " << cmd.deviceName << std::endl;

    switch (cmd.type) {
        case PendingCommand::Connect:
            executeConnect(cmd.deviceName);
            break;

        case PendingCommand::Disconnect:
            executeDisconnect(cmd.deviceName);
            break;
    }

    m_busy.store(false);
}

// ============================================================================
// PRIVATE - Execute commands
// ============================================================================

void SMUConnectionWorker::executeConnect(const std::string& deviceName)
{
    emit operationStarted("Connect", QString::fromStdString(deviceName));
    emit operationProgress("Attempting to connect to " + QString::fromStdString(deviceName) + "...");

    auto smu = m_manager->GetDevice(deviceName);
    if (!smu) {
        std::cerr << "[SMUConnectionWorker] Device not found: " << deviceName << std::endl;
        emit connectionFailed(QString::fromStdString(deviceName), "Device not found in manager");
        return;
    }

    std::cout << "[SMUConnectionWorker] Calling Connect() for: " << deviceName << std::endl;

    try {
        if (smu->Connect()) {
            std::string deviceId = smu->GetDeviceID();
            std::cout << "[SMUConnectionWorker] Connected successfully: " << deviceName
                      << " (ID: " << deviceId << ")" << std::endl;
            emit connectionSuccess(QString::fromStdString(deviceName),
                                   QString::fromStdString(deviceId));
        } else {
            std::string error = smu->GetLastError();
            std::cerr << "[SMUConnectionWorker] Connection failed: " << error << std::endl;
            emit connectionFailed(QString::fromStdString(deviceName),
                                  QString::fromStdString(error));
        }
    }
    catch (const std::exception& e) {
        std::cerr << "[SMUConnectionWorker] Exception during connect: " << e.what() << std::endl;
        emit connectionFailed(QString::fromStdString(deviceName),
                              QString::fromStdString(e.what()));
    }
}

void SMUConnectionWorker::executeDisconnect(const std::string& deviceName)
{
    emit operationStarted("Disconnect", QString::fromStdString(deviceName));

    auto smu = m_manager->GetDevice(deviceName);
    if (!smu) {
        std::cerr << "[SMUConnectionWorker] Device not found: " << deviceName << std::endl;
        emit disconnectionComplete(QString::fromStdString(deviceName));
        return;
    }

    std::cout << "[SMUConnectionWorker] Calling Disconnect() for: " << deviceName << std::endl;

    try {
        smu->Disconnect();
        std::cout << "[SMUConnectionWorker] Disconnected: " << deviceName << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "[SMUConnectionWorker] Exception during disconnect: " << e.what() << std::endl;
    }

    emit disconnectionComplete(QString::fromStdString(deviceName));
}
