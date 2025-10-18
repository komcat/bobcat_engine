// PathPreview.h
#pragma once

#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsLineItem>
#include <QGraphicsEllipseItem>
#include <QList>
#include <QPointF>
#include <vector>
#include <string>

class PathPreview
{
public:
  PathPreview(QGraphicsScene* scene);
  ~PathPreview();

  // Show path with separate entry and exit waypoints
  void showPath(const QPointF& currentPosition,
    const QPointF& targetPosition,
    const std::vector<QPointF>& entryWaypointPositions,
    const std::vector<QPointF>& exitWaypointPositions);

  // Hide and clear all path elements
  void hidePath();

  // Update path styling
  void setPathColor(const QColor& color);
  void setExitPathColor(const QColor& color);
  void setPathWidth(int width);

private:
  void clearPathElements();
  void createPathLine(const QPointF& start, const QPointF& end,
    const QColor& color, Qt::PenStyle style);
  void createWaypointMarker(const QPointF& position, const QColor& color);

  QGraphicsScene* m_scene;
  QList<QGraphicsItem*> m_pathElements;

  // Styling
  QColor m_pathColor;        // Entry waypoint color (yellow)
  QColor m_exitPathColor;    // Exit waypoint color (orange)
  int m_pathWidth;
  int m_zValue;
};