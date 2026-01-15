// DependencyGraphScene.h
#pragma once

#include <QGraphicsScene>
#include <QMap>
#include <QPointF>

class RecipeNodeItem;
class DependencyEdgeItem;
class ProgramManager;

/**
 * @brief Custom QGraphicsScene for the dependency graph
 *
 * Features:
 * - Manages recipe nodes and dependency edges
 * - Handles drag-to-connect interaction for creating dependencies
 * - Context menu for editing/deleting dependencies
 * - Validates connections (prevents cycles)
 */
class DependencyGraphScene : public QGraphicsScene
{
  Q_OBJECT

public:
  explicit DependencyGraphScene(ProgramManager* programManager, QObject* parent = nullptr);
  ~DependencyGraphScene();

  // Load graph from ProgramManager
  void loadFromProgram();

  // Clear all nodes and edges
  void clearGraph();

  // Node management
  RecipeNodeItem* getNodeByIndex(int recipeIndex) const;

  // Edge management
  DependencyEdgeItem* findEdge(int sourceIndex, int targetIndex) const;
  void removeEdge(DependencyEdgeItem* edge);

  // Called by RecipeNodeItem when node position changes
  void onNodeMoved(RecipeNodeItem* node);

  // Auto-layout the graph
  void autoLayout();

  // Create link from selected nodes (returns error message, empty on success)
  QString linkSelectedNodes();

signals:
  // Emitted when dependency is created
  void dependencyCreated(int sourceIndex, int targetIndex);

  // Emitted when dependency is removed
  void dependencyRemoved(int sourceIndex, int targetIndex);

  // Emitted when user wants to edit condition
  void editConditionRequested(int sourceIndex, int targetIndex);

  // Emitted when a node is moved (for position persistence)
  void nodePositionChanged(int recipeIndex, double x, double y);

protected:
  void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
  void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
  void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
  void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;

private:
  // Cycle detection: returns true if adding edge from source to target would create a cycle
  bool wouldCreateCycle(int sourceIndex, int targetIndex) const;

  // Create visual items
  void createNodes();
  void createEdges();

  // Temporary line while dragging to create connection
  void updateTempLine(const QPointF& endPoint);
  void clearTempLine();

  // Calculate layer assignments for auto-layout (topological ordering)
  QVector<int> calculateLayers() const;

  ProgramManager* m_programManager;

  // Node tracking: recipeIndex -> node item
  QMap<int, RecipeNodeItem*> m_nodes;

  // Edge tracking: "sourceIndex_targetIndex" -> edge item
  QMap<QString, DependencyEdgeItem*> m_edges;

  // Drag-to-connect state
  bool m_isConnecting;
  RecipeNodeItem* m_connectSourceNode;
  QGraphicsLineItem* m_tempLine;
};
