// PSUParameterWorker.cpp
#include "PSUParameterWorker.h"
#include "PowerSupplyManager.h"
#include "IPowerSupply.h"
#include <QMutexLocker>
#include <algorithm>
#include <iostream>

PSUParameterWorker::PSUParameterWorker(PowerSupplyManager* manager, QObject* parent)
    : QObject(parent)
    , m_manager(manager)
    , m_workerThread(nullptr)
    , m_processTimer(nullptr)
    , m_pollTimer(nullptr)
    , m_running(false)
    , m_measurementPollingEnabled(true)
    , m_channelCount(1)
{
    std::cout << "[PSUParameterWorker] Created" << std::endl;
}

PSUParameterWorker::~PSUParameterWorker()
{
    stop();
    std::cout << "[PSUParameterWorker] Destroyed" << std::endl;
}

void PSUParameterWorker::start()
{
    if (m_running.load()) {
        return;  // Already running
    }

    // Create worker thread
    m_workerThread = new QThread();

    // Create timers (will be moved to worker thread)
    m_processTimer = new QTimer();
    m_processTimer->setInterval(PROCESS_INTERVAL_MS);
    connect(m_processTimer, &QTimer::timeout, this, &PSUParameterWorker::processQueue);

    m_pollTimer = new QTimer();
    m_pollTimer->setInterval(DEFAULT_POLL_INTERVAL_MS);
    connect(m_pollTimer, &QTimer::timeout, this, &PSUParameterWorker::pollMeasurements);

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

    std::cout << "[PSUParameterWorker] Started" << std::endl;
}

void PSUParameterWorker::stop()
{
    if (!m_running.load()) {
        return;  // Not running
    }

    m_running.store(false);

    if (m_workerThread) {
        m_workerThread->quit();
        if (!m_workerThread->wait(1000)) {  // Wait up to 1 second
            std::cerr << "[PSUParameterWorker] Thread did not stop gracefully, terminating" << std::endl;
            m_workerThread->terminate();
            m_workerThread->wait();
        }
        m_workerThread = nullptr;
    }

    // Timers are deleted by thread cleanup
    m_processTimer = nullptr;
    m_pollTimer = nullptr;

    std::cout << "[PSUParameterWorker] Stopped" << std::endl;
}

void PSUParameterWorker::setCurrentDevice(const std::string& deviceName)
{
    QMutexLocker locker(&m_queueMutex);
    m_deviceName = deviceName;
    // Clear pending commands when device changes
    m_pendingCommands.clear();
    std::cout << "[PSUParameterWorker] Device set to: " << deviceName
              << " (running=" << m_running.load() << ")" << std::endl;
}

void PSUParameterWorker::setChannelCount(int count)
{
    QMutexLocker locker(&m_queueMutex);
    m_channelCount = count;
}

// ============================================================================
// PUBLIC SLOTS - Queue requests from UI (thread-safe)
// ============================================================================

void PSUParameterWorker::requestSetVoltage(int channel, double voltage)
{
    QMutexLocker locker(&m_queueMutex);
    std::cout << "[PSUParameterWorker] requestSetVoltage Ch" << channel
              << ": " << voltage << " V (device=" << m_deviceName << ")" << std::endl;

    // Remove existing voltage command for this channel (coalescing)
    m_pendingCommands.erase(
        std::remove_if(m_pendingCommands.begin(), m_pendingCommands.end(),
            [channel](const PendingCommand& cmd) {
                return cmd.type == PendingCommand::SetVoltage && cmd.channel == channel;
            }),
        m_pendingCommands.end());

    // Add new command
    m_pendingCommands.push_back({PendingCommand::SetVoltage, channel, voltage});
}

void PSUParameterWorker::requestSetCurrent(int channel, double current)
{
    QMutexLocker locker(&m_queueMutex);

    // Remove existing current command for this channel (coalescing)
    m_pendingCommands.erase(
        std::remove_if(m_pendingCommands.begin(), m_pendingCommands.end(),
            [channel](const PendingCommand& cmd) {
                return cmd.type == PendingCommand::SetCurrent && cmd.channel == channel;
            }),
        m_pendingCommands.end());

    m_pendingCommands.push_back({PendingCommand::SetCurrent, channel, current});
}

void PSUParameterWorker::requestSetOVP(int channel, double ovp)
{
    QMutexLocker locker(&m_queueMutex);

    m_pendingCommands.erase(
        std::remove_if(m_pendingCommands.begin(), m_pendingCommands.end(),
            [channel](const PendingCommand& cmd) {
                return cmd.type == PendingCommand::SetOVP && cmd.channel == channel;
            }),
        m_pendingCommands.end());

    m_pendingCommands.push_back({PendingCommand::SetOVP, channel, ovp});
}

void PSUParameterWorker::requestSetOCP(int channel, double ocp)
{
    QMutexLocker locker(&m_queueMutex);

    m_pendingCommands.erase(
        std::remove_if(m_pendingCommands.begin(), m_pendingCommands.end(),
            [channel](const PendingCommand& cmd) {
                return cmd.type == PendingCommand::SetOCP && cmd.channel == channel;
            }),
        m_pendingCommands.end());

    m_pendingCommands.push_back({PendingCommand::SetOCP, channel, ocp});
}

void PSUParameterWorker::requestOutputOn(int channel)
{
    QMutexLocker locker(&m_queueMutex);
    std::cout << "[PSUParameterWorker] requestOutputOn Ch" << channel
              << " (device=" << m_deviceName << ")" << std::endl;

    // Remove any pending output commands for this channel
    m_pendingCommands.erase(
        std::remove_if(m_pendingCommands.begin(), m_pendingCommands.end(),
            [channel](const PendingCommand& cmd) {
                return (cmd.type == PendingCommand::OutputOn ||
                        cmd.type == PendingCommand::OutputOff) &&
                       cmd.channel == channel;
            }),
        m_pendingCommands.end());

    m_pendingCommands.push_back({PendingCommand::OutputOn, channel, 0.0});
}

void PSUParameterWorker::requestOutputOff(int channel)
{
    QMutexLocker locker(&m_queueMutex);
    std::cout << "[PSUParameterWorker] requestOutputOff Ch" << channel
              << " (device=" << m_deviceName << ")" << std::endl;

    m_pendingCommands.erase(
        std::remove_if(m_pendingCommands.begin(), m_pendingCommands.end(),
            [channel](const PendingCommand& cmd) {
                return (cmd.type == PendingCommand::OutputOn ||
                        cmd.type == PendingCommand::OutputOff) &&
                       cmd.channel == channel;
            }),
        m_pendingCommands.end());

    m_pendingCommands.push_back({PendingCommand::OutputOff, channel, 0.0});
}

void PSUParameterWorker::requestReadMeasurements(int channel)
{
    QMutexLocker locker(&m_queueMutex);
    m_pendingCommands.push_back({PendingCommand::ReadMeasurements, channel, 0.0});
}

void PSUParameterWorker::requestReadAllChannels()
{
    QMutexLocker locker(&m_queueMutex);
    // Add read request for all channels
    for (int ch = 1; ch <= m_channelCount; ++ch) {
        m_pendingCommands.push_back({PendingCommand::ReadMeasurements, ch, 0.0});
    }
}

void PSUParameterWorker::requestConnect()
{
    QMutexLocker locker(&m_queueMutex);
    m_pendingCommands.push_back({PendingCommand::Connect, 0, 0.0});
}

void PSUParameterWorker::requestDisconnect()
{
    QMutexLocker locker(&m_queueMutex);
    m_pendingCommands.push_back({PendingCommand::Disconnect, 0, 0.0});
}

void PSUParameterWorker::setMeasurementPollingEnabled(bool enabled)
{
    m_measurementPollingEnabled.store(enabled);
}

void PSUParameterWorker::setMeasurementPollingInterval(int milliseconds)
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

void PSUParameterWorker::processQueue()
{
    if (!m_running.load() || !m_manager) {
        return;
    }

    // Copy device name and queue under mutex protection
    std::string deviceName;
    std::vector<PendingCommand> commands;
    {
        QMutexLocker locker(&m_queueMutex);
        deviceName = m_deviceName;
        commands = std::move(m_pendingCommands);
        m_pendingCommands.clear();
    }

    if (deviceName.empty() || commands.empty()) {
        return;
    }

    std::cout << "[PSUParameterWorker] processQueue: " << commands.size()
              << " commands for device: " << deviceName << std::endl;

    auto psu = m_manager->getPowerSupply(deviceName);
    if (!psu) {
        std::cerr << "[PSUParameterWorker] Device not found: " << deviceName << std::endl;
        return;
    }

    // Process each command
    for (const auto& cmd : commands) {
        switch (cmd.type) {
            case PendingCommand::SetVoltage:
                applySetVoltage(deviceName, cmd.channel, cmd.value);
                break;

            case PendingCommand::SetCurrent:
                applySetCurrent(deviceName, cmd.channel, cmd.value);
                break;

            case PendingCommand::SetOVP:
                applySetOVP(deviceName, cmd.channel, cmd.value);
                break;

            case PendingCommand::SetOCP:
                applySetOCP(deviceName, cmd.channel, cmd.value);
                break;

            case PendingCommand::OutputOn:
                applyOutputOn(deviceName, cmd.channel);
                break;

            case PendingCommand::OutputOff:
                applyOutputOff(deviceName, cmd.channel);
                break;

            case PendingCommand::ReadMeasurements:
                readMeasurements(deviceName, cmd.channel);
                break;

            case PendingCommand::Connect:
                if (psu->connect()) {
                    emit deviceConnected(QString::fromStdString(deviceName));
                } else {
                    emit commandError(0, "Connect",
                        QString::fromStdString(psu->getLastError()));
                }
                break;

            case PendingCommand::Disconnect:
                psu->disconnect();
                emit deviceDisconnected(QString::fromStdString(deviceName));
                break;
        }
    }
}

void PSUParameterWorker::pollMeasurements()
{
    if (!m_running.load() || !m_measurementPollingEnabled.load() || !m_manager) {
        return;
    }

    // Copy device name under mutex protection
    std::string deviceName;
    int channelCount;
    {
        QMutexLocker locker(&m_queueMutex);
        deviceName = m_deviceName;
        channelCount = m_channelCount;
    }

    if (deviceName.empty()) {
        return;
    }

    auto psu = m_manager->getPowerSupply(deviceName);
    if (!psu || !psu->isConnected()) {
        return;
    }

    // Read all channels
    for (int ch = 1; ch <= channelCount; ++ch) {
        readMeasurements(deviceName, ch);
    }
}

// ============================================================================
// PRIVATE - Apply commands to PSU
// ============================================================================

bool PSUParameterWorker::applySetVoltage(const std::string& deviceName, int channel, double voltage)
{
    auto psu = m_manager->getPowerSupply(deviceName);
    if (!psu || !psu->isConnected()) {
        emit commandError(channel, "SetVoltage", "Device not connected");
        return false;
    }

    try {
        if (psu->setVoltage(voltage, channel)) {
            std::cout << "[PSUParameterWorker] Applied voltage Ch" << channel
                      << ": " << voltage << " V" << std::endl;
            emit voltageApplied(channel, voltage);
            return true;
        } else {
            emit commandError(channel, "SetVoltage",
                QString::fromStdString(psu->getLastError()));
            return false;
        }
    }
    catch (const std::exception& e) {
        emit commandError(channel, "SetVoltage", QString::fromStdString(e.what()));
        return false;
    }
}

bool PSUParameterWorker::applySetCurrent(const std::string& deviceName, int channel, double current)
{
    auto psu = m_manager->getPowerSupply(deviceName);
    if (!psu || !psu->isConnected()) {
        emit commandError(channel, "SetCurrent", "Device not connected");
        return false;
    }

    try {
        if (psu->setCurrent(current, channel)) {
            std::cout << "[PSUParameterWorker] Applied current Ch" << channel
                      << ": " << current << " A" << std::endl;
            emit currentApplied(channel, current);
            return true;
        } else {
            emit commandError(channel, "SetCurrent",
                QString::fromStdString(psu->getLastError()));
            return false;
        }
    }
    catch (const std::exception& e) {
        emit commandError(channel, "SetCurrent", QString::fromStdString(e.what()));
        return false;
    }
}

bool PSUParameterWorker::applySetOVP(const std::string& deviceName, int channel, double ovp)
{
    auto psu = m_manager->getPowerSupply(deviceName);
    if (!psu || !psu->isConnected()) {
        emit commandError(channel, "SetOVP", "Device not connected");
        return false;
    }

    try {
        if (psu->setOVP(ovp, channel)) {
            std::cout << "[PSUParameterWorker] Applied OVP Ch" << channel
                      << ": " << ovp << " V" << std::endl;
            emit ovpApplied(channel, ovp);
            return true;
        } else {
            emit commandError(channel, "SetOVP",
                QString::fromStdString(psu->getLastError()));
            return false;
        }
    }
    catch (const std::exception& e) {
        emit commandError(channel, "SetOVP", QString::fromStdString(e.what()));
        return false;
    }
}

bool PSUParameterWorker::applySetOCP(const std::string& deviceName, int channel, double ocp)
{
    auto psu = m_manager->getPowerSupply(deviceName);
    if (!psu || !psu->isConnected()) {
        emit commandError(channel, "SetOCP", "Device not connected");
        return false;
    }

    try {
        if (psu->setOCP(ocp, channel)) {
            std::cout << "[PSUParameterWorker] Applied OCP Ch" << channel
                      << ": " << ocp << " A" << std::endl;
            emit ocpApplied(channel, ocp);
            return true;
        } else {
            emit commandError(channel, "SetOCP",
                QString::fromStdString(psu->getLastError()));
            return false;
        }
    }
    catch (const std::exception& e) {
        emit commandError(channel, "SetOCP", QString::fromStdString(e.what()));
        return false;
    }
}

bool PSUParameterWorker::applyOutputOn(const std::string& deviceName, int channel)
{
    auto psu = m_manager->getPowerSupply(deviceName);
    if (!psu || !psu->isConnected()) {
        emit commandError(channel, "OutputOn", "Device not connected");
        return false;
    }

    try {
        if (psu->setOutputOn(channel)) {
            std::cout << "[PSUParameterWorker] Output ON Ch" << channel << std::endl;
            emit outputStateChanged(channel, true);
            return true;
        } else {
            emit commandError(channel, "OutputOn",
                QString::fromStdString(psu->getLastError()));
            return false;
        }
    }
    catch (const std::exception& e) {
        emit commandError(channel, "OutputOn", QString::fromStdString(e.what()));
        return false;
    }
}

bool PSUParameterWorker::applyOutputOff(const std::string& deviceName, int channel)
{
    auto psu = m_manager->getPowerSupply(deviceName);
    if (!psu || !psu->isConnected()) {
        emit commandError(channel, "OutputOff", "Device not connected");
        return false;
    }

    try {
        if (psu->setOutputOff(channel)) {
            std::cout << "[PSUParameterWorker] Output OFF Ch" << channel << std::endl;
            emit outputStateChanged(channel, false);
            return true;
        } else {
            emit commandError(channel, "OutputOff",
                QString::fromStdString(psu->getLastError()));
            return false;
        }
    }
    catch (const std::exception& e) {
        emit commandError(channel, "OutputOff", QString::fromStdString(e.what()));
        return false;
    }
}

void PSUParameterWorker::readMeasurements(const std::string& deviceName, int channel)
{
    auto psu = m_manager->getPowerSupply(deviceName);
    if (!psu || !psu->isConnected()) {
        return;
    }

    try {
        double voltage = psu->readVoltage(channel);
        double current = psu->readCurrent(channel);
        bool outputOn = psu->readOutputState(channel);

        emit measurementsUpdated(channel, voltage, current, outputOn);
    }
    catch (const std::exception& e) {
        std::cerr << "[PSUParameterWorker] Read error Ch" << channel
                  << ": " << e.what() << std::endl;
    }
}
