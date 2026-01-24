// TECParameterWorker.cpp
#include "TECParameterWorker.h"
#include "TECManager.h"
#include "TECTypes.h"
#include <QMutexLocker>
#include <algorithm>
#include <iostream>

TECParameterWorker::TECParameterWorker(TECManager* manager, QObject* parent)
    : QObject(parent)
    , m_manager(manager)
    , m_workerThread(nullptr)
    , m_processTimer(nullptr)
    , m_pollTimer(nullptr)
    , m_running(false)
    , m_statusPollingEnabled(false)
{
    std::cout << "[TECParameterWorker] Created" << std::endl;
}

TECParameterWorker::~TECParameterWorker()
{
    stop();
    std::cout << "[TECParameterWorker] Destroyed" << std::endl;
}

void TECParameterWorker::start()
{
    if (m_running.load()) {
        return;  // Already running
    }

    // Create worker thread
    m_workerThread = new QThread();

    // Create timers (will be moved to worker thread)
    m_processTimer = new QTimer();
    m_processTimer->setInterval(PROCESS_INTERVAL_MS);
    connect(m_processTimer, &QTimer::timeout, this, &TECParameterWorker::processQueue);

    m_pollTimer = new QTimer();
    m_pollTimer->setInterval(DEFAULT_POLL_INTERVAL_MS);
    connect(m_pollTimer, &QTimer::timeout, this, &TECParameterWorker::pollStatus);

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

    std::cout << "[TECParameterWorker] Started" << std::endl;
}

void TECParameterWorker::stop()
{
    if (!m_running.load()) {
        return;  // Not running
    }

    m_running.store(false);

    if (m_workerThread) {
        m_workerThread->quit();
        if (!m_workerThread->wait(1000)) {  // Wait up to 1 second
            std::cerr << "[TECParameterWorker] Thread did not stop gracefully, terminating" << std::endl;
            m_workerThread->terminate();
            m_workerThread->wait();
        }
        m_workerThread = nullptr;
    }

    m_processTimer = nullptr;
    m_pollTimer = nullptr;

    std::cout << "[TECParameterWorker] Stopped" << std::endl;
}

void TECParameterWorker::setCurrentDevice(const std::string& deviceName)
{
    QMutexLocker locker(&m_queueMutex);
    m_currentDeviceName = deviceName;
    // Clear pending commands when device changes
    m_pendingCommands.clear();
    std::cout << "[TECParameterWorker] Device set to: " << deviceName << std::endl;
}

// ============================================================================
// PUBLIC SLOTS - Queue requests from UI (thread-safe)
// ============================================================================

void TECParameterWorker::requestSetTemperature(double temperature)
{
    QMutexLocker locker(&m_queueMutex);

    // Coalesce: Remove existing temperature commands
    m_pendingCommands.erase(
        std::remove_if(m_pendingCommands.begin(), m_pendingCommands.end(),
            [](const PendingCommand& cmd) {
                return cmd.type == PendingCommand::SetTemperature;
            }),
        m_pendingCommands.end());

    PendingCommand cmd;
    cmd.type = PendingCommand::SetTemperature;
    cmd.value1 = temperature;
    m_pendingCommands.push_back(cmd);
}

void TECParameterWorker::requestTECOn()
{
    QMutexLocker locker(&m_queueMutex);

    // Remove any pending TEC on/off commands
    m_pendingCommands.erase(
        std::remove_if(m_pendingCommands.begin(), m_pendingCommands.end(),
            [](const PendingCommand& cmd) {
                return cmd.type == PendingCommand::TECOn ||
                       cmd.type == PendingCommand::TECOff;
            }),
        m_pendingCommands.end());

    PendingCommand cmd;
    cmd.type = PendingCommand::TECOn;
    m_pendingCommands.push_back(cmd);
}

void TECParameterWorker::requestTECOff()
{
    QMutexLocker locker(&m_queueMutex);

    m_pendingCommands.erase(
        std::remove_if(m_pendingCommands.begin(), m_pendingCommands.end(),
            [](const PendingCommand& cmd) {
                return cmd.type == PendingCommand::TECOn ||
                       cmd.type == PendingCommand::TECOff;
            }),
        m_pendingCommands.end());

    PendingCommand cmd;
    cmd.type = PendingCommand::TECOff;
    m_pendingCommands.push_back(cmd);
}

void TECParameterWorker::requestSetLaserCurrent(double currentMA)
{
    QMutexLocker locker(&m_queueMutex);

    // Coalesce: Remove existing laser current commands
    m_pendingCommands.erase(
        std::remove_if(m_pendingCommands.begin(), m_pendingCommands.end(),
            [](const PendingCommand& cmd) {
                return cmd.type == PendingCommand::SetLaserCurrent;
            }),
        m_pendingCommands.end());

    PendingCommand cmd;
    cmd.type = PendingCommand::SetLaserCurrent;
    cmd.value1 = currentMA;
    m_pendingCommands.push_back(cmd);
}

void TECParameterWorker::requestLaserOn()
{
    QMutexLocker locker(&m_queueMutex);

    m_pendingCommands.erase(
        std::remove_if(m_pendingCommands.begin(), m_pendingCommands.end(),
            [](const PendingCommand& cmd) {
                return cmd.type == PendingCommand::LaserOn ||
                       cmd.type == PendingCommand::LaserOff;
            }),
        m_pendingCommands.end());

    PendingCommand cmd;
    cmd.type = PendingCommand::LaserOn;
    m_pendingCommands.push_back(cmd);
}

void TECParameterWorker::requestLaserOff()
{
    QMutexLocker locker(&m_queueMutex);

    m_pendingCommands.erase(
        std::remove_if(m_pendingCommands.begin(), m_pendingCommands.end(),
            [](const PendingCommand& cmd) {
                return cmd.type == PendingCommand::LaserOn ||
                       cmd.type == PendingCommand::LaserOff;
            }),
        m_pendingCommands.end());

    PendingCommand cmd;
    cmd.type = PendingCommand::LaserOff;
    m_pendingCommands.push_back(cmd);
}

void TECParameterWorker::requestApplyPreset(double temperature, double laserCurrentMA, bool tecOn, bool laserOn)
{
    QMutexLocker locker(&m_queueMutex);

    std::cout << "[TECParameterWorker] requestApplyPreset: " << temperature << "C, "
              << laserCurrentMA << "mA, TEC:" << (tecOn ? "ON" : "OFF")
              << ", Laser:" << (laserOn ? "ON" : "OFF") << std::endl;

    // Clear all pending parameter commands (preset overrides everything)
    m_pendingCommands.erase(
        std::remove_if(m_pendingCommands.begin(), m_pendingCommands.end(),
            [](const PendingCommand& cmd) {
                return cmd.type != PendingCommand::RefreshStatus;
            }),
        m_pendingCommands.end());

    PendingCommand cmd;
    cmd.type = PendingCommand::ApplyPreset;
    cmd.value1 = temperature;
    cmd.value2 = laserCurrentMA;
    cmd.boolValue1 = tecOn;
    cmd.boolValue2 = laserOn;
    m_pendingCommands.push_back(cmd);
}

void TECParameterWorker::requestRefreshStatus()
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
    m_pendingCommands.push_back(cmd);
}

void TECParameterWorker::setStatusPollingEnabled(bool enabled)
{
    m_statusPollingEnabled.store(enabled);
    std::cout << "[TECParameterWorker] Polling " << (enabled ? "enabled" : "disabled") << std::endl;
}

void TECParameterWorker::setStatusPollingInterval(int milliseconds)
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

void TECParameterWorker::processQueue()
{
    if (!m_running.load() || !m_manager) {
        return;
    }

    // Copy device name and queue under mutex protection
    std::string deviceName;
    std::vector<PendingCommand> commands;
    {
        QMutexLocker locker(&m_queueMutex);
        deviceName = m_currentDeviceName;
        commands = std::move(m_pendingCommands);
        m_pendingCommands.clear();
    }

    if (deviceName.empty() || commands.empty()) {
        return;
    }

    // Check if device is connected
    TECStatus status = m_manager->GetDeviceStatus(deviceName);
    if (!status.Connected) {
        return;
    }

    // Process each command
    for (const auto& cmd : commands) {
        switch (cmd.type) {
            case PendingCommand::SetTemperature:
                executeSetTemperature(deviceName, cmd.value1);
                break;

            case PendingCommand::TECOn:
                executeTECOn(deviceName);
                break;

            case PendingCommand::TECOff:
                executeTECOff(deviceName);
                break;

            case PendingCommand::SetLaserCurrent:
                executeSetLaserCurrent(deviceName, cmd.value1);
                break;

            case PendingCommand::LaserOn:
                executeLaserOn(deviceName);
                break;

            case PendingCommand::LaserOff:
                executeLaserOff(deviceName);
                break;

            case PendingCommand::ApplyPreset:
                executeApplyPreset(deviceName, cmd.value1, cmd.value2, cmd.boolValue1, cmd.boolValue2);
                break;

            case PendingCommand::RefreshStatus:
                executeRefreshStatus(deviceName);
                break;
        }
    }
}

void TECParameterWorker::pollStatus()
{
    if (!m_running.load() || !m_statusPollingEnabled.load() || !m_manager) {
        return;
    }

    std::string deviceName;
    {
        QMutexLocker locker(&m_queueMutex);
        deviceName = m_currentDeviceName;
    }

    if (deviceName.empty()) {
        return;
    }

    TECStatus status = m_manager->GetDeviceStatus(deviceName);
    if (!status.Connected) {
        return;
    }

    executeRefreshStatus(deviceName);
}

// ============================================================================
// PRIVATE - Execute commands
// ============================================================================

void TECParameterWorker::executeSetTemperature(const std::string& deviceName, double temperature)
{
    try {
        auto result = m_manager->SetTemperature(deviceName, temperature);
        if (result.Success) {
            emit temperatureApplied(temperature);
        } else {
            emit commandError("SetTemperature", QString::fromStdString(result.Message));
        }
    }
    catch (const std::exception& e) {
        emit commandError("SetTemperature", QString::fromStdString(e.what()));
    }
}

void TECParameterWorker::executeTECOn(const std::string& deviceName)
{
    std::cout << "[TECParameterWorker] Executing TEC ON" << std::endl;

    try {
        auto result = m_manager->TEC_On(deviceName);
        if (result.Success) {
            emit tecStateChanged(true);
            emit commandSuccess("TECOn", "TEC turned ON");
        } else {
            emit commandError("TECOn", QString::fromStdString(result.Message));
        }
    }
    catch (const std::exception& e) {
        emit commandError("TECOn", QString::fromStdString(e.what()));
    }
}

void TECParameterWorker::executeTECOff(const std::string& deviceName)
{
    std::cout << "[TECParameterWorker] Executing TEC OFF" << std::endl;

    try {
        auto result = m_manager->TEC_Off(deviceName);
        if (result.Success) {
            emit tecStateChanged(false);
            emit commandSuccess("TECOff", "TEC turned OFF");
        } else {
            emit commandError("TECOff", QString::fromStdString(result.Message));
        }
    }
    catch (const std::exception& e) {
        emit commandError("TECOff", QString::fromStdString(e.what()));
    }
}

void TECParameterWorker::executeSetLaserCurrent(const std::string& deviceName, double currentMA)
{
    try {
        auto result = m_manager->SetLaserCurrent(deviceName, currentMA);
        if (result.Success) {
            emit laserCurrentApplied(currentMA);
        } else {
            emit commandError("SetLaserCurrent", QString::fromStdString(result.Message));
        }
    }
    catch (const std::exception& e) {
        emit commandError("SetLaserCurrent", QString::fromStdString(e.what()));
    }
}

void TECParameterWorker::executeLaserOn(const std::string& deviceName)
{
    std::cout << "[TECParameterWorker] Executing Laser ON" << std::endl;

    try {
        auto result = m_manager->Laser_On(deviceName);
        if (result.Success) {
            emit laserStateChanged(true);
            emit commandSuccess("LaserOn", "Laser turned ON");
        } else {
            emit commandError("LaserOn", QString::fromStdString(result.Message));
        }
    }
    catch (const std::exception& e) {
        emit commandError("LaserOn", QString::fromStdString(e.what()));
    }
}

void TECParameterWorker::executeLaserOff(const std::string& deviceName)
{
    std::cout << "[TECParameterWorker] Executing Laser OFF" << std::endl;

    try {
        auto result = m_manager->Laser_Off(deviceName);
        if (result.Success) {
            emit laserStateChanged(false);
            emit commandSuccess("LaserOff", "Laser turned OFF");
        } else {
            emit commandError("LaserOff", QString::fromStdString(result.Message));
        }
    }
    catch (const std::exception& e) {
        emit commandError("LaserOff", QString::fromStdString(e.what()));
    }
}

void TECParameterWorker::executeApplyPreset(const std::string& deviceName, double temperature, double laserCurrentMA, bool tecOn, bool laserOn)
{
    std::cout << "[TECParameterWorker] Applying preset: " << temperature << "C, "
              << laserCurrentMA << "mA, TEC:" << (tecOn ? "ON" : "OFF")
              << ", Laser:" << (laserOn ? "ON" : "OFF") << std::endl;

    // Set temperature
    executeSetTemperature(deviceName, temperature);

    // Set laser current
    executeSetLaserCurrent(deviceName, laserCurrentMA);

    // Set TEC state
    if (tecOn) {
        executeTECOn(deviceName);
    } else {
        executeTECOff(deviceName);
    }

    // Set laser state
    if (laserOn) {
        executeLaserOn(deviceName);
    } else {
        executeLaserOff(deviceName);
    }

    emit commandSuccess("ApplyPreset", "Preset applied");
}

void TECParameterWorker::executeRefreshStatus(const std::string& deviceName)
{
    try {
        // Read current temperature
        auto tempResult = m_manager->ReadTemperature(deviceName);
        double currentTemp = tempResult.Success ? tempResult.Value : -999.0;

        // Read TEC current
        auto tecCurrentResult = m_manager->ReadTECCurrent(deviceName);
        double tecCurrent = tecCurrentResult.Success ? tecCurrentResult.Value : -999.0;

        // Read laser current
        auto laserCurrentResult = m_manager->ReadLaserCurrent(deviceName);
        double laserCurrent = laserCurrentResult.Success ? laserCurrentResult.Value : -999.0;

        // Get status for on/off states
        TECStatus status = m_manager->GetDeviceStatus(deviceName);

        emit statusUpdated(currentTemp, tecCurrent, laserCurrent, status.TECOn, status.LaserOn);
    }
    catch (const std::exception& e) {
        std::cerr << "[TECParameterWorker] Status read error: " << e.what() << std::endl;
    }
}
