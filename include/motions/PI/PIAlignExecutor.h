// PIAlignExecutor.h
// Background worker for PI FSM (Fast Scan Maximum) operations
// Ensures thread-safe execution off the UI thread
#pragma once

#include <QObject>
#include <QThread>
#include <QMutex>
#include <atomic>
#include <string>

class PIController;
class UnifiedMotionManager;

/**
 * @brief FSM (Fast Scan Maximum) alignment parameters
 * FSM scans a 2D area to find the global maximum intensity of an analog input signal
 */
struct FSMParameters {
    std::string axis1 = "X";      // Axis for line-to-line movement
    double length1 = 0.020;       // Scan length along axis1 (mm)
    std::string axis2 = "Y";      // Axis for scanning lines
    double length2 = 0.020;       // Scan length along axis2 (mm)
    double threshold = 0.01;      // Intensity threshold (V)
    double distance = 0.001;      // Distance between scanning lines (mm)
    int analogChannel = 1;        // Analog input channel (1-6)

    // Z-iteration parameters (for 3D search)
    bool zScanEnabled = false;    // Enable Z-iteration mode
    int zDirection = -1;          // +1 for Z+, -1 for Z- (default Z-)
    double zRange = 0.005;        // Total Z distance to scan (mm)
    double zStep = 0.001;         // Z step between iterations (mm)
};

// Forward declaration
class PIAlignWorker;

/**
 * @brief Background executor for PI Fast Scan Maximum (FSM) operations
 *
 * Handles FSM command execution in a background thread to prevent UI blocking.
 * Polls scan status and emits signals for UI updates.
 *
 * Usage:
 *   1. Create executor with motion manager
 *   2. Connect signals to UI slots
 *   3. Call startAlignment() with device name and parameters
 *   4. Handle alignmentCompleted/alignmentFailed signals
 */
class PIAlignExecutor : public QObject {
    Q_OBJECT

public:
    explicit PIAlignExecutor(UnifiedMotionManager* motionManager, QObject* parent = nullptr);
    ~PIAlignExecutor();

    // Check if alignment is currently running
    bool isRunning() const { return m_running.load(); }

    // Get current device being aligned
    std::string currentDevice() const;

signals:
    // Alignment lifecycle signals
    void alignmentStarted(QString deviceName, int channel);
    void alignmentProgress(QString deviceName, int status);  // status from GetFastScanStatus
    void alignmentCompleted(QString deviceName, int channel);
    void alignmentFailed(QString deviceName, int channel, QString errorMessage);

    // Z-scan specific progress (iteration, total, currentZ, peakZ, peakVoltage)
    void zScanProgress(QString deviceName, int iteration, int total, double currentZ, double peakZ, double peakVoltage);

    // Status change for button state management
    void busyStateChanged(bool busy);

public slots:
    /**
     * @brief Start FSM alignment on specified device
     * @param deviceName PI controller device name
     * @param params FSM parameters
     */
    void startAlignment(const QString& deviceName, const FSMParameters& params);

    /**
     * @brief Request to stop/abort current alignment
     * Note: FSM may not be stoppable mid-scan on all controllers
     */
    void requestStop();

private slots:
    // Handlers for worker signals
    void onWorkerStarted(QString deviceName, int channel);
    void onWorkerCompleted(QString deviceName, int channel);
    void onWorkerFailed(QString deviceName, int channel, QString errorMessage);
    void onWorkerProgress(QString deviceName, int status);
    void onWorkerZScanProgress(QString deviceName, int iteration, int total, double currentZ, double peakZ, double peakVoltage);
    void onWorkerFinished();

private:
    void cleanup();

    UnifiedMotionManager* m_motionManager;

    // Worker thread
    QThread* m_workerThread;
    PIAlignWorker* m_worker;
    std::atomic<bool> m_running;

    // Current alignment info
    QMutex m_mutex;
    std::string m_deviceName;
};

/**
 * @brief Worker object that runs in background thread
 * This object is moved to QThread and does the actual work
 */
class PIAlignWorker : public QObject {
    Q_OBJECT

public:
    explicit PIAlignWorker(UnifiedMotionManager* motionManager);
    ~PIAlignWorker();

    void setParameters(const QString& deviceName, const FSMParameters& params);
    void requestStop() { m_stopRequested.store(true); }

signals:
    void started(QString deviceName, int channel);
    void completed(QString deviceName, int channel);
    void failed(QString deviceName, int channel, QString errorMessage);
    void progress(QString deviceName, int status);
    void zScanProgress(QString deviceName, int iteration, int total, double currentZ, double peakZ, double peakVoltage);
    void finished();

public slots:
    void doWork();

private:
    void doZScanWork(PIController* piController);
    bool runSingleFSM(PIController* piController);  // Returns true if FSM completed successfully

    UnifiedMotionManager* m_motionManager;
    std::atomic<bool> m_stopRequested;

    QString m_deviceName;
    FSMParameters m_params;

    static constexpr int STATUS_POLL_INTERVAL_MS = 200;
};
