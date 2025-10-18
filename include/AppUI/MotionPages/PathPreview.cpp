// PathPreview.cpp - Using different dash patterns for entry/exit
#include "PathPreview.h"
#include <QPen>
#include <QBrush>

PathPreview::PathPreview(QGraphicsScene* scene)
  : m_scene(scene)
  , m_pathColor(Qt::yellow)                    // Entry waypoints = yellow
  , m_exitPathColor(QColor(255, 165, 0))       // Exit waypoints = orange
  , m_pathWidth(3)
  , m_zValue(100)
{
}

PathPreview::~PathPreview()
{
  clearPathElements();
}

void PathPreview::showPath(const QPointF& currentPosition,
  const QPointF& targetPosition,
  const std::vector<QPointF>& entryWaypointPositions,
  const std::vector<QPointF>& exitWaypointPositions)
{
  clearPathElements();

  if (!m_scene) {
    return;
  }

  // ✅ Build EXIT path first (if coming from a previous position)
  // Exit path: current -> exit waypoints
  if (!exitWaypointPositions.empty()) {
    QList<QPointF> exitPath;
    exitPath.append(currentPosition);

    for (const auto& waypoint : exitWaypointPositions) {
      exitPath.append(waypoint);
    }

    // Draw exit path lines in orange with DOT-DASH pattern
    for (int i = 0; i < exitPath.size() - 1; ++i) {
      createPathLine(exitPath[i], exitPath[i + 1], m_exitPathColor, Qt::DashDotLine);
    }

    // Draw exit waypoint markers in orange (skip current position)
    for (int i = 1; i < exitPath.size(); ++i) {
      createWaypointMarker(exitPath[i], m_exitPathColor);
    }
  }

  // Build ENTRY path: (last exit waypoint or current) -> entry waypoints -> target
  QList<QPointF> entryPath;

  if (!exitWaypointPositions.empty()) {
    // Start from last exit waypoint
    entryPath.append(exitWaypointPositions.back());
  }
  else {
    // Start from current position
    entryPath.append(currentPosition);
  }

  for (const auto& waypoint : entryWaypointPositions) {
    entryPath.append(waypoint);
  }

  entryPath.append(targetPosition);

  // Draw entry path lines in yellow with DASH pattern
  for (int i = 0; i < entryPath.size() - 1; ++i) {
    createPathLine(entryPath[i], entryPath[i + 1], m_pathColor, Qt::DashLine);
  }

  // Draw entry waypoint markers in yellow (skip start, include target)
  for (int i = 1; i < entryPath.size(); ++i) {
    createWaypointMarker(entryPath[i], m_pathColor);
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

void PathPreview::setExitPathColor(const QColor& color)
{
  m_exitPathColor = color;
}

void PathPreview::setPathWidth(int width)
{
  m_pathWidth = width;
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

void PathPreview::createPathLine(const QPointF& start, const QPointF& end,
  const QColor& color, Qt::PenStyle style)
{
  // Use cosmetic pen to keep line width constant at all zoom levels
  QPen pathPen(color, m_pathWidth, style);
  pathPen.setCosmetic(true);

  QGraphicsLineItem* line = new QGraphicsLineItem(
    start.x(), start.y(), end.x(), end.y()
  );

  line->setPen(pathPen);
  line->setZValue(m_zValue);

  m_scene->addItem(line);
  m_pathElements.append(line);
}

void PathPreview::createWaypointMarker(const QPointF& position, const QColor& color)
{
  // Create marker that ignores transformations to stay fixed size
  QGraphicsEllipseItem* marker = new QGraphicsEllipseItem(-4, -4, 8, 8);

  // Position in scene coordinates
  marker->setPos(position);

  // Keep marker at fixed pixel size when zooming
  marker->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);

  QPen markerPen(color, 2);
  marker->setPen(markerPen);
  marker->setBrush(QBrush(QColor(color.red(), color.green(),
    color.blue(), 150)));
  marker->setZValue(m_zValue + 1);

  m_scene->addItem(marker);
  m_pathElements.append(marker);
}