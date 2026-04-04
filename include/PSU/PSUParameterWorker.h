// PSUParameterWorker.h
// Background worker for power supply parameter updates to improve UI responsiveness
#pragma once

#include <QObject>
#include <QThread>
#include <QTimer>
#include <QMutex>
#include <atomic>
#include <string>
#include <vector>

class PowerSupplyManager;

/**
 * @brief Background worker for power supply parameter changes
 *
 * Handles voltage, current, OVP, OCP, and output state updates in a background
 * thread to prevent UI blocking during TCP/SCPI operations.
 *
 * Features:
 * - Non-blocking parameter updates
 * - Command queue with coalescing (multiple rapid changes -> single PSU call)
 * - Measurement polling for real-time value display
 * - Multi-channel support
 */
class PSUParameterWorker : public QObject {
    Q_OBJECT

public:
    explicit PSUParameterWorker(PowerSupplyManager* manager, QObject* parent = nullptr);
    ~PSUParameterWorker();

    // Thread management
    void start();
    void stop();
    bool isRunning() const { return m_running.load(); }

    // Device management
    void setCurrentDevice(const std::string& deviceName);
    void setChannelCount(int count);

signals:
    // Command completion signals (with channel)
    void voltageApplied(int channel, double voltage);
    void currentApplied(int channel, double current);
    void ovpApplied(int channel, double ovp);
    void ocpApplied(int channel, double ocp);
    void outputStateChanged(int channel, bool on);

    // Measurement update signals
    void measurementsUpdated(int channel, double voltage, double current, bool outputOn);

    // Error/status signals
    void commandError(int channel, QString operation, QString error);
    void deviceConnected(QString deviceName);
    void deviceDisconnected(QString deviceName);

public slots:
    // Command requests (queued from UI thread)
    void requestSetVoltage(int channel, double voltage);
    void requestSetCurrent(int channel, double current);
    void requestSetOVP(int channel, double ovp);
    void requestSetOCP(int channel, double ocp);
    void requestOutputOn(int channel);
    void requestOutputOff(int channel);
    void requestReadMeasurements(int channel);
    void requestReadAllChannels();
    void requestConnect();
    void requestDisconnect();

    // Polling control
    void setMeasurementPollingEnabled(bool enabled);
    void setMeasurementPollingInterval(int milliseconds);

private slots:
    void processQueue();
    void pollMeasurements();

private:
    // Command types for queue
    struct PendingCommand {
        enum Type {
            SetVoltage,
            SetCurrent,
            SetOVP,
            SetOCP,
            OutputOn,
            OutputOff,
            ReadMeasurements,
            Connect,
            Disconnect
        };
        Type type;
        int channel;
        double value;
    };

    // Apply commands to PSU (deviceName passed for thread-safety)
    bool applySetVoltage(const std::string& deviceName, int channel, double voltage);
    bool applySetCurrent(const std::string& deviceName, int channel, double current);
    bool applySetOVP(const std::string& deviceName, int channel, double ovp);
    bool applySetOCP(const std::string& deviceName, int channel, double ocp);
    bool applyOutputOn(const std::string& deviceName, int channel);
    bool applyOutputOff(const std::string& deviceName, int channel);
    void readMeasurements(const std::string& deviceName, int channel);

    PowerSupplyManager* m_manager;
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
    int m_channelCount;

    // Processing intervals (ms)
    static constexpr int PROCESS_INTERVAL_MS = 50;
    static constexpr int DEFAULT_POLL_INTERVAL_MS = 200;
};
