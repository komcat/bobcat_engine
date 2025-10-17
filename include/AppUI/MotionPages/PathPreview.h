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

  // Show path from current position through waypoints to target
  void showPath(const QPointF& currentPosition,
    const QPointF& targetPosition,
    const std::vector<QPointF>& waypointPositions);

  // Hide and clear all path elements
  void hidePath();

  // Update path styling
  void setPathColor(const QColor& color);
  void setPathWidth(int width);
  void setPathStyle(Qt::PenStyle style);

private:
  void clearPathElements();
  void createPathLine(const QPointF& start, const QPointF& end);
  void createWaypointMarker(const QPointF& position);

  QGraphicsScene* m_scene;
  QList<QGraphicsItem*> m_pathElements;

  // Styling
  QColor m_pathColor;
  int m_pathWidth;
  Qt::PenStyle m_pathStyle;
  int m_zValue;
};