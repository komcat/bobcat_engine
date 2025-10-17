// PositionLabel.h
#pragma once
#include <QGraphicsEllipseItem>
#include <QGraphicsTextItem>
#include <QString>
#include <QObject>

/**
 * @brief Draggable label circle that can be positioned away from actual position
 * Connected to anchor point with a line
 */
class PositionLabel : public QObject, public QGraphicsEllipseItem
{
  Q_OBJECT  // Add this to enable signals

public:
  PositionLabel(const QString& name, double anchorX, double anchorY,
    double offsetX = 0.0, double offsetY = 0.0);

  QString getName() const { return m_name; }
  double getAnchorX() const { return m_anchorX; }
  double getAnchorY() const { return m_anchorY; }

  // Get current offset from anchor
  double getOffsetX() const;
  double getOffsetY() const;

  void setSelected(bool selected);
  void setConnectionLine(QGraphicsLineItem* line);  // CHANGED

signals:
  void positionChanged(const QString& name, double offsetX, double offsetY);  // ADD THIS

protected:
  void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
  void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
  void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
  void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
  void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
  QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;  // ADD THIS

private:
  QString m_name;
  double m_anchorX, m_anchorY;
  QGraphicsTextItem* m_textItem;
  QGraphicsLineItem* m_connectionLine;

  bool m_isSelected;
  bool m_isDragging;

  static constexpr double LABEL_RADIUS = 12.0;

  void updateTextPosition();
  void updateConnectionLine();  // ADD THIS
};