// PositionAnchor.cpp - FIXED for proper zoom behavior
#include "PositionAnchor.h"
#include <QPen>
#include <QBrush>

PositionAnchor::PositionAnchor(const QString& name, double x, double y, double z)
  : m_name(name)
  , m_x(x)
  , m_y(y)
  , m_z(z)
{
  // Create small circle at actual position
  setRect(-ANCHOR_RADIUS, -ANCHOR_RADIUS, ANCHOR_RADIUS * 2, ANCHOR_RADIUS * 2);
  setPos(x, y);

  // ✅ Keep anchor at fixed pixel size when zooming
  setFlag(QGraphicsItem::ItemIgnoresTransformations, true);

  // Fixed visual style - small red dot
  QPen pen(QColor(231, 76, 60), 2);
  setPen(pen);
  setBrush(QBrush(QColor(231, 76, 60, 200)));

  // Cannot be moved or selected
  setFlag(QGraphicsItem::ItemIsMovable, false);
  setFlag(QGraphicsItem::ItemIsSelectable, false);

  setZValue(5);
}

void PositionAnchor::updateColor(double zMin, double zMax)
{
  if (zMax <= zMin) return;

  double normalized = (m_z - zMin) / (zMax - zMin);

  // Color gradient: Blue (low) -> Red (high)
  int r = static_cast<int>(255 * normalized);
  int b = static_cast<int>(255 * (1.0 - normalized));

  QPen pen(QColor(r, 100, b), 2);
  pen.setCosmetic(true);  // ✅ Keep pen width constant
  setPen(pen);
  setBrush(QBrush(QColor(r, 100, b, 200)));
}