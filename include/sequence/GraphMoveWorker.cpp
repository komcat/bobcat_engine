// GraphMoveWorker.cpp
#include "GraphMoveWorker.h"

#include "../motions/unified_motion_manager.h"
#include "../products/product_position_manager.h"
#include "../graph/MotionGraphManager.h"
#include "../graph/MotionGraph.h"

#include <cmath>
#include <iostream>
#include <chrono>
#include <thread>

GraphMoveWorker::GraphMoveWorker(
    UnifiedMotionManager* motionManager,
    ProductPositionManager* positionManager,
    const std::string& deviceName,
    const std::string& graphName,
    const std::string& targetNodeName,
    QObject* parent)
    : QThread(parent)
    , m_motionManager(motionManager)
    , m_positionManager(positionManager)
    , m_deviceName(deviceName)
    , m_graphName(graphName)
    , m_targetNodeName(targetNodeName)
{
}

void GraphMoveWorker::requestStop()
{
    m_stopRequested.store(true);
    emit logMessage(QString("[GraphMoveWorker] Stop requested for %1")
                    .arg(QString::fromStdString(m_deviceName)));
}

void GraphMoveWorker::run()
{
    emit moveStarted(QString::fromStdString(m_deviceName),
                     QString::fromStdString(m_targetNodeName));

    emit logMessage(QString("[GraphMoveWorker] %1: Starting graph move to '%2' via graph '%3'")
                    .arg(QString::fromStdString(m_deviceName))
                    .arg(QString::fromStdString(m_targetNodeName))
                    .arg(QString::fromStdString(m_graphName)));

    // Validate managers
    if (!m_motionManager || !m_positionManager) {
        m_errorMessage = "Invalid motion or position manager";
        m_success.store(false);
        m_completed.store(true);
        emit moveCompleted(QString::fromStdString(m_deviceName), false, m_errorMessage);
        return;
    }

    // Get graph
    MotionGraphManager& graphManager = m_positionManager->GetGraphManager();
    const MotionGraph* graph = graphManager.getGraph(
        QString::fromStdString(m_deviceName),
        QString::fromStdString(m_graphName)
    );

    if (!graph) {
        m_errorMessage = QString("Graph '%1' not found for device '%2'")
                         .arg(QString::fromStdString(m_graphName))
                         .arg(QString::fromStdString(m_deviceName));
        m_success.store(false);
        m_completed.store(true);
        emit moveCompleted(QString::fromStdString(m_deviceName), false, m_errorMessage);
        return;
    }

    // Get all known positions for this device
    std::map<std::string, PositionStruct> allPositions =
        m_positionManager->GetAllPositions(m_deviceName);

    // Find current position in graph
    PositionStruct currentPos = m_motionManager->GetCurrentPosition(m_deviceName);
    std::string currentPosName;
    double closestDistance = std::numeric_limits<double>::max();

    for (const auto& pair : allPositions) {
        double dx = currentPos.x - pair.second.x;
        double dy = currentPos.y - pair.second.y;
        double dz = currentPos.z - pair.second.z;
        double distance = std::sqrt(dx*dx + dy*dy + dz*dz);

        if (distance < closestDistance) {
            closestDistance = distance;
            if (distance < POSITION_TOLERANCE) {
                currentPosName = pair.first;
                break;
            }
        }
    }

    if (currentPosName.empty()) {
        m_errorMessage = QString("Device '%1' not at a known graph position (closest: %2mm)")
                         .arg(QString::fromStdString(m_deviceName))
                         .arg(closestDistance, 0, 'f', 2);
        m_success.store(false);
        m_completed.store(true);
        emit moveCompleted(QString::fromStdString(m_deviceName), false, m_errorMessage);
        return;
    }

    emit logMessage(QString("[GraphMoveWorker] %1: Current position: '%2'")
                    .arg(QString::fromStdString(m_deviceName))
                    .arg(QString::fromStdString(currentPosName)));

    // Find nodes in graph
    const MotionNode* startNode = graph->getNodeByPosition(QString::fromStdString(currentPosName));
    const MotionNode* endNode = graph->getNodeByPosition(QString::fromStdString(m_targetNodeName));

    if (!startNode) {
        m_errorMessage = QString("Start position '%1' not found in graph '%2'")
                         .arg(QString::fromStdString(currentPosName))
                         .arg(QString::fromStdString(m_graphName));
        m_success.store(false);
        m_completed.store(true);
        emit moveCompleted(QString::fromStdString(m_deviceName), false, m_errorMessage);
        return;
    }

    if (!endNode) {
        m_errorMessage = QString("Target position '%1' not found in graph '%2'")
                         .arg(QString::fromStdString(m_targetNodeName))
                         .arg(QString::fromStdString(m_graphName));
        m_success.store(false);
        m_completed.store(true);
        emit moveCompleted(QString::fromStdString(m_deviceName), false, m_errorMessage);
        return;
    }

    // Already at target?
    if (startNode->id == endNode->id) {
        emit logMessage(QString("[GraphMoveWorker] %1: Already at target '%2'")
                        .arg(QString::fromStdString(m_deviceName))
                        .arg(QString::fromStdString(m_targetNodeName)));
        m_success.store(true);
        m_completed.store(true);
        emit moveCompleted(QString::fromStdString(m_deviceName), true, "");
        return;
    }

    // Find path
    GraphPath path = graphManager.findPath(
        QString::fromStdString(m_deviceName),
        QString::fromStdString(m_graphName),
        startNode->id,
        endNode->id
    );

    if (!path.isValid || path.positionNames.empty()) {
        m_errorMessage = QString("No valid path from '%1' to '%2' in graph '%3'")
                         .arg(QString::fromStdString(currentPosName))
                         .arg(QString::fromStdString(m_targetNodeName))
                         .arg(QString::fromStdString(m_graphName));
        m_success.store(false);
        m_completed.store(true);
        emit moveCompleted(QString::fromStdString(m_deviceName), false, m_errorMessage);
        return;
    }

    // Log path
    QString pathStr;
    for (int i = 0; i < path.positionNames.size(); ++i) {
        if (i > 0) pathStr += " -> ";
        pathStr += path.positionNames[i];
    }
    emit logMessage(QString("[GraphMoveWorker] %1: Path: %2")
                    .arg(QString::fromStdString(m_deviceName))
                    .arg(pathStr));

    // Execute path waypoint by waypoint
    int totalWaypoints = path.positionNames.size();
    for (int i = 0; i < totalWaypoints; ++i) {
        // Check stop request
        if (m_stopRequested.load()) {
            m_errorMessage = "Stop requested";
            m_success.store(false);
            m_completed.store(true);
            emit moveCompleted(QString::fromStdString(m_deviceName), false, m_errorMessage);
            return;
        }

        QString waypointName = path.positionNames[i];
        std::string wpName = waypointName.toStdString();

        // Check if already at this waypoint
        PositionStruct wpCurrentPos = m_motionManager->GetCurrentPosition(m_deviceName);
        if (allPositions.count(wpName) > 0) {
            const PositionStruct& targetPos = allPositions[wpName];
            double dx = wpCurrentPos.x - targetPos.x;
            double dy = wpCurrentPos.y - targetPos.y;
            double dz = wpCurrentPos.z - targetPos.z;
            double distance = std::sqrt(dx*dx + dy*dy + dz*dz);

            if (distance < POSITION_TOLERANCE) {
                emit logMessage(QString("[GraphMoveWorker] %1: Already at waypoint '%2', skipping")
                                .arg(QString::fromStdString(m_deviceName))
                                .arg(waypointName));
                emit waypointReached(QString::fromStdString(m_deviceName), waypointName, i + 1, totalWaypoints);
                continue;
            }
        }

        // Start move to waypoint
        // NOTE: Use blocking=true because each worker runs in its own thread
        // Non-blocking mode uses a waypoint state machine that conflicts with
        // multiple sequential moves from the same thread
        emit logMessage(QString("[GraphMoveWorker] %1: Moving to waypoint '%2' (%3/%4)")
                        .arg(QString::fromStdString(m_deviceName))
                        .arg(waypointName)
                        .arg(i + 1)
                        .arg(totalWaypoints));

        // Check stop before starting move
        if (m_stopRequested.load()) {
            m_errorMessage = "Stop requested before move";
            m_success.store(false);
            m_completed.store(true);
            emit moveCompleted(QString::fromStdString(m_deviceName), false, m_errorMessage);
            return;
        }

        // Use blocking move - this waits for motion to complete
        // This is safe because we're in our own worker thread
        bool moveSuccess = m_motionManager->MoveToNamedPosition(m_deviceName, wpName, true);

        if (!moveSuccess) {
            m_errorMessage = QString("Failed to move to waypoint '%1'").arg(waypointName);
            m_success.store(false);
            m_completed.store(true);
            emit moveCompleted(QString::fromStdString(m_deviceName), false, m_errorMessage);
            return;
        }

        // Check stop after move completed
        if (m_stopRequested.load()) {
            m_errorMessage = "Stop requested after move";
            m_success.store(false);
            m_completed.store(true);
            emit moveCompleted(QString::fromStdString(m_deviceName), false, m_errorMessage);
            return;
        }

        // Settling time - shorter for hexapods
        int settleMs = (m_deviceName.find("hex") != std::string::npos) ? 50 : 200;
        msleep(settleMs);

        emit waypointReached(QString::fromStdString(m_deviceName), waypointName, i + 1, totalWaypoints);
        emit logMessage(QString("[GraphMoveWorker] %1: Reached waypoint '%2'")
                        .arg(QString::fromStdString(m_deviceName))
                        .arg(waypointName));
    }

    // Success!
    emit logMessage(QString("[GraphMoveWorker] %1: Completed graph path to '%2'")
                    .arg(QString::fromStdString(m_deviceName))
                    .arg(QString::fromStdString(m_targetNodeName)));

    m_success.store(true);
    m_completed.store(true);
    emit moveCompleted(QString::fromStdString(m_deviceName), true, "");
}
