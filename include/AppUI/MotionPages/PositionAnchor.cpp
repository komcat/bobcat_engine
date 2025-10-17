// PositionAnchor.cpp
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

  // Fixed visual style - small red dot
  setPen(QPen(QColor(231, 76, 60), 2));  // Red outline
  setBrush(QBrush(QColor(231, 76, 60, 200)));  // Semi-transparent red fill

  // Cannot be moved or selected
  setFlag(QGraphicsItem::ItemIsMovable, false);
  setFlag(QGraphicsItem::ItemIsSelectable, false);

  setZValue(5);  // Above grid, below labels
}

void PositionAnchor::updateColor(double zMin, double zMax)
{
  // Anchor color based on Z height (optional)
  if (zMax <= zMin) return;

  double normalized = (m_z - zMin) / (zMax - zMin);

  // Color gradient: Blue (low) -> Red (high)
  int r = static_cast<int>(255 * normalized);
  int b = static_cast<int>(255 * (1.0 - normalized));

  setPen(QPen(QColor(r, 100, b), 2));
  setBrush(QBrush(QColor(r, 100, b, 200)));
}