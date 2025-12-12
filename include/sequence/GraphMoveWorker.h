// GraphMoveWorker.h
// Worker thread for executing graph path traversal asynchronously
#pragma once

#include <QThread>
#include <QString>
#include <atomic>
#include <string>

// Forward declarations
class UnifiedMotionManager;
class MotionGraphManager;
class ProductPositionManager;

/**
 * @brief Worker thread for executing graph path moves asynchronously
 *
 * Each worker handles one device's complete graph path traversal.
 * Multiple workers can run simultaneously for true parallel motion.
 *
 * Usage:
 *   auto* worker = new GraphMoveWorker(motionManager, posManager,
 *                                       "hex-left", "default", "lensplace");
 *   connect(worker, &GraphMoveWorker::moveCompleted, ...);
 *   worker->start();
 */
class GraphMoveWorker : public QThread
{
    Q_OBJECT

public:
    /**
     * @brief Construct a graph move worker
     * @param motionManager UnifiedMotionManager for motion control
     * @param positionManager ProductPositionManager for graph and position access
     * @param deviceName Device to move (e.g., "hex-left", "gantry-main")
     * @param graphName Graph to use (e.g., "default")
     * @param targetNodeName Target position name in the graph
     * @param parent QObject parent
     */
    GraphMoveWorker(
        UnifiedMotionManager* motionManager,
        ProductPositionManager* positionManager,
        const std::string& deviceName,
        const std::string& graphName,
        const std::string& targetNodeName,
        QObject* parent = nullptr
    );

    ~GraphMoveWorker() = default;

    // Request graceful stop
    void requestStop();

    // Status accessors (thread-safe)
    bool isCompleted() const { return m_completed.load(); }
    bool isSuccess() const { return m_success.load(); }
    QString getErrorMessage() const { return m_errorMessage; }
    std::string getDeviceName() const { return m_deviceName; }

signals:
    // Emitted when move starts
    void moveStarted(QString deviceName, QString targetNode);

    // Emitted when a waypoint is reached
    void waypointReached(QString deviceName, QString waypointName, int index, int total);

    // Emitted when move completes (success or failure)
    void moveCompleted(QString deviceName, bool success, QString message);

    // Log messages
    void logMessage(QString message);

protected:
    void run() override;

private:
    // Managers (not owned)
    UnifiedMotionManager* m_motionManager;
    ProductPositionManager* m_positionManager;

    // Move specification
    std::string m_deviceName;
    std::string m_graphName;
    std::string m_targetNodeName;

    // Thread-safe state
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_completed{false};
    std::atomic<bool> m_success{false};
    QString m_errorMessage;

    // Constants
    static constexpr double POSITION_TOLERANCE = 0.2;  // mm
    static constexpr int WAYPOINT_TIMEOUT_MS = 30000;  // 30 seconds per waypoint
};
