// MotionGraph.h
#pragma once

#include "MotionNode.h"
#include "MotionEdge.h"
#include <QMap>
#include <QList>
#include <QString>
#include <QJsonObject>

/**
 * @brief Container for a single motion graph (nodes + edges)
 *
 * Represents one named graph for a device. A device can have
 * multiple graphs (e.g., "default", "inspection", "assembly").
 */
class MotionGraph
{
public:
  MotionGraph();
  explicit MotionGraph(const QString& name);

  // Graph identity
  QString name;  // Graph name (e.g., "default", "inspection")

  // ============================================================================
  // NODE OPERATIONS
  // ============================================================================

  void addNode(const MotionNode& node);
  bool removeNode(const QString& nodeId);
  MotionNode* getNode(const QString& nodeId);
  const MotionNode* getNode(const QString& nodeId) const;
  bool hasNode(const QString& nodeId) const;
  QList<MotionNode> getAllNodes() const;
  int getNodeCount() const;
  void clearNodes();

  // Find node by position name (for recipe execution)
  MotionNode* getNodeByPosition(const QString& positionName);
  const MotionNode* getNodeByPosition(const QString& positionName) const;

  // ============================================================================
  // EDGE OPERATIONS
  // ============================================================================

  void addEdge(const MotionEdge& edge);
  bool removeEdge(const QString& edgeId);
  MotionEdge* getEdge(const QString& edgeId);
  const MotionEdge* getEdge(const QString& edgeId) const;
  bool hasEdge(const QString& edgeId) const;
  QList<MotionEdge> getAllEdges() const;
  int getEdgeCount() const;
  void clearEdges();

  // Get all edges connected to a specific node
  QList<MotionEdge> getConnectedEdges(const QString& nodeId) const;

  // Remove all edges connected to a node (called before removing node)
  void removeConnectedEdges(const QString& nodeId);

  // ============================================================================
  // GRAPH TRAVERSAL (for future execution)
  // ============================================================================

  // Get all neighboring node IDs
  QList<QString> getNeighbors(const QString& nodeId) const;

  // Check if two nodes are connected
  bool areConnected(const QString& nodeId1, const QString& nodeId2) const;

  // ============================================================================
  // SERIALIZATION
  // ============================================================================

  QJsonObject toJson() const;
  static MotionGraph fromJson(const QString& name, const QJsonObject& json);

  // ============================================================================
  // VALIDATION
  // ============================================================================

  bool isValid() const;

  // Clear all data
  void clear();

private:
  QMap<QString, MotionNode> m_nodes;
  QMap<QString, MotionEdge> m_edges;
};
