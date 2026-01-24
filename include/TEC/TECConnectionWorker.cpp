// TECConnectionWorker.cpp
#include "TECConnectionWorker.h"
#include "TECManager.h"
#include <QMutexLocker>
#include <algorithm>
#include <iostream>

TECConnectionWorker::TECConnectionWorker(TECManager* manager, QObject* parent)
    : QObject(parent)
    , m_manager(manager)
    , m_workerThread(nullptr)
    , m_processTimer(nullptr)
    , m_running(false)
    , m_busy(false)
{
    std::cout << "[TECConnectionWorker] Created" << std::endl;
}

TECConnectionWorker::~TECConnectionWorker()
{
    stop();
    std::cout << "[TECConnectionWorker] Destroyed" << std::endl;
}

void TECConnectionWorker::start()
{
    if (m_running.load()) {
        return;  // Already running
    }

    // Create worker thread
    m_workerThread = new QThread();

    // Create timer (will be moved to worker thread)
    m_processTimer = new QTimer();
    m_processTimer->setInterval(PROCESS_INTERVAL_MS);
    connect(m_processTimer, &QTimer::timeout, this, &TECConnectionWorker::processQueue);

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

    std::cout << "[TECConnectionWorker] Started" << std::endl;
}

void TECConnectionWorker::stop()
{
    if (!m_running.load()) {
        return;  // Not running
    }

    m_running.store(false);

    if (m_workerThread) {
        m_workerThread->quit();
        if (!m_workerThread->wait(2000)) {  // Wait up to 2 seconds
            std::cerr << "[TECConnectionWorker] Thread did not stop gracefully, terminating" << std::endl;
            m_workerThread->terminate();
            m_workerThread->wait();
        }
        m_workerThread = nullptr;
    }

    m_processTimer = nullptr;

    std::cout << "[TECConnectionWorker] Stopped" << std::endl;
}

// ============================================================================
// PUBLIC SLOTS - Queue requests from UI (thread-safe)
// ============================================================================

void TECConnectionWorker::requestConnect(const QString& deviceName)
{
    QMutexLocker locker(&m_queueMutex);

    std::cout << "[TECConnectionWorker] requestConnect: " << deviceName.toStdString() << std::endl;

    // Clear any pending commands for this device
    m_pendingCommands.erase(
        std::remove_if(m_pendingCommands.begin(), m_pendingCommands.end(),
            [&deviceName](const PendingCommand& cmd) {
                return cmd.deviceName == deviceName.toStdString();
            }),
        m_pendingCommands.end());

    m_pendingCommands.push_back({PendingCommand::Connect, deviceName.toStdString()});
}

void TECConnectionWorker::requestDisconnect(const QString& deviceName)
{
    QMutexLocker locker(&m_queueMutex);

    std::cout << "[TECConnectionWorker] requestDisconnect: " << deviceName.toStdString() << std::endl;

    m_pendingCommands.erase(
        std::remove_if(m_pendingCommands.begin(), m_pendingCommands.end(),
            [&deviceName](const PendingCommand& cmd) {
                return cmd.deviceName == deviceName.toStdString();
            }),
        m_pendingCommands.end());

    m_pendingCommands.push_back({PendingCommand::Disconnect, deviceName.toStdString()});
}

// ============================================================================
// PRIVATE SLOTS - Process in worker thread
// ============================================================================

void TECConnectionWorker::processQueue()
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

    std::cout << "[TECConnectionWorker] Processing command for device: " << cmd.deviceName << std::endl;

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

void TECConnectionWorker::executeConnect(const std::string& deviceName)
{
    emit operationStarted("Connect", QString::fromStdString(deviceName));

    std::cout << "[TECConnectionWorker] Calling ConnectDevice() for: " << deviceName << std::endl;

    try {
        if (m_manager->ConnectDevice(deviceName)) {
            std::cout << "[TECConnectionWorker] Connected successfully: " << deviceName << std::endl;
            emit connectionSuccess(QString::fromStdString(deviceName));
        } else {
            std::cerr << "[TECConnectionWorker] Connection failed: " << deviceName << std::endl;
            emit connectionFailed(QString::fromStdString(deviceName), "Connection failed");
        }
    }
    catch (const std::exception& e) {
        std::cerr << "[TECConnectionWorker] Exception during connect: " << e.what() << std::endl;
        emit connectionFailed(QString::fromStdString(deviceName), QString::fromStdString(e.what()));
    }
}

void TECConnectionWorker::executeDisconnect(const std::string& deviceName)
{
    emit operationStarted("Disconnect", QString::fromStdString(deviceName));

    std::cout << "[TECConnectionWorker] Calling DisconnectDevice() for: " << deviceName << std::endl;

    try {
        m_manager->DisconnectDevice(deviceName);
        std::cout << "[TECConnectionWorker] Disconnected: " << deviceName << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "[TECConnectionWorker] Exception during disconnect: " << e.what() << std::endl;
    }

    emit disconnectionComplete(QString::fromStdString(deviceName));
}
