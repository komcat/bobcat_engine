// PositionAnchor.h
#pragma once
#include <QGraphicsEllipseItem>
#include <QString>

/**
 * @brief Small fixed dot representing the actual position
 * This is the true position that cannot be moved
 */
class PositionAnchor : public QGraphicsEllipseItem
{
public:
  PositionAnchor(const QString& name, double x, double y, double z);

  QString getName() const { return m_name; }
  double getX() const { return m_x; }
  double getY() const { return m_y; }
  double getZ() const { return m_z; }

  void updateColor(double zMin, double zMax);

private:
  QString m_name;
  double m_x, m_y, m_z;

  static constexpr double ANCHOR_RADIUS = 3.0;  // Small dot
};