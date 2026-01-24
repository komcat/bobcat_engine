// CameraParameterWorker.cpp
#include "CameraParameterWorker.h"
#include "ApplicationContext.h"
#include "CameraManager.h"
#include "ICameraHardware.h"
#include <QMutexLocker>
#include <iostream>

CameraParameterWorker::CameraParameterWorker(ApplicationContext* context,
                                               const std::string& cameraId,
                                               QObject* parent)
    : QObject(parent)
    , m_context(context)
    , m_cameraId(cameraId)
    , m_workerThread(nullptr)
    , m_processTimer(nullptr)
    , m_pollTimer(nullptr)
    , m_running(false)
    , m_exposurePending(false)
    , m_gainPending(false)
    , m_frameRatePending(false)
    , m_autoExposurePending(false)
    , m_autoGainPending(false)
    , m_readAllPending(false)
    , m_pendingExposure(1000)
    , m_pendingGain(1.0)
    , m_pendingFrameRate(8.0)
    , m_pendingAutoExposure(false)
    , m_pendingAutoGain(false)
    , m_isAutoExposure(false)
    , m_isAutoGain(false)
    , m_autoPollingEnabled(true)
    , m_autoPollingInterval(DEFAULT_POLL_INTERVAL_MS)
{
    std::cout << "[CameraParameterWorker] Created for camera: " << m_cameraId << std::endl;
}

CameraParameterWorker::~CameraParameterWorker()
{
    stop();
    std::cout << "[CameraParameterWorker] Destroyed for camera: " << m_cameraId << std::endl;
}

void CameraParameterWorker::start()
{
    if (m_running.load()) {
        return;  // Already running
    }

    // Create worker thread
    m_workerThread = new QThread();

    // Create timers (will be moved to worker thread)
    m_processTimer = new QTimer();
    m_processTimer->setInterval(PROCESS_INTERVAL_MS);
    connect(m_processTimer, &QTimer::timeout, this, &CameraParameterWorker::processQueue);

    m_pollTimer = new QTimer();
    m_pollTimer->setInterval(m_autoPollingInterval);
    connect(m_pollTimer, &QTimer::timeout, this, &CameraParameterWorker::pollAutoValues);

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

    std::cout << "[CameraParameterWorker] Started for camera: " << m_cameraId << std::endl;
}

void CameraParameterWorker::stop()
{
    if (!m_running.load()) {
        return;  // Not running
    }

    m_running.store(false);

    if (m_workerThread) {
        m_workerThread->quit();
        if (!m_workerThread->wait(1000)) {  // Wait up to 1 second
            std::cerr << "[CameraParameterWorker] Thread did not stop gracefully, terminating" << std::endl;
            m_workerThread->terminate();
            m_workerThread->wait();
        }
        m_workerThread = nullptr;
    }

    // Timers are deleted by thread cleanup
    m_processTimer = nullptr;
    m_pollTimer = nullptr;

    std::cout << "[CameraParameterWorker] Stopped for camera: " << m_cameraId << std::endl;
}

void CameraParameterWorker::setCameraId(const std::string& cameraId)
{
    QMutexLocker locker(&m_queueMutex);
    m_cameraId = cameraId;
    std::cout << "[CameraParameterWorker] Camera ID changed to: " << m_cameraId << std::endl;
}

// ============================================================================
// PUBLIC SLOTS - Queue requests from UI (thread-safe)
// ============================================================================

void CameraParameterWorker::requestExposure(int microseconds)
{
    QMutexLocker locker(&m_queueMutex);
    m_pendingExposure = microseconds;
    m_exposurePending.store(true);
}

void CameraParameterWorker::requestGain(double gain)
{
    QMutexLocker locker(&m_queueMutex);
    m_pendingGain = gain;
    m_gainPending.store(true);
}

void CameraParameterWorker::requestFrameRate(double fps)
{
    QMutexLocker locker(&m_queueMutex);
    m_pendingFrameRate = fps;
    m_frameRatePending.store(true);
}

void CameraParameterWorker::requestAutoExposure(bool enabled)
{
    QMutexLocker locker(&m_queueMutex);
    m_pendingAutoExposure = enabled;
    m_autoExposurePending.store(true);
}

void CameraParameterWorker::requestAutoGain(bool enabled)
{
    QMutexLocker locker(&m_queueMutex);
    m_pendingAutoGain = enabled;
    m_autoGainPending.store(true);
}

void CameraParameterWorker::requestReadAll()
{
    m_readAllPending.store(true);
}

void CameraParameterWorker::setAutoPollingEnabled(bool enabled)
{
    m_autoPollingEnabled.store(enabled);
}

void CameraParameterWorker::setAutoPollingInterval(int milliseconds)
{
    m_autoPollingInterval = milliseconds;
    if (m_pollTimer) {
        // Use invokeMethod for thread safety
        QMetaObject::invokeMethod(m_pollTimer, [this, milliseconds]() {
            m_pollTimer->setInterval(milliseconds);
        }, Qt::QueuedConnection);
    }
}

// ============================================================================
// PRIVATE SLOTS - Process in worker thread
// ============================================================================

void CameraParameterWorker::processQueue()
{
    if (!m_running.load() || !m_context || !m_context->cameraManager) {
        return;
    }

    // Check if camera is connected
    auto status = m_context->cameraManager->GetCameraStatus(m_cameraId);
    if (!status.connected) {
        return;
    }

    bool anyApplied = false;

    // Process auto exposure first (affects whether exposure value applies)
    if (m_autoExposurePending.exchange(false)) {
        bool enabled;
        {
            QMutexLocker locker(&m_queueMutex);
            enabled = m_pendingAutoExposure;
        }
        if (applyAutoExposure(enabled)) {
            m_isAutoExposure.store(enabled);
            emit autoExposureChanged(enabled);
            anyApplied = true;
        }
    }

    // Process auto gain
    if (m_autoGainPending.exchange(false)) {
        bool enabled;
        {
            QMutexLocker locker(&m_queueMutex);
            enabled = m_pendingAutoGain;
        }
        if (applyAutoGain(enabled)) {
            m_isAutoGain.store(enabled);
            emit autoGainChanged(enabled);
            anyApplied = true;
        }
    }

    // Process exposure (only if not auto)
    if (m_exposurePending.exchange(false)) {
        if (!m_isAutoExposure.load()) {
            int value;
            {
                QMutexLocker locker(&m_queueMutex);
                value = m_pendingExposure;
            }
            if (applyExposure(value)) {
                // Read back actual value
                int actual = readExposure();
                emit exposureUpdated(actual);
                anyApplied = true;
            }
        }
    }

    // Process gain (only if not auto)
    if (m_gainPending.exchange(false)) {
        if (!m_isAutoGain.load()) {
            double value;
            {
                QMutexLocker locker(&m_queueMutex);
                value = m_pendingGain;
            }
            if (applyGain(value)) {
                // Read back actual value
                double actual = readGain();
                emit gainUpdated(actual);
                anyApplied = true;
            }
        }
    }

    // Process frame rate
    if (m_frameRatePending.exchange(false)) {
        double value;
        {
            QMutexLocker locker(&m_queueMutex);
            value = m_pendingFrameRate;
        }
        if (applyFrameRate(value)) {
            // Read back actual value
            double actual = readFrameRate();
            emit frameRateUpdated(actual);
            anyApplied = true;
        }
    }

    // Process read-all request
    if (m_readAllPending.exchange(false)) {
        readAllValues();
    }

    if (anyApplied) {
        emit settingsApplied();
    }
}

void CameraParameterWorker::pollAutoValues()
{
    if (!m_running.load() || !m_autoPollingEnabled.load()) {
        return;
    }

    if (!m_context || !m_context->cameraManager) {
        return;
    }

    // Check if camera is connected
    auto status = m_context->cameraManager->GetCameraStatus(m_cameraId);
    if (!status.connected) {
        return;
    }

    // Poll exposure if in auto mode
    if (m_isAutoExposure.load()) {
        int exposure = readExposure();
        if (exposure > 0) {
            emit exposureUpdated(exposure);
        }
    }

    // Poll gain if in auto mode
    if (m_isAutoGain.load()) {
        double gain = readGain();
        if (gain >= 0) {
            emit gainUpdated(gain);
        }
    }
}

// ============================================================================
// PRIVATE - Apply settings to camera
// ============================================================================

bool CameraParameterWorker::applyExposure(int microseconds)
{
    if (!m_context || !m_context->cameraManager) {
        return false;
    }

    try {
        auto settings = m_context->cameraManager->GetExposureSettings(m_cameraId);
        settings.exposure_time = static_cast<double>(microseconds);

        bool result = m_context->cameraManager->ApplyExposureSettings(m_cameraId, settings);
        if (result) {
            std::cout << "[CameraParameterWorker] Applied exposure: " << microseconds << " us" << std::endl;
        }
        return result;
    }
    catch (const std::exception& e) {
        emit workerError(QString("Exposure error: %1").arg(e.what()));
        return false;
    }
}

bool CameraParameterWorker::applyGain(double gain)
{
    if (!m_context || !m_context->cameraManager) {
        return false;
    }

    try {
        auto settings = m_context->cameraManager->GetExposureSettings(m_cameraId);
        settings.gain = gain;

        bool result = m_context->cameraManager->ApplyExposureSettings(m_cameraId, settings);
        if (result) {
            std::cout << "[CameraParameterWorker] Applied gain: " << gain << std::endl;
        }
        return result;
    }
    catch (const std::exception& e) {
        emit workerError(QString("Gain error: %1").arg(e.what()));
        return false;
    }
}

bool CameraParameterWorker::applyFrameRate(double fps)
{
    if (!m_context || !m_context->cameraManager) {
        return false;
    }

    try {
        auto settings = m_context->cameraManager->GetExposureSettings(m_cameraId);
        settings.frame_rate = fps;

        bool result = m_context->cameraManager->ApplyExposureSettings(m_cameraId, settings);
        if (result) {
            std::cout << "[CameraParameterWorker] Applied frame rate: " << fps << " fps" << std::endl;
        }
        return result;
    }
    catch (const std::exception& e) {
        emit workerError(QString("Frame rate error: %1").arg(e.what()));
        return false;
    }
}

bool CameraParameterWorker::applyAutoExposure(bool enabled)
{
    if (!m_context || !m_context->cameraManager) {
        return false;
    }

    try {
        auto settings = m_context->cameraManager->GetExposureSettings(m_cameraId);
        settings.auto_exposure = enabled;

        bool result = m_context->cameraManager->ApplyExposureSettings(m_cameraId, settings);
        if (result) {
            std::cout << "[CameraParameterWorker] Applied auto exposure: " << (enabled ? "ON" : "OFF") << std::endl;
        }
        return result;
    }
    catch (const std::exception& e) {
        emit workerError(QString("Auto exposure error: %1").arg(e.what()));
        return false;
    }
}

bool CameraParameterWorker::applyAutoGain(bool enabled)
{
    if (!m_context || !m_context->cameraManager) {
        return false;
    }

    try {
        auto settings = m_context->cameraManager->GetExposureSettings(m_cameraId);
        settings.auto_gain = enabled;

        bool result = m_context->cameraManager->ApplyExposureSettings(m_cameraId, settings);
        if (result) {
            std::cout << "[CameraParameterWorker] Applied auto gain: " << (enabled ? "ON" : "OFF") << std::endl;
        }
        return result;
    }
    catch (const std::exception& e) {
        emit workerError(QString("Auto gain error: %1").arg(e.what()));
        return false;
    }
}

// ============================================================================
// PRIVATE - Read values from camera
// ============================================================================

void CameraParameterWorker::readAllValues()
{
    if (!m_context || !m_context->cameraManager) {
        return;
    }

    auto status = m_context->cameraManager->GetCameraStatus(m_cameraId);
    if (!status.connected) {
        return;
    }

    auto settings = m_context->cameraManager->GetExposureSettings(m_cameraId);

    m_isAutoExposure.store(settings.auto_exposure);
    m_isAutoGain.store(settings.auto_gain);

    emit exposureUpdated(static_cast<int>(settings.exposure_time));
    emit gainUpdated(settings.gain);
    emit frameRateUpdated(settings.frame_rate);
    emit autoExposureChanged(settings.auto_exposure);
    emit autoGainChanged(settings.auto_gain);

    std::cout << "[CameraParameterWorker] Read all values: "
              << "Exposure=" << settings.exposure_time << "us, "
              << "Gain=" << settings.gain << ", "
              << "FPS=" << settings.frame_rate << ", "
              << "AutoExp=" << (settings.auto_exposure ? "ON" : "OFF") << ", "
              << "AutoGain=" << (settings.auto_gain ? "ON" : "OFF") << std::endl;
}

int CameraParameterWorker::readExposure()
{
    if (!m_context || !m_context->cameraManager) {
        return -1;
    }

    try {
        auto settings = m_context->cameraManager->GetExposureSettings(m_cameraId);
        return static_cast<int>(settings.exposure_time);
    }
    catch (...) {
        return -1;
    }
}

double CameraParameterWorker::readGain()
{
    if (!m_context || !m_context->cameraManager) {
        return -1.0;
    }

    try {
        auto settings = m_context->cameraManager->GetExposureSettings(m_cameraId);
        return settings.gain;
    }
    catch (...) {
        return -1.0;
    }
}

double CameraParameterWorker::readFrameRate()
{
    if (!m_context || !m_context->cameraManager) {
        return -1.0;
    }

    try {
        auto settings = m_context->cameraManager->GetExposureSettings(m_cameraId);
        return settings.frame_rate;
    }
    catch (...) {
        return -1.0;
    }
}

bool CameraParameterWorker::readAutoExposure()
{
    if (!m_context || !m_context->cameraManager) {
        return false;
    }

    try {
        auto settings = m_context->cameraManager->GetExposureSettings(m_cameraId);
        return settings.auto_exposure;
    }
    catch (...) {
        return false;
    }
}

bool CameraParameterWorker::readAutoGain()
{
    if (!m_context || !m_context->cameraManager) {
        return false;
    }

    try {
        auto settings = m_context->cameraManager->GetExposureSettings(m_cameraId);
        return settings.auto_gain;
    }
    catch (...) {
        return false;
    }
}
