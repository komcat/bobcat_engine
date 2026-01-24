// LightingParameterWorker.h
// Background worker for lighting controller operations to improve UI responsiveness
#pragma once

#include <QObject>
#include <QThread>
#include <QTimer>
#include <QMutex>
#include <atomic>
#include <string>
#include <vector>

namespace Lighting {
class LightingManager;
struct ControllerStatus;
}

/**
 * @brief Background worker for lighting controller operations
 *
 * Handles connect, disconnect, intensity, and toggle operations in a background
 * thread to prevent UI blocking during TCP/serial communication.
 *
 * Features:
 * - Non-blocking parameter updates
 * - Command queue with coalescing (multiple rapid slider changes -> single call)
 * - Status polling for real-time display updates
 */
class LightingParameterWorker : public QObject {
    Q_OBJECT

public:
    explicit LightingParameterWorker(Lighting::LightingManager* manager, QObject* parent = nullptr);
    ~LightingParameterWorker();

    // Thread management
    void start();
    void stop();
    bool isRunning() const { return m_running.load(); }

    // Device management
    void setCurrentDevice(const std::string& deviceId);

signals:
    // Connection result signals
    void connectionSuccess(QString deviceId);
    void connectionFailed(QString deviceId, QString error);
    void disconnectionComplete(QString deviceId);

    // Command completion signals
    void intensityApplied(int channel, int intensity);
    void channelToggled(int channel, bool on);
    void allOffComplete();

    // Status update signal
    void statusUpdated(QString deviceId, std::vector<int> intensities, std::vector<bool> onStates);

    // Error/status signals
    void commandError(QString operation, QString error);
    void commandSuccess(QString operation, QString message);

public slots:
    // Connection requests
    void requestConnect(const QString& deviceId);
    void requestDisconnect(const QString& deviceId);

    // Parameter requests (queued from UI thread)
    void requestSetIntensity(int channel, int intensity);
    void requestSetOn(int channel, bool on);
    void requestAllOff();
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
            Connect,
            Disconnect,
            SetIntensity,
            SetOn,
            AllOff,
            RefreshStatus
        };
        Type type;
        int channel;
        int intValue;
        bool boolValue;
        std::string deviceId;
    };

    // Execute commands
    void executeConnect(const std::string& deviceId);
    void executeDisconnect(const std::string& deviceId);
    void executeSetIntensity(const std::string& deviceId, int channel, int intensity);
    void executeSetOn(const std::string& deviceId, int channel, bool on);
    void executeAllOff(const std::string& deviceId);
    void executeRefreshStatus(const std::string& deviceId);

    Lighting::LightingManager* m_manager;
    std::string m_currentDeviceId;

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
    static constexpr int PROCESS_INTERVAL_MS = 30;  // Faster for responsive slider
    static constexpr int DEFAULT_POLL_INTERVAL_MS = 500;
};
