// MotionGraphManager.cpp
#include "MotionGraphManager.h"
#include <QJsonObject>
#include <QSet>
#include <limits>
#include <iostream>

MotionGraphManager::MotionGraphManager()
{
}

// ============================================================================
// DEVICE OPERATIONS
// ============================================================================

QList<QString> MotionGraphManager::getAllDevices() const
{
  return m_graphs.keys();
}

bool MotionGraphManager::hasDevice(const QString& deviceName) const
{
  return m_graphs.contains(deviceName);
}

QList<QString> MotionGraphManager::getGraphNames(const QString& deviceName) const
{
  auto deviceIt = m_graphs.find(deviceName);
  if (deviceIt != m_graphs.end()) {
    return deviceIt.value().keys();
  }
  return QList<QString>();
}

void MotionGraphManager::clearDevice(const QString& deviceName)
{
  m_graphs.remove(deviceName);
}

// ============================================================================
// GRAPH OPERATIONS
// ============================================================================

MotionGraph* MotionGraphManager::getGraph(const QString& deviceName, const QString& graphName)
{
  if (!m_graphs.contains(deviceName)) {
    return nullptr;
  }

  if (!m_graphs[deviceName].contains(graphName)) {
    return nullptr;
  }

  return &m_graphs[deviceName][graphName];
}

const MotionGraph* MotionGraphManager::getGraph(const QString& deviceName, const QString& graphName) const
{
  auto deviceIt = m_graphs.find(deviceName);
  if (deviceIt == m_graphs.end()) {
    return nullptr;
  }

  auto graphIt = deviceIt.value().find(graphName);
  if (graphIt == deviceIt.value().end()) {
    return nullptr;
  }

  return &graphIt.value();
}

bool MotionGraphManager::hasGraph(const QString& deviceName, const QString& graphName) const
{
  auto deviceIt = m_graphs.find(deviceName);
  if (deviceIt == m_graphs.end()) {
    return false;
  }
  return deviceIt.value().contains(graphName);
}

void MotionGraphManager::setGraph(const QString& deviceName, const QString& graphName, const MotionGraph& graph)
{
  m_graphs[deviceName][graphName] = graph;
}

bool MotionGraphManager::removeGraph(const QString& deviceName, const QString& graphName)
{
  if (!hasGraph(deviceName, graphName)) {
    return false;
  }

  m_graphs[deviceName].remove(graphName);

  // Clean up device entry if no graphs remain
  if (m_graphs[deviceName].isEmpty()) {
    m_graphs.remove(deviceName);
  }

  return true;
}

void MotionGraphManager::createGraph(const QString& deviceName, const QString& graphName)
{
  MotionGraph newGraph(graphName);
  m_graphs[deviceName][graphName] = newGraph;
}

bool MotionGraphManager::renameGraph(const QString& deviceName, const QString& oldName, const QString& newName)
{
  if (!hasGraph(deviceName, oldName)) {
    return false;
  }

  if (hasGraph(deviceName, newName)) {
    return false;  // New name already exists
  }

  // Copy the graph with new name
  MotionGraph graph = m_graphs[deviceName][oldName];
  graph.name = newName;
  m_graphs[deviceName][newName] = graph;

  // Remove old graph
  m_graphs[deviceName].remove(oldName);

  return true;
}

// ============================================================================
// SERIALIZATION
// ============================================================================

QJsonObject MotionGraphManager::toJson() const
{
  QJsonObject graphsJson;

  // Iterate through devices
  for (auto deviceIt = m_graphs.constBegin(); deviceIt != m_graphs.constEnd(); ++deviceIt) {
    const QString& deviceName = deviceIt.key();
    const QMap<QString, MotionGraph>& deviceGraphs = deviceIt.value();

    QJsonObject deviceJson;

    // Iterate through graphs for this device
    for (auto graphIt = deviceGraphs.constBegin(); graphIt != deviceGraphs.constEnd(); ++graphIt) {
      const QString& graphName = graphIt.key();
      const MotionGraph& graph = graphIt.value();

      deviceJson[graphName] = graph.toJson();
    }

    graphsJson[deviceName] = deviceJson;
  }

  return graphsJson;
}

MotionGraphManager MotionGraphManager::fromJson(const QJsonObject& json)
{
  MotionGraphManager manager;

  // Iterate through devices
  for (const QString& deviceName : json.keys()) {
    QJsonObject deviceJson = json[deviceName].toObject();

    // Iterate through graphs for this device
    for (const QString& graphName : deviceJson.keys()) {
      QJsonObject graphJson = deviceJson[graphName].toObject();

      MotionGraph graph = MotionGraph::fromJson(graphName, graphJson);
      manager.setGraph(deviceName, graphName, graph);
    }
  }

  return manager;
}

// ============================================================================
// PATH PLANNING
// ============================================================================

GraphPath MotionGraphManager::findPath(const QString& deviceName,
                                        const QString& graphName,
                                        const QString& startNodeId,
                                        const QString& goalNodeId) const
{
  GraphPath result;

  // Get the graph
  const MotionGraph* graph = getGraph(deviceName, graphName);
  if (!graph) {
    result.errorMessage = "Graph not found";
    return result;
  }

  // Validate nodes exist
  if (!graph->hasNode(startNodeId)) {
    result.errorMessage = QString("Start node '%1' not found").arg(startNodeId);
    return result;
  }

  if (!graph->hasNode(goalNodeId)) {
    result.errorMessage = QString("Goal node '%1' not found").arg(goalNodeId);
    return result;
  }

  // Special case: start == goal
  if (startNodeId == goalNodeId) {
    result.isValid = true;
    result.nodeIds.append(startNodeId);
    const MotionNode* node = graph->getNode(startNodeId);
    if (node) {
      result.positionNames.append(node->position);
    }
    result.totalDistance = 0.0;
    result.maxSpeed = std::numeric_limits<double>::infinity();
    return result;
  }

  // Dijkstra's algorithm
  QMap<QString, double> distances;      // Best known distance to each node
  QMap<QString, QString> predecessors;  // Previous node in optimal path
  QSet<QString> unvisited;             // Nodes not yet processed

  // Initialize all nodes
  QList<MotionNode> allNodes = graph->getAllNodes();
  for (const MotionNode& node : allNodes) {
    distances[node.id] = std::numeric_limits<double>::infinity();
    unvisited.insert(node.id);
  }
  distances[startNodeId] = 0.0;

  while (!unvisited.isEmpty()) {
    // Find unvisited node with smallest distance
    QString currentNode;
    double minDistance = std::numeric_limits<double>::infinity();

    for (const QString& nodeId : unvisited) {
      if (distances[nodeId] < minDistance) {
        minDistance = distances[nodeId];
        currentNode = nodeId;
      }
    }

    // No path exists if we can't reach any more nodes
    if (minDistance == std::numeric_limits<double>::infinity()) {
      result.errorMessage = QString("No path exists from '%1' to '%2'")
        .arg(startNodeId).arg(goalNodeId);
      return result;
    }

    // Found the goal
    if (currentNode == goalNodeId) {
      break;
    }

    unvisited.remove(currentNode);

    // Check all neighbors (edges are bidirectional)
    QList<QString> neighbors = graph->getNeighbors(currentNode);
    for (const QString& neighbor : neighbors) {
      if (!unvisited.contains(neighbor)) {
        continue;  // Already visited
      }

      // Find the edge between current and neighbor
      QList<MotionEdge> allEdges = graph->getAllEdges();
      double edgeWeight = 1.0;  // Default weight

      for (const MotionEdge& edge : allEdges) {
        if ((edge.fromNodeId == currentNode && edge.toNodeId == neighbor) ||
            (edge.toNodeId == currentNode && edge.fromNodeId == neighbor)) {
          // Use maxSpeed as inverse weight (lower speed = higher cost)
          // If maxSpeed is 0 (unlimited), use minimal cost
          if (edge.maxSpeed > 0) {
            edgeWeight = 1.0 / edge.maxSpeed;  // Inverse speed as weight
          } else {
            edgeWeight = 0.01;  // Very low cost for unlimited speed
          }
          break;
        }
      }

      double altDistance = distances[currentNode] + edgeWeight;

      if (altDistance < distances[neighbor]) {
        distances[neighbor] = altDistance;
        predecessors[neighbor] = currentNode;
      }
    }
  }

  // Check if we found a path
  if (!predecessors.contains(goalNodeId) && startNodeId != goalNodeId) {
    result.errorMessage = QString("No path exists from '%1' to '%2'")
      .arg(startNodeId).arg(goalNodeId);
    return result;
  }

  // Reconstruct path from predecessors
  QList<QString> path;
  QString current = goalNodeId;

  while (current != startNodeId) {
    path.prepend(current);
    if (!predecessors.contains(current)) {
      result.errorMessage = "Path reconstruction failed";
      return result;
    }
    current = predecessors[current];
  }
  path.prepend(startNodeId);

  // Build result
  result.isValid = true;
  result.nodeIds = path;
  result.totalDistance = distances[goalNodeId];

  // Get position names and calculate slowest speed along path
  double slowestSpeed = std::numeric_limits<double>::infinity();

  for (const QString& nodeId : path) {
    const MotionNode* node = graph->getNode(nodeId);
    if (node) {
      result.positionNames.append(node->position);
    }
  }

  // Find slowest speed (bottleneck) along path
  QList<MotionEdge> allEdges = graph->getAllEdges();
  for (int i = 0; i < path.size() - 1; ++i) {
    QString fromNode = path[i];
    QString toNode = path[i + 1];

    for (const MotionEdge& edge : allEdges) {
      if ((edge.fromNodeId == fromNode && edge.toNodeId == toNode) ||
          (edge.toNodeId == fromNode && edge.fromNodeId == toNode)) {
        if (edge.maxSpeed > 0 && edge.maxSpeed < slowestSpeed) {
          slowestSpeed = edge.maxSpeed;
        }
        break;
      }
    }
  }

  result.maxSpeed = (slowestSpeed == std::numeric_limits<double>::infinity()) ? 0.0 : slowestSpeed;

  std::cout << "[MotionGraphManager] Path found: ";
  for (const QString& pos : result.positionNames) {
    std::cout << pos.toStdString() << " -> ";
  }
  std::cout << "END (cost: " << result.totalDistance << ", max speed: " << result.maxSpeed << ")" << std::endl;

  return result;
}

// ============================================================================
// BULK OPERATIONS
// ============================================================================

void MotionGraphManager::clearAll()
{
  m_graphs.clear();
}

int MotionGraphManager::getTotalGraphCount() const
{
  int count = 0;
  for (const QMap<QString, MotionGraph>& deviceGraphs : m_graphs.values()) {
    count += deviceGraphs.size();
  }
  return count;
}

int MotionGraphManager::getDeviceCount() const
{
  return m_graphs.size();
}
