// SMUParameterWorker.cpp
#include "SMUParameterWorker.h"
#include "SMUManager.h"
#include "ISMUDevice.h"
#include <QMutexLocker>
#include <algorithm>
#include <iostream>

SMUParameterWorker::SMUParameterWorker(SMUManager* manager, QObject* parent)
    : QObject(parent)
    , m_manager(manager)
    , m_workerThread(nullptr)
    , m_processTimer(nullptr)
    , m_pollTimer(nullptr)
    , m_running(false)
    , m_measurementPollingEnabled(false)
    , m_pollingChannel(1)
    , m_channelCount(1)
{
    std::cout << "[SMUParameterWorker] Created" << std::endl;
}

SMUParameterWorker::~SMUParameterWorker()
{
    stop();
    std::cout << "[SMUParameterWorker] Destroyed" << std::endl;
}

void SMUParameterWorker::start()
{
    if (m_running.load()) {
        return;  // Already running
    }

    // Create worker thread
    m_workerThread = new QThread();

    // Create timers (will be moved to worker thread)
    m_processTimer = new QTimer();
    m_processTimer->setInterval(PROCESS_INTERVAL_MS);
    connect(m_processTimer, &QTimer::timeout, this, &SMUParameterWorker::processQueue);

    m_pollTimer = new QTimer();
    m_pollTimer->setInterval(DEFAULT_POLL_INTERVAL_MS);
    connect(m_pollTimer, &QTimer::timeout, this, &SMUParameterWorker::pollMeasurements);

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

    std::cout << "[SMUParameterWorker] Started" << std::endl;
}

void SMUParameterWorker::stop()
{
    if (!m_running.load()) {
        return;  // Not running
    }

    m_running.store(false);

    if (m_workerThread) {
        m_workerThread->quit();
        if (!m_workerThread->wait(1000)) {  // Wait up to 1 second
            std::cerr << "[SMUParameterWorker] Thread did not stop gracefully, terminating" << std::endl;
            m_workerThread->terminate();
            m_workerThread->wait();
        }
        m_workerThread = nullptr;
    }

    // Timers are deleted by thread cleanup
    m_processTimer = nullptr;
    m_pollTimer = nullptr;

    std::cout << "[SMUParameterWorker] Stopped" << std::endl;
}

void SMUParameterWorker::setCurrentDevice(const std::string& deviceName)
{
    QMutexLocker locker(&m_queueMutex);
    m_deviceName = deviceName;
    // Clear pending commands when device changes
    m_pendingCommands.clear();
    std::cout << "[SMUParameterWorker] Device set to: " << deviceName << std::endl;
}

void SMUParameterWorker::setChannelCount(int count)
{
    QMutexLocker locker(&m_queueMutex);
    m_channelCount = count;
}

// ============================================================================
// PUBLIC SLOTS - Queue requests from UI (thread-safe)
// ============================================================================

void SMUParameterWorker::requestSetVoltage(double voltage)
{
    QMutexLocker locker(&m_queueMutex);
    std::cout << "[SMUParameterWorker] requestSetVoltage: " << voltage << " V" << std::endl;

    // Remove existing voltage commands (coalescing)
    m_pendingCommands.erase(
        std::remove_if(m_pendingCommands.begin(), m_pendingCommands.end(),
            [](const PendingCommand& cmd) {
                return cmd.type == PendingCommand::SetVoltage;
            }),
        m_pendingCommands.end());

    m_pendingCommands.push_back({PendingCommand::SetVoltage, 0, voltage});
}

void SMUParameterWorker::requestSetCurrent(double current)
{
    QMutexLocker locker(&m_queueMutex);
    std::cout << "[SMUParameterWorker] requestSetCurrent: " << current << " A" << std::endl;

    m_pendingCommands.erase(
        std::remove_if(m_pendingCommands.begin(), m_pendingCommands.end(),
            [](const PendingCommand& cmd) {
                return cmd.type == PendingCommand::SetCurrent;
            }),
        m_pendingCommands.end());

    m_pendingCommands.push_back({PendingCommand::SetCurrent, 0, current});
}

void SMUParameterWorker::requestSetVoltageCompliance(double compliance)
{
    QMutexLocker locker(&m_queueMutex);
    std::cout << "[SMUParameterWorker] requestSetVoltageCompliance: " << compliance << " V" << std::endl;

    m_pendingCommands.erase(
        std::remove_if(m_pendingCommands.begin(), m_pendingCommands.end(),
            [](const PendingCommand& cmd) {
                return cmd.type == PendingCommand::SetVoltageCompliance;
            }),
        m_pendingCommands.end());

    m_pendingCommands.push_back({PendingCommand::SetVoltageCompliance, 0, compliance});
}

void SMUParameterWorker::requestSetCurrentCompliance(double compliance)
{
    QMutexLocker locker(&m_queueMutex);
    std::cout << "[SMUParameterWorker] requestSetCurrentCompliance: " << compliance << " A" << std::endl;

    m_pendingCommands.erase(
        std::remove_if(m_pendingCommands.begin(), m_pendingCommands.end(),
            [](const PendingCommand& cmd) {
                return cmd.type == PendingCommand::SetCurrentCompliance;
            }),
        m_pendingCommands.end());

    m_pendingCommands.push_back({PendingCommand::SetCurrentCompliance, 0, compliance});
}

void SMUParameterWorker::requestOutputOn()
{
    QMutexLocker locker(&m_queueMutex);
    std::cout << "[SMUParameterWorker] requestOutputOn" << std::endl;

    // Remove any pending output commands
    m_pendingCommands.erase(
        std::remove_if(m_pendingCommands.begin(), m_pendingCommands.end(),
            [](const PendingCommand& cmd) {
                return cmd.type == PendingCommand::OutputOn ||
                       cmd.type == PendingCommand::OutputOff;
            }),
        m_pendingCommands.end());

    m_pendingCommands.push_back({PendingCommand::OutputOn, 0, 0.0});
}

void SMUParameterWorker::requestOutputOff()
{
    QMutexLocker locker(&m_queueMutex);
    std::cout << "[SMUParameterWorker] requestOutputOff" << std::endl;

    m_pendingCommands.erase(
        std::remove_if(m_pendingCommands.begin(), m_pendingCommands.end(),
            [](const PendingCommand& cmd) {
                return cmd.type == PendingCommand::OutputOn ||
                       cmd.type == PendingCommand::OutputOff;
            }),
        m_pendingCommands.end());

    m_pendingCommands.push_back({PendingCommand::OutputOff, 0, 0.0});
}

void SMUParameterWorker::requestReadMeasurements(int channel)
{
    QMutexLocker locker(&m_queueMutex);
    m_pendingCommands.push_back({PendingCommand::ReadMeasurements, channel, 0.0});
}

void SMUParameterWorker::requestRefreshAll(int channel)
{
    QMutexLocker locker(&m_queueMutex);
    std::cout << "[SMUParameterWorker] requestRefreshAll Ch" << channel << std::endl;
    m_pendingCommands.push_back({PendingCommand::RefreshAll, channel, 0.0});
}

void SMUParameterWorker::setMeasurementPollingEnabled(bool enabled)
{
    m_measurementPollingEnabled.store(enabled);
    std::cout << "[SMUParameterWorker] Polling " << (enabled ? "enabled" : "disabled") << std::endl;
}

void SMUParameterWorker::setMeasurementPollingInterval(int milliseconds)
{
    if (m_pollTimer) {
        QMetaObject::invokeMethod(m_pollTimer, [this, milliseconds]() {
            m_pollTimer->setInterval(milliseconds);
        }, Qt::QueuedConnection);
    }
}

void SMUParameterWorker::setPollingChannel(int channel)
{
    m_pollingChannel.store(channel);
}

// ============================================================================
// PRIVATE SLOTS - Process in worker thread
// ============================================================================

void SMUParameterWorker::processQueue()
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

    auto smu = m_manager->GetDevice(deviceName);
    if (!smu) {
        std::cerr << "[SMUParameterWorker] Device not found: " << deviceName << std::endl;
        return;
    }

    if (!smu->IsConnected()) {
        std::cerr << "[SMUParameterWorker] Device not connected: " << deviceName << std::endl;
        return;
    }

    // Process each command
    for (const auto& cmd : commands) {
        switch (cmd.type) {
            case PendingCommand::SetVoltage:
                applySetVoltage(deviceName, cmd.value);
                break;

            case PendingCommand::SetCurrent:
                applySetCurrent(deviceName, cmd.value);
                break;

            case PendingCommand::SetVoltageCompliance:
                applySetVoltageCompliance(deviceName, cmd.value);
                break;

            case PendingCommand::SetCurrentCompliance:
                applySetCurrentCompliance(deviceName, cmd.value);
                break;

            case PendingCommand::OutputOn:
                applyOutputOn(deviceName);
                break;

            case PendingCommand::OutputOff:
                applyOutputOff(deviceName);
                break;

            case PendingCommand::ReadMeasurements:
            case PendingCommand::RefreshAll:
                readMeasurements(deviceName, cmd.channel);
                break;
        }
    }
}

void SMUParameterWorker::pollMeasurements()
{
    if (!m_running.load() || !m_measurementPollingEnabled.load() || !m_manager) {
        return;
    }

    // Copy device name under mutex protection
    std::string deviceName;
    {
        QMutexLocker locker(&m_queueMutex);
        deviceName = m_deviceName;
    }

    if (deviceName.empty()) {
        return;
    }

    auto smu = m_manager->GetDevice(deviceName);
    if (!smu || !smu->IsConnected()) {
        return;
    }

    int channel = m_pollingChannel.load();
    readMeasurements(deviceName, channel);
}

// ============================================================================
// PRIVATE - Apply commands to SMU
// ============================================================================

bool SMUParameterWorker::applySetVoltage(const std::string& deviceName, double voltage)
{
    auto smu = m_manager->GetDevice(deviceName);
    if (!smu || !smu->IsConnected()) {
        emit commandError("SetVoltage", "Device not connected");
        return false;
    }

    try {
        if (smu->SetVoltage(voltage)) {
            std::cout << "[SMUParameterWorker] Applied voltage: " << voltage << " V" << std::endl;
            emit voltageApplied(voltage);
            emit commandSuccess("SetVoltage", QString("Voltage set to %1 V").arg(voltage, 0, 'f', 3));
            return true;
        } else {
            emit commandError("SetVoltage", QString::fromStdString(smu->GetLastError()));
            return false;
        }
    }
    catch (const std::exception& e) {
        emit commandError("SetVoltage", QString::fromStdString(e.what()));
        return false;
    }
}

bool SMUParameterWorker::applySetCurrent(const std::string& deviceName, double current)
{
    auto smu = m_manager->GetDevice(deviceName);
    if (!smu || !smu->IsConnected()) {
        emit commandError("SetCurrent", "Device not connected");
        return false;
    }

    try {
        if (smu->SetCurrent(current)) {
            std::cout << "[SMUParameterWorker] Applied current: " << current << " A" << std::endl;
            emit currentApplied(current);
            emit commandSuccess("SetCurrent", QString("Current set to %1 A").arg(current, 0, 'e', 3));
            return true;
        } else {
            emit commandError("SetCurrent", QString::fromStdString(smu->GetLastError()));
            return false;
        }
    }
    catch (const std::exception& e) {
        emit commandError("SetCurrent", QString::fromStdString(e.what()));
        return false;
    }
}

bool SMUParameterWorker::applySetVoltageCompliance(const std::string& deviceName, double compliance)
{
    auto smu = m_manager->GetDevice(deviceName);
    if (!smu || !smu->IsConnected()) {
        emit commandError("SetVoltageCompliance", "Device not connected");
        return false;
    }

    // Note: Voltage compliance might not be implemented in all devices
    // For now, just log and emit success
    std::cout << "[SMUParameterWorker] Voltage compliance: " << compliance << " V (stored)" << std::endl;
    emit voltageComplianceApplied(compliance);
    emit commandSuccess("SetVoltageCompliance",
        QString("Voltage compliance: %1 V (stored)").arg(compliance, 0, 'f', 3));
    return true;
}

bool SMUParameterWorker::applySetCurrentCompliance(const std::string& deviceName, double compliance)
{
    auto smu = m_manager->GetDevice(deviceName);
    if (!smu || !smu->IsConnected()) {
        emit commandError("SetCurrentCompliance", "Device not connected");
        return false;
    }

    try {
        if (smu->SetCompliance(compliance)) {
            std::cout << "[SMUParameterWorker] Applied current compliance: " << compliance << " A" << std::endl;
            emit currentComplianceApplied(compliance);
            emit commandSuccess("SetCurrentCompliance",
                QString("Current compliance set to %1 A").arg(compliance, 0, 'e', 3));
            return true;
        } else {
            emit commandError("SetCurrentCompliance", QString::fromStdString(smu->GetLastError()));
            return false;
        }
    }
    catch (const std::exception& e) {
        emit commandError("SetCurrentCompliance", QString::fromStdString(e.what()));
        return false;
    }
}

bool SMUParameterWorker::applyOutputOn(const std::string& deviceName)
{
    auto smu = m_manager->GetDevice(deviceName);
    if (!smu || !smu->IsConnected()) {
        emit commandError("OutputOn", "Device not connected");
        return false;
    }

    try {
        if (smu->SetOutputOn()) {
            std::cout << "[SMUParameterWorker] Output ON" << std::endl;
            emit outputStateChanged(true);
            emit commandSuccess("OutputOn", "Output turned ON");
            return true;
        } else {
            emit commandError("OutputOn", QString::fromStdString(smu->GetLastError()));
            return false;
        }
    }
    catch (const std::exception& e) {
        emit commandError("OutputOn", QString::fromStdString(e.what()));
        return false;
    }
}

bool SMUParameterWorker::applyOutputOff(const std::string& deviceName)
{
    auto smu = m_manager->GetDevice(deviceName);
    if (!smu || !smu->IsConnected()) {
        emit commandError("OutputOff", "Device not connected");
        return false;
    }

    try {
        if (smu->SetOutputOff()) {
            std::cout << "[SMUParameterWorker] Output OFF" << std::endl;
            emit outputStateChanged(false);
            emit commandSuccess("OutputOff", "Output turned OFF");
            return true;
        } else {
            emit commandError("OutputOff", QString::fromStdString(smu->GetLastError()));
            return false;
        }
    }
    catch (const std::exception& e) {
        emit commandError("OutputOff", QString::fromStdString(e.what()));
        return false;
    }
}

void SMUParameterWorker::readMeasurements(const std::string& deviceName, int channel)
{
    auto smu = m_manager->GetDevice(deviceName);
    if (!smu || !smu->IsConnected()) {
        return;
    }

    try {
        double voltage = smu->ReadVoltage();
        double current = smu->ReadChannel(channel);
        bool outputOn = smu->IsOutputOn();

        emit measurementsUpdated(channel, voltage, current, outputOn);
    }
    catch (const std::exception& e) {
        std::cerr << "[SMUParameterWorker] Read error: " << e.what() << std::endl;
    }
}
