// SMUConnectionWorker.h
// Background worker for SMU connection operations to prevent UI freezing
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
 * @brief Background worker for SMU connection operations
 *
 * Handles connect/disconnect in a background thread to prevent UI blocking
 * when hardware is unavailable or slow to respond.
 *
 * Features:
 * - Non-blocking connect/disconnect operations
 * - Timeout handling for unresponsive hardware
 * - Command queue to prevent overlapping operations
 */
class SMUConnectionWorker : public QObject {
    Q_OBJECT

public:
    explicit SMUConnectionWorker(SMUManager* manager, QObject* parent = nullptr);
    ~SMUConnectionWorker();

    // Thread management
    void start();
    void stop();
    bool isRunning() const { return m_running.load(); }

    // Device management
    void setCurrentDevice(const std::string& deviceName);
    std::string getCurrentDevice() const;

    // Check if busy (operation in progress)
    bool isBusy() const { return m_busy.load(); }

signals:
    // Connection result signals
    void connectionSuccess(QString deviceName, QString deviceId);
    void connectionFailed(QString deviceName, QString error);
    void disconnectionComplete(QString deviceName);

    // Status signals
    void operationStarted(QString operation, QString deviceName);
    void operationProgress(QString message);

public slots:
    // Request operations (queued from UI thread)
    void requestConnect(const QString& deviceName);
    void requestDisconnect(const QString& deviceName);

private slots:
    void processQueue();

private:
    // Command types
    struct PendingCommand {
        enum Type {
            Connect,
            Disconnect
        };
        Type type;
        std::string deviceName;
    };

    // Execute commands
    void executeConnect(const std::string& deviceName);
    void executeDisconnect(const std::string& deviceName);

    SMUManager* m_manager;
    std::string m_currentDevice;

    // Worker thread
    QThread* m_workerThread;
    QTimer* m_processTimer;
    std::atomic<bool> m_running;
    std::atomic<bool> m_busy;

    // Command queue with mutex protection
    QMutex m_queueMutex;
    std::vector<PendingCommand> m_pendingCommands;

    // Processing interval (ms)
    static constexpr int PROCESS_INTERVAL_MS = 50;
};
