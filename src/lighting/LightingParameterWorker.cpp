// LightingParameterWorker.cpp
#include "lighting/LightingParameterWorker.h"
#include "lighting/LightingManager.h"
#include "lighting/LightingTypes.h"
#include <QMutexLocker>
#include <algorithm>
#include <iostream>

LightingParameterWorker::LightingParameterWorker(Lighting::LightingManager* manager, QObject* parent)
    : QObject(parent)
    , m_manager(manager)
    , m_workerThread(nullptr)
    , m_processTimer(nullptr)
    , m_pollTimer(nullptr)
    , m_running(false)
    , m_statusPollingEnabled(false)
{
    std::cout << "[LightingParameterWorker] Created" << std::endl;
}

LightingParameterWorker::~LightingParameterWorker()
{
    stop();
    std::cout << "[LightingParameterWorker] Destroyed" << std::endl;
}

void LightingParameterWorker::start()
{
    if (m_running.load()) {
        return;  // Already running
    }

    // Create worker thread
    m_workerThread = new QThread();

    // Create timers (will be moved to worker thread)
    m_processTimer = new QTimer();
    m_processTimer->setInterval(PROCESS_INTERVAL_MS);
    connect(m_processTimer, &QTimer::timeout, this, &LightingParameterWorker::processQueue);

    m_pollTimer = new QTimer();
    m_pollTimer->setInterval(DEFAULT_POLL_INTERVAL_MS);
    connect(m_pollTimer, &QTimer::timeout, this, &LightingParameterWorker::pollStatus);

    // Move this object and timers to worker thread
    this->moveToThread(m_workerThread);
    m_processTimer->moveToThread(m_workerThread);
    m_pollTimer->moveToThread(m_workerThread);

    // Start timers when thread starts
    connect(m_workerThread, &QThread::started, m_processTimer, QOverload<>::of(&QTimer::start));
    connect(m_workerThread, &QThread::started, m_pollTimer, QOverload<>::of(&QTimer::start));

    // Cleanup on thread finish
    connect(m_workerThread, &QThread::finished, m_processTimer, &QTimer::stop);
    connect(m_workerThread, &QThread::finished, m_pollTimer, &QTimer::stop);
    connect(m_workerThread, &QThread::finished, m_workerThread, &QThread::deleteLater);

    m_running.store(true);
    m_workerThread->start();

    std::cout << "[LightingParameterWorker] Started" << std::endl;
}

void LightingParameterWorker::stop()
{
    if (!m_running.load()) {
        return;  // Not running
    }

    m_running.store(false);

    if (m_workerThread) {
        m_workerThread->quit();
        if (!m_workerThread->wait(1000)) {  // Wait up to 1 second
            std::cerr << "[LightingParameterWorker] Thread did not stop gracefully, terminating" << std::endl;
            m_workerThread->terminate();
            m_workerThread->wait();
        }
        m_workerThread = nullptr;
    }

    // Timers are deleted by thread cleanup
    m_processTimer = nullptr;
    m_pollTimer = nullptr;

    std::cout << "[LightingParameterWorker] Stopped" << std::endl;
}

void LightingParameterWorker::setCurrentDevice(const std::string& deviceId)
{
    QMutexLocker locker(&m_queueMutex);
    m_currentDeviceId = deviceId;
    // Clear pending commands when device changes
    m_pendingCommands.clear();
    std::cout << "[LightingParameterWorker] Device set to: " << deviceId << std::endl;
}

// ============================================================================
// PUBLIC SLOTS - Queue requests from UI (thread-safe)
// ============================================================================

void LightingParameterWorker::requestConnect(const QString& deviceId)
{
    QMutexLocker locker(&m_queueMutex);
    std::cout << "[LightingParameterWorker] requestConnect: " << deviceId.toStdString() << std::endl;

    // Remove existing connect/disconnect commands
    m_pendingCommands.erase(
        std::remove_if(m_pendingCommands.begin(), m_pendingCommands.end(),
            [](const PendingCommand& cmd) {
                return cmd.type == PendingCommand::Connect ||
                       cmd.type == PendingCommand::Disconnect;
            }),
        m_pendingCommands.end());

    PendingCommand cmd;
    cmd.type = PendingCommand::Connect;
    cmd.deviceId = deviceId.toStdString();
    m_pendingCommands.push_back(cmd);
}

void LightingParameterWorker::requestDisconnect(const QString& deviceId)
{
    QMutexLocker locker(&m_queueMutex);
    std::cout << "[LightingParameterWorker] requestDisconnect: " << deviceId.toStdString() << std::endl;

    m_pendingCommands.erase(
        std::remove_if(m_pendingCommands.begin(), m_pendingCommands.end(),
            [](const PendingCommand& cmd) {
                return cmd.type == PendingCommand::Connect ||
                       cmd.type == PendingCommand::Disconnect;
            }),
        m_pendingCommands.end());

    PendingCommand cmd;
    cmd.type = PendingCommand::Disconnect;
    cmd.deviceId = deviceId.toStdString();
    m_pendingCommands.push_back(cmd);
}

void LightingParameterWorker::requestSetIntensity(int channel, int intensity)
{
    QMutexLocker locker(&m_queueMutex);

    // Coalesce: Remove existing intensity commands for same channel
    m_pendingCommands.erase(
        std::remove_if(m_pendingCommands.begin(), m_pendingCommands.end(),
            [channel](const PendingCommand& cmd) {
                return cmd.type == PendingCommand::SetIntensity &&
                       cmd.channel == channel;
            }),
        m_pendingCommands.end());

    PendingCommand cmd;
    cmd.type = PendingCommand::SetIntensity;
    cmd.channel = channel;
    cmd.intValue = intensity;
    cmd.deviceId = m_currentDeviceId;
    m_pendingCommands.push_back(cmd);
}

void LightingParameterWorker::requestSetOn(int channel, bool on)
{
    QMutexLocker locker(&m_queueMutex);

    // Coalesce: Remove existing toggle commands for same channel
    m_pendingCommands.erase(
        std::remove_if(m_pendingCommands.begin(), m_pendingCommands.end(),
            [channel](const PendingCommand& cmd) {
                return cmd.type == PendingCommand::SetOn &&
                       cmd.channel == channel;
            }),
        m_pendingCommands.end());

    PendingCommand cmd;
    cmd.type = PendingCommand::SetOn;
    cmd.channel = channel;
    cmd.boolValue = on;
    cmd.deviceId = m_currentDeviceId;
    m_pendingCommands.push_back(cmd);
}

void LightingParameterWorker::requestAllOff()
{
    QMutexLocker locker(&m_queueMutex);
    std::cout << "[LightingParameterWorker] requestAllOff" << std::endl;

    // Remove any existing AllOff commands
    m_pendingCommands.erase(
        std::remove_if(m_pendingCommands.begin(), m_pendingCommands.end(),
            [](const PendingCommand& cmd) {
                return cmd.type == PendingCommand::AllOff;
            }),
        m_pendingCommands.end());

    PendingCommand cmd;
    cmd.type = PendingCommand::AllOff;
    cmd.deviceId = m_currentDeviceId;
    m_pendingCommands.push_back(cmd);
}

void LightingParameterWorker::requestRefreshStatus()
{
    QMutexLocker locker(&m_queueMutex);

    // Remove existing refresh commands (coalesce)
    m_pendingCommands.erase(
        std::remove_if(m_pendingCommands.begin(), m_pendingCommands.end(),
            [](const PendingCommand& cmd) {
                return cmd.type == PendingCommand::RefreshStatus;
            }),
        m_pendingCommands.end());

    PendingCommand cmd;
    cmd.type = PendingCommand::RefreshStatus;
    cmd.deviceId = m_currentDeviceId;
    m_pendingCommands.push_back(cmd);
}

void LightingParameterWorker::setStatusPollingEnabled(bool enabled)
{
    m_statusPollingEnabled.store(enabled);
    std::cout << "[LightingParameterWorker] Polling " << (enabled ? "enabled" : "disabled") << std::endl;
}

void LightingParameterWorker::setStatusPollingInterval(int milliseconds)
{
    if (m_pollTimer) {
        QMetaObject::invokeMethod(m_pollTimer, [this, milliseconds]() {
            m_pollTimer->setInterval(milliseconds);
        }, Qt::QueuedConnection);
    }
}

// ============================================================================
// PRIVATE SLOTS - Process in worker thread
// ============================================================================

void LightingParameterWorker::processQueue()
{
    if (!m_running.load() || !m_manager) {
        return;
    }

    // Copy device name and queue under mutex protection
    std::vector<PendingCommand> commands;
    {
        QMutexLocker locker(&m_queueMutex);
        commands = std::move(m_pendingCommands);
        m_pendingCommands.clear();
    }

    if (commands.empty()) {
        return;
    }

    // Process each command
    for (const auto& cmd : commands) {
        std::string deviceId = cmd.deviceId.empty() ? m_currentDeviceId : cmd.deviceId;
        if (deviceId.empty()) {
            continue;
        }

        switch (cmd.type) {
            case PendingCommand::Connect:
                executeConnect(deviceId);
                break;

            case PendingCommand::Disconnect:
                executeDisconnect(deviceId);
                break;

            case PendingCommand::SetIntensity:
                executeSetIntensity(deviceId, cmd.channel, cmd.intValue);
                break;

            case PendingCommand::SetOn:
                executeSetOn(deviceId, cmd.channel, cmd.boolValue);
                break;

            case PendingCommand::AllOff:
                executeAllOff(deviceId);
                break;

            case PendingCommand::RefreshStatus:
                executeRefreshStatus(deviceId);
                break;
        }
    }
}

void LightingParameterWorker::pollStatus()
{
    if (!m_running.load() || !m_statusPollingEnabled.load() || !m_manager) {
        return;
    }

    std::string deviceId;
    {
        QMutexLocker locker(&m_queueMutex);
        deviceId = m_currentDeviceId;
    }

    if (deviceId.empty()) {
        return;
    }

    if (!m_manager->IsConnected(deviceId)) {
        return;
    }

    executeRefreshStatus(deviceId);
}

// ============================================================================
// PRIVATE - Execute commands
// ============================================================================

void LightingParameterWorker::executeConnect(const std::string& deviceId)
{
    std::cout << "[LightingParameterWorker] Executing Connect: " << deviceId << std::endl;

    try {
        if (m_manager->Connect(deviceId)) {
            std::cout << "[LightingParameterWorker] Connected successfully: " << deviceId << std::endl;
            // Apply default intensities after connection
            m_manager->ApplyDefaultIntensities(deviceId);
            emit connectionSuccess(QString::fromStdString(deviceId));
        } else {
            std::cerr << "[LightingParameterWorker] Connection failed: " << deviceId << std::endl;
            emit connectionFailed(QString::fromStdString(deviceId), "Connection failed");
        }
    }
    catch (const std::exception& e) {
        std::cerr << "[LightingParameterWorker] Exception during connect: " << e.what() << std::endl;
        emit connectionFailed(QString::fromStdString(deviceId), QString::fromStdString(e.what()));
    }
}

void LightingParameterWorker::executeDisconnect(const std::string& deviceId)
{
    std::cout << "[LightingParameterWorker] Executing Disconnect: " << deviceId << std::endl;

    try {
        m_manager->Disconnect(deviceId);
        std::cout << "[LightingParameterWorker] Disconnected: " << deviceId << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "[LightingParameterWorker] Exception during disconnect: " << e.what() << std::endl;
    }

    emit disconnectionComplete(QString::fromStdString(deviceId));
}

void LightingParameterWorker::executeSetIntensity(const std::string& deviceId, int channel, int intensity)
{
    if (!m_manager->IsConnected(deviceId)) {
        emit commandError("SetIntensity", "Device not connected");
        return;
    }

    try {
        auto result = m_manager->SetIntensity(deviceId, channel, intensity);
        if (result == Lighting::LightingResult::Success) {
            // Save the new intensity as default for persistence
            m_manager->UpdateChannelDefaultIntensity(deviceId, channel, intensity);
            m_manager->SaveConfig();
            emit intensityApplied(channel, intensity);
        } else {
            emit commandError("SetIntensity",
                QString::fromStdString(Lighting::LightingResultToString(result)));
        }
    }
    catch (const std::exception& e) {
        emit commandError("SetIntensity", QString::fromStdString(e.what()));
    }
}

void LightingParameterWorker::executeSetOn(const std::string& deviceId, int channel, bool on)
{
    if (!m_manager->IsConnected(deviceId)) {
        emit commandError("SetOn", "Device not connected");
        return;
    }

    try {
        auto result = m_manager->SetOn(deviceId, channel, on);
        if (result == Lighting::LightingResult::Success) {
            emit channelToggled(channel, on);
        } else {
            emit commandError("SetOn",
                QString::fromStdString(Lighting::LightingResultToString(result)));
        }
    }
    catch (const std::exception& e) {
        emit commandError("SetOn", QString::fromStdString(e.what()));
    }
}

void LightingParameterWorker::executeAllOff(const std::string& deviceId)
{
    if (!m_manager->IsConnected(deviceId)) {
        emit commandError("AllOff", "Device not connected");
        return;
    }

    try {
        auto result = m_manager->SetAllOff(deviceId);
        if (result == Lighting::LightingResult::Success) {
            std::cout << "[LightingParameterWorker] All channels OFF" << std::endl;
            emit allOffComplete();
            emit commandSuccess("AllOff", "All channels turned OFF");
        } else {
            emit commandError("AllOff",
                QString::fromStdString(Lighting::LightingResultToString(result)));
        }
    }
    catch (const std::exception& e) {
        emit commandError("AllOff", QString::fromStdString(e.what()));
    }
}

void LightingParameterWorker::executeRefreshStatus(const std::string& deviceId)
{
    if (!m_manager->IsConnected(deviceId)) {
        return;
    }

    try {
        auto status = m_manager->GetControllerStatus(deviceId);

        std::vector<int> intensities;
        std::vector<bool> onStates;

        for (const auto& chStatus : status.channelStatuses) {
            intensities.push_back(chStatus.intensity);
            onStates.push_back(chStatus.isOn);
        }

        emit statusUpdated(QString::fromStdString(deviceId), intensities, onStates);
    }
    catch (const std::exception& e) {
        std::cerr << "[LightingParameterWorker] Status read error: " << e.what() << std::endl;
    }
}
