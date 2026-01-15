// DependencyGraphScene.cpp
#include "DependencyGraphScene.h"
#include "RecipeNodeItem.h"
#include "DependencyEdgeItem.h"
#include "ProgramManager.h"
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneContextMenuEvent>
#include <QMenu>
#include <QInputDialog>
#include <QMessageBox>
#include <QQueue>
#include <QSet>

DependencyGraphScene::DependencyGraphScene(ProgramManager* programManager, QObject* parent)
  : QGraphicsScene(parent)
  , m_programManager(programManager)
  , m_isConnecting(false)
  , m_connectSourceNode(nullptr)
  , m_tempLine(nullptr)
{
  // Dark background
  setBackgroundBrush(QColor("#1e1e1e"));

  // Connect to track selection order
  connect(this, &QGraphicsScene::selectionChanged,
          this, &DependencyGraphScene::onSelectionChanged);
}

DependencyGraphScene::~DependencyGraphScene()
{
  clearGraph();
}

void DependencyGraphScene::loadFromProgram()
{
  clearGraph();

  if (!m_programManager) {
    return;
  }

  createNodes();
  createEdges();

  // Only auto-layout if no saved positions exist
  // If positions were saved, they were already applied in createNodes()
  if (m_programManager->getAllNodePositions().empty()) {
    autoLayout();
  } else {
    // Still need to update edges after positions are set
    for (DependencyEdgeItem* edge : m_edges.values()) {
      edge->updatePath();
    }
  }
}

void DependencyGraphScene::clearGraph()
{
  // Clear temp line
  clearTempLine();

  // Clear selection order tracking
  m_selectionOrder.clear();

  // Remove all items
  clear();
  m_nodes.clear();
  m_edges.clear();
}

void DependencyGraphScene::createNodes()
{
  if (!m_programManager) {
    return;
  }

  int recipeCount = m_programManager->getRecipeCount();

  for (int i = 0; i < recipeCount; ++i) {
    const RecipeReference* recipe = m_programManager->getRecipe(i);
    if (!recipe) continue;

    QString name = QString::fromStdString(recipe->displayName);
    bool enabled = recipe->enabled;

    RecipeNodeItem* node = new RecipeNodeItem(i, name, enabled);
    node->setGraphScene(this);
    addItem(node);
    m_nodes[i] = node;

    // Apply saved position if available
    if (m_programManager->hasNodePosition(i)) {
      GraphNodePosition pos = m_programManager->getNodePosition(i);
      node->setPos(pos.x, pos.y);
    }
  }
}

void DependencyGraphScene::createEdges()
{
  if (!m_programManager) {
    return;
  }

  int recipeCount = m_programManager->getRecipeCount();

  for (int i = 0; i < recipeCount; ++i) {
    std::vector<DependencyLink> deps = m_programManager->getRecipeDependencies(i);

    for (const DependencyLink& dep : deps) {
      int sourceIndex = dep.predecessorIndex;
      int targetIndex = i;

      RecipeNodeItem* sourceNode = m_nodes.value(sourceIndex, nullptr);
      RecipeNodeItem* targetNode = m_nodes.value(targetIndex, nullptr);

      if (sourceNode && targetNode) {
        QString condition = QString::fromStdString(dep.condition);
        DependencyEdgeItem* edge = new DependencyEdgeItem(sourceNode, targetNode, condition);
        addItem(edge);

        QString edgeKey = QString("%1_%2").arg(sourceIndex).arg(targetIndex);
        m_edges[edgeKey] = edge;
      }
    }
  }
}

RecipeNodeItem* DependencyGraphScene::getNodeByIndex(int recipeIndex) const
{
  return m_nodes.value(recipeIndex, nullptr);
}

DependencyEdgeItem* DependencyGraphScene::findEdge(int sourceIndex, int targetIndex) const
{
  QString edgeKey = QString("%1_%2").arg(sourceIndex).arg(targetIndex);
  return m_edges.value(edgeKey, nullptr);
}

void DependencyGraphScene::removeEdge(DependencyEdgeItem* edge)
{
  if (!edge) return;

  int sourceIndex = edge->getSourceNode()->getRecipeIndex();
  int targetIndex = edge->getTargetNode()->getRecipeIndex();

  QString edgeKey = QString("%1_%2").arg(sourceIndex).arg(targetIndex);
  m_edges.remove(edgeKey);

  removeItem(edge);
  delete edge;

  emit dependencyRemoved(sourceIndex, targetIndex);
}

void DependencyGraphScene::onNodeMoved(RecipeNodeItem* node)
{
  // Update all edges connected to any node
  for (DependencyEdgeItem* edge : m_edges.values()) {
    edge->updatePath();
  }

  // Emit position change for persistence
  if (node) {
    QPointF pos = node->pos();
    emit nodePositionChanged(node->getRecipeIndex(), pos.x(), pos.y());
  }
}

QString DependencyGraphScene::linkSelectedNodes()
{
  // Need exactly 2 nodes selected (use tracked selection order)
  if (m_selectionOrder.size() != 2) {
    return QString("Select exactly 2 nodes. Currently selected: %1").arg(m_selectionOrder.size());
  }

  // First selected (pink) = source, second selected (yellow) = target
  RecipeNodeItem* sourceNode = m_selectionOrder[0];
  RecipeNodeItem* targetNode = m_selectionOrder[1];

  int sourceIndex = sourceNode->getRecipeIndex();
  int targetIndex = targetNode->getRecipeIndex();

  // Check if edge already exists
  if (findEdge(sourceIndex, targetIndex)) {
    return QString("Dependency from #%1 to #%2 already exists").arg(sourceIndex).arg(targetIndex);
  }

  // Check for cycle
  if (wouldCreateCycle(sourceIndex, targetIndex)) {
    return QString("Cannot create dependency - would create a cycle");
  }

  // Create the dependency edge
  DependencyEdgeItem* edge = new DependencyEdgeItem(sourceNode, targetNode);
  addItem(edge);

  QString edgeKey = QString("%1_%2").arg(sourceIndex).arg(targetIndex);
  m_edges[edgeKey] = edge;

  emit dependencyCreated(sourceIndex, targetIndex);

  return QString();  // Success - empty string
}

void DependencyGraphScene::autoLayout()
{
  if (m_nodes.isEmpty()) {
    return;
  }

  // Calculate layer assignments
  QVector<int> layers = calculateLayers();

  // Group nodes by layer
  QMap<int, QVector<int>> layerNodes;
  int maxLayer = 0;
  for (int i = 0; i < layers.size(); ++i) {
    int layer = layers[i];
    layerNodes[layer].append(i);
    maxLayer = qMax(maxLayer, layer);
  }

  // Position nodes
  const qreal layerSpacing = 200.0;  // Horizontal spacing between layers
  const qreal nodeSpacing = 80.0;    // Vertical spacing between nodes in same layer

  for (int layer = 0; layer <= maxLayer; ++layer) {
    QVector<int> nodesInLayer = layerNodes.value(layer);
    int nodeCount = nodesInLayer.size();

    qreal x = layer * layerSpacing;
    qreal startY = -(nodeCount - 1) * nodeSpacing / 2.0;

    for (int j = 0; j < nodeCount; ++j) {
      int recipeIndex = nodesInLayer[j];
      RecipeNodeItem* node = m_nodes.value(recipeIndex);
      if (node) {
        qreal y = startY + j * nodeSpacing;
        node->setPos(x, y);
      }
    }
  }

  // Update all edges after repositioning
  for (DependencyEdgeItem* edge : m_edges.values()) {
    edge->updatePath();
  }
}

QVector<int> DependencyGraphScene::calculateLayers() const
{
  int nodeCount = m_nodes.size();
  QVector<int> layers(nodeCount, 0);

  if (!m_programManager || nodeCount == 0) {
    return layers;
  }

  // Build adjacency list (predecessor -> successors)
  QMap<int, QVector<int>> successors;
  QVector<int> inDegree(nodeCount, 0);

  for (int i = 0; i < nodeCount; ++i) {
    std::vector<DependencyLink> deps = m_programManager->getRecipeDependencies(i);
    for (const DependencyLink& dep : deps) {
      successors[dep.predecessorIndex].append(i);
      inDegree[i]++;
    }
  }

  // Topological sort with layer assignment (Kahn's algorithm)
  QQueue<int> queue;
  for (int i = 0; i < nodeCount; ++i) {
    if (inDegree[i] == 0) {
      queue.enqueue(i);
      layers[i] = 0;
    }
  }

  while (!queue.isEmpty()) {
    int current = queue.dequeue();

    for (int succ : successors[current]) {
      // Layer of successor is max of current layer + 1
      layers[succ] = qMax(layers[succ], layers[current] + 1);
      inDegree[succ]--;

      if (inDegree[succ] == 0) {
        queue.enqueue(succ);
      }
    }
  }

  return layers;
}

bool DependencyGraphScene::wouldCreateCycle(int sourceIndex, int targetIndex) const
{
  if (!m_programManager) {
    return true;
  }

  // Self-loop
  if (sourceIndex == targetIndex) {
    return true;
  }

  // BFS from target to see if we can reach source (would create cycle)
  QSet<int> visited;
  QQueue<int> queue;
  queue.enqueue(targetIndex);

  while (!queue.isEmpty()) {
    int current = queue.dequeue();

    if (current == sourceIndex) {
      return true;  // Found a path from target to source, would create cycle
    }

    if (visited.contains(current)) {
      continue;
    }
    visited.insert(current);

    // Follow existing dependencies from current
    std::vector<DependencyLink> deps = m_programManager->getRecipeDependencies(current);
    for (const DependencyLink& dep : deps) {
      if (!visited.contains(dep.predecessorIndex)) {
        queue.enqueue(dep.predecessorIndex);
      }
    }
  }

  return false;
}

void DependencyGraphScene::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
  if (event->button() == Qt::LeftButton) {
    // Check if clicking on a node's right edge (output connection point)
    QGraphicsItem* item = itemAt(event->scenePos(), QTransform());

    // Walk up parent chain to find RecipeNodeItem (might have clicked on child text item)
    RecipeNodeItem* node = nullptr;
    while (item) {
      node = dynamic_cast<RecipeNodeItem*>(item);
      if (node) break;
      item = item->parentItem();
    }

    if (node) {
      // Check if click is near the right edge (connection point area)
      QPointF localPos = node->mapFromScene(event->scenePos());
      if (localPos.x() > RecipeNodeItem::NODE_WIDTH / 2 - 20) {
        // Start connection drag
        m_isConnecting = true;
        m_connectSourceNode = node;

        // Create temporary line
        QPointF startPoint = node->getOutputConnectionPoint();
        m_tempLine = new QGraphicsLineItem(QLineF(startPoint, event->scenePos()));
        m_tempLine->setPen(QPen(QColor("#1177bb"), 2, Qt::DashLine));
        m_tempLine->setZValue(10);
        addItem(m_tempLine);

        event->accept();
        return;
      }
    }
  }

  QGraphicsScene::mousePressEvent(event);
}

void DependencyGraphScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
  if (m_isConnecting && m_tempLine) {
    updateTempLine(event->scenePos());
    event->accept();
    return;
  }

  QGraphicsScene::mouseMoveEvent(event);
}

void DependencyGraphScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
  if (m_isConnecting && event->button() == Qt::LeftButton) {
    // Check if we released on a different node
    QGraphicsItem* item = itemAt(event->scenePos(), QTransform());

    // Walk up parent chain to find RecipeNodeItem (might have released on child text item)
    RecipeNodeItem* targetNode = nullptr;
    while (item) {
      targetNode = dynamic_cast<RecipeNodeItem*>(item);
      if (targetNode) break;
      item = item->parentItem();
    }

    if (targetNode && targetNode != m_connectSourceNode) {
      int sourceIndex = m_connectSourceNode->getRecipeIndex();
      int targetIndex = targetNode->getRecipeIndex();

      // Check if edge already exists
      if (findEdge(sourceIndex, targetIndex)) {
        // Edge already exists
      }
      // Check for cycle
      else if (wouldCreateCycle(sourceIndex, targetIndex)) {
        QMessageBox::warning(nullptr, "Invalid Dependency",
                             "Cannot create this dependency - it would create a cycle.");
      }
      else {
        // Create the dependency
        DependencyEdgeItem* edge = new DependencyEdgeItem(m_connectSourceNode, targetNode);
        addItem(edge);

        QString edgeKey = QString("%1_%2").arg(sourceIndex).arg(targetIndex);
        m_edges[edgeKey] = edge;

        emit dependencyCreated(sourceIndex, targetIndex);
      }
    }

    clearTempLine();
    m_isConnecting = false;
    m_connectSourceNode = nullptr;

    event->accept();
    return;
  }

  QGraphicsScene::mouseReleaseEvent(event);
}

void DependencyGraphScene::contextMenuEvent(QGraphicsSceneContextMenuEvent* event)
{
  QGraphicsItem* item = itemAt(event->scenePos(), QTransform());

  // Check if right-clicking on an edge
  DependencyEdgeItem* edge = dynamic_cast<DependencyEdgeItem*>(item);
  if (edge) {
    QMenu menu;
    QAction* editAction = menu.addAction("Edit Condition...");
    QAction* deleteAction = menu.addAction("Delete Dependency");

    QAction* selected = menu.exec(event->screenPos());

    if (selected == editAction) {
      int sourceIndex = edge->getSourceNode()->getRecipeIndex();
      int targetIndex = edge->getTargetNode()->getRecipeIndex();
      emit editConditionRequested(sourceIndex, targetIndex);
    }
    else if (selected == deleteAction) {
      removeEdge(edge);
    }

    event->accept();
    return;
  }

  QGraphicsScene::contextMenuEvent(event);
}

void DependencyGraphScene::updateTempLine(const QPointF& endPoint)
{
  if (m_tempLine && m_connectSourceNode) {
    QPointF startPoint = m_connectSourceNode->getOutputConnectionPoint();
    m_tempLine->setLine(QLineF(startPoint, endPoint));
  }
}

void DependencyGraphScene::clearTempLine()
{
  if (m_tempLine) {
    removeItem(m_tempLine);
    delete m_tempLine;
    m_tempLine = nullptr;
  }
}

void DependencyGraphScene::onSelectionChanged()
{
  // Get currently selected nodes
  QList<QGraphicsItem*> selected = selectedItems();
  QSet<RecipeNodeItem*> currentlySelected;

  for (QGraphicsItem* item : selected) {
    RecipeNodeItem* node = dynamic_cast<RecipeNodeItem*>(item);
    if (node) {
      currentlySelected.insert(node);
    }
  }

  // Remove deselected nodes from order list
  for (int i = m_selectionOrder.size() - 1; i >= 0; --i) {
    if (!currentlySelected.contains(m_selectionOrder[i])) {
      m_selectionOrder[i]->setSelectionOrder(0);
      m_selectionOrder.removeAt(i);
    }
  }

  // Add newly selected nodes to end of order list
  for (RecipeNodeItem* node : currentlySelected) {
    if (!m_selectionOrder.contains(node)) {
      m_selectionOrder.append(node);
    }
  }

  // Update selection order visual for all tracked nodes
  for (int i = 0; i < m_selectionOrder.size(); ++i) {
    m_selectionOrder[i]->setSelectionOrder(i + 1);  // 1-based: 1=first(pink), 2=second(yellow)
  }
}
