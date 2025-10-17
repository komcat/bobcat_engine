// PositionDot.h
#pragma once
#include <QGraphicsEllipseItem>
#include <QGraphicsTextItem>
#include <QString>

/**
 * @brief Represents a single position dot on the 2D map
 * - Shows as colored circle based on Z value
 * - Displays position name
 * - Shows Z value on hover for overlapping positions
 * - Selectable to show details
 * - Maintains constant pixel size when zooming
 */
class PositionDot : public QGraphicsEllipseItem
{
public:
  PositionDot(const QString& name, double x, double y, double z,
    double zMin, double zMax, QGraphicsItem* parent = nullptr);

  // Getters
  QString getName() const { return m_name; }
  double getX() const { return m_x; }
  double getY() const { return m_y; }
  double getZ() const { return m_z; }

  // Update appearance
  void updateColor(double zMin, double zMax);
  void setSelected(bool selected);
  void updateScale(qreal viewScale);

  // NEW - Show Z value on label
  void setShowZValue(bool show);

protected:
  void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
  void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
  void mousePressEvent(QGraphicsSceneMouseEvent* event) override;

private:
  QString m_name;
  double m_x, m_y, m_z;
  QGraphicsTextItem* m_label;
  qreal m_baseScale;
  bool m_showZValue;  // NEW

  void updateLabel();  // NEW
  QColor calculateColorFromZ(double z, double zMin, double zMax);
};