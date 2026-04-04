// SMUParameterWorker.h
// Background worker for SMU parameter updates to improve UI responsiveness
#pragma once

#include <QObject>
#include <QThread>
#include <QTimer>
#include <QMutex>
#include <atomic>
#include <string>
#include <vector>

class SMUManager;

/**
 * @brief Background worker for SMU parameter changes and measurements
 *
 * Handles voltage, current, compliance, and output state updates in a background
 * thread to prevent UI blocking during VISA/SCPI operations.
 *
 * Features:
 * - Non-blocking parameter updates
 * - Command queue with coalescing (multiple rapid changes -> single SMU call)
 * - Measurement polling for real-time value display
 * - Multi-channel support
 */
class SMUParameterWorker : public QObject {
    Q_OBJECT

public:
    explicit SMUParameterWorker(SMUManager* manager, QObject* parent = nullptr);
    ~SMUParameterWorker();

    // Thread management
    void start();
    void stop();
    bool isRunning() const { return m_running.load(); }

    // Device management
    void setCurrentDevice(const std::string& deviceName);
    void setChannelCount(int count);

signals:
    // Command completion signals
    void voltageApplied(double voltage);
    void currentApplied(double current);
    void voltageComplianceApplied(double compliance);
    void currentComplianceApplied(double compliance);
    void outputStateChanged(bool on);

    // Measurement update signals
    void measurementsUpdated(int channel, double voltage, double current, bool outputOn);

    // Error/status signals
    void commandError(QString operation, QString error);
    void commandSuccess(QString operation, QString message);

public slots:
    // Command requests (queued from UI thread)
    void requestSetVoltage(int channel, double voltage);
    void requestSetCurrent(int channel, double current);
    void requestSetVoltageCompliance(double compliance);
    void requestSetCurrentCompliance(double compliance);
    void requestOutputOn(int channel);
    void requestOutputOff(int channel);
    void requestReadMeasurements(int channel);
    void requestRefreshAll(int channel);

    // Polling control
    void setMeasurementPollingEnabled(bool enabled);
    void setMeasurementPollingInterval(int milliseconds);
    void setPollingChannel(int channel);

private slots:
    void processQueue();
    void pollMeasurements();

private:
    // Command types for queue
    struct PendingCommand {
        enum Type {
            SetVoltage,
            SetCurrent,
            SetVoltageCompliance,
            SetCurrentCompliance,
            OutputOn,
            OutputOff,
            ReadMeasurements,
            RefreshAll
        };
        Type type;
        int channel;
        double value;
    };

    // Apply commands to SMU
    bool applySetVoltage(const std::string& deviceName, double voltage);
    bool applySetCurrent(const std::string& deviceName, double current);
    bool applySetVoltageCompliance(const std::string& deviceName, double compliance);
    bool applySetCurrentCompliance(const std::string& deviceName, double compliance);
    bool applyOutputOn(const std::string& deviceName);
    bool applyOutputOff(const std::string& deviceName);
    void readMeasurements(const std::string& deviceName, int channel);

    SMUManager* m_manager;
    std::string m_deviceName;

    // Worker thread
    QThread* m_workerThread;
    QTimer* m_processTimer;
    QTimer* m_pollTimer;
    std::atomic<bool> m_running;

    // Command queue with mutex protection
    QMutex m_queueMutex;
    std::vector<PendingCommand> m_pendingCommands;

    // Polling settings
    std::atomic<bool> m_measurementPollingEnabled;
    std::atomic<int> m_pollingChannel;
    int m_channelCount;

    // Processing intervals (ms)
    static constexpr int PROCESS_INTERVAL_MS = 50;
    static constexpr int DEFAULT_POLL_INTERVAL_MS = 200;
};
