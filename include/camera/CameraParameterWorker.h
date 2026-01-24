// CameraParameterWorker.h
// Background worker for camera parameter updates to improve UI responsiveness
#pragma once

#include <QObject>
#include <QThread>
#include <QTimer>
#include <QMutex>
#include <atomic>
#include <string>

struct ApplicationContext;

/**
 * @brief Background worker for camera parameter changes
 *
 * Handles exposure, gain, and frame rate updates in a background thread
 * to prevent UI blocking during camera SDK calls.
 *
 * Features:
 * - Non-blocking parameter updates
 * - Request coalescing (multiple rapid changes -> single camera call)
 * - Read-back of actual camera values
 * - Auto mode polling for real-time value display
 */
class CameraParameterWorker : public QObject {
    Q_OBJECT

public:
    explicit CameraParameterWorker(ApplicationContext* context,
                                    const std::string& cameraId,
                                    QObject* parent = nullptr);
    ~CameraParameterWorker();

    // Thread management
    void start();
    void stop();
    bool isRunning() const { return m_running.load(); }

    // Set camera ID (for switching cameras)
    void setCameraId(const std::string& cameraId);

signals:
    // Signals to UI - actual values read from camera
    void exposureUpdated(int microseconds);
    void gainUpdated(double gain);
    void frameRateUpdated(double fps);
    void autoExposureChanged(bool enabled);
    void autoGainChanged(bool enabled);

    // Status signals
    void workerError(QString error);
    void settingsApplied();  // Emitted after successful apply

public slots:
    // Slots for UI requests - queue changes for background processing
    void requestExposure(int microseconds);
    void requestGain(double gain);
    void requestFrameRate(double fps);
    void requestAutoExposure(bool enabled);
    void requestAutoGain(bool enabled);

    // Request immediate read of all values
    void requestReadAll();

    // Auto mode polling control
    void setAutoPollingEnabled(bool enabled);
    void setAutoPollingInterval(int milliseconds);

private slots:
    void processQueue();      // Main processing loop
    void pollAutoValues();    // Poll values when in auto mode

private:
    // Apply settings to camera (called from worker thread)
    bool applyExposure(int microseconds);
    bool applyGain(double gain);
    bool applyFrameRate(double fps);
    bool applyAutoExposure(bool enabled);
    bool applyAutoGain(bool enabled);

    // Read current values from camera
    void readAllValues();
    int readExposure();
    double readGain();
    double readFrameRate();
    bool readAutoExposure();
    bool readAutoGain();

    ApplicationContext* m_context;
    std::string m_cameraId;

    // Worker thread
    QThread* m_workerThread;
    QTimer* m_processTimer;     // Timer for processing queue
    QTimer* m_pollTimer;        // Timer for auto mode polling
    std::atomic<bool> m_running;

    // Request queue with coalescing (atomic flags)
    QMutex m_queueMutex;
    std::atomic<bool> m_exposurePending;
    std::atomic<bool> m_gainPending;
    std::atomic<bool> m_frameRatePending;
    std::atomic<bool> m_autoExposurePending;
    std::atomic<bool> m_autoGainPending;
    std::atomic<bool> m_readAllPending;

    // Pending values (protected by mutex)
    int m_pendingExposure;
    double m_pendingGain;
    double m_pendingFrameRate;
    bool m_pendingAutoExposure;
    bool m_pendingAutoGain;

    // Current state (for auto polling)
    std::atomic<bool> m_isAutoExposure;
    std::atomic<bool> m_isAutoGain;
    std::atomic<bool> m_autoPollingEnabled;
    int m_autoPollingInterval;

    // Processing interval (ms)
    static constexpr int PROCESS_INTERVAL_MS = 50;
    static constexpr int DEFAULT_POLL_INTERVAL_MS = 500;
};
