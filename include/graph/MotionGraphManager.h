// MotionGraphManager.h
#pragma once

#include "MotionGraph.h"
#include <QMap>
#include <QString>
#include <QJsonObject>
#include <QList>

/**
 * @brief Result of path planning between two nodes
 */
struct GraphPath
{
  bool isValid;                    // True if path was found
  QList<QString> nodeIds;          // Sequence of node IDs from start to goal
  QList<QString> positionNames;    // Corresponding position names
  double totalDistance;            // Total estimated distance (sum of edges)
  double maxSpeed;                 // Slowest max speed along the path (bottleneck)
  QString errorMessage;            // Error description if path not found

  GraphPath() : isValid(false), totalDistance(0.0), maxSpeed(0.0) {}
};

/**
 * @brief Manages all motion graphs across all devices
 *
 * Structure: device -> graph_name -> MotionGraph
 * Example:
 *   - gantry-main -> "default" -> MotionGraph
 *   - gantry-main -> "inspection" -> MotionGraph
 *   - hex-left -> "default" -> MotionGraph
 */
class MotionGraphManager
{
public:
  MotionGraphManager();

  // ============================================================================
  // DEVICE OPERATIONS
  // ============================================================================

  // Get list of all devices that have graphs
  QList<QString> getAllDevices() const;

  // Check if device has any graphs
  bool hasDevice(const QString& deviceName) const;

  // Get all graph names for a device
  QList<QString> getGraphNames(const QString& deviceName) const;

  // Clear all graphs for a device
  void clearDevice(const QString& deviceName);

  // ============================================================================
  // GRAPH OPERATIONS
  // ============================================================================

  // Get or create a graph (creates if doesn't exist)
  MotionGraph* getGraph(const QString& deviceName, const QString& graphName);
  const MotionGraph* getGraph(const QString& deviceName, const QString& graphName) const;

  // Check if specific graph exists
  bool hasGraph(const QString& deviceName, const QString& graphName) const;

  // Add/set a graph
  void setGraph(const QString& deviceName, const QString& graphName, const MotionGraph& graph);

  // Remove a specific graph
  bool removeGraph(const QString& deviceName, const QString& graphName);

  // Create a new empty graph for device
  void createGraph(const QString& deviceName, const QString& graphName);

  // Rename a graph
  bool renameGraph(const QString& deviceName, const QString& oldName, const QString& newName);

  // ============================================================================
  // SERIALIZATION
  // ============================================================================

  // Convert all graphs to JSON
  QJsonObject toJson() const;

  // Load all graphs from JSON
  static MotionGraphManager fromJson(const QJsonObject& json);

  // ============================================================================
  // PATH PLANNING
  // ============================================================================

  /**
   * @brief Find shortest path between two nodes using Dijkstra's algorithm
   * @param deviceName Device name
   * @param graphName Graph name
   * @param startNodeId Starting node ID
   * @param goalNodeId Goal node ID
   * @return GraphPath with node sequence and metrics (isValid=false if no path found)
   */
  GraphPath findPath(const QString& deviceName,
                     const QString& graphName,
                     const QString& startNodeId,
                     const QString& goalNodeId) const;

  // ============================================================================
  // BULK OPERATIONS
  // ============================================================================

  // Clear all graphs for all devices
  void clearAll();

  // Get total number of graphs across all devices
  int getTotalGraphCount() const;

  // Get total number of devices with graphs
  int getDeviceCount() const;

private:
  // Structure: deviceName -> (graphName -> MotionGraph)
  QMap<QString, QMap<QString, MotionGraph>> m_graphs;
};
