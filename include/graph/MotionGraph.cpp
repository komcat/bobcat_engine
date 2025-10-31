// MotionGraph.cpp
#include "MotionGraph.h"
#include <QJsonArray>

MotionGraph::MotionGraph()
  : name("default")
{
}

MotionGraph::MotionGraph(const QString& name)
  : name(name)
{
}

// ============================================================================
// NODE OPERATIONS
// ============================================================================

void MotionGraph::addNode(const MotionNode& node)
{
  if (node.isValid()) {
    m_nodes[node.id] = node;
  }
}

bool MotionGraph::removeNode(const QString& nodeId)
{
  if (!m_nodes.contains(nodeId)) {
    return false;
  }

  // Remove all edges connected to this node first
  removeConnectedEdges(nodeId);

  // Remove the node
  m_nodes.remove(nodeId);
  return true;
}

MotionNode* MotionGraph::getNode(const QString& nodeId)
{
  if (m_nodes.contains(nodeId)) {
    return &m_nodes[nodeId];
  }
  return nullptr;
}

const MotionNode* MotionGraph::getNode(const QString& nodeId) const
{
  auto it = m_nodes.find(nodeId);
  if (it != m_nodes.end()) {
    return &it.value();
  }
  return nullptr;
}

bool MotionGraph::hasNode(const QString& nodeId) const
{
  return m_nodes.contains(nodeId);
}

QList<MotionNode> MotionGraph::getAllNodes() const
{
  return m_nodes.values();
}

int MotionGraph::getNodeCount() const
{
  return m_nodes.size();
}

void MotionGraph::clearNodes()
{
  m_nodes.clear();
}

MotionNode* MotionGraph::getNodeByPosition(const QString& positionName)
{
  // Search through all nodes to find one with matching position name
  for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
    if (it.value().position == positionName) {
      return &it.value();
    }
  }
  return nullptr;
}

const MotionNode* MotionGraph::getNodeByPosition(const QString& positionName) const
{
  // Search through all nodes to find one with matching position name
  for (auto it = m_nodes.constBegin(); it != m_nodes.constEnd(); ++it) {
    if (it.value().position == positionName) {
      return &it.value();
    }
  }
  return nullptr;
}

// ============================================================================
// EDGE OPERATIONS
// ============================================================================

void MotionGraph::addEdge(const MotionEdge& edge)
{
  if (edge.isValid() && hasNode(edge.fromNodeId) && hasNode(edge.toNodeId)) {
    m_edges[edge.id] = edge;
  }
}

bool MotionGraph::removeEdge(const QString& edgeId)
{
  if (!m_edges.contains(edgeId)) {
    return false;
  }

  m_edges.remove(edgeId);
  return true;
}

MotionEdge* MotionGraph::getEdge(const QString& edgeId)
{
  if (m_edges.contains(edgeId)) {
    return &m_edges[edgeId];
  }
  return nullptr;
}

const MotionEdge* MotionGraph::getEdge(const QString& edgeId) const
{
  auto it = m_edges.find(edgeId);
  if (it != m_edges.end()) {
    return &it.value();
  }
  return nullptr;
}

bool MotionGraph::hasEdge(const QString& edgeId) const
{
  return m_edges.contains(edgeId);
}

QList<MotionEdge> MotionGraph::getAllEdges() const
{
  return m_edges.values();
}

int MotionGraph::getEdgeCount() const
{
  return m_edges.size();
}

void MotionGraph::clearEdges()
{
  m_edges.clear();
}

QList<MotionEdge> MotionGraph::getConnectedEdges(const QString& nodeId) const
{
  QList<MotionEdge> connectedEdges;

  for (const MotionEdge& edge : m_edges.values()) {
    if (edge.connectsNode(nodeId)) {
      connectedEdges.append(edge);
    }
  }

  return connectedEdges;
}

void MotionGraph::removeConnectedEdges(const QString& nodeId)
{
  // Find all edges to remove
  QList<QString> edgesToRemove;
  for (const MotionEdge& edge : m_edges.values()) {
    if (edge.connectsNode(nodeId)) {
      edgesToRemove.append(edge.id);
    }
  }

  // Remove them
  for (const QString& edgeId : edgesToRemove) {
    m_edges.remove(edgeId);
  }
}

// ============================================================================
// GRAPH TRAVERSAL
// ============================================================================

QList<QString> MotionGraph::getNeighbors(const QString& nodeId) const
{
  QList<QString> neighbors;

  for (const MotionEdge& edge : m_edges.values()) {
    if (edge.connectsNode(nodeId)) {
      QString neighbor = edge.getOtherNode(nodeId);
      if (!neighbor.isEmpty() && !neighbors.contains(neighbor)) {
        neighbors.append(neighbor);
      }
    }
  }

  return neighbors;
}

bool MotionGraph::areConnected(const QString& nodeId1, const QString& nodeId2) const
{
  for (const MotionEdge& edge : m_edges.values()) {
    if ((edge.fromNodeId == nodeId1 && edge.toNodeId == nodeId2) ||
        (edge.fromNodeId == nodeId2 && edge.toNodeId == nodeId1)) {
      return true;
    }
  }
  return false;
}

// ============================================================================
// SERIALIZATION
// ============================================================================

QJsonObject MotionGraph::toJson() const
{
  QJsonObject json;

  // Nodes array
  QJsonArray nodesArray;
  for (const MotionNode& node : m_nodes.values()) {
    nodesArray.append(node.toJson());
  }
  json["nodes"] = nodesArray;

  // Edges array
  QJsonArray edgesArray;
  for (const MotionEdge& edge : m_edges.values()) {
    edgesArray.append(edge.toJson());
  }
  json["edges"] = edgesArray;

  return json;
}

MotionGraph MotionGraph::fromJson(const QString& name, const QJsonObject& json)
{
  MotionGraph graph(name);

  // Load nodes
  if (json.contains("nodes") && json["nodes"].isArray()) {
    QJsonArray nodesArray = json["nodes"].toArray();
    for (const QJsonValue& value : nodesArray) {
      if (value.isObject()) {
        MotionNode node = MotionNode::fromJson(value.toObject());
        if (node.isValid()) {
          graph.addNode(node);
        }
      }
    }
  }

  // Load edges (only after nodes are loaded)
  if (json.contains("edges") && json["edges"].isArray()) {
    QJsonArray edgesArray = json["edges"].toArray();
    for (const QJsonValue& value : edgesArray) {
      if (value.isObject()) {
        MotionEdge edge = MotionEdge::fromJson(value.toObject());
        if (edge.isValid()) {
          graph.addEdge(edge);
        }
      }
    }
  }

  return graph;
}

// ============================================================================
// VALIDATION
// ============================================================================

bool MotionGraph::isValid() const
{
  // Check all nodes are valid
  for (const MotionNode& node : m_nodes.values()) {
    if (!node.isValid()) {
      return false;
    }
  }

  // Check all edges are valid and reference existing nodes
  for (const MotionEdge& edge : m_edges.values()) {
    if (!edge.isValid() || !hasNode(edge.fromNodeId) || !hasNode(edge.toNodeId)) {
      return false;
    }
  }

  return true;
}

void MotionGraph::clear()
{
  m_nodes.clear();
  m_edges.clear();
}
