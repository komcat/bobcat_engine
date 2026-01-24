// TECParameterWorker.h
// Background worker for TEC parameter updates to improve UI responsiveness
#pragma once

#include <QObject>
#include <QThread>
#include <QTimer>
#include <QMutex>
#include <atomic>
#include <string>
#include <vector>

class TECManager;

/**
 * @brief Background worker for TEC parameter changes and status reading
 *
 * Handles temperature, laser current, on/off controls, and status polling
 * in a background thread to prevent UI blocking during VISA/serial operations.
 *
 * Features:
 * - Non-blocking parameter updates
 * - Command queue with coalescing
 * - Status polling for real-time display
 */
class TECParameterWorker : public QObject {
    Q_OBJECT

public:
    explicit TECParameterWorker(TECManager* manager, QObject* parent = nullptr);
    ~TECParameterWorker();

    // Thread management
    void start();
    void stop();
    bool isRunning() const { return m_running.load(); }

    // Device management
    void setCurrentDevice(const std::string& deviceName);

signals:
    // Command completion signals
    void temperatureApplied(double temperature);
    void laserCurrentApplied(double current);
    void tecStateChanged(bool on);
    void laserStateChanged(bool on);

    // Status update signal
    void statusUpdated(double currentTemp, double tecCurrent, double laserCurrent,
                       bool tecOn, bool laserOn);

    // Error/status signals
    void commandError(QString operation, QString error);
    void commandSuccess(QString operation, QString message);

public slots:
    // TEC control requests
    void requestSetTemperature(double temperature);
    void requestTECOn();
    void requestTECOff();

    // Laser control requests
    void requestSetLaserCurrent(double currentMA);
    void requestLaserOn();
    void requestLaserOff();

    // Preset request (combines multiple commands)
    void requestApplyPreset(double temperature, double laserCurrentMA, bool tecOn, bool laserOn);

    // Status refresh
    void requestRefreshStatus();

    // Polling control
    void setStatusPollingEnabled(bool enabled);
    void setStatusPollingInterval(int milliseconds);

private slots:
    void processQueue();
    void pollStatus();

private:
    // Command types for queue
    struct PendingCommand {
        enum Type {
            SetTemperature,
            TECOn,
            TECOff,
            SetLaserCurrent,
            LaserOn,
            LaserOff,
            ApplyPreset,
            RefreshStatus
        };
        Type type;
        double value1;      // temperature or laser current
        double value2;      // laser current (for preset)
        bool boolValue1;    // tecOn (for preset)
        bool boolValue2;    // laserOn (for preset)
    };

    // Execute commands
    void executeSetTemperature(const std::string& deviceName, double temperature);
    void executeTECOn(const std::string& deviceName);
    void executeTECOff(const std::string& deviceName);
    void executeSetLaserCurrent(const std::string& deviceName, double currentMA);
    void executeLaserOn(const std::string& deviceName);
    void executeLaserOff(const std::string& deviceName);
    void executeApplyPreset(const std::string& deviceName, double temperature, double laserCurrentMA, bool tecOn, bool laserOn);
    void executeRefreshStatus(const std::string& deviceName);

    TECManager* m_manager;
    std::string m_currentDeviceName;

    // Worker thread
    QThread* m_workerThread;
    QTimer* m_processTimer;
    QTimer* m_pollTimer;
    std::atomic<bool> m_running;

    // Command queue with mutex protection
    QMutex m_queueMutex;
    std::vector<PendingCommand> m_pendingCommands;

    // Polling settings
    std::atomic<bool> m_statusPollingEnabled;

    // Processing intervals (ms)
    static constexpr int PROCESS_INTERVAL_MS = 30;  // Fast for responsive sliders
    static constexpr int DEFAULT_POLL_INTERVAL_MS = 500;
};
