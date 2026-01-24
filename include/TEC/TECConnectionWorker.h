// TECConnectionWorker.h
// Background worker for TEC connection operations to prevent UI freezing
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
 * @brief Background worker for TEC device connection operations
 *
 * Handles connect/disconnect in a background thread to prevent UI blocking
 * when hardware is unavailable or slow to respond.
 */
class TECConnectionWorker : public QObject {
    Q_OBJECT

public:
    explicit TECConnectionWorker(TECManager* manager, QObject* parent = nullptr);
    ~TECConnectionWorker();

    // Thread management
    void start();
    void stop();
    bool isRunning() const { return m_running.load(); }

    // Check if busy (operation in progress)
    bool isBusy() const { return m_busy.load(); }

signals:
    // Connection result signals
    void connectionSuccess(QString deviceName);
    void connectionFailed(QString deviceName, QString error);
    void disconnectionComplete(QString deviceName);

    // Status signals
    void operationStarted(QString operation, QString deviceName);

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

    TECManager* m_manager;

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
