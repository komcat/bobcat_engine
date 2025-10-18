// PathPreview.cpp - FIXED for proper zoom behavior
#include "PathPreview.h"
#include <QPen>
#include <QBrush>

PathPreview::PathPreview(QGraphicsScene* scene)
  : m_scene(scene)
  , m_pathColor(Qt::yellow)
  , m_pathWidth(3)
  , m_pathStyle(Qt::DashLine)
  , m_zValue(100)
{
}

PathPreview::~PathPreview()
{
  clearPathElements();
}

void PathPreview::showPath(const QPointF& currentPosition,
  const QPointF& targetPosition,
  const std::vector<QPointF>& waypointPositions)
{
  clearPathElements();

  if (!m_scene) {
    return;
  }

  // Build complete path: current -> waypoints -> target
  QList<QPointF> pathPoints;
  pathPoints.append(currentPosition);

  for (const auto& waypoint : waypointPositions) {
    pathPoints.append(waypoint);
  }

  pathPoints.append(targetPosition);

  // Create path lines
  for (int i = 0; i < pathPoints.size() - 1; ++i) {
    createPathLine(pathPoints[i], pathPoints[i + 1]);
  }

  // Create waypoint markers (skip current position, include target)
  for (int i = 1; i < pathPoints.size(); ++i) {
    createWaypointMarker(pathPoints[i]);
  }
}

void PathPreview::hidePath()
{
  clearPathElements();
}

void PathPreview::setPathColor(const QColor& color)
{
  m_pathColor = color;
}

void PathPreview::setPathWidth(int width)
{
  m_pathWidth = width;
}

void PathPreview::setPathStyle(Qt::PenStyle style)
{
  m_pathStyle = style;
}

void PathPreview::clearPathElements()
{
  for (auto* item : m_pathElements) {
    if (item && m_scene) {
      m_scene->removeItem(item);
      delete item;
    }
  }
  m_pathElements.clear();
}

void PathPreview::createPathLine(const QPointF& start, const QPointF& end)
{
  // ✅ Use cosmetic pen to keep line width constant at all zoom levels
  QPen pathPen(m_pathColor, m_pathWidth, m_pathStyle);
  pathPen.setCosmetic(true);

  QGraphicsLineItem* line = new QGraphicsLineItem(
    start.x(), start.y(), end.x(), end.y()
  );

  line->setPen(pathPen);
  line->setZValue(m_zValue);

  m_scene->addItem(line);
  m_pathElements.append(line);
}

void PathPreview::createWaypointMarker(const QPointF& position)
{
  // ✅ Create marker that ignores transformations to stay fixed size
  QGraphicsEllipseItem* marker = new QGraphicsEllipseItem(-4, -4, 8, 8);

  // ✅ Position in scene coordinates
  marker->setPos(position);

  // ✅ Keep marker at fixed pixel size when zooming
  marker->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);

  QPen markerPen(m_pathColor, 2);
  marker->setPen(markerPen);
  marker->setBrush(QBrush(QColor(m_pathColor.red(), m_pathColor.green(),
    m_pathColor.blue(), 150)));
  marker->setZValue(m_zValue + 1);

  m_scene->addItem(marker);
  m_pathElements.append(marker);
}